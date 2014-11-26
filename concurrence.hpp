#ifndef __CERBERUS_CONCURRENCE_HPP__
#define __CERBERUS_CONCURRENCE_HPP__

#include <thread>

#include "utils/pointer.h"
#include "proxy.hpp"

namespace cerb {

    class ListenThread {
        Proxy _proxy;
        int const _listen_port;
        util::sptr<std::thread> _thread;
    public:
        explicit ListenThread(int listen_port)
            : _listen_port(listen_port)
            , _thread(nullptr)
        {}

        void run();
        void join();
    };

}

#endif /* __CERBERUS_CONCURRENCE_HPP__ */
