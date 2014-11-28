#include <csignal>
#include <vector>
#include <algorithm>
#include <iostream>

#include "proxy.hpp"
#include "concurrence.hpp"

int const PORT = 8889;

void exit_on_int(int)
{
    std::cout << "Keyboard Interrupted. Exit" << std::endl;
    exit(0);
}

int main()
{
    signal(SIGINT, exit_on_int);
    std::vector<cerb::ListenThread> threads;
    for (int i = 0; i < 4; ++i) {
        threads.push_back(cerb::ListenThread(PORT));
    }
    std::for_each(threads.begin(), threads.end(),
                  [](cerb::ListenThread& t)
                  {
                      t.run();
                  });
    std::cout << "Started" << std::endl;
    std::for_each(threads.begin(), threads.end(),
                  [](cerb::ListenThread& t)
                  {
                      t.join();
                  });
    return 0;
}
