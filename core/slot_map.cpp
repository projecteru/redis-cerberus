#include <algorithm>
#include <stdexcept>

#include "slot_map.hpp"
#include "server.hpp"
#include "fdutil.hpp"
#include "proxy.hpp"
#include "buffer.hpp"
#include "utils/random.hpp"
#include "utils/logging.hpp"
#include "utils/string.h"

using namespace cerb;

static void fillServers(SlotMap& m)
{
    std::fill(m.begin(), m.end(), nullptr);
}

SlotMap::SlotMap()
{
    fillServers(*this);
}

static std::function<std::set<Server*>(
        Server* servers[],
        std::vector<RedisNode> const& nodes,
        Proxy* proxy)> replace_map(
    [](Server* servers[], std::vector<RedisNode> const& nodes, Proxy* proxy)
    {
        std::set<Server*> removed;
        std::set<Server*> new_mapped;
        for (auto const& node: nodes) {
            if (node.slot_ranges.empty()) {
                continue;
            }
            Server* server = Server::get_server(node.addr, proxy);
            LOG(DEBUG) << "Get " << server->fd << " for " << node.addr.str();
            for (auto const& rg: node.slot_ranges) {
                for (slot s = rg.first; s <= rg.second; ++s) {
                    removed.insert(servers[s]);
                    new_mapped.insert(server);
                    servers[s] = server;
                }
            }
        }
        std::set<Server*> r;
        std::set_difference(
            removed.begin(), removed.end(), new_mapped.begin(), new_mapped.end(),
            std::inserter(r, r.end()));
        r.erase(nullptr);
        return std::move(r);
    });

void SlotMap::replace_map(std::vector<RedisNode> const& nodes, Proxy* proxy)
{
    for (Server* s: ::replace_map(this->_servers, nodes, proxy)) {
        s->close_conn();
    }
}

void SlotMap::clear()
{
    std::set<Server*> r;
    std::for_each(this->begin(), this->end(), [&](Server* s) {r.insert(s);});
    r.erase(nullptr);
    for (Server* s: r) {
        s->close_conn();
    }
    fillServers(*this);
}

Server* SlotMap::random_addr() const
{
    return this->_servers[util::randint(0, CLUSTER_SLOT_COUNT)];
}

static RedisNode parse_node(
    std::string address, std::string node_id, std::string master_id,
    std::vector<std::string>::iterator slot_ranges_begin,
    std::vector<std::string>::iterator slot_ranges_end)
{
    RedisNode node(util::Address::from_host_port(address), node_id);
    if (master_id != "-") {
        node.master_id = master_id;
    }
    std::for_each(
        slot_ranges_begin, slot_ranges_end,
        [&](std::string const& rg)
        {
            if (rg[0] == '[') {
                return;
            }
            std::vector<std::string> begin_end(util::split_str(rg, "-", true));
            if (begin_end.empty()) {
                return;
            }
            if (begin_end.size() == 1) {
                slot s = util::atoi(begin_end[0]);
                node.slot_ranges.insert(std::make_pair(s, s));
                return;
            }
            node.slot_ranges.insert(
                std::make_pair(util::atoi(begin_end[0]), util::atoi(begin_end[1])));
        });
    return std::move(node);
}

std::vector<RedisNode> cerb::parse_slot_map(std::string const& nodes_info,
                                            std::string const& default_host)
{
    std::vector<RedisNode> slot_map;
    for (std::string const& line: util::split_str(nodes_info, "\n", true)) {
        std::vector<std::string> line_cont(util::split_str(line, " ", true));
        if (line_cont.size() < 8) {
            continue;
        }
        if (line_cont[2].find("fail") != std::string::npos) {
            continue;
        }
        try {
            RedisNode node(parse_node(
                std::move(line_cont[1]), std::move(line_cont[0]), std::move(line_cont[3]),
                line_cont.begin() + 8, line_cont.end()));
            if (node.addr.host.empty() && line_cont[2].find("myself") != std::string::npos) {
                node.addr.host = default_host;
            }
            slot_map.push_back(std::move(node));
        } catch (std::runtime_error&) {
            LOG(ERROR) << "Discard invalid line: " << line;
        }
    }
    return std::move(slot_map);
}

static cerb::Buffer const CLUSTER_NODES_CMD(Buffer::from_string("*2\r\n$7\r\ncluster\r\n$5\r\nnodes\r\n"));

void cerb::write_slot_map_cmd_to(int fd)
{
    CLUSTER_NODES_CMD.write(fd);
}

void SlotMap::select_slave_if_possible()
{
    ::replace_map =
        [](Server* servers[], std::vector<RedisNode> const& nodes, Proxy* proxy)
        {
            std::map<std::string, RedisNode const*> slave_of_map;
            for (auto const& node: nodes) {
                if (node.is_master()) {
                    continue;
                }
                slave_of_map.insert(std::make_pair(node.master_id, &node));
            }
            std::set<Server*> removed;
            std::set<Server*> new_mapped;
            for (auto const& node: nodes) {
                if (node.slot_ranges.empty()) {
                    continue;
                }
                auto slave_i = slave_of_map.find(node.node_id);
                Server* server = Server::get_server(
                        slave_i == slave_of_map.end() ? node.addr
                                                      : slave_i->second->addr,
                        proxy);
                LOG(DEBUG) << "Select " << server->addr.str() << " for " << node.addr.str();
                for (auto const& rg: node.slot_ranges) {
                    for (slot s = rg.first; s <= rg.second; ++s) {
                        removed.insert(servers[s]);
                        new_mapped.insert(server);
                        servers[s] = server;
                    }
                }
            }
            std::set<Server*> r;
            std::set_difference(
                removed.begin(), removed.end(), new_mapped.begin(), new_mapped.end(),
                std::inserter(r, r.end()));
            r.erase(nullptr);
            return std::move(r);
        };
}
