#ifndef __CERBERUS_UTILITY_MEMORY_PAGE_HPP__
#define __CERBERUS_UTILITY_MEMORY_PAGE_HPP__

#include <memory>
#include <deque>

#include "common.hpp"

namespace util {

    struct MemPage {
        typedef cerb::byte byte;
        static int const PAGE_SIZE = 16 * 1024;

        byte page[PAGE_SIZE];

        MemPage() = default;
        MemPage(std::string const& direct_string);
        MemPage(MemPage const&) = delete;
    };

    struct SharedMemPage {
        typedef cerb::msize_t msize_t;
        typedef cerb::msize_t offset_t;

        std::shared_ptr<MemPage> mempg;
        offset_t begin;
        msize_t size;

        SharedMemPage(std::shared_ptr<MemPage> mp, msize_t sz)
            : mempg(std::move(mp))
            , begin(0)
            , size(sz)
        {}

        SharedMemPage(std::shared_ptr<MemPage> mp, offset_t b, msize_t sz)
            : mempg(std::move(mp))
            , begin(b)
            , size(sz)
        {}

        SharedMemPage(SharedMemPage const& rhs, offset_t b, msize_t sz)
            : mempg(rhs.mempg)
            , begin(b)
            , size(sz)
        {}

        offset_t end() const
        {
            return begin + size;
        }

        SharedMemPage truncate_from(offset_t off) const
        {
            return SharedMemPage(mempg, off, size - (off - begin));
        }

        MemPage::byte* page() const
        {
            return mempg->page + begin;
        }
    };

    class MemoryPages {
        typedef std::deque<SharedMemPage> ContainerType;
        typedef SharedMemPage::msize_t msize_t;

        ContainerType _pages;
        msize_t _total_size;

        struct Iterator {
            typedef SharedMemPage::offset_t offset_t;
            ContainerType const* container;
            ContainerType::const_iterator container_it;
            offset_t offset;

            Iterator(ContainerType const& c, ContainerType::const_iterator it,
                     offset_t o)
                : container(&c)
                , container_it(it)
                , offset(o)
            {}

            Iterator& operator++()
            {
                _next();
                return *this;
            }

            Iterator operator++(int)
            {
                Iterator it(*this);
                _next();
                return it;
            }

            Iterator operator=(Iterator const& rhs)
            {
                container = rhs.container;
                container_it = rhs.container_it;
                offset = rhs.offset;
                return *this;
            }

            MemPage::byte operator*() const
            {
                return container_it->mempg->page[offset];
            }

            bool operator==(Iterator const& rhs) const
            {
                return container_it == rhs.container_it && offset == rhs.offset;
            }

            bool operator!=(Iterator const& rhs) const
            {
                return !operator==(rhs);
            }
        private:
            void _next();
        };
    public:
        typedef Iterator const_iterator;

        MemoryPages()
            : _total_size(0)
        {}

        MemoryPages(MemoryPages&& rhs)
            : _pages(std::move(rhs._pages))
            , _total_size(rhs._total_size)
        {}

        MemoryPages(MemoryPages const& rhs)
            : _pages(rhs._pages)
            , _total_size(rhs._total_size)
        {}

        MemoryPages(const_iterator begin, const_iterator end);

        MemoryPages& operator=(MemoryPages&& rhs)
        {
            _pages = std::move(rhs._pages);
            _total_size = rhs._total_size;
            return *this;
        }

        msize_t size() const
        {
            return _total_size;
        }

        bool empty() const
        {
            return _pages.empty();
        }

        void clear()
        {
            _pages.clear();
            _total_size = 0;
        }

        const_iterator begin() const
        {
            auto i = _pages.cbegin();
            if (i == _pages.cend()) {
                return this->end();
            }
            return Iterator(_pages, i, i->begin);
        }

        const_iterator end() const
        {
            return Iterator(_pages, _pages.cend(), 0);
        }

        void append_page(SharedMemPage const& page);
        void append_range(const_iterator begin, const_iterator end);
        void erase_from_begin(const_iterator end);

        ContainerType::const_iterator pages_begin() const
        {
            return _pages.begin();
        }

        ContainerType::const_iterator pages_end() const
        {
            return _pages.end();
        }
    };

}

#endif /* __CERBERUS_UTILITY_MEMORY_PAGE_HPP__ */
