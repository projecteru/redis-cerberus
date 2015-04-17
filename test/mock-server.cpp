#include <functional>

#include "core/server.hpp"

using namespace cerb;

namespace {

    struct ServerManager {
        std::function<void(Server*)> const dtor;

        explicit ServerManager(std::function<void(Server*)> d)
            : dtor(d)
        {}

        ServerManager(ServerManager const&) = delete;

        ~ServerManager()
        {
            for (Server* s: allocated) {
                dtor(s);
            }
        }

        std::set<Server*> allocated;
        std::map<util::Address, Server*> addr_map;

        template <typename Constructor>
        Server* allocate(Constructor ctor)
        {
            Server* s = ctor();
            this->allocated.insert(s);
            return s;
        }

        template <typename Constructor>
        Server* get(util::Address const& addr, Constructor ctor)
        {
            auto it = this->addr_map.find(addr);
            if (it != this->addr_map.end()) {
                return it->second;
            }
            return addr_map[addr] = allocate(ctor);
        }
    };

}

Server* Server::get_server(util::Address addr, Proxy*)
{
    static ServerManager server_manager([](Server* s) { delete s; });
    Server* s = server_manager.get(addr, []() { return new Server; });
    s->addr = addr;
    return s;
}

void Server::on_events(int) {}
void Server::after_events(std::set<Connection*>&) {}
