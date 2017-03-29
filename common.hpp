#ifndef __CERBERUS_COMMON_HPP__
#define __CERBERUS_COMMON_HPP__

#include <chrono>

#define VERSION "0.7.11-2017-03-29"

namespace cerb {

    using byte = unsigned char;
    using rint = int64_t;
    using slot = unsigned int;
    using msize_t = uint64_t;

    typedef std::chrono::high_resolution_clock Clock;
    typedef Clock::time_point Time;
    typedef std::chrono::duration<double> Interval;

    constexpr msize_t CLUSTER_SLOT_COUNT = 16384;

}

#endif /* __CERBERUS_COMMON_HPP__ */
