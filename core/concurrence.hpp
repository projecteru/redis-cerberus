#ifndef __CERBERUS_CONCURRENCE_HPP__
#define __CERBERUS_CONCURRENCE_HPP__

#include <thread>

#include "utils/pointer.h"
#include "proxy.hpp"

namespace cerb {

    class ListenThread {
        int const _listen_port;
        util::sptr<Proxy> _proxy;
        util::sptr<std::thread> _thread;
    public:
        ListenThread(int listen_port, std::string const& remote);
        ListenThread(ListenThread const&) = delete;

        ListenThread(ListenThread&& rhs)
            : _listen_port(rhs._listen_port)
            , _proxy(std::move(rhs._proxy))
            , _thread(std::move(rhs._thread))
        {}

        void run();
        void join();

        util::sref<Proxy const> get_proxy() const
        {
            return *_proxy;
        }
    };

}

#endif /* __CERBERUS_CONCURRENCE_HPP__ */
