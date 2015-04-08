#include "core/stats.hpp"
#include "core/globals.hpp"
#include "utils/string.h"

using namespace cerb;

static bool read_slave = false;

std::string cerb::stats_all()
{
    std::vector<std::string> clients_counts;
    std::vector<std::string> mem_buffer_allocs;
    std::vector<std::string> avg_cmd_elapse;
    long total_commands = 0;
    Interval total_cmd_elapse(0);
    for (auto const& thread: cerb_global::all_threads) {
        util::sref<Proxy const> proxy(thread.get_proxy());
        clients_counts.push_back(util::str(proxy->clients_count()));
        total_commands += proxy->total_cmd();
        total_cmd_elapse += proxy->total_cmd_elapse();
        mem_buffer_allocs.push_back(util::str(thread.buffer_allocated()));
    }
    return util::join("", {
        "version:" VERSION
        "\nthreads:", util::str(msize_t(cerb_global::all_threads.size())),
        "\nclients_count:", util::join(",", clients_counts),
        "\nmem_buffer_alloc:", util::join(",", mem_buffer_allocs),
        "\ncompleted_commands:", util::str(total_commands),
        "\ntotal_process_elapse:", util::str(total_cmd_elapse),
        "\nread_slave:", ::read_slave ? "1" : "0",
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
