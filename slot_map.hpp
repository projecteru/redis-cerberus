#ifndef __CERBERUS_SLOT_MAP_HPP__
#define __CERBERUS_SLOT_MAP_HPP__

#include <map>
#include <algorithm>

#include "common.hpp"

namespace cerb {

    class Address {
    public:
        std::string host;
        int port;

        Address(std::string h, int p)
            : host(std::move(h))
            , port(p)
        {}

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

    template <typename Type>
    class SlotMap {
        std::map<slot, Address> _slot_range_to_addr;
        std::map<Address, Type*> _addr_to_val;
        std::function<Type*(std::string const&, int)> _val_factory;
    public:
        SlotMap(std::function<Type*(std::string const&, int)> vf,
                std::map<slot, Address> map)
            : _slot_range_to_addr(std::move(map))
            , _val_factory(std::move(vf))
        {}

        SlotMap(SlotMap const&) = delete;

        Type* get_by_slot(slot s)
        {
            auto slot_it = _slot_range_to_addr.upper_bound(s);
            if (slot_it == _slot_range_to_addr.end()) {
                return nullptr;
            }
            auto val_it = _addr_to_val.find(slot_it->second);
            if (val_it == _addr_to_val.end()) {
                return _addr_to_val[slot_it->second] = _val_factory(
                    slot_it->second.host, slot_it->second.port);
            }
            return val_it->second;
        }

        void set_map(std::map<slot, Address> map)
        {
            std::map<Address, Type*> addr_to_val;
            std::for_each(map.begin(), map.end(),
                          [&](std::pair<slot, Address>& item)
                          {
                              auto val_it = _addr_to_val.find(item.second);
                              if (val_it == _addr_to_val.end()) {
                                  return;
                              }
                              addr_to_val[item.second] = val_it->second;
                              _addr_to_val.erase(val_it);
                          });
            std::for_each(_addr_to_val.begin(), _addr_to_val.end(),
                          [&](std::pair<Address, Type*>& item)
                          {
                              delete item.second;
                          });
            _addr_to_val = std::move(addr_to_val);
            _slot_range_to_addr = std::move(map);
        }

        void erase_val(Type* val)
        {
            for (auto i = _addr_to_val.begin(); i != _addr_to_val.end();) {
                if (i->second == val) {
                    i = _addr_to_val.erase(i);
                } else {
                    ++i;
                }
            }
        }
    };

}

#endif /* __CERBERUS_SLOT_MAP_HPP__ */
