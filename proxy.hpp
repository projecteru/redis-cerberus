#ifndef __CERBERUS_PROXY_HPP__
#define __CERBERUS_PROXY_HPP__

#include <vector>

#define LOGGING_OFF 1

#if LOGGING_OFF
#define printf(...) \
    do {} while(0)
#endif

int const BUFFER_SIZE = 512 * 1024;

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

    class IOConnection
        : public Connection
    {
    public:
        int write_size;
        char buf[BUFFER_SIZE];

        explicit IOConnection(int fd)
            : Connection(fd)
            , write_size(-1)
        {}
    };

    class Server
        : public IOConnection
    {
        Proxy* const _proxy;
    public:
        std::vector<Client*> clients;
        std::vector<Client*> ready_clients;

        Server(int fd, Proxy* p)
            : IOConnection(fd)
            , _proxy(p)
        {}

        ~Server();

        void triggered(Proxy* p, int events);

        void push_client(Client* cli);
    };

    class Client
        : public IOConnection
    {
    public:
        Server* peer;

        explicit Client(int fd)
            : IOConnection(fd)
            , peer(nullptr)
        {}

        ~Client();

        void triggered(Proxy* p, int events);
    };

    struct Proxy {
        int epfd;
        Server* server_conn;

        Proxy();
        ~Proxy();

        void run(Connection& listen_conn);
    };

}

#endif /* __CERBERUS_PROXY_HPP__ */
