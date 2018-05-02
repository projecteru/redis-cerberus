#include <ctime>
#include <cstdlib>

#include "random.hpp"

void util::random_init()
{
    ::srand(time(nullptr));
}

int util::randint(int min, int max)
{
    return min + (::rand() % (max - min));
}
