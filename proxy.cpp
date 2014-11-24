#include <sys/epoll.h>
#include <netinet/in.h>
#include <unistd.h>
#include <stdexcept>

#include "proxy.hpp"

using namespace cerb;

void setnonblocking(int sockfd);
int set_tcpnodelay(int sockfd);
void client_recv(cerb::Proxy* p, Client* conn);
void server_recv(Proxy* p, Server* conn);
void send_to_server(Proxy* p, Server* conn);
void send_to_client(Proxy* p, Client* conn);
void free_connection(Proxy* p, Connection* c);
void pop_client(Client* cli);

namespace {

    int const MAX_EVENTS = 1024;

    void accept_conn(cerb::Proxy* p, int listen_fd)
    {
        int conn_sock;
        struct sockaddr_in remote;
        socklen_t addrlen = sizeof remote;
        printf("Listen FD\n");
        while ((conn_sock = accept(listen_fd, (struct sockaddr*)&remote,
                                   &addrlen)) > 0)
        {
            printf("Accepted %d\n", conn_sock);
            setnonblocking(conn_sock);
            set_tcpnodelay(conn_sock);
            Connection* c = new Client(conn_sock);
            struct epoll_event ev;
            ev.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
            ev.data.ptr = c;
            printf("Create binder %p\n", c);
            if (epoll_ctl(p->epfd, EPOLL_CTL_ADD, conn_sock, &ev) == -1) {
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
            Connection* conn = static_cast<Connection*>(events[i].data.ptr);
            conn->triggered(p, events[i].events);
        }
    }

}

Connection::~Connection()
{
    close(fd);
}

void Acceptor::triggered(Proxy* p, int)
{
    accept_conn(p, this->fd);
}

void Server::triggered(Proxy* p, int events)
{
    if (events & EPOLLRDHUP) {
        free_connection(p, this);
        return;
    }
    if (events & EPOLLIN) {
        server_recv(p, this);
    }
    if (events & EPOLLOUT) {
        send_to_server(p, this);
    }
}

Server::~Server()
{
    _proxy->server_conn = nullptr;
}

void Server::push_client(Client* cli)
{
    clients.push_back(cli);
    printf(" Add client %p, now %d\n", cli, svr->clients.size());
}

Client::~Client()
{
    pop_client(this);
}

void Client::triggered(Proxy* p, int events)
{
    if (events & EPOLLRDHUP) {
        free_connection(p, this);
        return;
    }
    if (events & EPOLLIN) {
        client_recv(p, this);
    }
    if (events & EPOLLOUT) {
        send_to_client(p, this);
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

void Proxy::run(Connection& listen_conn)
{
    struct epoll_event ev;
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
