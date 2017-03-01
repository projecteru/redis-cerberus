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

std::set<util::Address> Address::from_hosts_ports(std::string const& addrs)
{
    std::set<util::Address> res;
    std::vector<std::string> hosts_ports(split_str(addrs, ","));
    for (auto &s: hosts_ports) {
        if (s.size() > 0) {
            res.insert(from_host_port(s));
        }
    }
    if (res.size() == 0) {
        throw std::runtime_error("remote address is empty.");
    }
    return res;
}

std::string Address::str() const
{
    return this->host + ':' + util::str(this->port);
}
