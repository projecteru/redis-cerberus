#include <cstring>
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

BadRedisMessage::BadRedisMessage(std::string const& what)
    : std::runtime_error(what)
{}

static std::string error_message(int errcode)
{
    char message[256];
    return std::string(strerror_r(errcode, message, 256));
}

SystemError::SystemError(std::string const& what, int errcode)
    : std::runtime_error(what + " " + error_message(errcode))
{}

UnknownHost::UnknownHost(std::string const& host)
    : std::runtime_error("Unknown host: " +
                         (host.empty() ? "(empty string)" : host))
{}

IOError::IOError(std::string const& what, int errcode)
    : IOErrorBase(what + " " + error_message(errcode))
    , errcode(errcode)
{}
