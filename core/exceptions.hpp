#ifndef __CERBERUS_EXCEPTIONS_HPP__
#define __CERBERUS_EXCEPTIONS_HPP__

#include <stdexcept>

#include "common.hpp"

namespace cerb {

    class BadRedisMessage
        : public std::runtime_error
    {
    public:
        explicit BadRedisMessage(byte token);
        explicit BadRedisMessage(std::string const& what);
    };

    class SystemError
        : public std::runtime_error
    {
    public:
        std::string const stack_trace;
        SystemError(std::string const& what, int errcode);
    };

    class UnknownHost
        : public std::runtime_error
    {
    public:
        explicit UnknownHost(std::string const& host);
    };

    class IOErrorBase
        : public std::runtime_error
    {
    protected:
        explicit IOErrorBase(std::string const& what)
            : std::runtime_error(what)
        {}
    };

    class ConnectionHungUp
        : public IOErrorBase
    {
    public:
        ConnectionHungUp()
            : IOErrorBase("Connection hung up")
        {}
    };

    class IOError
        : public IOErrorBase
    {
    public:
        int const errcode;

        IOError(std::string const& what, int errcode);
    };

    class SocketAcceptError
        : public IOError
    {
    public:
        explicit SocketAcceptError(int errcode)
            : IOError("accept", errcode)
        {}
    };

    class SocketCreateError
        : public IOError
    {
    public:
        SocketCreateError(std::string const& what, int errcode)
            : IOError(what, errcode)
        {}
    };

    class ConnectionRefused
        : public IOErrorBase
    {
    public:
        ConnectionRefused(std::string const& host, int port, int errcode);
    };

}

#endif /* __CERBERUS_EXCEPTIONS_HPP__ */
