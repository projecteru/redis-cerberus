#include <gtest/gtest.h>

#include "utils/object_pool.hpp"

TEST(ObjectPool, CreateDestroy)
{
    util::ObjectPool<int, 2> p;
    ASSERT_EQ(0, p.alloc_size());
    ASSERT_EQ(0, p.using_size());

    int* m[3];

    m[0] = p.create(42);
    ASSERT_EQ(42, *m[0]);
    ASSERT_EQ(2 * sizeof(int), p.alloc_size());
    ASSERT_EQ(sizeof(int), p.using_size());

    p.destroy(m[0]);
    ASSERT_EQ(2 * sizeof(int), p.alloc_size());
    ASSERT_EQ(0, p.using_size());

    m[0] = p.create(43);
    ASSERT_EQ(43, *m[0]);
    ASSERT_EQ(2 * sizeof(int), p.alloc_size());
    ASSERT_EQ(sizeof(int), p.using_size());

    m[1] = p.create(44);
    ASSERT_EQ(44, *m[1]);
    ASSERT_EQ(2 * sizeof(int), p.alloc_size());
    ASSERT_EQ(2 * sizeof(int), p.using_size());

    m[2] = p.create(45);
    ASSERT_EQ(45, *m[2]);
    ASSERT_EQ(4 * sizeof(int), p.alloc_size());
    ASSERT_EQ(3 * sizeof(int), p.using_size());

    p.destroy(m[0]);
    p.destroy(m[1]);
    p.destroy(m[2]);
    ASSERT_EQ(4 * sizeof(int), p.alloc_size());
    ASSERT_EQ(0, p.using_size());
}
