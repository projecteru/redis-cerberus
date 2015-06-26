#include <mutex>

#include "globals.hpp"

std::vector<cerb::ListenThread> cerb_global::all_threads;
thread_local cerb::msize_t cerb_global::allocated_buffer(0);

static std::mutex remote_addrs_mutex;
static std::set<util::Address> remote_addrs;

void cerb_global::set_remotes(std::set<util::Address> remotes)
{
    std::lock_guard<std::mutex> _(::remote_addrs_mutex);
    ::remote_addrs = std::move(remotes);
}

std::set<util::Address> cerb_global::get_remotes()
{
    std::lock_guard<std::mutex> _(::remote_addrs_mutex);
    return ::remote_addrs;
}
