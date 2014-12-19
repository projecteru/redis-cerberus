#include <ctime>
#include <cstdlib>

#include "random.hpp"

static int init()
{
    ::srand(time(nullptr));
    return 0;
}

static auto _(init());

int util::randint(int min, int max)
{
    return min + (::rand() % (max - min));
}
