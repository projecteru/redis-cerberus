#include "utils/string.h"
#include "core/proxy.hpp"
#include "core/server.hpp"
#include "core/client.hpp"
#include "core/message.hpp"
#include "mock-poll.hpp"

using namespace cerb;
using cerb::msg::format_command;

static Proxy fake_proxy;
static int fd_iter = 0;

static int next_fd()
{
    return ++::fd_iter;
}

struct ServerClientTestIO
    : PollerBufferIO
{
    explicit ServerClientTestIO(util::sref<ManualPoller> p)
        : PollerBufferIO(p)
    {}

    int new_stream_socket()
    {
        return ::next_fd();
    }
};

struct ServerClientTest
    : testing::Test
{
    static std::set<Connection*> active_conns;
    static util::sref<ManualPoller> poll_obj;
    static util::sref<ServerClientTestIO> io_obj;
    static int fd_iter;
    static Server* server;

    static void set_server(Server* s)
    {
        if (ServerClientTest::server != nullptr) {
            ServerClientTest::server->close_conn();
        }
        ServerClientTest::server = s;
        if (s != nullptr) {
            ServerClientTest::active_conns.insert(s);
        }
    }

    void SetUp()
    {
        ::fd_iter = 0;
        util::sptr<ManualPoller> p(new ManualPoller);
        ServerClientTest::poll_obj = *p;
        PollNotImplement::set_impl(std::move(p));

        util::sptr<ServerClientTestIO> poll_io(new ServerClientTestIO(ServerClientTest::poll_obj));
        ServerClientTest::io_obj = *poll_io;
        CIOImplement::set_impl(std::move(poll_io));
    }

    void TearDown()
    {
        for (Connection* c: ServerClientTest::active_conns) {
            c->close();
        }
        for (Connection* c: ServerClientTest::active_conns) {
            c->after_events(ServerClientTest::active_conns);
        }
        ServerClientTest::active_conns.clear();

        ServerClientTest::io_obj.reset();
        CIOImplement::set_impl(util::mkptr(new CIOImplement));

        ServerClientTest::poll_obj.reset();
        PollNotImplement::set_impl(util::mkptr(new PollNotImplement));
        ServerClientTest::set_server(nullptr);
    }
};

std::set<Connection*> ServerClientTest::active_conns;
util::sref<ManualPoller> ServerClientTest::poll_obj(nullptr);
util::sref<ServerClientTestIO> ServerClientTest::io_obj(nullptr);
int ServerClientTest::fd_iter(0);
Server* ServerClientTest::server(nullptr);

#define ASSERT_RO_CONN(conn) \
    do {                                                                       \
        Connection* __c = (conn);                                              \
        ASSERT_FALSE(__c->closed());                                           \
        ASSERT_TRUE(ServerClientTest::poll_obj->has_pollee(__c->fd));          \
        int events = ServerClientTest::poll_obj->get_pollee_events(__c->fd);   \
        ASSERT_TRUE(poll::event_is_read(events));                              \
        ASSERT_FALSE(poll::event_is_write(events));                            \
    } while (false)

#define ASSERT_RW_CONN(conn) \
    do {                                                                       \
        Connection* __c = (conn);                                              \
        ASSERT_FALSE(__c->closed());                                           \
        ASSERT_TRUE(ServerClientTest::poll_obj->has_pollee(__c->fd));          \
        int events = ServerClientTest::poll_obj->get_pollee_events(__c->fd);   \
        ASSERT_TRUE(poll::event_is_read(events));                              \
        ASSERT_TRUE(poll::event_is_write(events));                             \
    } while (false)

Server* Proxy::get_server_by_slot(slot)
{
    return ServerClientTest::server;
}

TEST_F(ServerClientTest, ClientReadWrite)
{
    ServerClientTest::io_obj->read_buffer.push_back("+PING\r\n");

    Client* client = new Client(::next_fd(), &::fake_proxy);
    ServerClientTest::active_conns.insert(client);

    ASSERT_RO_CONN(client);
    ServerClientTest::poll_obj->clear_pollee_events(client->fd);
    client->on_events(ManualPoller::EV_READ);

    ASSERT_RW_CONN(client);
    ServerClientTest::poll_obj->clear_pollee_events(client->fd);
    client->on_events(ManualPoller::EV_WRITE);

    ASSERT_RO_CONN(client);
    ASSERT_EQ(1, ServerClientTest::io_obj->write_buffer.size());
    ASSERT_EQ("+PONG\r\n", ServerClientTest::io_obj->write_buffer[0]);
}

TEST_F(ServerClientTest, ClientReadWriteSegments)
{
    Client* client = new Client(::next_fd(), &::fake_proxy);
    ServerClientTest::active_conns.insert(client);

    ServerClientTest::io_obj->read_buffer.push_back("+PIN");

    ASSERT_RO_CONN(client);
    ServerClientTest::poll_obj->clear_pollee_events(client->fd);
    client->on_events(ManualPoller::EV_READ);

    ASSERT_RO_CONN(client);
    ServerClientTest::io_obj->read_buffer.push_back("G\r\n");
    ServerClientTest::poll_obj->clear_pollee_events(client->fd);
    client->on_events(ManualPoller::EV_READ);

    ASSERT_RW_CONN(client);
    ServerClientTest::io_obj->writing_sizes.push_back(4);
    ServerClientTest::poll_obj->clear_pollee_events(client->fd);
    client->on_events(ManualPoller::EV_WRITE);

    ASSERT_RW_CONN(client);
    ASSERT_EQ(1, ServerClientTest::io_obj->write_buffer.size());
    ASSERT_EQ("+PON", ServerClientTest::io_obj->write_buffer[0]);

    ServerClientTest::poll_obj->clear_pollee_events(client->fd);
    client->on_events(ManualPoller::EV_WRITE);

    ASSERT_RO_CONN(client);
    ASSERT_EQ(2, ServerClientTest::io_obj->write_buffer.size());
    ASSERT_EQ("+PON", ServerClientTest::io_obj->write_buffer[0]);
    ASSERT_EQ("G\r\n", ServerClientTest::io_obj->write_buffer[1]);
}

TEST_F(ServerClientTest, SimpleRemoteCommand)
{
    Client* client = new Client(::next_fd(), &::fake_proxy);
    ServerClientTest::active_conns.insert(client);
    Server* server = Server::get_server(util::Address("", 0), &::fake_proxy);
    ASSERT_NE(nullptr, server);
    ASSERT_FALSE(server->closed());
    ServerClientTest::set_server(server);

    ASSERT_RO_CONN(client);
    ASSERT_RW_CONN(server);

    ServerClientTest::poll_obj->clear_pollee_events(client->fd);
    ServerClientTest::poll_obj->clear_pollee_events(server->fd);
    ServerClientTest::io_obj->read_buffer.push_back("*2\r\n$3\r\nGET\r\n$3\r\nmio\r\n");
    client->on_events(ManualPoller::EV_READ);

    ASSERT_RO_CONN(client);
    ASSERT_RW_CONN(server);

    ServerClientTest::poll_obj->clear_pollee_events(server->fd);
    server->on_events(ManualPoller::EV_WRITE);

    ASSERT_EQ(1, ServerClientTest::io_obj->write_buffer.size());
    ASSERT_EQ("*2\r\n$3\r\nGET\r\n$3\r\nmio\r\n", ServerClientTest::io_obj->write_buffer[0]);

    ASSERT_RO_CONN(client);
    ASSERT_RO_CONN(server);
    ServerClientTest::io_obj->read_buffer.push_back("$10\r\nnaganohara\r\n");

    ServerClientTest::poll_obj->clear_pollee_events(client->fd);
    ServerClientTest::poll_obj->clear_pollee_events(server->fd);
    server->on_events(ManualPoller::EV_READ);

    ASSERT_RW_CONN(client);
    ASSERT_RO_CONN(server);
    ServerClientTest::poll_obj->clear_pollee_events(client->fd);
    client->on_events(ManualPoller::EV_WRITE);

    ASSERT_RO_CONN(client);
    ASSERT_RO_CONN(server);

    ASSERT_EQ(2, ServerClientTest::io_obj->write_buffer.size());
    ASSERT_EQ("*2\r\n$3\r\nGET\r\n$3\r\nmio\r\n", ServerClientTest::io_obj->write_buffer[0]);
    ASSERT_EQ("$10\r\nnaganohara\r\n", ServerClientTest::io_obj->write_buffer[1]);
}

TEST_F(ServerClientTest, PipeRemoteCommands)
{
    Client* client = new Client(::next_fd(), &::fake_proxy);
    ServerClientTest::active_conns.insert(client);
    Server* server = Server::get_server(util::Address("", 0), &::fake_proxy);
    ASSERT_NE(nullptr, server);
    ASSERT_FALSE(server->closed());
    ServerClientTest::set_server(server);

    ASSERT_RO_CONN(client);
    ASSERT_RW_CONN(server);

    ServerClientTest::poll_obj->clear_pollee_events(server->fd);
    server->on_events(ManualPoller::EV_WRITE);

    ASSERT_FALSE(server->closed());
    ASSERT_RO_CONN(client);
    ServerClientTest::io_obj->read_buffer.push_back("*2\r\n$3\r\nGET\r\n$3\r\nm");
    client->on_events(ManualPoller::EV_READ);

    ASSERT_RO_CONN(client);
    ServerClientTest::io_obj->read_buffer.push_back("io\r\n*2\r\n$3\r\nGET\r\n$4\r\n");
    client->on_events(ManualPoller::EV_READ);

    ASSERT_RO_CONN(client);
    ASSERT_RW_CONN(server);

    ServerClientTest::poll_obj->clear_pollee_events(client->fd);
    ServerClientTest::poll_obj->clear_pollee_events(server->fd);
    server->on_events(ManualPoller::EV_WRITE);
    ServerClientTest::io_obj->read_buffer.push_back("yuko\r\n");
    ASSERT_RO_CONN(server);
    client->on_events(ManualPoller::EV_READ);
    ASSERT_RO_CONN(client);
    ASSERT_RO_CONN(server);
    ASSERT_EQ(1, ServerClientTest::io_obj->write_buffer.size());
    ASSERT_EQ("*2\r\n$3\r\nGET\r\n$3\r\nmio\r\n", ServerClientTest::io_obj->write_buffer[0]);

    ServerClientTest::poll_obj->clear_pollee_events(client->fd);
    ServerClientTest::poll_obj->clear_pollee_events(server->fd);
    ServerClientTest::io_obj->read_buffer.push_back("$10\r\nnaganohara\r\n");
    server->on_events(ManualPoller::EV_READ);
    ASSERT_EQ(1, ServerClientTest::io_obj->write_buffer.size());
    ASSERT_EQ("*2\r\n$3\r\nGET\r\n$3\r\nmio\r\n", ServerClientTest::io_obj->write_buffer[0]);
    ASSERT_RW_CONN(client);
    ASSERT_RO_CONN(server);

    ServerClientTest::poll_obj->clear_pollee_events(client->fd);
    ServerClientTest::poll_obj->clear_pollee_events(server->fd);
    client->on_events(ManualPoller::EV_WRITE);
    ASSERT_EQ(2, ServerClientTest::io_obj->write_buffer.size());
    ASSERT_EQ("*2\r\n$3\r\nGET\r\n$3\r\nmio\r\n", ServerClientTest::io_obj->write_buffer[0]);
    ASSERT_EQ("$10\r\nnaganohara\r\n", ServerClientTest::io_obj->write_buffer[1]);
    ASSERT_RO_CONN(client);
    ASSERT_RW_CONN(server);

    ServerClientTest::io_obj->writing_sizes.push_back(8);
    server->on_events(ManualPoller::EV_WRITE);
    ASSERT_EQ(3, ServerClientTest::io_obj->write_buffer.size());
    ASSERT_EQ("*2\r\n$3\r\nGET\r\n$3\r\nmio\r\n", ServerClientTest::io_obj->write_buffer[0]);
    ASSERT_EQ("$10\r\nnaganohara\r\n", ServerClientTest::io_obj->write_buffer[1]);
    ASSERT_EQ("*2\r\n$3\r\n", ServerClientTest::io_obj->write_buffer[2]);
    ASSERT_RO_CONN(client);
    ASSERT_RW_CONN(server);

    server->on_events(ManualPoller::EV_WRITE);
    ASSERT_EQ(4, ServerClientTest::io_obj->write_buffer.size());
    ASSERT_EQ("*2\r\n$3\r\n", ServerClientTest::io_obj->write_buffer[2]);
    ASSERT_EQ("GET\r\n$4\r\nyuko\r\n", ServerClientTest::io_obj->write_buffer[3]);
    ASSERT_RO_CONN(client);
    ASSERT_RO_CONN(server);

    ServerClientTest::io_obj->read_buffer.push_back("$4\r\n");
    server->on_events(ManualPoller::EV_READ);
    ASSERT_RO_CONN(client);
    ASSERT_RO_CONN(server);

    ServerClientTest::io_obj->read_buffer.push_back("yuko\r\n");
    server->on_events(ManualPoller::EV_READ);
    ASSERT_RW_CONN(client);
    ASSERT_RO_CONN(server);

    client->on_events(ManualPoller::EV_WRITE);
    ASSERT_RO_CONN(client);
    ASSERT_RO_CONN(server);
    ASSERT_EQ(5, ServerClientTest::io_obj->write_buffer.size());
    ASSERT_EQ("*2\r\n$3\r\nGET\r\n$3\r\nmio\r\n", ServerClientTest::io_obj->write_buffer[0]);
    ASSERT_EQ("$10\r\nnaganohara\r\n", ServerClientTest::io_obj->write_buffer[1]);
    ASSERT_EQ("*2\r\n$3\r\n", ServerClientTest::io_obj->write_buffer[2]);
    ASSERT_EQ("GET\r\n$4\r\nyuko\r\n", ServerClientTest::io_obj->write_buffer[3]);
    ASSERT_EQ("$4\r\nyuko\r\n", ServerClientTest::io_obj->write_buffer[4]);
}

TEST_F(ServerClientTest, MultipleClientsPipelineTest)
{
    int const PIPE_X = 20;
    int const PIPE_Y = 200;
    int const PIPE_Z = 2000;
    int const CLIENT_COUNT = PIPE_X + PIPE_Y + PIPE_Z;
    Command::allow_write_commands();
    std::vector<Client*> clients;
    for (int i = 0; i < CLIENT_COUNT; ++i) {
        Client* c = new Client(::next_fd(), &::fake_proxy);
        clients.push_back(c);
        ServerClientTest::active_conns.insert(c);
    }
    Server* server = Server::get_server(util::Address("", 0), &::fake_proxy);
    ServerClientTest::poll_obj->clear_pollee_events(server->fd);
    ASSERT_NE(nullptr, server);
    ASSERT_FALSE(server->closed());
    ServerClientTest::set_server(server);
    for (Client* c: clients) {
        ASSERT_RO_CONN(c);
    }

    std::string const X90(90, 'x');
    std::string const OK("+OK\r\n");
    std::vector<std::string> responses;
    std::vector<std::string> requests;
    for (int i = 0; i < PIPE_X; ++i) {
        std::string cmd(msg::format_command(
            "SET", {"KEY:" + util::str(100000000000LL + i),
                    X90 + util::str(1000000000LL + i * i)}));
        requests.push_back(cmd);
        ServerClientTest::io_obj->read_buffer.push_back(std::move(cmd));
        clients[i]->on_events(ManualPoller::EV_READ);
        responses.push_back(OK);
    }

    ASSERT_RW_CONN(server);
    server->on_events(ManualPoller::EV_WRITE);
    ASSERT_EQ(PIPE_X, ServerClientTest::io_obj->write_buffer.size());
    for (int i = 0; i < PIPE_X; ++i) {
        ASSERT_EQ(requests[i], ServerClientTest::io_obj->write_buffer[i]);
    }
    ASSERT_RO_CONN(server);
    ServerClientTest::io_obj->write_buffer.clear();

    ServerClientTest::io_obj->read_buffer.push_back(util::join("", responses));
    responses.clear();
    server->on_events(ManualPoller::EV_READ);
    for (int i = 0; i < PIPE_X; ++i) {
        ASSERT_RW_CONN(clients[i]);
    }
    ASSERT_RO_CONN(server);
    for (int i = 0; i < PIPE_X; ++i) {
        clients[i]->on_events(ManualPoller::EV_WRITE);
    }
    for (int i = 0; i < PIPE_X; ++i) {
        ASSERT_RO_CONN(clients[i]);
    }
    ASSERT_EQ(PIPE_X, ServerClientTest::io_obj->write_buffer.size());
    for (int i = 0; i < PIPE_X; ++i) {
        ASSERT_EQ(OK, ServerClientTest::io_obj->write_buffer[i]);
    }
    ServerClientTest::io_obj->write_buffer.clear();

    /* Write 200 as the first group, then 2000 before the first group returns */

    requests.clear();
    for (int i = 0; i < PIPE_Y; ++i) {
        std::string cmd(msg::format_command(
            "SET", {"KEY:" + util::str(100000000000LL + i),
                    X90 + util::str(1000000000LL + i * i)}));
        requests.push_back(cmd);
        ServerClientTest::io_obj->read_buffer.push_back(std::move(cmd));
        clients[i]->on_events(ManualPoller::EV_READ);
        responses.push_back(OK);
    }

    ASSERT_RW_CONN(server);
    server->on_events(ManualPoller::EV_WRITE);
    ASSERT_EQ(PIPE_Y, ServerClientTest::io_obj->write_buffer.size());
    for (int i = 0; i < PIPE_Y; ++i) {
        ASSERT_EQ(requests[i], ServerClientTest::io_obj->write_buffer[i]);
    }
    ASSERT_RO_CONN(server);
    ServerClientTest::io_obj->write_buffer.clear();

    std::vector<std::string> requests_z;
    std::vector<std::string> responses_z;
    for (int i = 0; i < PIPE_Z; ++i) {
        int j = i + 3000;
        std::string cmd(msg::format_command(
            "GET", {"KEY:" + util::str(100000000000LL + j)}));
        requests_z.push_back(cmd);
        ServerClientTest::io_obj->read_buffer.push_back(std::move(cmd));
        clients[i]->on_events(ManualPoller::EV_READ);
        responses_z.push_back("+RESULT:" + util::str(100000000L + j) + "\r\n");
    }

    ServerClientTest::io_obj->read_buffer.push_back(util::join("", responses));
    responses.clear();
    ASSERT_RW_CONN(server);
    server->on_events(ManualPoller::EV_READ | ManualPoller::EV_WRITE);
    ASSERT_EQ(PIPE_Z - PIPE_Y, ServerClientTest::io_obj->write_buffer.size());
    for (int i = PIPE_Y; i < PIPE_Z; ++i) {
        ASSERT_EQ(requests_z[i], ServerClientTest::io_obj->write_buffer[i - PIPE_Y]);
    }
    ASSERT_RO_CONN(server);
    ServerClientTest::io_obj->write_buffer.clear();

    for (int i = 0; i < PIPE_Y; ++i) {
        ASSERT_RW_CONN(clients[i]);
    }
    for (int i = PIPE_Y; i < PIPE_Z; ++i) {
        ASSERT_RO_CONN(clients[i]);
    }

    for (int i = 0; i < PIPE_Y; ++i) {
        clients[i]->on_events(ManualPoller::EV_WRITE);
    }

    for (int i = 0; i < PIPE_Z; ++i) {
        ASSERT_RO_CONN(clients[i]);
    }
    ASSERT_RW_CONN(server);
    ASSERT_EQ(PIPE_Y, ServerClientTest::io_obj->write_buffer.size());
    for (int i = 0; i < PIPE_Y; ++i) {
        ASSERT_EQ(OK, ServerClientTest::io_obj->write_buffer[i]);
    }
    ServerClientTest::io_obj->write_buffer.clear();

    server->on_events(ManualPoller::EV_WRITE);
    ASSERT_RW_CONN(server);
    ASSERT_EQ(0, ServerClientTest::io_obj->write_buffer.size());

    std::vector<std::string> rsp_y_to_z(responses_z.begin() + PIPE_Y, responses_z.end());
    ServerClientTest::io_obj->read_buffer.push_back(util::join("", rsp_y_to_z));

    server->on_events(ManualPoller::EV_READ | ManualPoller::EV_WRITE);

    for (int i = 0; i < PIPE_Y; ++i) {
        ASSERT_RO_CONN(clients[i]);
    }
    for (int i = PIPE_Y; i < PIPE_Z; ++i) {
        ASSERT_RW_CONN(clients[i]);
    }
    ASSERT_RO_CONN(server);
    ASSERT_EQ(PIPE_Y, ServerClientTest::io_obj->write_buffer.size());
    for (int i = 0; i < PIPE_Y; ++i) {
        ASSERT_EQ(requests_z[i], ServerClientTest::io_obj->write_buffer[i]);
    }
    ServerClientTest::io_obj->write_buffer.clear();

    std::vector<std::string> rsp_0_to_y(responses_z.begin(), responses_z.begin() + PIPE_Y);
    ServerClientTest::io_obj->read_buffer.push_back(util::join("", rsp_0_to_y));
    server->on_events(ManualPoller::EV_READ);
    for (int i = 0; i < PIPE_Z; ++i) {
        ASSERT_RW_CONN(clients[i]);
    }
    ASSERT_EQ(0, ServerClientTest::io_obj->write_buffer.size());

    for (int i = 0; i < PIPE_Z; ++i) {
        clients[i]->on_events(ManualPoller::EV_WRITE);
    }
    ASSERT_EQ(PIPE_Z, ServerClientTest::io_obj->write_buffer.size());
    for (int i = 0; i < PIPE_Y; ++i) {
        ASSERT_EQ(responses_z[i], ServerClientTest::io_obj->write_buffer[i]);
    }
}
