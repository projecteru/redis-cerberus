#include "mock-acceptor.hpp"

using namespace cerb;

static std::function<int()> client_fd_gen([]() {return -1;});
static Acceptor* acceptor(nullptr);
static int client_fd = -1;

void set_acceptor_fd_gen(std::function<int()> fd_gen)
{
    ::client_fd_gen = fd_gen;
}

Acceptor* get_acceptor()
{
    return ::acceptor;
}

Acceptor::Acceptor(util::sref<Proxy> p, int)
    : Connection(0)
    , _proxy(p)
{
    ::acceptor = this;
}

void Acceptor::on_events(int)
{
    this->_proxy->new_client(::client_fd = ::client_fd_gen());
}

int last_client_fd()
{
    return ::client_fd;
}
