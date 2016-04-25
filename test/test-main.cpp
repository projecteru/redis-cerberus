#include <gtest/gtest.h>

#include "utils/logging.hpp"

int main(int argc, char** argv)
{
    logging::init();
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
