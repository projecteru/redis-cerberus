#include <atomic>
#include <mutex>

#include "globals.hpp"

std::vector<cerb::ListenThread> cerb_global::all_threads;
thread_local cerb::msize_t cerb_global::allocated_buffer(0);

static std::mutex remote_addrs_mutex;
static std::set<util::Address> remote_addrs;
static bool cluster_ok = false;

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

static std::atomic_bool req_full_cov(true);

void cerb_global::set_cluster_req_full_cov(bool c)
{
    ::req_full_cov = c;
}

bool cerb_global::cluster_req_full_cov()
{
    return ::req_full_cov;
}

void cerb_global::set_cluster_ok(bool ok)
{
    ::cluster_ok = ok;
}

bool cerb_global::cluster_ok()
{
    return ::cluster_ok;
}
