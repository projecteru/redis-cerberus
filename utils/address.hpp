#ifndef __CERBERUS_UTILITY_ADDRESS_HPP__
#define __CERBERUS_UTILITY_ADDRESS_HPP__

#include <string>

namespace util {

    struct Address {
        std::string host;
        int port;

        Address(std::string h, int p)
            : host(std::move(h))
            , port(p)
        {}

        Address(Address const& rhs)
            : host(rhs.host)
            , port(rhs.port)
        {}

        Address(Address&& rhs)
            : host(std::move(rhs.host))
            , port(rhs.port)
        {}

        static Address from_host_port(std::string const& addr);

        Address& operator=(Address const& rhs)
        {
            this->host = rhs.host;
            this->port = rhs.port;
            return *this;
        }

        bool operator==(Address const& rhs) const
        {
            return host == rhs.host && port == rhs.port;
        }

        bool operator<(Address const& rhs) const
        {
            if (host == rhs.host) {
                return port < rhs.port;
            }
            return host < rhs.host;
        }
    };

}

#endif /* __CERBERUS_UTILITY_ADDRESS_HPP__ */
