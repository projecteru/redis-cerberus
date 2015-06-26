#include <gtest/gtest.h>

#include "mock-server.hpp"
#include "core/slot_map.hpp"

struct SlotMapTest
    : testing::Test
{
    void TearDown()
    {
        clear_all_servers();
    }
};

TEST_F(SlotMapTest, ParseMap)
{
    {
        std::vector<cerb::RedisNode> nodes(cerb::parse_slot_map(
            "29fa34bf473c742c91cee391a908a30eb4139292 :7000 myself,master - 0 0 0 connected 0-16383",
            "127.0.0.1"));
        ASSERT_EQ(1, nodes.size());
        ASSERT_EQ("127.0.0.1", nodes[0].addr.host);
        ASSERT_EQ(7000, nodes[0].addr.port);
        ASSERT_EQ("29fa34bf473c742c91cee391a908a30eb4139292", nodes[0].node_id);
        ASSERT_TRUE(nodes[0].is_master());

        ASSERT_EQ(1, nodes[0].slot_ranges.size());
        std::vector<std::pair<cerb::slot, cerb::slot>> slot_ranges(
            nodes[0].slot_ranges.begin(), nodes[0].slot_ranges.end());
        ASSERT_EQ(0, slot_ranges[0].first);
        ASSERT_EQ(16383, slot_ranges[0].second);
    }

    {
        std::vector<cerb::RedisNode> nodes(cerb::parse_slot_map(
            "21952b372055dfdb5fa25b2761857831040472e1 127.0.0.1:7001 master - 0 1428573582310 1 connected 0-3883\n"
            "29fa34bf473c742c91cee391a908a30eb4139292 127.0.0.1:7000 myself,master - 0 0 0 connected 3884-16383",
            "127.0.0.1"));
        ASSERT_EQ(2, nodes.size());
        ASSERT_EQ("127.0.0.1", nodes[0].addr.host);
        ASSERT_EQ(7001, nodes[0].addr.port);
        ASSERT_EQ("21952b372055dfdb5fa25b2761857831040472e1", nodes[0].node_id);
        ASSERT_TRUE(nodes[0].is_master());

        ASSERT_EQ(1, nodes[0].slot_ranges.size());
        std::vector<std::pair<cerb::slot, cerb::slot>> slot_ranges(
            nodes[0].slot_ranges.begin(), nodes[0].slot_ranges.end());
        ASSERT_EQ(0, slot_ranges[0].first);
        ASSERT_EQ(3883, slot_ranges[0].second);

        ASSERT_EQ("127.0.0.1", nodes[1].addr.host);
        ASSERT_EQ(7000, nodes[1].addr.port);
        ASSERT_EQ("29fa34bf473c742c91cee391a908a30eb4139292", nodes[1].node_id);
        ASSERT_TRUE(nodes[1].is_master());

        ASSERT_EQ(1, nodes[1].slot_ranges.size());
        slot_ranges = std::vector<std::pair<cerb::slot, cerb::slot>>(
            nodes[1].slot_ranges.begin(), nodes[1].slot_ranges.end());
        ASSERT_EQ(3884, slot_ranges[0].first);
        ASSERT_EQ(16383, slot_ranges[0].second);
    }

    {
        std::vector<cerb::RedisNode> nodes(cerb::parse_slot_map(
            "21952b372055dfdb5fa25b2761857831040472e1 127.0.0.1:7001 master - 0 1428573582310 1 connected 0-3883 3885\n"
            "29fa34bf473c742c91cee391a908a30eb4139292 :7000 myself,master - 0 0 0 connected 3884 3886-16383",
            "127.0.0.1"));
        ASSERT_EQ(2, nodes.size());
        ASSERT_EQ("127.0.0.1", nodes[0].addr.host);
        ASSERT_EQ(7001, nodes[0].addr.port);
        ASSERT_EQ("21952b372055dfdb5fa25b2761857831040472e1", nodes[0].node_id);
        ASSERT_TRUE(nodes[0].is_master());

        ASSERT_EQ(2, nodes[0].slot_ranges.size());
        std::vector<std::pair<cerb::slot, cerb::slot>> slot_ranges(
            nodes[0].slot_ranges.begin(), nodes[0].slot_ranges.end());
        ASSERT_EQ(0, slot_ranges[0].first);
        ASSERT_EQ(3883, slot_ranges[0].second);
        ASSERT_EQ(3885, slot_ranges[1].first);
        ASSERT_EQ(3885, slot_ranges[1].second);

        ASSERT_EQ("127.0.0.1", nodes[1].addr.host);
        ASSERT_EQ(7000, nodes[1].addr.port);
        ASSERT_EQ("29fa34bf473c742c91cee391a908a30eb4139292", nodes[1].node_id);
        ASSERT_TRUE(nodes[1].is_master());

        ASSERT_EQ(2, nodes[1].slot_ranges.size());
        slot_ranges = std::vector<std::pair<cerb::slot, cerb::slot>>(
            nodes[1].slot_ranges.begin(), nodes[1].slot_ranges.end());
        ASSERT_EQ(3884, slot_ranges[0].first);
        ASSERT_EQ(3884, slot_ranges[0].second);
        ASSERT_EQ(3886, slot_ranges[1].first);
        ASSERT_EQ(16383, slot_ranges[1].second);
    }

    {
        std::vector<cerb::RedisNode> nodes(cerb::parse_slot_map(
            "21952b372055dfdb5fa25b2761857831040472e1 :7001 master - 0 1428573582310 1 connected 0-3883 3885-4000\n"
            "29fa34bf473c742c91cee391a908a30eb4139292 127.0.0.1:7000 myself,master - 0 0 0 connected 3884 4001-16383\n"
            "e391a908a30eb413929229fa34bf473c742c91ce 127.0.0.1:7002 master - 0 0 0 connected",
            "127.0.0.1"));
        ASSERT_EQ(3, nodes.size());
        ASSERT_TRUE(nodes[0].addr.host.empty());
        ASSERT_EQ(7001, nodes[0].addr.port);
        ASSERT_EQ("21952b372055dfdb5fa25b2761857831040472e1", nodes[0].node_id);
        ASSERT_TRUE(nodes[0].is_master());

        ASSERT_EQ(2, nodes[0].slot_ranges.size());
        std::vector<std::pair<cerb::slot, cerb::slot>> slot_ranges(
            nodes[0].slot_ranges.begin(), nodes[0].slot_ranges.end());
        ASSERT_EQ(0, slot_ranges[0].first);
        ASSERT_EQ(3883, slot_ranges[0].second);
        ASSERT_EQ(3885, slot_ranges[1].first);
        ASSERT_EQ(4000, slot_ranges[1].second);

        ASSERT_EQ("127.0.0.1", nodes[1].addr.host);
        ASSERT_EQ(7000, nodes[1].addr.port);
        ASSERT_EQ("29fa34bf473c742c91cee391a908a30eb4139292", nodes[1].node_id);
        ASSERT_TRUE(nodes[1].is_master());

        ASSERT_EQ(2, nodes[1].slot_ranges.size());
        slot_ranges = std::vector<std::pair<cerb::slot, cerb::slot>>(
            nodes[1].slot_ranges.begin(), nodes[1].slot_ranges.end());
        ASSERT_EQ(3884, slot_ranges[0].first);
        ASSERT_EQ(3884, slot_ranges[0].second);
        ASSERT_EQ(4001, slot_ranges[1].first);

        ASSERT_EQ("127.0.0.1", nodes[2].addr.host);
        ASSERT_EQ(7002, nodes[2].addr.port);
        ASSERT_EQ("e391a908a30eb413929229fa34bf473c742c91ce", nodes[2].node_id);
        ASSERT_TRUE(nodes[2].is_master());

        ASSERT_TRUE(nodes[2].slot_ranges.empty());
    }

    {
        std::vector<cerb::RedisNode> nodes(cerb::parse_slot_map(
            "69853562969c74ff387f9e491d025b2a86ac478f 192.168.1.100:7002 master - 0 0 3 connected 8192-12287\n"
            "2f53d0fb4a59274e83e47b1dca02697384822ca5 192.168.1.100:7006 slave 69853562969c74ff387f9e491d025b2a86ac478f 0 0 3 connected\n"
            "2560c867f9ca2ef4cc872eb85ce985373ad9e815 192.168.1.101:7003 master - 0 0 2 connected 0-4095\n"
            "933970b4fd2d1ad06166ab1d893e8cac7b129ebd 192.168.1.101:7001 master - 0 0 4 connected 4096-8191\n"
            "d3adf40539ad749d214609987563bf9903a57ffc 192.168.1.101:7007 slave 2560c867f9ca2ef4cc872eb85ce985373ad9e815 0 0 2 connected\n"
            "6c001456aff0ae537ba242d4e86fb325c5babbea 192.168.1.100:7000 myself,master - 0 0 1 connected 12288-16383\n",
            "127.0.0.1"));
        ASSERT_EQ(6, nodes.size());
        ASSERT_EQ("192.168.1.100", nodes[0].addr.host);
        ASSERT_EQ(7002, nodes[0].addr.port);
        ASSERT_EQ("69853562969c74ff387f9e491d025b2a86ac478f", nodes[0].node_id);
        ASSERT_TRUE(nodes[0].is_master());

        ASSERT_EQ(1, nodes[0].slot_ranges.size());
        std::vector<std::pair<cerb::slot, cerb::slot>> slot_ranges(
            nodes[0].slot_ranges.begin(), nodes[0].slot_ranges.end());
        ASSERT_EQ(8192, slot_ranges[0].first);
        ASSERT_EQ(12287, slot_ranges[0].second);

        ASSERT_EQ("192.168.1.100", nodes[1].addr.host);
        ASSERT_EQ(7006, nodes[1].addr.port);
        ASSERT_EQ("2f53d0fb4a59274e83e47b1dca02697384822ca5", nodes[1].node_id);
        ASSERT_FALSE(nodes[1].is_master());

        ASSERT_TRUE(nodes[1].slot_ranges.empty());

        ASSERT_EQ("192.168.1.101", nodes[2].addr.host);
        ASSERT_EQ(7003, nodes[2].addr.port);
        ASSERT_EQ("2560c867f9ca2ef4cc872eb85ce985373ad9e815", nodes[2].node_id);
        ASSERT_TRUE(nodes[2].is_master());

        ASSERT_EQ(1, nodes[2].slot_ranges.size());
        slot_ranges = std::vector<std::pair<cerb::slot, cerb::slot>>(
            nodes[2].slot_ranges.begin(), nodes[2].slot_ranges.end());
        ASSERT_EQ(0, slot_ranges[0].first);
        ASSERT_EQ(4095, slot_ranges[0].second);

        ASSERT_EQ("192.168.1.101", nodes[3].addr.host);
        ASSERT_EQ(7001, nodes[3].addr.port);
        ASSERT_EQ("933970b4fd2d1ad06166ab1d893e8cac7b129ebd", nodes[3].node_id);
        ASSERT_TRUE(nodes[3].is_master());

        ASSERT_EQ(1, nodes[3].slot_ranges.size());
        slot_ranges = std::vector<std::pair<cerb::slot, cerb::slot>>(
            nodes[3].slot_ranges.begin(), nodes[3].slot_ranges.end());
        ASSERT_EQ(4096, slot_ranges[0].first);
        ASSERT_EQ(8191, slot_ranges[0].second);

        ASSERT_EQ("192.168.1.101", nodes[4].addr.host);
        ASSERT_EQ(7007, nodes[4].addr.port);
        ASSERT_EQ("d3adf40539ad749d214609987563bf9903a57ffc", nodes[4].node_id);
        ASSERT_FALSE(nodes[4].is_master());

        ASSERT_TRUE(nodes[4].slot_ranges.empty());

        ASSERT_EQ("192.168.1.100", nodes[5].addr.host);
        ASSERT_EQ(7000, nodes[5].addr.port);
        ASSERT_EQ("6c001456aff0ae537ba242d4e86fb325c5babbea", nodes[5].node_id);
        ASSERT_TRUE(nodes[5].is_master());

        ASSERT_EQ(1, nodes[5].slot_ranges.size());
        slot_ranges = std::vector<std::pair<cerb::slot, cerb::slot>>(
            nodes[5].slot_ranges.begin(), nodes[5].slot_ranges.end());
        ASSERT_EQ(12288, slot_ranges[0].first);
        ASSERT_EQ(16383, slot_ranges[0].second);
    }
}

TEST_F(SlotMapTest, ReplaceNodesAllMasters)
{
    cerb::SlotMap slot_map;

    slot_map.replace_map(cerb::parse_slot_map(
        "21952b372055dfdb5fa25b2761857831040472e1 127.0.0.1:7001 master - 0 1428573582310 1 connected 0-3883\n"
        "29fa34bf473c742c91cee391a908a30eb4139292 127.0.0.1:7000 myself,master - 0 0 0 connected 3884-16383",
        "127.0.0.1"), nullptr);

    ASSERT_TRUE(closed_servers().empty());
    ASSERT_EQ(2, created_servers().size());
    clear_all_servers();

    for (cerb::slot s = 0; s < 3883; ++s) {
        cerb::Server* svr = slot_map.get_by_slot(s);
        ASSERT_NE(nullptr, svr) << " slot #" << s;
        ASSERT_EQ("127.0.0.1", svr->addr.host) << " slot #" << s;
        ASSERT_EQ(7001, svr->addr.port) << " slot #" << s;
    }

    for (cerb::slot s = 3884; s < 16384; ++s) {
        cerb::Server* svr = slot_map.get_by_slot(s);
        ASSERT_NE(nullptr, svr) << " slot #" << s;
        ASSERT_EQ("127.0.0.1", svr->addr.host) << " slot #" << s;
        ASSERT_EQ(7000, svr->addr.port) << " slot #" << s;
    }

    std::set<cerb::Server*> to_be_replaced({slot_map.get_by_slot(0), slot_map.get_by_slot(3884)});
    slot_map.replace_map(cerb::parse_slot_map(
        "69853562969c74ff387f9e491d025b2a86ac478f 192.168.1.100:7002 master - 0 0 3 connected 8192-12287\n"
        "2f53d0fb4a59274e83e47b1dca02697384822ca5 192.168.1.100:7006 slave 69853562969c74ff387f9e491d025b2a86ac478f 0 0 3 connected\n"
        "2560c867f9ca2ef4cc872eb85ce985373ad9e815 192.168.1.101:7003 master - 0 0 2 connected 0-4095\n"
        "933970b4fd2d1ad06166ab1d893e8cac7b129ebd 192.168.1.101:7001 master - 0 0 4 connected 4096-8191\n"
        "d3adf40539ad749d214609987563bf9903a57ffc 192.168.1.101:7007 slave 2560c867f9ca2ef4cc872eb85ce985373ad9e815 0 0 2 connected\n"
        "6c001456aff0ae537ba242d4e86fb325c5babbea 192.168.1.100:7000 myself,master - 0 0 1 connected 12288-16383\n",
        "127.0.0.1"), nullptr);
    ASSERT_EQ(to_be_replaced, closed_servers());
    ASSERT_EQ(4, created_servers().size());
    clear_all_servers();

    for (cerb::slot s = 0; s < 4096; ++s) {
        cerb::Server* svr = slot_map.get_by_slot(s);
        ASSERT_NE(nullptr, svr) << " slot #" << s;
        ASSERT_EQ("192.168.1.101", svr->addr.host) << " slot #" << s;
        ASSERT_EQ(7003, svr->addr.port) << " slot #" << s;
    }

    for (cerb::slot s = 4096; s < 8192; ++s) {
        cerb::Server* svr = slot_map.get_by_slot(s);
        ASSERT_NE(nullptr, svr) << " slot #" << s;
        ASSERT_EQ("192.168.1.101", svr->addr.host) << " slot #" << s;
        ASSERT_EQ(7001, svr->addr.port) << " slot #" << s;
    }

    for (cerb::slot s = 8192; s < 12288; ++s) {
        cerb::Server* svr = slot_map.get_by_slot(s);
        ASSERT_NE(nullptr, svr) << " slot #" << s;
        ASSERT_EQ("192.168.1.100", svr->addr.host) << " slot #" << s;
        ASSERT_EQ(7002, svr->addr.port) << " slot #" << s;
    }

    for (cerb::slot s = 12288; s < 16384; ++s) {
        cerb::Server* svr = slot_map.get_by_slot(s);
        ASSERT_NE(nullptr, svr) << " slot #" << s;
        ASSERT_EQ("192.168.1.100", svr->addr.host) << " slot #" << s;
        ASSERT_EQ(7000, svr->addr.port) << " slot #" << s;
    }

    to_be_replaced.clear();
    to_be_replaced.insert(slot_map.get_by_slot(0));

    slot_map.replace_map(cerb::parse_slot_map(
        "69853562969c74ff387f9e491d025b2a86ac478f 192.168.1.100:7002 master - 0 0 3 connected 8192-12287\n"
        "2f53d0fb4a59274e83e47b1dca02697384822ca5 192.168.1.100:7006 slave 69853562969c74ff387f9e491d025b2a86ac478f 0 0 3 connected\n"
        "2560c867f9ca2ef4cc872eb85ce985373ad9e815 192.168.1.102:7000 master - 0 0 2 connected 0-3839\n"
        "933970b4fd2d1ad06166ab1d893e8cac7b129ebd 192.168.1.101:7001 master - 0 0 4 connected 3840-8191\n"
        "6c001456aff0ae537ba242d4e86fb325c5babbea 192.168.1.100:7000 myself,master - 0 0 1 connected 12288-16383\n",
        "127.0.0.1"), nullptr);

    ASSERT_EQ(to_be_replaced, closed_servers());

    for (cerb::slot s = 0; s < 3840; ++s) {
        cerb::Server* svr = slot_map.get_by_slot(s);
        ASSERT_NE(nullptr, svr) << " slot #" << s;
        ASSERT_EQ("192.168.1.102", svr->addr.host) << " slot #" << s;
        ASSERT_EQ(7000, svr->addr.port) << " slot #" << s;
    }

    for (cerb::slot s = 3840; s < 8192; ++s) {
        cerb::Server* svr = slot_map.get_by_slot(s);
        ASSERT_NE(nullptr, svr) << " slot #" << s;
        ASSERT_EQ("192.168.1.101", svr->addr.host) << " slot #" << s;
        ASSERT_EQ(7001, svr->addr.port) << " slot #" << s;
    }

    for (cerb::slot s = 8192; s < 12288; ++s) {
        cerb::Server* svr = slot_map.get_by_slot(s);
        ASSERT_NE(nullptr, svr) << " slot #" << s;
        ASSERT_EQ("192.168.1.100", svr->addr.host) << " slot #" << s;
        ASSERT_EQ(7002, svr->addr.port) << " slot #" << s;
    }

    for (cerb::slot s = 12288; s < 16384; ++s) {
        cerb::Server* svr = slot_map.get_by_slot(s);
        ASSERT_NE(nullptr, svr) << " slot #" << s;
        ASSERT_EQ("192.168.1.100", svr->addr.host) << " slot #" << s;
        ASSERT_EQ(7000, svr->addr.port) << " slot #" << s;
    }
}

TEST_F(SlotMapTest, ReplaceNodesAlsoSlave)
{
    cerb::SlotMap::select_slave_if_possible();
    cerb::SlotMap slot_map;

    slot_map.replace_map(cerb::parse_slot_map(
        "69853562969c74ff387f9e491d025b2a86ac478f 192.168.1.100:7002 master - 0 0 3 connected 8192-12287\n"
        "2f53d0fb4a59274e83e47b1dca02697384822ca5 192.168.1.100:7006 slave 69853562969c74ff387f9e491d025b2a86ac478f 0 0 3 connected\n"
        "2560c867f9ca2ef4cc872eb85ce985373ad9e815 192.168.1.101:7003 master - 0 0 2 connected 0-4095\n"
        "933970b4fd2d1ad06166ab1d893e8cac7b129ebd 192.168.1.101:7001 master - 0 0 4 connected 4096-8191\n"
        "d3adf40539ad749d214609987563bf9903a57ffc 192.168.1.101:7007 slave 2560c867f9ca2ef4cc872eb85ce985373ad9e815 0 0 2 connected\n"
        "6c001456aff0ae537ba242d4e86fb325c5babbea 192.168.1.100:7000 myself,master - 0 0 1 connected 12288-16383\n",
        "127.0.0.1"), nullptr);

    ASSERT_TRUE(closed_servers().empty());
    clear_all_servers();

    for (cerb::slot s = 0; s < 4096; ++s) {
        cerb::Server* svr = slot_map.get_by_slot(s);
        ASSERT_NE(nullptr, svr) << " slot #" << s;
        ASSERT_EQ("192.168.1.101", svr->addr.host) << " slot #" << s;
        ASSERT_EQ(7007, svr->addr.port) << " slot #" << s;
    }

    for (cerb::slot s = 4096; s < 8192; ++s) {
        cerb::Server* svr = slot_map.get_by_slot(s);
        ASSERT_NE(nullptr, svr) << " slot #" << s;
        ASSERT_EQ("192.168.1.101", svr->addr.host) << " slot #" << s;
        ASSERT_EQ(7001, svr->addr.port) << " slot #" << s;
    }

    for (cerb::slot s = 8192; s < 12288; ++s) {
        cerb::Server* svr = slot_map.get_by_slot(s);
        ASSERT_NE(nullptr, svr) << " slot #" << s;
        ASSERT_EQ("192.168.1.100", svr->addr.host) << " slot #" << s;
        ASSERT_EQ(7006, svr->addr.port) << " slot #" << s;
    }

    for (cerb::slot s = 12288; s < 16384; ++s) {
        cerb::Server* svr = slot_map.get_by_slot(s);
        ASSERT_NE(nullptr, svr) << " slot #" << s;
        ASSERT_EQ("192.168.1.100", svr->addr.host) << " slot #" << s;
        ASSERT_EQ(7000, svr->addr.port) << " slot #" << s;
    }

    std::set<cerb::Server*> to_be_replaced;
    to_be_replaced.insert(slot_map.get_by_slot(0));

    slot_map.replace_map(cerb::parse_slot_map(
        "69853562969c74ff387f9e491d025b2a86ac478f 192.168.1.100:7002 master - 0 0 3 connected 8192-12287\n"
        "2f53d0fb4a59274e83e47b1dca02697384822ca5 192.168.1.100:7006 slave 69853562969c74ff387f9e491d025b2a86ac478f 0 0 3 connected\n"
        "2560c867f9ca2ef4cc872eb85ce985373ad9e815 192.168.1.101:7003 master - 0 0 2 connected 0-4095\n"
        "933970b4fd2d1ad06166ab1d893e8cac7b129ebd 192.168.1.101:7001 master - 0 0 4 connected 4096-8191\n"
        "6c001456aff0ae537ba242d4e86fb325c5babbea 192.168.1.100:7000 myself,master - 0 0 1 connected 12288-16383\n",
        "127.0.0.1"), nullptr);

    ASSERT_EQ(to_be_replaced, closed_servers());
    clear_all_servers();

    for (cerb::slot s = 0; s < 4096; ++s) {
        cerb::Server* svr = slot_map.get_by_slot(s);
        ASSERT_NE(nullptr, svr) << " slot #" << s;
        ASSERT_EQ("192.168.1.101", svr->addr.host) << " slot #" << s;
        ASSERT_EQ(7003, svr->addr.port) << " slot #" << s;
    }

    for (cerb::slot s = 4096; s < 8192; ++s) {
        cerb::Server* svr = slot_map.get_by_slot(s);
        ASSERT_NE(nullptr, svr) << " slot #" << s;
        ASSERT_EQ("192.168.1.101", svr->addr.host) << " slot #" << s;
        ASSERT_EQ(7001, svr->addr.port) << " slot #" << s;
    }

    for (cerb::slot s = 8192; s < 12288; ++s) {
        cerb::Server* svr = slot_map.get_by_slot(s);
        ASSERT_NE(nullptr, svr) << " slot #" << s;
        ASSERT_EQ("192.168.1.100", svr->addr.host) << " slot #" << s;
        ASSERT_EQ(7006, svr->addr.port) << " slot #" << s;
    }

    for (cerb::slot s = 12288; s < 16384; ++s) {
        cerb::Server* svr = slot_map.get_by_slot(s);
        ASSERT_NE(nullptr, svr) << " slot #" << s;
        ASSERT_EQ("192.168.1.100", svr->addr.host) << " slot #" << s;
        ASSERT_EQ(7000, svr->addr.port) << " slot #" << s;
    }
}

TEST_F(SlotMapTest, NonsenseProof)
{
    cerb::SlotMap slot_map;
    slot_map.replace_map(cerb::parse_slot_map(
        "The quick brown fox jumps over a lazy dog.",
        "127.0.0.1"), nullptr);
    ASSERT_TRUE(closed_servers().empty());
    ASSERT_TRUE(created_servers().empty());
    slot_map.replace_map(cerb::parse_slot_map(
        "6c001456aff0ae537ba242d4e86fb325c5babbea 192.168.1.100:7000 myself,master - 0 0 1 connected abc",
        "127.0.0.1"), nullptr);
    ASSERT_TRUE(closed_servers().empty());
    ASSERT_TRUE(created_servers().empty());
}
