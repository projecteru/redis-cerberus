#ifndef __CERBERUS_CLIENT_HPP__
#define __CERBERUS_CLIENT_HPP__

#include <vector>

#include "command.hpp"
#include "connection.hpp"

namespace cerb {

    class Proxy;
    class Server;

    class Client
        : public ProxyConnection
    {
        void _write_response();
        void _read_request();

        Proxy* const _proxy;
        std::set<Server*> _peers;
        std::vector<util::sptr<CommandGroup>> _parsed_groups;
        std::vector<util::sptr<CommandGroup>> _awaiting_groups;
        std::vector<util::sptr<CommandGroup>> _ready_groups;
        int _awaiting_count;
        Buffer _buffer;

        void _process();
        void _response_ready();
    public:
        Client(int fd, Proxy* p)
            : ProxyConnection(fd)
            , _proxy(p)
            , _awaiting_count(0)
        {}

        ~Client();

        void on_events(int events);
        void after_events(std::set<Connection*>&);

        void group_responsed();
        void add_peer(Server* svr);
        void reactivate(util::sref<Command> cmd);
        void push_command(util::sptr<CommandGroup> g);
    };

}

#endif /* __CERBERUS_CLIENT_HPP__ */
