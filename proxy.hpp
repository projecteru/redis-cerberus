#ifndef __CERBERUS_PROXY_HPP__
#define __CERBERUS_PROXY_HPP__

#include <vector>

#include "common.hpp"

int const BUFFER_SIZE = 2 * 1024 * 1024;

namespace cerb {

    class Proxy;

    class Connection {
    public:
        int fd;

        explicit Connection(int fd)
            : fd(fd)
        {}

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

    class IOConnection
        : public Connection
    {
    public:
        int write_size;
        byte buf[BUFFER_SIZE];

        explicit IOConnection(int fd)
            : Connection(fd)
            , write_size(0)
        {}
    };

    class Client;

    class Server
        : public IOConnection
    {
        Proxy* const _proxy;
        int _buffer_used;

        void _send_to();
        void _recv_from();
    public:
        std::vector<Client*> clients;
        std::vector<Client*> ready_clients;

        Server(int fd, Proxy* p)
            : IOConnection(fd)
            , _proxy(p)
            , _buffer_used(0)
        {}

        ~Server();

        void triggered(Proxy* p, int events);

        void push_client(Client* cli);
        void pop_client(Client* cli);
    };

    class Client
        : public IOConnection
    {
        void _send_to();
        void _recv_from();

        Proxy* const _proxy;
    public:
        Server* peer;

        Client(int fd, Proxy* p)
            : IOConnection(fd)
            , _proxy(p)
            , peer(nullptr)
        {}

        ~Client();

        void triggered(Proxy* p, int events);
    };

    class Proxy {
    public:
        int epfd;
        Server* server_conn;

        Proxy();
        ~Proxy();

        void run(int port);
        void notify_each(std::vector<Client*>::iterator begin,
                         std::vector<Client*>::iterator end);

        void accept_from(int listen_fd);
        Server* connect_to(char const* host, int port);
        void shut_client(Client* cli);
        void shut_server(Server* svr);
    };

}

#endif /* __CERBERUS_PROXY_HPP__ */
