#ifndef __CERBERUS_SUBSCRIPTION_HPP__
#define __CERBERUS_SUBSCRIPTION_HPP__

#include "proxy.hpp"

namespace cerb {

    class Server;

    class LongConnection
        : public ProxyConnection
    {
    protected:
        util::sref<Server> const _attached_server;
    public:
        LongConnection(int clientfd, Server* svr);
        ~LongConnection();

        void on_events(int events);
    };

    class Subscription
        : public LongConnection
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
            std::string str() const;
        };

        ServerConn _server;
    public:
        Subscription(Proxy* proxy, int clientfd, Server* peer, Buffer subs_cmd);

        void after_events(std::set<Connection*>& active_conns);
        std::string str() const;
    };

    class BlockedListPop
        : public LongConnection
    {
        class ServerConn
            : public ProxyConnection
        {
            BlockedListPop* const _peer;
            Buffer _buffer;
        public:
            ServerConn(util::Address const& addr, Buffer cmd, BlockedListPop* peer);

            void on_events(int events);
            void on_error();
            void after_events(std::set<Connection*>& active_conns);
            std::string str() const;
        };

        ServerConn _server;
        Proxy* const _proxy;
    public:
        BlockedListPop(Proxy* proxy, int clientfd, Server* peer, Buffer cmd);

        void after_events(std::set<Connection*>& active_conns);
        std::string str() const;
        void restore_client(Buffer const& rsp, bool update_slot_map);
    };

}

#endif /* __CERBERUS_SUBSCRIPTION_HPP__ */
