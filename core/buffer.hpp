#ifndef __CERBERUS_BUFFER_HPP__
#define __CERBERUS_BUFFER_HPP__

#include <vector>
#include <string>

#include "common.hpp"
#include "utils/mempage.hpp"

struct iovec;

namespace cerb {

    class Buffer {
        util::MemoryPages _buffer;
    public:
        typedef util::SharedMemPage::msize_t size_type;
        typedef util::MemPage::byte value_type;
        typedef util::MemoryPages::const_iterator const_iterator;
        typedef const_iterator iterator;

        Buffer() {}

        static Buffer from_string(std::string const& s);

        Buffer(Buffer const&) = delete;

        Buffer(Buffer&& rhs)
            : _buffer(std::move(rhs._buffer))
        {}

        Buffer(const_iterator first, const_iterator last)
            : _buffer(first, last)
        {}

        Buffer& operator=(Buffer&& rhs)
        {
            _buffer = std::move(rhs._buffer);
            return *this;
        }

        iterator begin() const
        {
            return _buffer.begin();
        }

        iterator end() const
        {
            return _buffer.end();
        }

        const_iterator cbegin() const
        {
            return _buffer.begin();
        }

        const_iterator cend() const
        {
            return _buffer.end();
        }

        size_type size() const
        {
            return _buffer.size();
        }

        bool empty() const
        {
            return _buffer.empty();
        }

        void clear()
        {
            _buffer.clear();
        }

        int read(int fd);
        void write(int fd);
        void truncate_from_begin(iterator i);
        void buffer_ready(std::vector<struct iovec>& iov);
        void append_from(const_iterator first, const_iterator last);
        std::string to_string() const;
        bool same_as_string(std::string const& s) const;
    };

}

#endif /* __CERBERUS_BUFFER_HPP__ */
