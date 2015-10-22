#ifndef __CERBERUS_SERVER_HPP__
#define __CERBERUS_SERVER_HPP__

#include <map>
#include <set>

#include "proxy.hpp"
#include "buffer.hpp"
#include "connection.hpp"
#include "utils/pointer.h"
#include "utils/address.hpp"

namespace cerb {

    class Client;
    class DataCommand;

    class Server
        : public ProxyConnection
    {
        Proxy* _proxy;
        Buffer _buffer;
        BufferSet _output_buffer_set;

        std::vector<util::sref<DataCommand>> _commands;
        std::vector<util::sref<DataCommand>> _sent_commands;

        void _recv_from();
        void _reconnect(util::Address const& addr, Proxy* p);
        void _push_to_buffer_set();

        Server()
            : ProxyConnection(-1)
            , _proxy(nullptr)
            , addr("", 0)
        {}

        ~Server() = default;

        static Server* _alloc_server(util::Address const& addr, Proxy* p);
    public:
        util::Address addr;
        std::set<ProxyConnection*> attached_long_connections;

        static void send_readonly_for_each_conn();
        static Server* get_server(util::Address addr, Proxy* p);
        static std::map<util::Address, Server*>::iterator addr_begin();
        static std::map<util::Address, Server*>::iterator addr_end();

        void on_events(int events);
        void after_events(std::set<Connection*>&);
        std::string str() const;

        void on_error()
        {
            this->close_conn();
        }

        void close_conn();
        void push_client_command(util::sref<DataCommand> cmd);
        void pop_client(Client* cli);
        std::vector<util::sref<DataCommand>> deliver_commands();

        void attach_long_connection(ProxyConnection* c)
        {
            this->attached_long_connections.insert(c);
            this->_proxy->incr_long_conn();
        }

        void detach_long_connection(ProxyConnection* c)
        {
            this->attached_long_connections.erase(c);
            this->_proxy->decr_long_conn();
        }
    };

}

#endif /* __CERBERUS_SERVER_HPP__ */
