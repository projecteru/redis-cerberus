#include <sys/socket.h>
#include <sys/wait.h>
#include <sys/epoll.h>
#include <sys/sendfile.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <memory.h>
#include <netdb.h>
#include <netinet/tcp.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <fcntl.h>
#include <errno.h> 
#include <signal.h>
#include <algorithm>

#include "proxy.hpp"

using cerb::Connection;
using cerb::Client;
using cerb::Server;

int const MAX_EVENTS = 10;
int const PORT = 8889;

void exit_on_int(int s)
{
    fprintf(stderr, "Exit %d\n", s);
    exit(0);
}

void setnonblocking(int sockfd) {
    int opts;

    opts = fcntl(sockfd, F_GETFL);
    if(opts < 0) {
        perror("fcntl(F_GETFL)\n");
        exit(1);
    }
    opts = (opts | O_NONBLOCK);
    if(fcntl(sockfd, F_SETFL, opts) < 0) {
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

void pop_client_from(std::vector<Client*>& clients, Client* cli)
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

void pop_client(Client* cli)
{
    printf(" - To pop %p at %p\n", cli, cli->peer);
    if (cli->peer == NULL) {
        return;
    }
    Server* svr = cli->peer;
    printf(" ^ %d %d\n", svr->ready_client_index, svr->client_index);
    pop_client_from(svr->ready_clients, cli);
    pop_client_from(svr->clients, cli);
}

static Server* new_server_connection(
        cerb::Proxy* p, char const* host, int port)
{
    if (p->server_conn != NULL) {
        return p->server_conn;
    }

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("Create socket");
        exit(1);
    }

    setnonblocking(fd);
    set_tcpnodelay(fd);

    struct hostent* server = gethostbyname(host);
    if (server == NULL) {
        perror("ERROR, no such host");
        exit(1);
    }
    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof serv_addr);
    serv_addr.sin_family = AF_INET;
    memcpy(&serv_addr.sin_addr.s_addr, server->h_addr, server->h_length);
    serv_addr.sin_port = htons(port);

    p->server_conn = new Server(fd, p);
    Server* c = p->server_conn;

    struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLOUT | EPOLLET;
    ev.data.ptr = c;
    if (epoll_ctl(p->epfd, EPOLL_CTL_ADD, fd, &ev) == -1) {
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

void free_connection(cerb::Proxy* p, Connection* c)
{
    if (c == NULL) {
        return;
    }
    printf("Remote shutdown to %d with conn %p\n", c->fd, c);
    delete c;
}

void client_recv(cerb::Proxy* p, Client* conn)
{
    int n = 0, nread;
    if (conn->peer == NULL) {
        conn->peer = new_server_connection(p, "127.0.0.1", 6379);
        printf("*Connect to %d in %p\n", conn->peer->fd, conn->peer);
    }
    Server* svr = conn->peer;
    svr->push_client(conn);

    while ((nread = read(conn->fd, conn->buf + n, BUFFER_SIZE - 1)) > 0) {
        n += nread;
    }
    if (n == 0) {
        free_connection(p, conn);
        return;
    }
    printf("+File descripter %d reading %d bytes from %p\n", conn->fd, n, conn);
    if (nread == -1 && errno != EAGAIN) {
        perror("read error");
        exit(1);
    }
    conn->buf[n] = 0;
    conn->write_size = n;
    printf("Client recv =====\n%s\n===\n", conn->buf);
    struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLOUT | EPOLLET;
    ev.data.ptr = svr;
    if (epoll_ctl(p->epfd, EPOLL_CTL_MOD, svr->fd, &ev) == -1) {
        perror("epoll_ctl: mod output");
        exit(1);
    }
}

char* copy_message(char* dst, char* src, char* src_end, int* size)
{
    *size = 0;
    while (src != src_end && ++*size && ('\n' != (*(dst++) = *(src++))))
        ;
    return src;
}

int split_message(std::vector<Client*>& clients, char* message, char* message_end)
{
    int i;
    for (i = 0; i < clients.size() && message != message_end; ++i) {
        message = copy_message(clients[i]->buf, message, message_end, &clients[i]->write_size);
    }
    return i;
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

void mod_each_client(cerb::Proxy* p, std::vector<Client*>& clients)
{
    int i;
    struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLOUT | EPOLLET;
    for (i = 0; i < clients.size(); ++i) {
        clients[i]->buf[clients[i]->write_size] = 0;
        printf(" - Client %d : <%d> %s\n", i, clients[i]->write_size, clients[i]->buf);
        ev.data.ptr = clients[i];
        if (epoll_ctl(p->epfd, EPOLL_CTL_MOD, clients[i]->fd, &ev) == -1) {
            perror("epoll_ctl: mod output (r)");
            exit(1);
        }
    }
}

void server_recv(cerb::Proxy* p, Server* conn)
{
    int i, n = read_from(conn->fd, conn->buf);
    if (n == 0) {
        return;
    }
    printf("-File descripter %d reading %d bytes from %p\n", conn->fd, n, conn);
    conn->buf[n] = 0;
    printf("====\n%s\n===\n", conn->buf);

    int client_cnt = split_message(conn->ready_clients, conn->buf, conn->buf + n);
    if (client_cnt != conn->ready_clients.size()) {
        fprintf(stderr, " Error on split, actual %d. Original message\n%s\n\n", client_cnt, conn->buf);
        for (i = 0; i < client_cnt; ++i) {
            fprintf(stderr, " + Client %d : <%d> %s\n", i, conn->ready_clients[i]->write_size, conn->ready_clients[i]->buf);
        }
        exit(1);
    }

    mod_each_client(p, conn->ready_clients);
    conn->ready_clients.clear();
    struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLOUT | EPOLLET;
    ev.data.ptr = conn;
    if (epoll_ctl(p->epfd, EPOLL_CTL_MOD, conn->fd, &ev) == -1) {
        perror("epoll_ctl: mod output (sr)");
        exit(1);
    }
}

void send_to_client(cerb::Proxy* p, Client* conn)
{
    int n = conn->write_size, nwrite;
    printf("-File descripter %d writing from %p, %d bytes\n", conn->fd, conn, n);
    while (n > 0) {
        nwrite = write(conn->fd, conn->buf + conn->write_size - n, n);
        if (nwrite < n) {
            if (nwrite == -1 && errno != EAGAIN) {
                perror("-write error");
                exit(1);
            }
            break;
        }
        n -= nwrite;
    }
    printf("Job finished %d on %p\n", conn->fd, conn);
    conn->write_size = -1;
    struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLET;
    ev.data.ptr = conn;
    if (epoll_ctl(p->epfd, EPOLL_CTL_MOD, conn->fd, &ev) == -1) {
        perror("epoll_ctl: mod (w*)");
        exit(1);
    }
    return;
}

void send_to_server(cerb::Proxy* p, Server* conn)
{
    if (conn->clients.empty()) {
        return;
    }
    if (!conn->ready_clients.empty()) {
        printf(" Write in process %d\n", conn->ready_client_index);
        return;
    }
    struct iovec iov[512];
    int n = 0, nwrite, i;

    conn->ready_clients = conn->clients;
    for (i = 0; i < conn->ready_clients.size(); ++i) {
        printf(" Ready to write %d bytes from %d in %p\n", conn->ready_clients[i]->write_size, conn->ready_clients[i]->fd, conn->ready_clients[i]);
        iov[i].iov_base = conn->ready_clients[i]->buf;
        iov[i].iov_len = conn->ready_clients[i]->write_size;
        n += iov[i].iov_len;
    }
    conn->clients.clear();

    while (1) {
        printf("+File descripter %d writing from %p, total %d bytes in %d elements\n", conn->fd, conn, n, conn->ready_client_index);
        nwrite = writev(conn->fd, iov, conn->ready_clients.size());
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
    ev.data.ptr = conn;
    if (epoll_ctl(p->epfd, EPOLL_CTL_MOD, conn->fd, &ev) == -1) {
        perror("epoll_ctl: mod (w#)");
        exit(1);
    }
}

int main()
{
    signal(SIGINT, exit_on_int);
    struct epoll_event ev;
    struct sockaddr_in local;

    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        perror("sockfd\n");
        exit(1);
    }
    cerb::Acceptor listen_conn(listen_fd);
    setnonblocking(listen_conn.fd);
    int option = 1;
    if (setsockopt(listen_conn.fd, SOL_SOCKET, SO_REUSEPORT | SO_REUSEADDR, &option,sizeof(option)) < 0) {
        perror("setsockopt");
        exit(1);
    }

    bzero(&local, sizeof(local));
    local.sin_family = AF_INET;
    local.sin_addr.s_addr = htonl(INADDR_ANY);;
    local.sin_port = htons(PORT);
    if (bind(listen_conn.fd, (struct sockaddr *) &local, sizeof(local)) < 0) {
        perror("bind\n");
        exit(1);
    }
    listen(listen_conn.fd, 20);

    cerb::Proxy p;
    p.run(listen_conn);

    return 0;
}
