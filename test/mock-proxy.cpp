#include "core/proxy.hpp"
#include "core/client.hpp"

using namespace cerb;

Proxy::Proxy()
    : _clients_count(0)
    , _total_cmd_elapse(0)
    , _total_remote_cost(0)
    , _total_cmd(0)
    , _last_cmd_elapse(0)
    , _last_remote_cost(0)
    , _slot_map_expired(false)
    , epfd(0)
{}

Proxy::~Proxy() {}

void Proxy::update_slot_map() {}
void Proxy::new_client(int) {}
void Proxy::pop_client(Client*) {}
void Proxy::retry_move_ask_command_later(util::sref<DataCommand>) {}
void Proxy::stat_proccessed(Interval, Interval) {}
void Proxy::inactivate_long_conn(cerb::Connection*) {}
