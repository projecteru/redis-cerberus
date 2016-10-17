#include <gtest/gtest.h>

#include "utils/logging.hpp"
#include "core/globals.hpp"

int main(int argc, char** argv)
{
    logging::init();
    cerb_global::slow_poll_elapse = std::chrono::milliseconds(1000);
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
