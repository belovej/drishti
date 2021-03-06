/*!
  @file   FaceDetector.cpp
  @author David Hirvonen
  @brief  Internal implementation of a face trained object detector.

  \copyright Copyright 2014-2016 Elucideye, Inc. All rights reserved.
  \license{This project is released under the 3 Clause BSD License.}

*/

#include "drishti/core/drishti_core.h"
#include "drishti/core/timing.h"
#include "drishti/core/Parallel.h"
#include "drishti/face/FaceDetector.h"
#include "drishti/face/FaceIO.h"
#include "drishti/geometry/Primitives.h"
#include "drishti/acf/ACF.h" // ACF detection
#include "drishti/ml/ShapeEstimator.h"
#include "drishti/ml/RegressionTreeEnsembleShapeEstimator.h"
#include "drishti/face/Face.h"
#include "drishti/eye/EyeModelEstimator.h"
#include "drishti/geometry/Rectangle.h"

// clang-format off
#if DRISHTI_SERIALIZE_WITH_BOOST
#  include "drishti/core/boost_serialize_common.h" // (optional)
#endif
// clang-format on

#include <stdio.h>

#define DRISHTI_FACE_DETECTOR_PREVIEW_DETECTIONS 0
#define DRISHTI_FACE_DETECTOR_DO_SIMILARITY_MOTION 1

DRISHTI_FACE_NAMESPACE_BEGIN

#if DRISHTI_FACE_DETECTOR_PREVIEW_DETECTIONS
static void previewDetections(const MatP& I, std::vector<dsdkc::Shape>& shapes);
#endif

using drishti::geometry::operator*;

// Map from normalized coordinate system to input ROI
static cv::Matx33f denormalize(const cv::Rect& roi);
static void chooseBest(std::vector<cv::Rect>& objects, std::vector<double>& scores);

// ((((((((((((((( Impl )))))))))))))))
class FaceDetector::Impl
{
public:
    typedef FaceDetector::MatLoggerType MatLoggerType;
    typedef FaceDetector::TimeLoggerType TimeLoggerType;
    typedef FaceDetector::EyeCropper EyeCropper;

    Impl(FaceDetectorFactory& resources)
    {
        create(resources);
    }

    void create(FaceDetectorFactory& resources)
    {
        m_detector = resources.getFaceDetector();
        m_regressor = resources.getInnerFaceEstimator();
        if (resources.sFaceRegressors.size() > 1)
        {
            m_regressor2 = resources.getOuterFaceEstimator();
        }
        m_eyeRegressor.resize(2);
        for (int i = 0; i < 2; i++)
        {
            m_eyeRegressor[i] = resources.getEyeEstimator();
        }
    }

    void setLandmarkFormat(FaceSpecification::Format format)
    {
        m_landmarkFormat = format;
    }

    /*
     * I    : input detectio image (typically planar format RGB)
     * Ib   : input image for regressor (graysacle uint8_t), often higer resolution
     * doMs : perform non max suppression on output detections
     * H    : homography expressing motion from detection image to regressor image
     * init : # of initializations to perform during regression
     */

    template <typename ImageType>
    void detect(const ImageType& I, std::vector<dsdkc::Shape>& shapes)
    {
        drishti::core::ScopeTimeLogger scopeTimeLogger = [this](double elapsed) {
            if (m_detectionTimeLogger)
            {
                m_detectionTimeLogger(elapsed);
            }
        };

        // Detect objects:
        std::vector<double> scores;
        std::vector<cv::Rect> objects;
        (*m_detector)(I, objects, &scores);

        if (m_doNMSGlobal)
        {
            chooseBest(objects, scores);
        }

        for (int i = 0; i < objects.size(); i++)
        {
            shapes.emplace_back(objects[i], scores[i]);
        }
    }

    void refineFace(const PaddedImage& Ib, std::vector<FaceModel>& faces, const cv::Matx33f& H, bool isDetection)
    {
        // Find the landmarks:
        if (m_regressor)
        {
            std::vector<dsdkc::Shape> shapes(faces.size());
            std::transform(faces.begin(), faces.end(), shapes.begin(), [](const FaceModel& face) {
                return dsdkc::Shape(face.roi);
            });
            findLandmarks(Ib, shapes, H, isDetection);
            shapesToFaces(shapes, faces);
        }

        if (m_eyeRegressor.size() && m_eyeRegressor[0] && m_eyeRegressor[1] && m_doEyeRefinement && faces.size())
        {
            DRISHTI_EYE::EyeModel eyeR, eyeL;
            segmentEyes(Ib.Ib, faces[0], eyeR, eyeL);
            if (eyeR.eyelids.size())
            {
                faces[0].eyeFullR = eyeR;
                faces[0].eyeRightCenter = core::centroid(eyeR.eyelids);
            }
            if (eyeL.eyelids.size())
            {
                faces[0].eyeFullL = eyeL;
                faces[0].eyeLeftCenter = core::centroid(eyeL.eyelids);
            }
        }
    }

    static void extractCrops(const cv::Mat& Ib, cv::Rect eyes[2], const cv::Rect& bounds, cv::Mat crops[2])
    {
        for (int i = 0; i < 2; i++)
        {
            // TODO: replace this with a zero copy version:
            // NOTE: Iris code seems to assume continuous memory layout
            cv::Rect roi = eyes[i] & bounds;
            crops[i].create(eyes[i].size(), CV_8UC1);
            crops[i].setTo(0);
            Ib(roi).copyTo(crops[i](roi - eyes[i].tl()));
        }
    }

    void segmentEyes(const cv::Mat1b& Ib, FaceModel& face, DRISHTI_EYE::EyeModel& eyeR, DRISHTI_EYE::EyeModel& eyeL)
    {
        cv::Rect2f roiR, roiL;
        bool hasEyes = face.getEyeRegions(roiR, roiL, 0.666);
        if (hasEyes && roiR.area() && roiL.area())
        {
            drishti::core::ScopeTimeLogger scopeTimeLogger = [this](double elapsed) {
                if (m_eyeRegressionTimeLogger)
                {
                    m_eyeRegressionTimeLogger(elapsed);
                }
            };

            // Apply some fixed offset to eye crops:
            //            const cv::Point2f centerR = drishti::geometry::centroid<float,float>(roiR);
            //            const cv::Point2f centerL = drishti::geometry::centroid<float,float>(roiL);
            //            const cv::Point2f v1 = (centerL - centerR), v2(-v1.y,v1.x);
            //            roiR -=(v2 * 0.05);
            //            roiL -=(v2 * 0.05);

            cv::Mat crops[2], flipped;
            cv::Rect eyes[2] = { roiR, roiL };
            extractCrops(Ib, eyes, { { 0, 0 }, Ib.size() }, crops);
            cv::flip(crops[1], flipped, 1); // Flip left eye to right eye cs
            crops[1] = flipped;

            cv::Point2f v = geometry::centroid<float, float>(roiR) - geometry::centroid<float, float>(roiL);
            float theta = std::atan2(v.y, v.x);
            eyeR.angle = theta;
            eyeL.angle = (-theta);
            DRISHTI_EYE::EyeModel* results[2]{ &eyeR, &eyeL };

            for (int i = 0; i < 2; i++)
            {
                m_eyeRegressor[i]->setDoIndependentIrisAndPupil(m_doIrisRefinement);
                m_eyeRegressor[i]->setEyelidInits(1);
                m_eyeRegressor[i]->setIrisInits(1);
            }

            drishti::core::ParallelHomogeneousLambda harness = [&](int i) {
                (*m_eyeRegressor[i])(crops[i], *results[i]);
            };

            //harness({0, 2});
            cv::parallel_for_({ 0, 2 }, harness, 2);

            eyeL.flop(crops[1].cols);
            eyeL += eyes[1].tl(); // shift features to image coordinate system
            eyeR += eyes[0].tl();
            eyeR.roi = eyes[0];
            eyeL.roi = eyes[1];
        }
    }

    static cv::Rect scaleRoi(cv::Rect& roi, float scale)
    {
        cv::Point2f tl(roi.tl()), br(roi.br()), center((tl + br) * 0.5f), diag(br - center);
        return cv::Rect(center - (diag * scale), center + (diag * scale));
    }

    void findLandmarks(const PaddedImage& Ib, std::vector<dsdkc::Shape>& shapes, const cv::Matx33f& Hdr_, bool isDetection)
    {
        // Scope based eye segmentation timer:
        drishti::core::ScopeTimeLogger scopeTimeLogger = [this](double elapsed) {
            if (m_regressionTimeLogger)
            {
                m_regressionTimeLogger(elapsed);
            }
        };

        const cv::Mat gray = Ib.Ib;
        CV_Assert(gray.type() == CV_8UC1);

        std::vector<std::pair<drishti::ml::ShapeEstimator*, cv::Matx33f>> regressors{
            { m_regressor.get(), m_Hrd },
        };

        if (m_regressor2.get())
        {
            regressors.emplace_back(m_regressor2.get(), m_Hrd2);
        }

        cv::Rect fullBounds({ 0, 0 }, Ib.Ib.size());
        cv::Rect bounds = Ib.roi.area() ? Ib.roi : fullBounds;

        for (int i = 0; i < shapes.size(); i++)
        {
            int best = 0;

            // Use richest regressor whos projected roi still fits in the image (or almost)
            cv::Rect roi;
            for (int j = int(regressors.size()) - 1; j >= 0; j--)
            {
                roi = isDetection ? mapDetectionToRegressor(shapes[i].roi, regressors[j].second, Hdr_) : shapes[i].roi;
                roi = scaleRoi(roi, m_scaling);
                if ((roi & bounds) == roi)
                {
                    best = j;
                    break;
                }
            }

            shapes[i].roi = roi;
            cv::Rect clipped = shapes[i].roi & fullBounds;
            cv::Mat crop = gray(clipped);

            // TODO [DJH]: Avoid the need for clipping the image
            if (clipped.size() != shapes[i].roi.size())
            {
                cv::Mat padded(shapes[i].roi.size(), crop.type(), cv::Scalar::all(0));
                crop.copyTo(padded(clipped - shapes[i].roi.tl()));
                cv::swap(crop, padded);
            }

            auto& regressor = regressors[best].first;

            const float scaleInv = 1.f;
            std::vector<cv::Point2f> points;
            std::vector<bool> mask;
            (*regressor)(crop, points, mask);

            for (const auto& p : points)
            {
                cv::Point q((p.x * scaleInv) + shapes[i].roi.x, (p.y * scaleInv) + shapes[i].roi.y);
                shapes[i].contour.emplace_back(q.x, q.y, 0);
            }
        }
    }

    // Notes on motion and coordinate systems:
    //
    // FaceDetector::m_Hrd : map normalized regressor mean face to normalized detector mean face
    // denormalize(s.roi)  : denormalize points
    // Hdr                 : detector image to regressor

    cv::Rect mapDetectionToRegressor(const cv::Rect& roi, const cv::Matx33f& Hrd, const cv::Matx33f& Hdr_)
    {
        // 1) map the unit square from regressor to the detector;
        // 2) denormalize coordinates in the detector image;
        // 3) transform from the detector image to the regressor image;
        cv::Matx33f H = Hdr_ * denormalize(roi) * Hrd;
        return H * cv::Rect_<float>(0, 0, 1, 1);
    }

    void mapDetectionsToRegressor(std::vector<dsdkc::Shape>& shapes, const cv::Matx33f& Hrd, const cv::Matx33f& Hdr_)
    {
        for (auto& s : shapes)
        {
            s.roi = mapDetectionToRegressor(s.roi, Hrd, Hdr_);
        }
    }

    void shapesToFaces(std::vector<dsdkc::Shape>& shapes, std::vector<FaceModel>& faces)
    {
        faces.clear();
        for (auto& s : shapes)
        {
            FaceModel f;
            faces.push_back(shapeToFace(s, m_landmarkFormat));
        }
    }

    FaceModel getMeanShape(const drishti::ml::ShapeEstimator& regressor, const cv::Rect2f& roi) const
    {
        auto mu = regressor.getMeanShape();
        drishti::core::Shape shape;
        for (auto& p : mu)
        {
            shape.contour.emplace_back(roi.x + p.x * roi.width, roi.y + p.y * roi.height, 0);
        }

        DRISHTI_FACE::FaceModel face = shapeToFace(shape, m_landmarkFormat);

        return face;
    }

    FaceModel getMeanShape(const cv::Size2f& size) const
    {
        return getMeanShape(*m_regressor, cv::Rect2f({ 0.f, 0.f }, size));
    }

    FaceModel getMeanShape(const cv::Rect2f& roi) const
    {
        return getMeanShape(*m_regressor, roi);
    }

    cv::Matx33f getAffineMotionFromRegressorToDetector(const ml::ShapeEstimator& regressor)
    {
        FaceModel faceRegressorMean = getMeanShape(regressor, { 0.f, 0.f, 1.f, 1.f });

#if DRISHTI_FACE_DETECTOR_DO_SIMILARITY_MOTION
        cv::Mat M = estimateMotionLeastSquares(faceRegressorMean, m_faceDetectorMean);
#else
        cv::Mat M = getAffineMotion(faceRegressorMean, m_faceDetectorMean);
#endif
        cv::Matx33f Hrd = cv::Matx33f::eye();
        for (int y = 0; y < 2; y++)
        {
            for (int x = 0; x < 3; x++)
            {
                Hrd(y, x) = M.at<double>(y, x);
            }
        }
        return Hrd;
    }

    void setFaceDetectorMean(const FaceModel& mu)
    {
        m_faceDetectorMean = mu;
        if (m_regressor)
        {
            m_Hrd = getAffineMotionFromRegressorToDetector(*m_regressor);
        }
        if (m_regressor2)
        {
            m_Hrd2 = getAffineMotionFromRegressorToDetector(*m_regressor2);
        }
    }

    const FaceModel& getFaceDetectorMean()
    {
        return m_faceDetectorMean;
    }

    void setScaling(float scale)
    {
        m_scaling = scale;
    }
    void setDoIrisRefinement(bool flag)
    {
        m_doIrisRefinement = flag;
    }
    void setDoEyeRefinement(bool flag)
    {
        m_doEyeRefinement = flag;
    }
    void setInits(int inits)
    {
        m_inits = inits;
    }
    void setDoNMS(bool doNMS)
    {
        m_detector->setDoNonMaximaSuppression(doNMS);
    }
    void setDoNMSGlobal(bool doNMS)
    {
        m_doNMSGlobal = doNMS;
    }
    void setLogger(MatLoggerType logger)
    {
        if (m_detector)
        {
            std::cerr << "TODO: must add logger method" << std::endl;
            //m_detector->setLogger(logger);
        }
    }
    void setDetectionTimeLogger(TimeLoggerType logger)
    {
        m_detectionTimeLogger = logger;
    }
    void setRegressionTimeLogger(TimeLoggerType logger)
    {
        m_regressionTimeLogger = logger;
    }
    void setEyeRegressionTimeLogger(TimeLoggerType logger)
    {
        m_eyeRegressionTimeLogger = logger;
    }
    void setEyeCropper(EyeCropper& cropper)
    {
        m_eyeCropper = cropper;
    }

    void setUprightImage(const cv::Mat& Ib)
    {
        m_Ib = Ib;
    }
    const cv::Mat& getUprightImage() const
    {
        return m_Ib;
    }

    void setFaceStagesHint(int stages)
    {
        if (m_regressor)
        {
            m_regressor->setStagesHint(stages);
        }
    }
    void setFace2StagesHint(int stages)
    {
        if (m_regressor2)
        {
            m_regressor2->setStagesHint(stages);
        }
    }
    void setEyelidStagesHint(int stages)
    {
        for (auto& regressor : m_eyeRegressor)
        {
            regressor->setEyelidStagesHint(stages);
        }
    }
    void setIrisStagesHint(int stages)
    {
        for (auto& regressor : m_eyeRegressor)
        {
            regressor->setIrisStagesHint(stages);
        }
    }
    void setIrisStagesRepetitionFactor(int x)
    {
        for (auto& regressor : m_eyeRegressor)
        {
            regressor->setIrisStagesRepetitionFactor(x);
        }
    }

    //private:

    FaceSpecification::Format m_landmarkFormat = FaceSpecification::HELEN;

    cv::Mat m_Ib; // TODO: review.  needed for clean virtual api

    bool m_doIrisRefinement = true;
    bool m_doEyeRefinement = true;
    bool m_doNMSGlobal = false;
    int m_inits = 1;
    float m_scaling = 1.0;

    FaceModel m_faceDetectorMean;
    cv::Matx33f m_Hrd = cv::Matx33f::eye();
    cv::Matx33f m_Hrd2 = cv::Matx33f::eye();

    TimeLoggerType m_detectionTimeLogger;
    TimeLoggerType m_regressionTimeLogger;
    TimeLoggerType m_eyeRegressionTimeLogger;
    std::unique_ptr<drishti::ml::ObjectDetector> m_detector;
    std::unique_ptr<drishti::ml::ShapeEstimator> m_regressor;
    std::unique_ptr<drishti::ml::ShapeEstimator> m_regressor2;

    std::vector<std::unique_ptr<DRISHTI_EYE::EyeModelEstimator>> m_eyeRegressor;

    EyeCropper m_eyeCropper;
};

// ((((((((((((( API )))))))))))))

std::vector<cv::Point2f> FaceDetector::getFeatures() const
{
    // noop
    std::vector<cv::Point2f> features;
    return features;
}

void FaceDetector::setDoIrisRefinement(bool flag)
{
    m_impl->setDoIrisRefinement(flag);
}
void FaceDetector::setDoEyeRefinement(bool flag)
{
    m_impl->setDoEyeRefinement(flag);
}
void FaceDetector::setInits(int inits)
{
    m_impl->setInits(inits);
}
void FaceDetector::setDoNMS(bool doNMS)
{
    m_impl->setDoNMS(doNMS);
}
void FaceDetector::setDoNMSGlobal(bool doNMS)
{
    m_impl->setDoNMSGlobal(doNMS);
}
void FaceDetector::setFaceDetectorMean(const FaceModel& mu)
{
    m_impl->setFaceDetectorMean(mu);
}
void FaceDetector::setLogger(MatLoggerType logger)
{
    m_impl->setLogger(logger);
}
drishti::ml::ObjectDetector* FaceDetector::getDetector()
{
    return m_impl->m_detector.get();
}

FaceDetector::FaceDetector(FaceDetectorFactory& resources)
    : m_impl(std::make_shared<Impl>(resources))
{
}

void FaceDetector::setLandmarkFormat(FaceSpecification::Format format)
{
    m_impl->setLandmarkFormat(format);
}

cv::Mat FaceDetector::getUprightImage()
{
    return m_impl->getUprightImage();
}

void FaceDetector::detect(const MatP& I, std::vector<FaceModel>& faces)
{
    // Run detections
    std::vector<dsdkc::Shape> shapes;
    m_impl->detect(I, shapes);

#if DRISHTI_FACE_DETECTOR_PREVIEW_DETECTIONS
    previewDetections(I, shapes);
#endif

    faces.resize(shapes.size());
    for (int i = 0; i < faces.size(); i++)
    {
        faces[i].roi = shapes[i].roi;

        // project default detection points into the roi
        //FaceModel &mu = m_impl->getFaceDetectorMean();
    }

    std::transform(shapes.begin(), shapes.end(), faces.begin(), [](const dsdkc::Shape& shape) {
        return FaceModel(shape.roi);
    });
}

void FaceDetector::refine(const PaddedImage& Ib, std::vector<FaceModel>& faces, const cv::Matx33f& H, bool isDetection)
{
    if (faces.size() && m_impl)
    {
        m_impl->refineFace(Ib, faces, H, isDetection);
    }
}

//virtual void operator()(const MatP &I, const cv::Mat &Ib, std::vector<FaceModel> &faces, const cv::Matx33f &H=EYE);

// face.area() > 0 indicates detection
void FaceDetector::operator()(const MatP& I, const PaddedImage& Ib, std::vector<FaceModel>& faces, const cv::Matx33f& H)
{
    detect(I, faces);
    refine(Ib, faces, H, true);
}

// Legacy (doesn't use virtual detect):
void FaceDetector::operator()(const MatP& I, const PaddedImage& Ib, std::vector<dsdkc::Shape>& shapes, const cv::Matx33f& H)
{
}

cv::Size FaceDetector::getWindowSize() const
{
    return m_impl->m_detector->getWindowSize();
}
void FaceDetector::setDetectionTimeLogger(TimeLoggerType logger)
{
    m_impl->setDetectionTimeLogger(logger);
}
void FaceDetector::setRegressionTimeLogger(TimeLoggerType logger)
{
    m_impl->setRegressionTimeLogger(logger);
}
void FaceDetector::setEyeRegressionTimeLogger(TimeLoggerType logger)
{
    m_impl->setEyeRegressionTimeLogger(logger);
}
void FaceDetector::setEyeCropper(EyeCropper& cropper)
{
    m_impl->setEyeCropper(cropper);
}
void FaceDetector::setScaling(float scale)
{
    m_impl->setScaling(scale);
}

FaceModel FaceDetector::getMeanShape(const cv::Size2f& size) const
{
    return m_impl->getMeanShape(size);
}
FaceModel FaceDetector::getMeanShape(const cv::Rect2f& roi) const
{
    return m_impl->getMeanShape(roi);
}

const FaceModel& FaceDetector::getFaceDetectorMean() const
{
    return m_impl->getFaceDetectorMean();
}

void FaceDetector::setFaceStagesHint(int stages)
{
    m_impl->setFaceStagesHint(stages);
}
void FaceDetector::setFace2StagesHint(int stages)
{
    m_impl->setFace2StagesHint(stages);
}
void FaceDetector::setEyelidStagesHint(int stages)
{
    m_impl->setEyelidStagesHint(stages);
}
void FaceDetector::setIrisStagesHint(int stages)
{
    m_impl->setIrisStagesRepetitionFactor(stages);
}
void FaceDetector::setIrisStagesRepetitionFactor(int x)
{
    m_impl->setIrisStagesRepetitionFactor(x);
}

#if DRISHTI_FACE_DETECTOR_PREVIEW_DETECTIONS

static void previewDetections(const MatP& I, std::vector<dsdkc::Shape>& shapes)
{
    if (shapes.size())
    {
        cv::Mat canvas;
        cv::Mat It = I[0].t();
        It.convertTo(canvas, CV_8UC1, 255);
        cv::cvtColor(canvas, canvas, cv::COLOR_GRAY2BGR);
        for (auto& f : shapes)
        {
            //std::cout << "Roi:" << f.roi << std::endl;
            cv::rectangle(canvas, f.roi, { 0, 255, 0 }, 1, 8);
        }
        cv::imshow("input", canvas), cv::waitKey(0);
    }
}

#endif

// utility

// Map from normalized coordinate system to input ROI
static cv::Matx33f denormalize(const cv::Rect& roi)
{
    cv::Point2f tl(roi.tl()), br(roi.br()), center((tl + br) * 0.5f);
    cv::Matx33f C1(1, 0, -0.5, 0, 1, -0.5, 0, 0, 1);
    cv::Matx33f C2(1, 0, +center.x, 0, 1, +center.y, 0, 0, 1);
    cv::Matx33f S = cv::Matx33f::diag({ static_cast<float>(roi.width), static_cast<float>(roi.height), 1.f });
    return (C2 * S * C1);
}

static void chooseBest(std::vector<cv::Rect>& objects, std::vector<double>& scores)
{
    if (objects.size() > 1)
    {
        int best = 0;
        for (int i = 1; i < objects.size(); i++)
        {
            if (scores[i] > scores[best])
            {
                best = i;
            }
        }
        objects = { objects[best] };
        scores = { scores[best] };
    }
}

DRISHTI_FACE_NAMESPACE_END
