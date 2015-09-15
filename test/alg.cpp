#include <gtest/gtest.h>

#include "utils/alg.hpp"

TEST(Algorithm, MaxElement)
{
    std::vector<std::string> s({
        "brown fox",
        "dreadful rabbit",
        "lazy dog",
    });

    ASSERT_EQ("lazy dog", *util::max_element(s, [](std::string const& m) { return m; }));
    ASSERT_EQ("dreadful rabbit", *util::max_element(
                    s, [](std::string const& m) { return m.size(); }));
}
