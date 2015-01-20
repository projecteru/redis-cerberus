#include <sstream>

#include "core/stats.hpp"
#include "core/globals.hpp"

std::string cerb::stats_all()
{
    std::stringstream ss;
    ss << "threads:" << cerb_global::all_threads.size()
       << "\nclients_count:";
    for (auto const& thread: cerb_global::all_threads) {
        ss << thread.get_proxy()->clients_count() << ',';
    }
    ss << "\nactive_masters_count:"
       << cerb_global::current_proxy->masters_count();
    return ss.str();
}
