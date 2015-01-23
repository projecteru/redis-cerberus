#ifndef __CERBERUS_BUFFER_HPP__
#define __CERBERUS_BUFFER_HPP__

#include <vector>
#include <string>

#include "stats.hpp"

struct iovec;

namespace cerb {

    class Buffer {
        typedef std::vector<byte, BufferStatAllocator> ContainerType;
        ContainerType _buffer;
    public:
        typedef ContainerType::size_type size_type;
        typedef ContainerType::value_type value_type;
        typedef ContainerType::iterator iterator;
        typedef ContainerType::const_iterator const_iterator;

        Buffer() {}

        static Buffer from_string(std::string const& s);

        Buffer(Buffer const&) = delete;

        Buffer(Buffer&& rhs)
            : _buffer(std::move(rhs._buffer))
        {}

        Buffer(iterator first, iterator last)
            : _buffer(first, last)
        {}

        Buffer& operator=(Buffer&& rhs)
        {
            _buffer = std::move(rhs._buffer);
            return *this;
        }

        iterator begin()
        {
            return _buffer.begin();
        }

        iterator end()
        {
            return _buffer.end();
        }

        const_iterator cbegin() const
        {
            return _buffer.cbegin();
        }

        const_iterator cend() const
        {
            return _buffer.cend();
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
        int write(int fd);
        void truncate_from_begin(iterator i);
        void buffer_ready(std::vector<struct iovec>& iov);
        void copy_from(const_iterator first, const_iterator last);
        void append_from(const_iterator first, const_iterator last);
        std::string to_string() const;
        bool same_as_string(std::string const& s) const;
    };

}

#endif /* __CERBERUS_BUFFER_HPP__ */
