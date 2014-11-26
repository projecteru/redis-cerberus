#include <thread>

#include "concurrence.hpp"

using namespace cerb;

void ListenThread::run()
{
    this->_thread.reset(new std::thread(
        [=]()
        {
            this->_proxy.run(this->_listen_port);
        }));
}

void ListenThread::join()
{
    this->_thread->join();
}
