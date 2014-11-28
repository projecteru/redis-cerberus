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

}

#endif /* __CERBERUS_EXCEPTIONS_HPP__ */
