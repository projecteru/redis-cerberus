#include <sstream>

#include "core/stats.hpp"
#include "core/globals.hpp"

using namespace cerb;

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
