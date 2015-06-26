#ifndef __CERBERUS_EXCEPTIONS_HPP__
#define __CERBERUS_EXCEPTIONS_HPP__

#include <cstring>
#include <sstream>
#include <stdexcept>

#include "common.hpp"
#include "utils/string.h"
#include "backtracpp/trace.h"

namespace cerb {

    class RuntimeError
        : public std::runtime_error
    {
    protected:
        RuntimeError(std::string what)
            : std::runtime_error(std::move(what))
        {}

        static std::string error_message(int errcode)
        {
            char message[256];
            return std::string(strerror_r(errcode, message, 256));
        }
    };

    class BadRedisMessage
        : public RuntimeError
    {
        static std::string format_byte_in(byte what)
        {
            std::stringstream ss;
            ss << "Unexpected token " << char(what) << " (" << int(what) << ")";
            return ss.str();
        }
    public:
        explicit BadRedisMessage(byte token)
            : RuntimeError(format_byte_in(token))
        {}

        explicit BadRedisMessage(std::string what)
            : RuntimeError(std::move(what))
        {}
    };

    class SystemError
        : public RuntimeError
    {
        static std::string stacktrace()
        {
            std::stringstream ss;
            trac::print_trace(ss);
            return ss.str();
        }
    public:
        std::string const stack_trace;
        SystemError(std::string const& what, int errcode)
            : RuntimeError(what + " " + error_message(errcode))
            , stack_trace(stacktrace())
        {}
    };

    class UnknownHost
        : public RuntimeError
    {
    public:
        explicit UnknownHost(std::string host)
            : RuntimeError("Unknown host: " + (host.empty() ? "(empty string)" : host))
        {}
    };

    class IOErrorBase
        : public RuntimeError
    {
    protected:
        explicit IOErrorBase(std::string what)
            : RuntimeError(std::move(what))
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

        IOError(std::string what, int errcode)
            : IOErrorBase(what + " " + error_message(errcode))
            , errcode(errcode)
        {}
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
        SocketCreateError(std::string what, int errcode)
            : IOError(std::move(what), errcode)
        {}
    };

    class ConnectionRefused
        : public IOErrorBase
    {
    public:
        ConnectionRefused(std::string host, int port, int errcode)
            : IOErrorBase(error_message(errcode) + " " + host + ":" + util::str(port))
        {}
    };

}

#endif /* __CERBERUS_EXCEPTIONS_HPP__ */
