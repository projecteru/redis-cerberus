#ifndef __CERBERUS_BUFFER_HPP__
#define __CERBERUS_BUFFER_HPP__

#include <vector>
#include <deque>
#include <string>

#include "stats.hpp"
#include "utils/pointer.h"
#include "syscalls/cio.h"

namespace cerb {

    void flush_string(int fd, std::string const& s);

    class Buffer {
        typedef std::vector<byte, BufferStatAllocator> ContainerType;
        ContainerType _buffer;
    public:
        typedef ContainerType::size_type size_type;
        typedef ContainerType::value_type value_type;
        typedef ContainerType::iterator iterator;
        typedef ContainerType::const_iterator const_iterator;

        Buffer() = default;

        Buffer(std::string const& s)
            : _buffer(s.begin(), s.end())
        {}

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

        void swap(Buffer& another)
        {
            this->_buffer.swap(another._buffer);
        }

        void swap(Buffer&& another)
        {
            this->_buffer.swap(another._buffer);
        }

        void clear()
        {
            _buffer.clear();
        }

        void* data()
        {
            return this->_buffer.data();
        }

        int read(int fd);
        int write(int fd) const;
        void truncate_from_begin(iterator i);
        void buffer_ready(std::vector<cio::iovec>& iov);
        void append_from(const_iterator first, const_iterator last);
        std::string to_string() const;
        bool same_as_string(std::string const& s) const;
    };

    class BufferSet {
        std::deque<std::shared_ptr<Buffer>> _buf_arr;
        int _1st_buf_offset;
    public:
        BufferSet(BufferSet const&) = delete;

        BufferSet()
            : _1st_buf_offset(0)
        {}

        void append(std::shared_ptr<Buffer> buf)
        {
            this->_buf_arr.push_back(buf);
        }

        void clear()
        {
            this->_buf_arr.clear();
        }

        bool empty() const
        {
            return this->_buf_arr.empty();
        }

        bool writev(int fd);
    };

}

#endif /* __CERBERUS_BUFFER_HPP__ */
