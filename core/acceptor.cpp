#include <cppformat/format.h>

#include "acceptor.hpp"
#include "proxy.hpp"
#include "stats.hpp"
#include "utils/logging.hpp"
#include "except/exceptions.hpp"
#include "syscalls/fctl.h"
#include "syscalls/poll.h"

using namespace cerb;

Acceptor::Acceptor(Proxy* p, int listen_port)
    : Connection(fctl::new_stream_socket())
    , _proxy(p)
    , _accepting(false)
{
    fctl::set_nonblocking(this->fd);
    fctl::bind_to(this->fd, listen_port);
}

void Acceptor::turn_on_accepting()
{
    if (!this->_accepting) {
        this->_accepting = true;
        this->_proxy->poll_add_ro(this);
        LOG(INFO) << "Start accepting - " << this->str();
    }
}

void Acceptor::on_events(int)
{
    int cfd;
    while ((cfd = cio::accept(this->fd)) > 0)
    {
        fctl::set_nonblocking(cfd);
        fctl::set_tcpnodelay(cfd);
        this->_proxy->new_client(cfd);
    }
    if (cfd == -1) {
        if (errno == ENFILE || errno == EMFILE) {
            LOG(WARNING) << "Too many open files. Stop accepting from " << this->str();
            LOG(WARNING) << stats_all();
            this->_accepting = false;
            return this->_proxy->poll_del(this);
        }
        if (errno != EAGAIN && errno != ECONNABORTED
            && errno != EPROTO && errno != EINTR)
        {
            throw SocketAcceptError(errno);
        }
    }
}

std::string Acceptor::str() const
{
    return fmt::format("Acceptor({}@{})", this->fd, static_cast<void const*>(this));
}
