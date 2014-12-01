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
    };

    class IOError
        : public std::runtime_error
    {
    public:
        int const errcode;

        IOError(std::string const& what, int errcode);
    };

}

#endif /* __CERBERUS_EXCEPTIONS_HPP__ */
