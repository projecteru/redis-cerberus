#include "core/stats.hpp"
#include "core/globals.hpp"
#include "utils/string.h"

using namespace cerb;

std::string cerb::stats_all()
{
    std::vector<std::string> clients_counts;
    std::vector<std::string> mem_buffer_allocs;
    for (auto const& thread: cerb_global::all_threads) {
        clients_counts.push_back(util::str(thread.get_proxy()->clients_count()));
        mem_buffer_allocs.push_back(util::str(thread.buffer_allocated()));
    }
    return util::join("", {
        "version:" VERSION
        "\nthreads:", util::str(msize_t(cerb_global::all_threads.size())),
        "\nclients_count:", util::join(",", clients_counts),
        "\nmem_buffer_alloc:", util::join(",", mem_buffer_allocs),
    });
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
