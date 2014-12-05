#include <cstdlib>
#include <fstream>

#include "concurrence.hpp"
#include "utils/string.h"

using namespace cerb;

static void set_slot_to(std::map<slot, Address>& map, std::string address,
                        std::vector<std::string>::iterator slot_range_begin,
                        std::vector<std::string>::iterator slot_range_end)
{
    std::vector<std::string> host_port(util::split(address, ":"));
    Address addr(host_port.at(0), atoi(host_port.at(1).data()));
    std::for_each(slot_range_begin, slot_range_end,
                  [&](std::string const& s)
                  {
                      if (s[0] == '[') {
                          return;
                      }
                      std::vector<std::string> range(util::split(s, "-"));
                      map.insert(std::make_pair(atoi(range.at(1).data()) + 1,
                                 addr));
                  });
}

static std::map<slot, Address> parse_slot_map(std::string const& nodes_file)
{
    std::ifstream s(nodes_file);
    std::string line;
    std::map<slot, Address> slot_map;
    while (std::getline(s, line)) {
        std::vector<std::string> line_cont(util::split(line, " "));
        if (line_cont.size() < 9) {
            continue;
        }
        set_slot_to(slot_map, line_cont[1], line_cont.begin() + 8,
                    line_cont.end());
    }
    return std::move(slot_map);
}

ListenThread::ListenThread(int listen_port, std::string const& nodes_file)
    : _listen_port(listen_port)
    , _proxy(new Proxy(parse_slot_map(nodes_file)))
    , _thread(nullptr)
{}

void ListenThread::run()
{
    this->_thread.reset(new std::thread(
        [=]()
        {
            this->_proxy->run(this->_listen_port);
        }));
}

void ListenThread::join()
{
    this->_thread->join();
}
