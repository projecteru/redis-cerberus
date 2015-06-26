#include "stats.hpp"
#include "globals.hpp"
#include "utils/string.h"

using namespace cerb;

static bool read_slave = false;

std::string cerb::stats_all()
{
    std::vector<std::string> clients_counts;
    std::vector<std::string> mem_buffer_allocs;
    std::vector<std::string> last_cmd_elapse;
    std::vector<std::string> last_remote_cost;
    long total_commands = 0;
    Interval total_cmd_elapse(0);
    Interval total_remote_cost(0);
    for (auto const& thread: cerb_global::all_threads) {
        util::sref<Proxy const> proxy(thread.get_proxy());
        clients_counts.push_back(util::str(proxy->clients_count()));
        total_commands += proxy->total_cmd();
        total_cmd_elapse += proxy->total_cmd_elapse();
        total_remote_cost += proxy->total_remote_cost();
        mem_buffer_allocs.push_back(util::str(thread.buffer_allocated()));
        last_cmd_elapse.push_back(util::str(proxy->last_cmd_elapse()));
        last_remote_cost.push_back(util::str(proxy->last_remote_cost()));
    }
    std::vector<std::string> remotes_addrs;
    for (util::Address const& a: cerb_global::get_remotes()) {
        remotes_addrs.push_back(a.str());
    }
    return util::join("", {
        "version:" VERSION
        "\nthreads:", util::str(msize_t(cerb_global::all_threads.size())),
        "\nread_slave:", ::read_slave ? "1" : "0",
        "\nclients_count:", util::join(",", clients_counts),
        "\nmem_buffer_alloc:", util::join(",", mem_buffer_allocs),
        "\ncompleted_commands:", util::str(total_commands),
        "\ntotal_process_elapse:", util::str(total_cmd_elapse),
        "\ntotal_remote_cost:", util::str(total_remote_cost),
        "\nlast_command_elapse:", util::join(",", last_cmd_elapse),
        "\nlast_remote_cost:", util::join(",", last_remote_cost),
        "\nremotes:", util::join(",", remotes_addrs),
    });
}

void cerb::stats_set_read_slave()
{
    ::read_slave = true;
}

BufferStatAllocator::pointer BufferStatAllocator::allocate(
    size_type n, void const* hint)
{
    cerb_global::allocated_buffer += n;
    return BaseType::allocate(n, hint);
}

void BufferStatAllocator::deallocate(pointer p, size_type n)
{
    cerb_global::allocated_buffer -= n;
    BaseType::deallocate(p, n);
}
