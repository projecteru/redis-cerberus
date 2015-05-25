#ifndef __CERBERUS_SLOT_MAP_HPP__
#define __CERBERUS_SLOT_MAP_HPP__

#include <set>
#include <string>

#include "common.hpp"
#include "utils/address.hpp"

namespace cerb {

    class Server;
    class Proxy;

    struct RedisNode {
        util::Address addr;
        std::string node_id;
        std::string master_id;
        std::set<std::pair<slot, slot>> slot_ranges;

        RedisNode(util::Address a, std::string nid)
            : addr(std::move(a))
            , node_id(std::move(nid))
        {}

        RedisNode(util::Address a, std::string nid, std::string mid)
            : addr(std::move(a))
            , node_id(std::move(nid))
            , master_id(std::move(mid))
        {}

        RedisNode(RedisNode&& rhs)
            : addr(std::move(rhs.addr))
            , node_id(std::move(rhs.node_id))
            , master_id(std::move(rhs.master_id))
            , slot_ranges(std::move(rhs.slot_ranges))
        {}

        RedisNode(RedisNode const& rhs)
            : addr(rhs.addr)
            , node_id(rhs.node_id)
            , master_id(rhs.master_id)
            , slot_ranges(rhs.slot_ranges)
        {}

        bool is_master() const
        {
            return master_id.empty();
        }
    };

    class SlotMap {
        Server* _servers[CLUSTER_SLOT_COUNT];
    public:
        SlotMap();
        SlotMap(SlotMap const&) = delete;

        Server** begin()
        {
            return _servers;
        }

        Server** end()
        {
            return _servers + CLUSTER_SLOT_COUNT;
        }

        Server* const* begin() const
        {
            return _servers;
        }

        Server* const* end() const
        {
            return _servers + CLUSTER_SLOT_COUNT;
        }

        Server* get_by_slot(slot s)
        {
            return _servers[s];
        }

        std::set<Server*> replace_map(std::vector<RedisNode> const& nodes, Proxy* proxy);
        std::set<Server*> deliver();
        Server* random_addr() const;

        static void select_slave_if_possible();
    };

    std::vector<RedisNode> parse_slot_map(std::string const& nodes_info,
                                          std::string const& default_host);
    void write_slot_map_cmd_to(int fd);

}

#endif /* __CERBERUS_SLOT_MAP_HPP__ */
