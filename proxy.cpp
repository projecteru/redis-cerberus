#include <cstring>
#include <climits>
#include <unistd.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/tcp.h>
#include <sys/epoll.h>
#include <algorithm>

#include "proxy.hpp"
#include "message.hpp"
#include "utils/string.h"
#include "utils/logging.hpp"

using namespace cerb;

namespace {

    int const MAX_EVENTS = 1024;

    void set_nonblocking(int sockfd) {
        int opts;

        opts = fcntl(sockfd, F_GETFL);
        if (opts < 0) {
            throw SystemError("fcntl(F_GETFL)", errno);
        }
        opts = (opts | O_NONBLOCK);
        if (fcntl(sockfd, F_SETFL, opts) < 0) {
            throw SystemError("fcntl(set nonblocking)", errno);
        }
    }

    int set_tcpnodelay(int sockfd)
    {
        int nodelay = 1;
        socklen_t len = sizeof nodelay;
        return setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, &nodelay, len);
    }

    template <typename T>
    class AutoReleaser {
        T* _p;
    public:
        explicit AutoReleaser(T* p)
            : _p(p)
        {}

        ~AutoReleaser()
        {
            delete _p;
        }

        T* operator->() const
        {
            return _p;
        }

        void detach()
        {
            _p = nullptr;
        }
    };

    void loop(cerb::Proxy* p)
    {
        struct epoll_event events[MAX_EVENTS];
        int nfds = epoll_wait(p->epfd, events, MAX_EVENTS, -1);
        if (nfds == -1) {
            if (errno == EINTR) {
                return;
            }
            throw SystemError("epoll_wait", errno);
        }

        for (int i = 0; i < nfds; ++i) {
            try {
                AutoReleaser<Connection> conn(
                    static_cast<Connection*>(events[i].data.ptr));
                conn->triggered(p, events[i].events);
                conn.detach();
            } catch (cerb::IOError& e) {
                LOG(FATAL) << " UNEXPECTED IOError " << e.what();
                exit(1);
            }
        }
    }

    int new_stream_socket()
    {
        int fd = socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) {
            throw SocketCreateError("Server create", errno);
        }
        return fd;
    }

    void connect_fd(std::string const& host, int port, int fd)
    {
        set_tcpnodelay(fd);

        struct hostent* server = gethostbyname(host.c_str());
        if (server == nullptr) {
            throw UnknownHost(host);
        }
        struct sockaddr_in serv_addr;
        bzero(&serv_addr, sizeof serv_addr);
        serv_addr.sin_family = AF_INET;
        memcpy(&serv_addr.sin_addr.s_addr, server->h_addr, server->h_length);
        serv_addr.sin_port = htons(port);

        if (connect(fd, (struct sockaddr*)&serv_addr, sizeof serv_addr) < 0) {
            if (errno == EINPROGRESS) {
                return;
            }
            throw ConnectionRefused(host, errno);
        }
        LOG(DEBUG) << "+connect " << fd;
    }

    class NodesRetriver
        : public Connection
    {
    public:
        explicit NodesRetriver(util::Address const& addr)
            : Connection(new_stream_socket())
        {
            connect_fd(addr.host, addr.port, this->fd);
        }

        void triggered(Proxy*, int) {}
    };

    void set_slot_to(std::map<slot, util::Address>& map, std::string address,
                            std::vector<std::string>::iterator slot_range_begin,
                            std::vector<std::string>::iterator slot_range_end)
    {
        util::Address addr(util::Address::from_host_port(address));
        std::for_each(slot_range_begin, slot_range_end,
                      [&](std::string const& s)
                      {
                          if (s[0] == '[') {
                              return;
                          }
                          std::vector<std::string> range(
                              util::split_str(s, "-", true));
                          map.insert(std::make_pair(atoi(range.at(1).data()) + 1,
                                     addr));
                      });
    }

    std::map<slot, util::Address> parse_slot_map(std::string const& nodes_info)
    {
        std::vector<std::string> lines(util::split_str(nodes_info, "\n", true));
        std::map<slot, util::Address> slot_map;
        std::for_each(lines.begin(), lines.end(),
                      [&](std::string const& line) {
                          std::vector<std::string> line_cont(
                              util::split_str(line, " ", true));
                          if (line_cont.size() < 9) {
                              return;
                          }
                          set_slot_to(slot_map, line_cont[1],
                                      line_cont.begin() + 8, line_cont.end());
                      });
        return std::move(slot_map);
    }

    std::map<slot, util::Address> slot_map_from_remote(util::Address const& a)
    {
        static std::string const CMD("*2\r\n$7\r\ncluster\r\n$5\r\nnodes\r\n");
        NodesRetriver s(a);
        if (-1 == write(s.fd, CMD.c_str(), CMD.size())) {
            throw IOError("Fetch cluster nodes info", errno);
        }
        Buffer r;
        r.read(s.fd);
        LOG(DEBUG) << "Cluster nodes:\n" << r.to_string();
        return parse_slot_map(r.to_string());
    }

}

Connection::~Connection()
{
    LOG(DEBUG) << "*close " << fd;
    close(fd);
}

void Acceptor::triggered(Proxy* p, int)
{
    p->accept_from(this->fd);
}

Server::Server(std::string const& host, int port, Proxy* p)
    : Connection(new_stream_socket())
    , _proxy(p)
{
    set_nonblocking(fd);
    connect_fd(host, port, this->fd);

    struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLOUT | EPOLLET;
    ev.data.ptr = this;
    if (epoll_ctl(_proxy->epfd, EPOLL_CTL_ADD, fd, &ev) == -1) {
        throw SystemError("epoll_ctl+add", errno);
    }
}

Server::~Server()
{
    _proxy->shut_server(this);
    epoll_ctl(_proxy->epfd, EPOLL_CTL_DEL, this->fd, NULL);
}

void Server::triggered(Proxy*, int events)
{
    if (events & EPOLLRDHUP) {
        delete this;
        return;
    }
    if (events & EPOLLIN) {
        try {
            this->_recv_from();
        } catch (BadRedisMessage& e) {
            LOG(FATAL) << "Receive bad message from server " << this->fd
                       << " because: " << e.what()
                       << " dump buffer (before close):";
            LOG(FATAL) << this->_buffer.to_string();
            exit(1);
        }
    }
    if (events & EPOLLOUT) {
        this->_send_to();
    }
}

void Server::_send_to()
{
    if (this->_commands.empty()) {
        return;
    }
    if (!this->_ready_commands.empty()) {
        LOG(DEBUG) << "+busy";
        return;
    }

    std::vector<struct iovec> iov;
    int n = 0, ntotal = 0, written_iov = 0;

    this->_ready_commands = std::move(this->_commands);
    std::for_each(this->_ready_commands.begin(), this->_ready_commands.end(),
                  [&](util::sref<Command>& cmd)
                  {
                      cmd->buffer.buffer_ready(iov);
                      n += cmd->buffer.size();
                  });
    LOG(DEBUG) << "+write to " << this->fd << " total vector size: " << iov.size();
    int rest_iov = iov.size();

    while (written_iov < int(iov.size())) {
        int iovcnt = std::min(rest_iov, IOV_MAX);
        int nwrite = writev(this->fd, iov.data() + written_iov, iovcnt);
        if (nwrite < 0) {
            if (errno == EAGAIN) {
                continue;
            }
            throw IOError("+writev", errno);
        }
        ntotal += nwrite;
        rest_iov -= iovcnt;
        written_iov += iovcnt;
    }

    if (ntotal != n) {
        throw IOError("+writev (should recover)", errno);
    }

    struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLET;
    ev.data.ptr = this;
    if (epoll_ctl(_proxy->epfd, EPOLL_CTL_MOD, this->fd, &ev) == -1) {
        throw SystemError("epoll_ctl+modi", errno);
    }
}

void Server::_recv_from()
{
    int n = this->_buffer.read(this->fd);
    LOG(DEBUG) << "+read from " << this->fd << " current buffer size: " << this->_buffer.size();
    if (n == 0) {
        return;
    }
    auto messages(msg::split(this->_buffer.begin(), this->_buffer.end()));
    LOG(DEBUG) << "+read from " << this->fd << " buffer size: " << this->_buffer.size();
    if (messages.size() > rint(this->_ready_commands.size())) {
        LOG(FATAL) << "+Error on split, expected size: " << this->_ready_commands.size() << " actual: " << messages.size() << " dump buffer:";
        LOG(FATAL) << this->_buffer.to_string();
        LOG(FATAL) << "=== END OF BUFFER ===";
        LOG(FATAL) << "Dump ready commands:";
        std::for_each(this->_ready_commands.begin(), this->_ready_commands.end(),
                      [&](util::sref<Command> cmd)
                      {
                          LOG(FATAL) << " Command with buffer size: " << cmd->buffer.size() << " buffer:";
                          LOG(FATAL) << cmd->buffer.to_string();
                      });
        exit(1);
    }
    auto client_it = this->_ready_commands.begin();
    for (auto range = messages.begin(); range != messages.end();
         ++range, ++client_it)
    {
        if (client_it->nul()) {
            continue;
        }
        (*client_it)->copy_response(range.range_begin(), range.range_end());
    }

    this->_ready_commands.erase(this->_ready_commands.begin(), client_it);
    if (messages.finished()) {
        this->_buffer.clear();
    } else {
        this->_buffer.truncate_from_begin(messages.interrupt_point());
    }
    struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLOUT | EPOLLET;
    ev.data.ptr = this;
    if (epoll_ctl(_proxy->epfd, EPOLL_CTL_MOD, this->fd, &ev) == -1) {
        throw SystemError("epoll_ctl+modio", errno);
    }
}

void Server::push_client_command(util::sref<Command> cmd)
{
    _commands.push_back(cmd);
}

void Server::pop_client(Client* cli)
{
    std::remove_if(this->_commands.begin(), this->_commands.end(),
                   [&](util::sref<Command>& cmd)
                   {
                       return cmd->group->client.is(cli);
                   });
    std::for_each(this->_ready_commands.begin(), this->_ready_commands.end(),
                  [&](util::sref<Command>& cmd)
                  {
                      if (cmd->group->client.is(cli)) {
                          cmd.reset();
                      }
                  });
}

Client::~Client()
{
    std::for_each(this->_peers.begin(), this->_peers.end(),
                  [&](Server* svr)
                  {
                      svr->pop_client(this);
                  });
    epoll_ctl(_proxy->epfd, EPOLL_CTL_DEL, this->fd, NULL);
}

void Client::triggered(Proxy*, int events)
{
    if (events & EPOLLRDHUP) {
        delete this;
        return;
    }
    try {
        if (events & EPOLLIN) {
            try {
                this->_recv_from();
            } catch (BadRedisMessage& e) {
                LOG(ERROR) << "Receive bad message from client " << this->fd
                           << " because: " << e.what()
                           << " dump buffer (before close):";
                LOG(ERROR) << this->_buffer.to_string();
                delete this;
                return;
            }
        }
        if (events & EPOLLOUT) {
            this->_send_to();
        }
    } catch (IOError& e) {
        LOG(ERROR) << "Client IO Error " << this->fd;
        LOG(ERROR) << "Closed because: " << e.what();
        delete this;
    }
}

void Client::_send_to()
{
    if (this->_awaiting_groups.empty()) {
        return;
    }
    if (!this->_ready_groups.empty()) {
        LOG(DEBUG) << "-busy";
        return;
    }

    std::vector<struct iovec> iov;
    int n = 0, nwrite = -1;

    this->_ready_groups = std::move(this->_awaiting_groups);
    std::for_each(this->_ready_groups.begin(), this->_ready_groups.end(),
                  [&](util::sptr<CommandGroup>& g)
                  {
                      g->append_buffer_to(iov);
                      n += g->total_buffer_size();
                  });

    LOG(DEBUG) << "-write to " << this->fd << " total vector size: " << iov.size();
    while (true) {
        nwrite = writev(this->fd, iov.data(), iov.size());
        if (nwrite < 0) {
            if (errno == EAGAIN) {
                continue;
            }
            throw IOError("-writev", errno);
        }
        break;
    }
    if (nwrite != n) {
        throw IOError("-writev (should recover)", errno);
    }
    this->_ready_groups.clear();
    this->_peers.clear();

    struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLET;
    ev.data.ptr = this;
    if (epoll_ctl(_proxy->epfd, EPOLL_CTL_MOD, this->fd, &ev) == -1) {
        throw SystemError("epoll_ctl-modi", errno);
    }

    if (!this->_buffer.empty()) {
        _process();
    }
}

void Client::_recv_from()
{
    int n = this->_buffer.read(this->fd);
    if (n == 0) {
        delete this;
        return;
    }
    if (!(_awaiting_groups.empty() && _ready_groups.empty())) {
        return;
    }
    _process();
}

void Client::_process()
{
    auto messages(split_client_command(this->_buffer, util::mkref(*this)));
    for (auto i = messages.begin(); i != messages.end(); ++i) {
        util::sptr<CommandGroup> g(std::move(*i));
        if (g->awaiting_count != 0) {
            ++_awaiting_count;
            for (auto ci = g->commands.begin(); ci != g->commands.end(); ++ci) {
                Server* svr = this->_proxy->get_server_by_slot((*ci)->key_slot);
                this->_peers.insert(svr);
                svr->push_client_command(**ci);
            }
        }
        _awaiting_groups.push_back(std::move(g));
    }
    this->_buffer.clear();
    if (0 < _awaiting_count) {
        struct epoll_event ev;
        ev.events = EPOLLIN | EPOLLOUT | EPOLLET;
        std::for_each(this->_peers.begin(), this->_peers.end(),
                      [&](Server* svr)
                      {
                           ev.data.ptr = svr;
                           if (epoll_ctl(this->_proxy->epfd, EPOLL_CTL_MOD,
                                         svr->fd, &ev) == -1)
                           {
                               throw SystemError("epoll_ctl+modio", errno);
                           }
                      });
    } else {
        _response_ready();
    }
}

void Client::_response_ready()
{
    struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLOUT | EPOLLET;
    ev.data.ptr = this;
    if (epoll_ctl(_proxy->epfd, EPOLL_CTL_MOD, this->fd, &ev) == -1) {
        throw SystemError("epoll_ctl-modio", errno);
    }
}

void Client::group_responsed()
{
    if (--_awaiting_count == 0) {
        _response_ready();
    }
}

Proxy::Proxy(util::Address const& remote)
    : _server_map([&](std::string const& host, int port)
                  {
                      return new Server(host, port, this);
                  }, slot_map_from_remote(remote))
    , epfd(epoll_create(MAX_EVENTS))
{
    if (epfd == -1) {
        throw std::runtime_error("epoll_create");
    }
}

Proxy::~Proxy()
{
    close(epfd);
}

void Proxy::set_slot_map(std::map<slot, util::Address> map)
{
    auto s(_server_map.set_map(std::move(map)));
    std::for_each(s.begin(), s.end(), [](Server* s) {delete s;});
}

void Proxy::run(int listen_port)
{
    struct epoll_event ev;
    struct sockaddr_in local;

    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        throw SocketCreateError("Listen socket", errno);
    }
    cerb::Acceptor listen_conn(listen_fd);
    set_nonblocking(listen_conn.fd);
    int option = 1;
    if (setsockopt(listen_conn.fd, SOL_SOCKET, SO_REUSEPORT | SO_REUSEADDR,
                   &option, sizeof option) < 0)
    {
        throw SystemError("set reuseport", errno);
    }

    bzero(&local, sizeof(local));
    local.sin_family = AF_INET;
    local.sin_addr.s_addr = htonl(INADDR_ANY);
    local.sin_port = htons(listen_port);
    if (bind(listen_conn.fd, (struct sockaddr*)&local, sizeof local) < 0) {
        throw SystemError("bind", errno);
    }
    listen(listen_conn.fd, 20);

    ev.events = EPOLLIN | EPOLLET;
    ev.data.ptr = &listen_conn;
    if (epoll_ctl(epfd, EPOLL_CTL_ADD, listen_conn.fd, &ev) == -1) {
        throw SystemError("epoll_ctl*listen", errno);
    }

    while (true) {
        loop(this);
    }
}

void Proxy::accept_from(int listen_fd)
{
    int cfd;
    struct sockaddr_in remote;
    socklen_t addrlen = sizeof remote;
    while ((cfd = accept(listen_fd, (struct sockaddr*)&remote, &addrlen)) > 0) {
        LOG(DEBUG) << "*accept " << cfd;
        set_nonblocking(cfd);
        set_tcpnodelay(cfd);
        Connection* c = new Client(cfd, this);
        struct epoll_event ev;
        ev.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
        ev.data.ptr = c;
        if (epoll_ctl(this->epfd, EPOLL_CTL_ADD, cfd, &ev) == -1) {
            throw SystemError("epoll_ctl-add", errno);
        }
    }
    if (cfd == -1) {
        if (errno != EAGAIN && errno != ECONNABORTED
            && errno != EPROTO && errno != EINTR)
        {
            throw SocketAcceptError(errno);
        }
    }
}

void Proxy::shut_server(Server* svr)
{
    _server_map.erase_val(svr);
}
