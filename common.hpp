#ifndef __CERBERUS_COMMON_HPP__
#define __CERBERUS_COMMON_HPP__

#include "utils/typetraits.hpp"

#define VERSION "0.4.1-2015-01-31"

namespace cerb {

    typedef tp::UInt<1>::type byte;
    typedef tp::Int<8>::type rint;
    typedef unsigned int slot;
    typedef tp::UInt<8>::type msize_t;

}

#endif /* __CERBERUS_COMMON_HPP__ */
