#ifndef __CERBERUS_COMMON_HPP__
#define __CERBERUS_COMMON_HPP__

#include <chrono>

#include "utils/typetraits.hpp"

#define VERSION "0.4.7-2015-03-12"

namespace cerb {

    typedef tp::UInt<1>::type byte;
    typedef tp::Int<8>::type rint;
    typedef unsigned int slot;
    typedef tp::UInt<8>::type msize_t;

    typedef std::chrono::high_resolution_clock Clock;
    typedef Clock::time_point Time;
    typedef std::chrono::duration<double> Interval;

}

#endif /* __CERBERUS_COMMON_HPP__ */
