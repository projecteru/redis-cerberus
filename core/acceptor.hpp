#ifndef __CERBERUS_ACCEPTOR_HPP__
#define __CERBERUS_ACCEPTOR_HPP__

#include "utils/pointer.h"
#include "connection.hpp"

namespace cerb {

    class Proxy;

    class Acceptor
        : public Connection
    {
        util::sref<Proxy> const _proxy;
        bool _accepting;
    public:
        Acceptor(Proxy* p, int listen_port);
        void turn_on_accepting();

        void on_events(int);
        void on_error() {}
        std::string str() const;

        bool accepting() const
        {
            return this->_accepting;
        }
    };

}

#endif /* __CERBERUS_ACCEPTOR_HPP__ */
