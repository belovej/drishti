/*!
  @file   drishti_cereal_pba.h
  @author David Hirvonen
  @brief  Provides compatibility with boost::serialization Archive::is_loading::value

*/

#ifndef __drishti_core_drishti_cereal_pba_h__
#define __drishti_core_drishti_cereal_pba_h__

#include "drishti/core/drishti_core.h"

// http://uscilab.github.io/cereal/serialization_archives.html
#include <cereal/cereal.hpp>
#include <cereal/archives/portable_binary.hpp>
#include <cereal/types/vector.hpp>
#include <cereal/types/string.hpp>
#include <cereal/types/memory.hpp>

#include <cassert>
#include <fstream>

inline bool is_cpb(std::istream& is)
{
    bool ok = false;
    is.seekg(0, std::ios_base::beg);
    try
    {
        cereal::PortableBinaryInputArchive ia(is);
        ok = true;
    }
    catch (...)
    {
    }

    is.seekg(0, std::ios_base::beg);
    is.clear();
    return ok;
}

template <typename T>
void load_cpb(std::istream& is, T& object)
{
    cereal::PortableBinaryInputArchive ia(is);
    ia >> object;
}

template <typename T>
void load_cpb(const std::string& filename, T& object)
{
    std::ifstream ifs(filename, std::ios::binary);
    assert(ifs);
    load_cpb(ifs, object);
}

template <typename T>
void save_cpb(std::ostream& os, T& object)
{
#if DRISHTI_BUILD_CEREAL_OUTPUT_ARCHIVES && DRISHTI_BUILD_CEREAL_OUTPUT_ARCHIVES
    cereal::PortableBinaryOutputArchive oa(os);
    oa << object;
#endif
}

template <typename T>
void save_cpb(const std::string& filename, T& object)
{
#if DRISHTI_BUILD_CEREAL_OUTPUT_ARCHIVES && DRISHTI_BUILD_CEREAL_OUTPUT_ARCHIVES
    std::ofstream ofs(filename, std::ios::binary);
    assert(ofs);
    save_cpb(ofs, object);
#endif
}

#endif // __drishti_core_drishti_cereal_pba_h__
