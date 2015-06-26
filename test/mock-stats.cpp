#include "core/stats.hpp"
#include "core/globals.hpp"

using namespace cerb;

std::string cerb::stats_all()
{
    return "$14\r\nMOCK STATISTIC\r\n";
}

BufferStatAllocator::pointer BufferStatAllocator::allocate(
    size_type n, void const* hint)
{
    return BaseType::allocate(n, hint);
}

void BufferStatAllocator::deallocate(pointer p, size_type n)
{
    BaseType::deallocate(p, n);
}
