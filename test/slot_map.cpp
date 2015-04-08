#include <gtest/gtest.h>

#include "core/slot_map.hpp"

TEST(SlotMap, ParseMap)
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
}
