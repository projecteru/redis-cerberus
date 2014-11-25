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

#include "proxy.hpp"

using namespace cerb;

int split_message(std::vector<Client*>& clients, char* message, char* message_end);

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

        printf("Events incoming %d\n", nfds);
        for (int i = 0; i < nfds; ++i) {
            printf("Index - %d\n", i);
            AutoReleaser<Connection> conn(
                static_cast<Connection*>(events[i].data.ptr));
            conn->triggered(p, events[i].events);
            conn.detach();
        }
    }

    int read_from(int fd, char* buf)
    {
        int n = 0, nread;
        while ((nread = read(fd, buf + n, BUFFER_SIZE - 1)) > 0) {
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

void Server::triggered(Proxy* p, int events)
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
        printf(" Write in process %d\n", this->ready_clients.size());
        return;
    }
    struct iovec iov[512];
    int n = 0, nwrite, i;

    this->ready_clients = this->clients;
    for (i = 0; i < this->ready_clients.size(); ++i) {
        printf(" Ready to write %d bytes from %d in %p\n", this->ready_clients[i]->write_size, this->ready_clients[i]->fd, this->ready_clients[i]);
        iov[i].iov_base = this->ready_clients[i]->buf;
        iov[i].iov_len = this->ready_clients[i]->write_size;
        n += iov[i].iov_len;
    }
    this->clients.clear();

    while (true) {
        printf("+File descripter %d writing from %p, total %d bytes in %d elements\n", this->fd, this, n, this->ready_client_index);
        nwrite = writev(this->fd, iov, this->ready_clients.size());
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

void Server::_recv_from()
{
    int i, n = read_from(this->fd, this->buf);
    if (n == 0) {
        return;
    }
    printf("-File descripter %d reading %d bytes from %p\n", this->fd, n, this);
    this->buf[n] = 0;
    printf("====\n%s\n===\n", this->buf);

    int client_cnt = split_message(this->ready_clients, this->buf, this->buf + n);
    if (client_cnt != this->ready_clients.size()) {
        fprintf(stderr, " Error on split, actual %d. Original message\n%s\n\n", client_cnt, this->buf);
        for (i = 0; i < client_cnt; ++i) {
            fprintf(stderr, " + Client %d : <%d> %s\n", i, this->ready_clients[i]->write_size, this->ready_clients[i]->buf);
        }
        exit(1);
    }

    _proxy->notify_each(this->ready_clients);
    this->ready_clients.clear();
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
    printf(" Add client %p, now %d\n", cli, svr->clients.size());
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
    printf(" ^ %d %d\n", svr->ready_clients.size(), svr->clients.size());
    pop_client_from(this->ready_clients, cli);
    pop_client_from(this->clients, cli);
}

Client::~Client()
{
    this->_proxy->shut_client(this);
}

void Client::triggered(Proxy* p, int events)
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
    printf("-File descripter %d writing from %p, %d bytes\n", this->fd, this, n);
    while (n > 0) {
        nwrite = write(this->fd, this->buf + this->write_size - n, n);
        if (nwrite < n) {
            if (nwrite == -1 && errno != EAGAIN) {
                perror("-write error");
                exit(1);
            }
            break;
        }
        n -= nwrite;
    }
    printf("Job finished %d on %p\n", this->fd, this);
    this->write_size = -1;
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
        printf("*Connect to %d in %p\n", this->peer->fd, this->peer);
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
    printf("+File descripter %d reading %d bytes from %p\n", this->fd, n, this);
    if (nread == -1 && errno != EAGAIN) {
        perror("read error");
        exit(1);
    }
    this->buf[n] = 0;
    this->write_size = n;
    printf("Client recv =====\n%s\n===\n", this->buf);
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

void Proxy::notify_each(std::vector<Client*> const& clients)
{
    int i;
    struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLOUT | EPOLLET;
    std::for_each(clients.begin()
                , clients.end()
                , [&](Client* cli)
                  {
                      cli->buf[cli->write_size] = 0;
                      printf(" - Client <%d> %s\n", cli->write_size, cli->buf);
                      ev.data.ptr = cli;
                      if (epoll_ctl(this->epfd, EPOLL_CTL_MOD, cli->fd, &ev) == -1) {
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
    printf("Listen FD\n");
    while ((conn_sock = accept(listen_fd, (struct sockaddr*)&remote,
                               &addrlen)) > 0)
    {
        printf("Accepted %d\n", conn_sock);
        set_nonblocking(conn_sock);
        set_tcpnodelay(conn_sock);
        Connection* c = new Client(conn_sock, this);
        struct epoll_event ev;
        ev.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
        ev.data.ptr = c;
        printf("Create binder %p\n", c);
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
}

void Proxy::shut_server(Server* svr)
{
    this->server_conn = nullptr;
}
