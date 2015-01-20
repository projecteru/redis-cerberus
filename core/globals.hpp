#ifndef __CERBERUS_GLOBALS_HPP__
#define __CERBERUS_GLOBALS_HPP__

#include <vector>

#include "concurrence.hpp"
#include "utils/pointer.h"

namespace cerb_global {

    extern std::vector<cerb::ListenThread> all_threads;
    extern thread_local util::sref<cerb::Proxy const> current_proxy;

}

#endif /* __CERBERUS_GLOBALS_HPP__ */
