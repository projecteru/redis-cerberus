#ifndef __CERBERUS_GLOBALS_HPP__
#define __CERBERUS_GLOBALS_HPP__

#include <vector>

#include "common.hpp"
#include "concurrence.hpp"
#include "utils/pointer.h"

namespace cerb_global {

    extern std::vector<cerb::ListenThread> all_threads;
    extern thread_local util::sref<cerb::Proxy const> current_proxy;
    extern thread_local cerb::msize_t allocated_buffer;

}

#endif /* __CERBERUS_GLOBALS_HPP__ */
