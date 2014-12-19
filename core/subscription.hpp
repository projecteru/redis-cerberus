#ifndef __CERBERUS_SUBSCRIPTION_HPP__
#define __CERBERUS_SUBSCRIPTION_HPP__

#include "proxy.hpp"

namespace cerb {

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

            void triggered(int events);
            void event_handled(std::set<Connection*>& active_conns);
        };

        ServerConn _server;
    public:
        Subscription(Proxy* proxy, int clientfd, util::Address const& addr,
                     Buffer subs_cmd);

        void triggered(int events);
        void event_handled(std::set<Connection*>& active_conns);
    };

}

#endif /* __CERBERUS_SUBSCRIPTION_HPP__ */
