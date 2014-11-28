#include <cstring>
#include <unistd.h>
#include <fcntl.h>
#include <strings.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/epoll.h>
#include <stdexcept>
#include <algorithm>

#include <iostream>

#include "proxy.hpp"
#include "message.hpp"

using namespace cerb;

namespace {

    int const MAX_EVENTS = 1024;

    void set_nonblocking(int sockfd) {
        int opts;

        opts = fcntl(sockfd, F_GETFL);
        if (opts < 0) {
            perror("fcntl(F_GETFL)\n");
            exit(1);
        }
        opts = (opts | O_NONBLOCK);
        if (fcntl(sockfd, F_SETFL, opts) < 0) {
            perror("fcntl(F_SETFL)\n");
            exit(1);
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
            perror("epoll_pwait");
            exit(EXIT_FAILURE);
        }

        for (int i = 0; i < nfds; ++i) {
            AutoReleaser<Connection> conn(
                static_cast<Connection*>(events[i].data.ptr));
            conn->triggered(p, events[i].events);
            conn.detach();
        }
    }

    int read_from(int fd, byte* buf, int offset)
    {
        int n = 0, nread;
        while ((nread = read(fd, buf + n + offset, BUFFER_SIZE - 1)) > 0) {
            n += nread;
        }
        if (nread == -1 && errno != EAGAIN) {
            perror("read error");
            exit(1);
        }
        return n;
    }

}

Connection::~Connection()
{
    close(fd);
}

void Acceptor::triggered(Proxy* p, int)
{
    p->accept_from(this->fd);
}

Server::~Server()
{
    _proxy->shut_server(this);
}

void Server::triggered(Proxy*, int events)
{
    if (events & EPOLLRDHUP) {
        delete this;
        return;
    }
    if (events & EPOLLIN) {
        this->_recv_from();
    }
    if (events & EPOLLOUT) {
        this->_send_to();
    }
}

void Server::_send_to()
{
    if (this->clients.empty()) {
        return;
    }
    if (!this->ready_clients.empty()) {
        return;
    }

    std::vector<struct iovec> iov;
    int n = 0;

    this->ready_clients = std::move(this->clients);
    std::for_each(this->ready_clients.begin(), this->ready_clients.end(),
                  [&](Client* cli)
                  {
                      struct iovec v = {cli->buf, size_t(cli->write_size)};
                      iov.push_back(v);
                      n += cli->write_size;
                  });

    while (true) {
        int nwrite = writev(this->fd, iov.data(), iov.size());
        if (nwrite <= 0 && errno == EAGAIN) {
            continue;
        }
        if (nwrite != n) {
            perror("+writev error");
            exit(1);
        }
        break;
    }
    struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLET;
    ev.data.ptr = this;
    if (epoll_ctl(_proxy->epfd, EPOLL_CTL_MOD, this->fd, &ev) == -1) {
        perror("epoll_ctl: mod (w#)");
        exit(1);
    }
}

template <typename Iterator>
static std::vector<Client*>::iterator copy_messages(
    std::vector<Client*>::iterator client_it, Iterator range, Iterator last)
{
    for (; range != last; ++client_it, ++range)
    {
        if (nullptr == *client_it) {
            continue;
        }
        (*client_it)->write_size = range.size();
        std::copy(range.range_begin(), range.range_end(), (*client_it)->buf);
    }
    return client_it;
}

void Server::_recv_from()
{
    int n = read_from(this->fd, this->buf, this->_buffer_used);
    if (n == 0) {
        return;
    }
    this->_buffer_used += n;
    this->buf[this->_buffer_used] = 0;
    try {
        auto messages(msg::split(this->buf, this->buf + this->_buffer_used));
        if (messages.size() > rint(this->ready_clients.size())) {
            fprintf(stderr, " Error on split, expected %zu, actual %lld. Original message\n%s\n\n", this->ready_clients.size(), messages.size(), this->buf);
            std::for_each(this->ready_clients.begin(), this->ready_clients.end(),
                          [&](Client* cli)
                          {
                              fprintf(stderr, " + Client <%d> %s\n", cli->write_size, cli->buf);
                          });
            exit(1);
        }
        auto client_it = copy_messages(this->ready_clients.begin(),
                                       messages.begin(), messages.end());

        _proxy->notify_each(this->ready_clients.begin(), client_it);
        this->ready_clients.erase(this->ready_clients.begin(), client_it);
        if (messages.finished()) {
            this->_buffer_used = 0;
        } else {
            int len = std::distance(messages.interrupt_point(),
                                    this->buf + this->_buffer_used);
            std::copy(messages.interrupt_point(),
                      this->buf + this->_buffer_used, this->buf);
            this->_buffer_used = len;
        }
    } catch (BadRedisMessage& e) {
        fprintf(stderr, "Bad message %d\n%s\n\n", n, this->buf);
        exit(1);
    }
    struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLOUT | EPOLLET;
    ev.data.ptr = this;
    if (epoll_ctl(_proxy->epfd, EPOLL_CTL_MOD, this->fd, &ev) == -1) {
        perror("epoll_ctl: mod output (sr)");
        exit(1);
    }
}

void Server::push_client(Client* cli)
{
    clients.push_back(cli);
}

static void pop_client_from(std::vector<Client*>& clients, Client* cli)
{
    auto i(std::find_if(clients.begin(), clients.end(),
                        [=](Connection* c)
                        {
                            return c == cli;
                        }));
    if (i != clients.end()) {
        clients.erase(i);
    }
}

void Server::pop_client(Client* cli)
{
    pop_client_from(this->clients, cli);
    std::replace(this->ready_clients.begin(), this->ready_clients.end(),
                 cli, static_cast<Client*>(nullptr));
}

Client::~Client()
{
    this->_proxy->shut_client(this);
}

void Client::triggered(Proxy*, int events)
{
    if (events & EPOLLRDHUP) {
        delete this;
        return;
    }
    if (events & EPOLLIN) {
        this->_recv_from();
    }
    if (events & EPOLLOUT) {
        this->_send_to();
    }
}

void Client::_send_to()
{
    int n = this->write_size, nwrite;
    while (n > 0) {
        nwrite = write(this->fd, this->buf + this->write_size - n, n);
        if (nwrite < n) {
            if (nwrite == -1 && errno != EAGAIN) {
                std::cerr << this->fd << ", " << this->write_size << std::endl;
                perror("-write error");
                exit(1);
            }
            break;
        }
        n -= nwrite;
    }
    this->write_size = 0;
    struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLET;
    ev.data.ptr = this;
    if (epoll_ctl(this->_proxy->epfd, EPOLL_CTL_MOD, this->fd, &ev) == -1) {
        perror("epoll_ctl: mod (w*)");
        exit(1);
    }
}

void Client::_recv_from()
{
    int n = 0, nread;
    if (this->peer == nullptr) {
        this->peer = this->_proxy->connect_to("127.0.0.1", 6379);
    }
    Server* svr = this->peer;
    svr->push_client(this);

    while ((nread = read(this->fd, this->buf + n, BUFFER_SIZE - 1)) > 0) {
        n += nread;
    }
    if (n == 0) {
        delete this;
        return;
    }
    if (nread == -1 && errno != EAGAIN) {
        perror("read error");
        exit(1);
    }
    this->buf[n] = 0;
    this->write_size = n;
    struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLOUT | EPOLLET;
    ev.data.ptr = svr;
    if (epoll_ctl(this->_proxy->epfd, EPOLL_CTL_MOD, svr->fd, &ev) == -1) {
        perror("epoll_ctl: mod output");
        exit(1);
    }
}

Proxy::Proxy()
    : epfd(epoll_create(MAX_EVENTS))
    , server_conn(nullptr)
{
    if (epfd == -1) {
        throw std::runtime_error("epoll_create");
    }
}

Proxy::~Proxy()
{
    close(epfd);
}

void Proxy::run(int port)
{
    struct epoll_event ev;
    struct sockaddr_in local;

    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        perror("sockfd\n");
        exit(1);
    }
    cerb::Acceptor listen_conn(listen_fd);
    set_nonblocking(listen_conn.fd);
    int option = 1;
    if (setsockopt(listen_conn.fd, SOL_SOCKET, SO_REUSEPORT | SO_REUSEADDR,
                   &option, sizeof option) < 0)
    {
        perror("setsockopt");
        exit(1);
    }

    bzero(&local, sizeof(local));
    local.sin_family = AF_INET;
    local.sin_addr.s_addr = htonl(INADDR_ANY);;
    local.sin_port = htons(port);
    if (bind(listen_conn.fd, (struct sockaddr*)&local, sizeof local) < 0) {
        perror("bind\n");
        exit(1);
    }
    listen(listen_conn.fd, 20);

    ev.events = EPOLLIN | EPOLLET;
    ev.data.ptr = &listen_conn;
    if (epoll_ctl(epfd, EPOLL_CTL_ADD, listen_conn.fd, &ev) == -1) {
        perror("epoll_ctl: listen_sock");
        exit(EXIT_FAILURE);
    }

    while (true) {
        loop(this);
    }
}

void Proxy::notify_each(std::vector<Client*>::iterator begin,
                        std::vector<Client*>::iterator end)
{
    struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLOUT | EPOLLET;
    std::for_each(begin, end,
                  [&](Client* cli)
                  {
                      if (cli == nullptr) {
                          return;
                      }
                      cli->buf[cli->write_size] = 0;
                      ev.data.ptr = cli;
                      if (epoll_ctl(this->epfd, EPOLL_CTL_MOD, cli->fd, &ev) == -1) {
                          std::cerr << this->epfd << ", " << cli->fd << std::endl;
                          perror("epoll_ctl: mod output (r)");
                          exit(1);
                      }
                  });
}

Server* Proxy::connect_to(char const* host, int port)
{
    if (this->server_conn != nullptr) {
        return this->server_conn;
    }

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("Create socket");
        exit(1);
    }

    set_nonblocking(fd);
    set_tcpnodelay(fd);

    struct hostent* server = gethostbyname(host);
    if (server == nullptr) {
        perror("ERROR, no such host");
        exit(1);
    }
    struct sockaddr_in serv_addr;
    bzero(&serv_addr, sizeof serv_addr);
    serv_addr.sin_family = AF_INET;
    memcpy(&serv_addr.sin_addr.s_addr, server->h_addr, server->h_length);
    serv_addr.sin_port = htons(port);

    this->server_conn = new Server(fd, this);

    Server* c = this->server_conn;

    struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLOUT | EPOLLET;
    ev.data.ptr = c;
    if (epoll_ctl(this->epfd, EPOLL_CTL_ADD, fd, &ev) == -1) {
        perror("epoll_ctl: mod output");
        exit(1);
    }

    if (connect(fd, (struct sockaddr*)&serv_addr, sizeof serv_addr) < 0) {
        if (errno == EINPROGRESS) {
            return c;
        }
        perror("ERROR connecting");
        exit(1);
    }

    return c;
}

void Proxy::accept_from(int listen_fd)
{
    int conn_sock;
    struct sockaddr_in remote;
    socklen_t addrlen = sizeof remote;
    while ((conn_sock = accept(listen_fd, (struct sockaddr*)&remote,
                               &addrlen)) > 0)
    {
        set_nonblocking(conn_sock);
        set_tcpnodelay(conn_sock);
        Connection* c = new Client(conn_sock, this);
        struct epoll_event ev;
        ev.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
        ev.data.ptr = c;
        if (epoll_ctl(this->epfd, EPOLL_CTL_ADD, conn_sock, &ev) == -1) {
            perror("epoll_ctl: add");
            exit(EXIT_FAILURE);
        }
    }
    if (conn_sock == -1) {
        if (errno != EAGAIN && errno != ECONNABORTED
            && errno != EPROTO && errno != EINTR)
        {
            perror("accept");
            exit(1);
        }
    }
}

void Proxy::shut_client(Client* cli)
{
    if (this->server_conn != nullptr) {
        this->server_conn->pop_client(cli);
    }
    epoll_ctl(this->epfd, EPOLL_CTL_DEL, cli->fd, NULL);
}

void Proxy::shut_server(Server*)
{
    this->server_conn = nullptr;
}
