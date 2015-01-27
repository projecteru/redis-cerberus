#include <stdexcept>

#include "concurrence.hpp"
#include "globals.hpp"
#include "utils/logging.hpp"

using namespace cerb;

ListenThread::ListenThread(int listen_port, std::string const& remote)
    : _listen_port(listen_port)
    , _proxy(new Proxy(util::Address::from_host_port(remote)))
    , _thread(nullptr)
    , _mem_buffer_stat(nullptr)
{}

void ListenThread::run()
{
    this->_thread.reset(new std::thread(
        [=]()
        {
            _mem_buffer_stat = &cerb_global::allocated_buffer;
            try {
                this->_proxy->run(this->_listen_port);
            } catch (std::runtime_error& e) {
                LOG(ERROR) << "Terminated by runtime error: " << e.what();
                exit(1);
            }
        }));
}

void ListenThread::join()
{
    this->_thread->join();
}
