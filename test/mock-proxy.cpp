#include "core/proxy.hpp"
#include "core/client.hpp"

using namespace cerb;

Proxy::Proxy(int)
    : _clients_count(0)
    , _total_cmd_elapse(0)
    , _total_remote_cost(0)
    , _total_cmd(0)
    , _last_cmd_elapse(0)
    , _last_remote_cost(0)
    , _slot_map_expired(false)
    , epfd(0)
    , acceptor(this, 0)
{}

Proxy::~Proxy() {}

void Proxy::update_slot_map() {}
void Proxy::new_client(int) {}
void Proxy::pop_client(Client*) {}
void Proxy::retry_move_ask_command_later(util::sref<DataCommand>) {}
void Proxy::stat_proccessed(Interval, Interval) {}
void Proxy::inactivate_long_conn(cerb::Connection*) {}

void Proxy::poll_add_ro(Connection* conn)
{
    poll::poll_add_read(this->epfd, conn->fd, conn);
}

void Proxy::poll_add_rw(Connection* conn)
{
    poll::poll_add_write(this->epfd, conn->fd, conn);
}

void Proxy::poll_ro(Connection* conn)
{
    poll::poll_read(this->epfd, conn->fd, conn);
}

void Proxy::poll_rw(Connection* conn)
{
    poll::poll_write(this->epfd, conn->fd, conn);
}

void Proxy::poll_del(Connection* conn)
{
    poll::poll_del(this->epfd, conn->fd);
}

void Proxy::handle_events(poll::pevent[], int)
{
    for (std::pair<Connection*, bool> conn_writable: this->_conn_poll_type) {
        Connection* c = conn_writable.first;
        if (c->closed()) {
            continue;
        }
        if (conn_writable.second) {
            this->poll_rw(c);
        } else {
            this->poll_ro(c);
        }
    }
    this->_conn_poll_type.clear();
}
