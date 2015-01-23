#include <algorithm>

#include "mempage.hpp"

using namespace util;

MemPage::MemPage(std::string const& s)
{
    std::copy(s.begin(), s.end(), page);
}

MemoryPages::MemoryPages(const_iterator begin, const_iterator end)
    : _total_size(0)
{
    append_range(begin, end);
}

void MemoryPages::append_page(SharedMemPage const& page)
{
    _pages.push_back(page);
    _total_size += page.size;
}

void MemoryPages::append_range(const_iterator begin, const_iterator end)
{
    if (begin.container_it == end.container_it) {
        if (begin.offset == end.offset) {
            return;
        }
        return append_page(SharedMemPage(*begin.container_it, begin.offset,
                                         end.offset - begin.offset));
    }
    ContainerType::const_iterator page_it = begin.container_it;
    append_page(page_it->truncate_from(begin.offset));
    for (++page_it; page_it != end.container_it; ++page_it) {
        append_page(*page_it);
    }
    if (end.offset == 0) { // also check if `end` is actually the end iterator
        return;
    }
    SharedMemPage const& last_page = *page_it;
    if (end.offset == last_page.begin) {
        return;
    }
    append_page(SharedMemPage(last_page, last_page.begin,
                              end.offset - last_page.begin));
}

void MemoryPages::erase_from_begin(const_iterator end)
{
    if (end == this->end()) {
        return clear();
    }
    auto page_it = this->_pages.begin();
    msize_t sz = 0;
    for (; page_it != end.container_it; ++page_it) {
        sz += page_it->size;
    }
    this->_pages.erase(this->_pages.begin(), page_it);
    this->_total_size -= sz;

    if (end.offset == page_it->begin) {
        return;
    }

    msize_t truncate = end.offset - page_it->begin;
    page_it->begin = end.offset;
    page_it->size -= truncate;
    this->_total_size -= truncate;
}

void MemoryPages::Iterator::_next()
{
    ++offset;
    if (offset == container_it->end()) {
        ++container_it;
        if (container_it == container->cend()) {
            offset = 0;
            return;
        }
        offset = container_it->begin;
    }
}
