#include <csignal>
#include <vector>
#include <algorithm>
#include <iostream>

#include "proxy.hpp"
#include "concurrence.hpp"
#include "utils/logging.hpp"

int const PORT = 8889;

void exit_on_int(int)
{
    LOG(INFO) << "C-c Exit.";
    exit(0);
}

int main(int argc, char* argv[])
{
    if (argc == 1) {
        std::cerr << "Usage:" << std::endl;
        std::cerr << "    cerberus NODES_FILE" << std::endl;
        return 1;
    }
    signal(SIGINT, exit_on_int);
    logging::init();
    std::vector<cerb::ListenThread> threads;
    for (int i = 0; i < 4; ++i) {
        threads.push_back(cerb::ListenThread(PORT, argv[1]));
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
