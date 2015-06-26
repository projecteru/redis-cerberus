#include <stdexcept>

#include "string.h"
#include "address.hpp"

using namespace util;

Address Address::from_host_port(std::string const& addr)
{
    std::vector<std::string> host_port(util::split_str(addr, ":"));
    if (host_port.size() != 2) {
        throw std::runtime_error("Invalid address: " + addr);
    }
    return Address(host_port[0], util::atoi(host_port[1].data()));
}

std::string Address::str() const
{
    return this->host + ':' + util::str(this->port);
}
