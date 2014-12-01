#ifndef __CERBERUS_PROXY_HPP__
#define __CERBERUS_PROXY_HPP__

#include <vector>

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

    class Server
        : public Connection
    {
        Proxy* const _proxy;
        Buffer _buffer;

        void _send_to();
        void _recv_from();
    public:
        std::vector<Client*> clients;
        std::vector<Client*> ready_clients;

        Server(int fd, Proxy* p)
            : Connection(fd)
            , _proxy(p)
        {}

        ~Server();

        void triggered(Proxy* p, int events);

        void push_client(Client* cli);
        void pop_client(Client* cli);
    };

    class Client
        : public Connection
    {
        void _send_to();
        void _recv_from();

        Proxy* const _proxy;
    public:
        Buffer buffer;
        Server* peer;

        Client(int fd, Proxy* p)
            : Connection(fd)
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
