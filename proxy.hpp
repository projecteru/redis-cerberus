#ifndef __CERBERUS_PROXY_HPP__
#define __CERBERUS_PROXY_HPP__

#include <vector>
#include <set>

#include "utils/pointer.h"
#include "common.hpp"
#include "command.hpp"
#include "slot_map.hpp"

namespace cerb {

    class Proxy;

    class Connection {
    public:
        int fd;

        explicit Connection(int fd)
            : fd(fd)
        {}

        Connection(Connection const&) = delete;

        virtual ~Connection();

        virtual void triggered(Proxy* p, int events) = 0;
    };

    class Acceptor
        : public Connection
    {
    public:
        explicit Acceptor(int fd)
            : Connection(fd)
        {}

        void triggered(Proxy* p, int events);
    };

    class Server
        : public Connection
    {
        Proxy* const _proxy;
        Buffer _buffer;

        std::vector<util::sref<Command>> _commands;
        std::vector<util::sref<Command>> _ready_commands;

        void _send_to();
        void _recv_from();
    public:
        Server(std::string const& host, int port, Proxy* p);
        ~Server();

        void triggered(Proxy* p, int events);

        void push_client_command(util::sref<Command> cmd);
        void pop_client(Client* cli);
    };

    class Client
        : public Connection
    {
        void _send_to();
        void _recv_from();

        Proxy* const _proxy;
        std::set<Server*> _peers;
        std::vector<util::sptr<CommandGroup>> _awaiting_groups;
        std::vector<util::sptr<CommandGroup>> _ready_groups;
        int _awaiting_count;
        Buffer _buffer;

        void _process();
        void _response_ready();
    public:
        Client(int fd, Proxy* p)
            : Connection(fd)
            , _proxy(p)
            , _awaiting_count(0)
        {}

        ~Client();

        void triggered(Proxy* p, int events);
        void group_responsed();
    };

    class Proxy {
        SlotMap<Server> _server_map;
    public:
        int epfd;

        explicit Proxy(std::map<slot, Address> slot_map);
        ~Proxy();

        Proxy(Proxy const&) = delete;

        Server* get_server_by_slot(slot key_slot)
        {
            return _server_map.get_by_slot(key_slot);
        }

        void set_slot_map(std::map<slot, Address> map);
        void run(int port);
        void accept_from(int listen_fd);
        void shut_server(Server* svr);
    };

}

#endif /* __CERBERUS_PROXY_HPP__ */
