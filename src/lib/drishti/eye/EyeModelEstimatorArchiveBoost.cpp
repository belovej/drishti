#include "drishti/eye/EyeModelEstimator.h"
#include "drishti/eye/EyeModelEstimatorImpl.h"

BOOST_CLASS_VERSION(DRISHTI_EYE::EyeModelEstimator, 1);
BOOST_CLASS_VERSION(DRISHTI_EYE::EyeModelEstimator::Impl, 1);

// ##################################################################
// #################### portable_binary_*archive ####################
// ##################################################################

DRISHTI_EYE_NAMESPACE_BEGIN

#if !DRISHTI_BUILD_MIN_SIZE
typedef portable_binary_oarchive OArchive;
template void EyeModelEstimator::Impl::serialize<OArchive>(OArchive& ar, const unsigned int);
template void EyeModelEstimator::serialize<OArchive>(OArchive& ar, const unsigned int);
#endif

typedef portable_binary_iarchive IArchive;
template void EyeModelEstimator::Impl::serialize<IArchive>(IArchive& ar, const unsigned int);
template void EyeModelEstimator::serialize<IArchive>(IArchive& ar, const unsigned int);

DRISHTI_EYE_NAMESPACE_END

// ##################################################################
// #################### text_*archive ###############################
// ##################################################################

#if DRISHTI_USE_TEXT_ARCHIVES

#include <boost/archive/text_iarchive.hpp>
#include <boost/archive/text_oarchive.hpp>

DRISHTI_EYE_NAMESPACE_BEGIN
typedef boost::archive::text_oarchive OArchiveTXT;
template void EyeModelEstimator::Impl::serialize<OArchiveTXT>(OArchiveTXT& ar, const unsigned int);
template void EyeModelEstimator::serialize<OArchiveTXT>(OArchiveTXT& ar, const unsigned int);

typedef boost::archive::text_iarchive IArchiveTXT;
template void EyeModelEstimator::Impl::serialize<IArchiveTXT>(IArchiveTXT& ar, const unsigned int);
template void EyeModelEstimator::serialize<IArchiveTXT>(IArchiveTXT& ar, const unsigned int);
DRISHTI_EYE_NAMESPACE_END
#endif

BOOST_CLASS_EXPORT_IMPLEMENT(DRISHTI_EYE::EyeModelEstimator);
BOOST_CLASS_EXPORT_IMPLEMENT(DRISHTI_EYE::EyeModelEstimator::Impl);
