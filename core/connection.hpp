#ifndef __CERBERUS_CONNECTION_HPP__
#define __CERBERUS_CONNECTION_HPP__

#include <set>

#include "fdutil.hpp"

namespace cerb {

    class Connection
        : public FDWrapper
    {
    public:
        explicit Connection(int fd)
            : FDWrapper(fd)
        {}

        virtual ~Connection() {}

        virtual void on_events(int events) = 0;
        virtual void after_events(std::set<Connection*>&) {}
        virtual void on_error() = 0;
    };

    class ProxyConnection
        : public Connection
    {
    public:
        explicit ProxyConnection(int fd)
            : Connection(fd)
        {}

        void after_events(std::set<Connection*>& conns) = 0;
        void on_error();
    };

}

#endif /* __CERBERUS_CONNECTION_HPP__ */
