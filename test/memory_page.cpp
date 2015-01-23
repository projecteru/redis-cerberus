#include <gtest/gtest.h>

#include "utils/mempage.hpp"

using namespace util;

static void advance(MemoryPages::const_iterator& it, int n)
{
    for (; 0 < n; --n) {
        ++it;
    }
}

static bool check_same_bytes(MemoryPages const& pages, std::string const& s)
{
    if (pages.size() != s.size()) {
        EXPECT_EQ(pages.size(), s.size());
        return false;
    }
    bool flag = true;
    MemoryPages::const_iterator it = pages.begin();
    for (std::string::size_type k = 0; k != s.size(); ++k) {
        if (s[k] != *it) {
            flag = false;
        }
        EXPECT_EQ(s[k], *it) << "ON k = " << k;
        ++it;
    }
    return flag;
}

TEST(MemoryPages, Iteration)
{
    MemoryPages pages;
    ASSERT_EQ(0, pages.size());
    ASSERT_TRUE(pages.empty());
    ASSERT_TRUE(pages.pages_begin() == pages.pages_end());
    ASSERT_EQ(pages.begin(), pages.end());

    {
        auto page = std::make_shared<MemPage>("the quick brown fox jumps over a lazy dog");
        pages.append_page(SharedMemPage(page, 4, 8));
    }
    ASSERT_TRUE(check_same_bytes(pages, "quick br"));

    {
        MemoryPages::const_iterator it = pages.begin();
        for (int k = 0; k < 7; ++k) {
            ++it;
            ASSERT_NE(it, pages.end()) << "ON k = " << k;
        }
        ++it;
        ASSERT_EQ(it, pages.end());
    }

    {
        auto page = std::make_shared<MemPage>("el psy congroo");
        pages.append_page(SharedMemPage(page, 8, 4));
    }
    ASSERT_TRUE(check_same_bytes(pages, "quick brongr"));

    {
        MemoryPages::const_iterator it = pages.begin();
        for (int k = 0; k < 11; ++k) {
            ++it;
            ASSERT_NE(it, pages.end()) << "ON k = " << k;
        }
        ++it;
        ASSERT_EQ(it, pages.end());
    }

    pages.clear();
    ASSERT_TRUE(check_same_bytes(pages, std::string()));
}

TEST(MemoryPages, SharingPages)
{
    auto page = std::make_shared<MemPage>("the quick brown fox jumps over a lazy dog");

    MemoryPages x;
    x.append_page(SharedMemPage(page, 4, 5));
    MemoryPages y;
    y.append_page(SharedMemPage(page, 10, 5));

    ASSERT_TRUE(check_same_bytes(x, "quick"));
    ASSERT_TRUE(check_same_bytes(y, "brown"));

    auto xit = x.pages_begin();
    ASSERT_TRUE(x.pages_end() != xit);
    auto yit = y.pages_begin();
    ASSERT_TRUE(y.pages_end() != yit);
    ASSERT_EQ(xit->mempg->page, yit->mempg->page);
}

TEST(MemoryPages, AppendRange)
{
    MemoryPages source;

    {
        auto page = std::make_shared<MemPage>("the quick brown fox jumps over a lazy dog");
        source.append_page(SharedMemPage(page, 4, 8));
    }
    {
        MemoryPages target;
        MemoryPages::const_iterator it = source.begin();
        MemoryPages::const_iterator jt = it;

        for (int k = 0; k < 8; ++k) {
            target.clear();
            ++jt;
            target.append_range(it, jt);
            ASSERT_EQ(k + 1, target.size()) << "ON k = " << k;
        }
        ASSERT_TRUE(check_same_bytes(target, "quick br"));
    }

    {
        auto page = std::make_shared<MemPage>("the bird of hermes is my name");
        source.append_page(SharedMemPage(page, 0, 16));
    }
    {
        MemoryPages target;
        MemoryPages::const_iterator it = source.begin();
        MemoryPages::const_iterator jt = it;

        advance(it, 2);
        advance(jt, 23);

        target.append_range(it, jt);
        ASSERT_TRUE(check_same_bytes(target, "ick brthe bird of her"));

        ++jt;
        target.clear();
        target.append_range(it, jt);
        ASSERT_TRUE(check_same_bytes(target, "ick brthe bird of herm"));
    }

    {
        auto page = std::make_shared<MemPage>("eating my wings to make me tame");
        source.append_page(SharedMemPage(page, 3, 3));
    }
    {
        MemoryPages target;
        MemoryPages::const_iterator it = source.begin();
        MemoryPages::const_iterator jt = it;

        advance(it, 3);
        advance(jt, 23);

        target.append_range(it, jt);
        ASSERT_TRUE(check_same_bytes(target, "ck brthe bird of her"));

        ++jt;
        target.clear();
        target.append_range(it, jt);
        ASSERT_TRUE(check_same_bytes(target, "ck brthe bird of herm"));

        ++jt;
        target.clear();
        target.append_range(it, jt);
        ASSERT_TRUE(check_same_bytes(target, "ck brthe bird of hermi"));

        ++jt;
        target.clear();
        target.append_range(it, jt);
        ASSERT_TRUE(check_same_bytes(target, "ck brthe bird of hermin"));

        ++jt;
        target.clear();
        target.append_range(it, jt);
        ASSERT_TRUE(check_same_bytes(target, "ck brthe bird of herming"));
    }

    {
        MemoryPages target;
        MemoryPages::const_iterator it = source.begin();
        MemoryPages::const_iterator jt = it;
        advance(it, 10);
        advance(jt, 26);

        target.append_range(it, jt);
        ASSERT_TRUE(check_same_bytes(target, "e bird of hermin"));
    }

    {
        MemoryPages target;
        MemoryPages::const_iterator it = source.begin();
        MemoryPages::const_iterator jt = it;

        advance(it, 4);
        advance(jt, 23);

        target.append_range(it, jt);
        ASSERT_TRUE(check_same_bytes(target, "k brthe bird of her"));

        advance(it, 10);
        advance(jt, 1);
        target.append_range(it, jt);
        ASSERT_TRUE(check_same_bytes(target, "k brthe bird of herrd of herm"));
    }
}

TEST(MemoryPages, AppendEmpty)
{
    MemoryPages source;

    {
        auto page = std::make_shared<MemPage>("hermes conrad");
        source.append_page(SharedMemPage(page, 4, 8));
    }
    {
        MemoryPages target;
        MemoryPages::const_iterator it = source.begin();
        MemoryPages::const_iterator jt = it;
        ++it;
        ++jt;

        target.append_range(it, jt);
        ASSERT_TRUE(target.empty());
        ASSERT_EQ(0, target.size());
        ASSERT_TRUE(target.pages_begin() == target.pages_end());
    }
}

TEST(MemoryPages, EraseFromBegin)
{
    MemoryPages pages;
    pages.erase_from_begin(pages.begin());
    ASSERT_EQ(0, pages.size());
    ASSERT_TRUE(pages.empty());
    ASSERT_TRUE(pages.pages_begin() == pages.pages_end());
    ASSERT_EQ(pages.begin(), pages.end());

    auto page = std::make_shared<MemPage>("the quick brown fox jumps over a lazy dog");
    pages.append_page(SharedMemPage(page, 16, 8));
    pages.erase_from_begin(pages.begin());
    ASSERT_TRUE(check_same_bytes(pages, "fox jump"));

    pages.erase_from_begin(pages.end());
    ASSERT_EQ(0, pages.size());
    ASSERT_TRUE(pages.empty());
    ASSERT_TRUE(pages.pages_begin() == pages.pages_end());
    ASSERT_EQ(pages.begin(), pages.end());

    page = std::make_shared<MemPage>("the quick brown fox jumps over a lazy dog");
    pages.append_page(SharedMemPage(page, 16, 8));
    pages.append_page(SharedMemPage(page, 32, 9));
    ASSERT_TRUE(check_same_bytes(pages, "fox jump lazy dog"));
    MemoryPages::const_iterator it = pages.begin();
    advance(it, 7);
    pages.erase_from_begin(it);
    ASSERT_TRUE(check_same_bytes(pages, "p lazy dog"));

    ++it;
    pages.erase_from_begin(it);
    ASSERT_TRUE(check_same_bytes(pages, " lazy dog"));

    ++it;
    pages.erase_from_begin(it);
    ASSERT_TRUE(check_same_bytes(pages, "lazy dog"));

    pages.clear();

    page = std::make_shared<MemPage>("the quick brown fox jumps over a lazy dog");
    pages.append_page(SharedMemPage(page, 16, 8));
    pages.append_page(SharedMemPage(page, 32, 9));
    ASSERT_TRUE(check_same_bytes(pages, "fox jump lazy dog"));
    it = pages.begin();
    advance(it, 9);
    pages.erase_from_begin(it);
    ASSERT_TRUE(check_same_bytes(pages, "lazy dog"));
}
