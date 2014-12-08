#include "string.h"
#include "address.hpp"

using namespace util;

Address Address::from_host_port(std::string const& addr)
{
    std::vector<std::string> host_port(util::split_str(addr, ":"));
    return Address(host_port.at(0), atoi(host_port.at(1).data()));
}
