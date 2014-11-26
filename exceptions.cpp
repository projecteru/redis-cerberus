#include <sstream>

#include "exceptions.hpp"

using namespace cerb;

static std::string format_byte_in(byte what)
{
    std::stringstream ss;
    ss << "Unexpected token " << char(what) << " (" << what << ")";
    return ss.str();
}

BadRedisMessage::BadRedisMessage(byte token)
    : std::runtime_error(format_byte_in(token))
{}
