#ifndef __CERBERUS_GLOBALS_HPP__
#define __CERBERUS_GLOBALS_HPP__

#include <vector>

#include "concurrence.hpp"
#include "utils/object_pool.hpp"
#include "utils/mempage.hpp"
#include "utils/pointer.h"

namespace cerb_global {

    extern std::vector<cerb::ListenThread> all_threads;
    extern thread_local util::sref<cerb::Proxy const> current_proxy;
    extern thread_local util::ObjectPool<util::MemPage> page_pool;

}

#endif /* __CERBERUS_GLOBALS_HPP__ */
