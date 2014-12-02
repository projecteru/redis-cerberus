#ifndef __CERBERUS_PROXY_HPP__
#define __CERBERUS_PROXY_HPP__

#include <vector>

#include "utils/pointer.h"
#include "common.hpp"
#include "buffer.hpp"

int const BUFFER_SIZE = 2 * 1024 * 1024;

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

    class Client;

    class ClientCommand {
    public:
        Client* const client;
        Buffer response;

        ClientCommand(ClientCommand const&) = delete;

        ClientCommand(Client* c, Buffer::iterator cmd_begin,
                      Buffer::iterator cmd_end)
            : client(c)
            , response(cmd_begin, cmd_end)
        {}

        void copy_response(Buffer::iterator begin, Buffer::iterator end);
    };

    class Server
        : public Connection
    {
        Proxy* const _proxy;
        Buffer _buffer;

        std::vector<util::sref<ClientCommand>> _commands;
        std::vector<util::sref<ClientCommand>> _ready_commands;

        void _send_to();
        void _recv_from();
    public:
        Server(int fd, Proxy* p)
            : Connection(fd)
            , _proxy(p)
        {}

        ~Server();

        void triggered(Proxy* p, int events);

        void push_client_command(util::sref<ClientCommand> cmd);
        void pop_client(Client* cli);
    };

    class Client
        : public Connection
    {
        void _send_to();
        void _recv_from();

        Proxy* const _proxy;
        std::vector<util::sptr<ClientCommand>> _awaiting_commands;
        std::vector<util::sptr<ClientCommand>> _ready_commands;
        int _awaiting_responses;
    public:
        Buffer buffer;
        Server* peer;

        Client(int fd, Proxy* p)
            : Connection(fd)
            , _proxy(p)
            , _awaiting_responses(0)
            , peer(nullptr)
        {}

        ~Client();

        void triggered(Proxy* p, int events);
        void command_responsed();
    };

    class Proxy {
    public:
        int epfd;
        Server* server_conn;

        Proxy();
        ~Proxy();

        void run(int port);
        void accept_from(int listen_fd);
        Server* connect_to(char const* host, int port);
        void shut_client(Client* cli);
        void shut_server(Server* svr);
    };

}

#endif /* __CERBERUS_PROXY_HPP__ */
