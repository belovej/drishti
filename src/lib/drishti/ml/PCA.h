/*!
  @file   PCA.h
  @author David Hirvonen
  @brief  Internal PCA class declaration.

  \copyright Copyright 2014-2016 Elucideye, Inc. All rights reserved.
  \license{This project is released under the 3 Clause BSD License.}

*/

#ifndef __drishti_ml_PCA_h__
#define __drishti_ml_PCA_h__

#include "drishti/core/drishti_core.h"
#include "drishti/ml/drishti_ml.h"

// clang-format off
#if DRISHTI_SERIALIZE_WITH_BOOST
#  include "drishti/core/drishti_serialization_boost.h" // for export
#endif
// clang-format on

#include <opencv2/core/core.hpp>

#include <memory>

DRISHTI_ML_NAMESPACE_BEGIN

class StandardizedPCA
{
public:
    struct Standardizer
    {
        Standardizer();
        Standardizer(int size, int type);
        void create(int size, int type);

        void compute(const cv::Mat& src);
        cv::Mat standardize(const cv::Mat& src) const;
        cv::Mat unstandardize(const cv::Mat& src) const;

        cv::Mat mu, sigma;

        template <class Archive>
        void serialize(Archive& ar, const unsigned int version);
    };

    StandardizedPCA(); // null constructor for file loading
    void compute(const cv::Mat& data, cv::Mat& projection, float retainedVariance);
    void compute(const cv::Mat& data, cv::Mat& projection, int maxComponents);
    void init();

    size_t getNumComponents() const;

    cv::Mat project(const cv::Mat& data, int n = 0) const;
    cv::Mat backProject(const cv::Mat& projection) const;
    const cv::Mat& getTransposedEigenvectors() const
    {
        return m_eT;
    }

    template <class Archive>
    void serialize(Archive& ar, const unsigned int version);

    static void gemm_transpose(const cv::Mat& A, const cv::Mat& Bt, cv::Mat& result);

protected:
    Standardizer m_transform;
    std::shared_ptr<cv::PCA> m_pca;

    cv::Mat m_eT; // transposed eigenvectors
};

DRISHTI_ML_NAMESPACE_END

#if DRISHTI_SERIALIZE_WITH_BOOST
BOOST_CLASS_EXPORT_KEY(drishti::ml::StandardizedPCA); // (optional)
#endif

#endif // __drishti_ml_PCA_h__
