#ifndef __CERBERUS_STATISTICS_HPP__
#define __CERBERUS_STATISTICS_HPP__

#include <string>

#include "common.hpp"

namespace cerb {

    std::string stats_all();

    class BufferStatAllocator
        : public std::allocator<byte>
    {
        typedef std::allocator<byte> BaseType;
    public:
        BufferStatAllocator() = default;
        BufferStatAllocator(BufferStatAllocator const&) = default;

        template<class U>
        struct rebind {
            typedef BufferStatAllocator other;
        };

        template <typename U>
        BufferStatAllocator(std::allocator<U> const& rhs)
            : BaseType(rhs)
        {}

        pointer allocate(size_type n, void const* hint=nullptr);
        void deallocate(pointer p, size_type n);
    };

}

#endif /* __CERBERUS_STATISTICS_HPP__ */
