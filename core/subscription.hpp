#ifndef __CERBERUS_SUBSCRIPTION_HPP__
#define __CERBERUS_SUBSCRIPTION_HPP__

#include "proxy.hpp"

namespace cerb {

    class Server;

    class Subscription
        : public ProxyConnection
    {
        class ServerConn
            : public ProxyConnection
        {
            Subscription* const _peer;
        public:
            ServerConn(util::Address const& addr, Buffer subs_cmd,
                       Subscription* peer);

            void on_events(int events);
            void after_events(std::set<Connection*>& active_conns);
        };

        ServerConn _server;
        Server* const _peer;
    public:
        Subscription(Proxy* proxy, int clientfd, Server* peer, Buffer subs_cmd);
        ~Subscription();

        void on_events(int events);
        void after_events(std::set<Connection*>& active_conns);
    };

}

#endif /* __CERBERUS_SUBSCRIPTION_HPP__ */
