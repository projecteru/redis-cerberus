#ifndef __CERBERUS_MESSAGE_HPP__
#define __CERBERUS_MESSAGE_HPP__

#include <memory>
#include <utility>
#include <vector>
#include <stack>
#include <iterator>

#include "exceptions.hpp"
#include "common.hpp"

namespace cerb { namespace msg {

    class MessageInterrupted
        : std::range_error
    {
    public:
        MessageInterrupted()
            : std::range_error("")
        {}
    };

    template <typename InputIterator>
    std::pair<rint, InputIterator> btou(InputIterator begin, InputIterator end)
    {
        rint i = 0;
        while (begin != end) {
            byte b = *begin++;
            if (b == '\r') {
                if (begin == end) {
                    throw MessageInterrupted();
                }
                return std::make_pair(i, ++begin); /* skip \n */
            }
            i = i * 10 + (b - '0');
        }
        throw MessageInterrupted();
    }

    template <typename InputIterator>
    std::pair<rint, InputIterator> btoi(InputIterator begin, InputIterator end)
    {
        if (*begin == '-') {
            auto r = btou(++begin, end);
            r.first = -r.first;
            return r;
        }
        return btou(begin, end);
    }

    template <typename InputIterator>
    InputIterator pass_nil(InputIterator begin, InputIterator end)
    {
        int size;
        for (size = 3 /* length of 1\r\n */; 0 < size && begin != end;
             --size, ++begin)
            ;
        if (size != 0) {
            throw MessageInterrupted();
        }
        return begin;
    }

    template <typename InputIterator, typename Consumer>
    InputIterator parse(InputIterator buffer_begin, InputIterator buffer_end,
                        Consumer& c)
    {
        byte b = *buffer_begin++;
        switch (b) {
            case ':': {
                auto r = btoi(buffer_begin, buffer_end);
                return c.on_int(r.first, r.second);
            }
            case '+': {
                return c.on_sstr(buffer_begin, buffer_end);
            }
            case '$': {
                if (buffer_begin != buffer_end && *buffer_begin == '-') {
                    return c.on_nil(pass_nil(++buffer_begin, buffer_end));
                }
                auto r = btou(buffer_begin, buffer_end);
                return c.on_str(r.first, r.second, buffer_end);
            }
            case '*': {
                if (buffer_begin != buffer_end && *buffer_begin == '-') {
                    return c.on_nil(pass_nil(++buffer_begin, buffer_end));
                }
                auto r = btou(buffer_begin, buffer_end);
                c.on_arr(r.first);
                buffer_begin = r.second;
                for (int i = 0; i < r.first && buffer_begin != buffer_end; ++i)
                {
                    buffer_begin = parse(buffer_begin, buffer_end, c);
                }
                return buffer_begin;
            }
            case '-': {
                return c.on_err(buffer_begin, buffer_end);
            }
            default:
                throw BadRedisMessage(b);
        }
    }

    template <typename InputIterator>
    class BulkMessageSplitter {
        std::vector<InputIterator> _split_points;
        std::stack<rint> _nested_array_element_count;
        bool _interrupted;

        void _on_element(InputIterator next)
        {
            if (_nested_array_element_count.size() == 0) {
                _split_points.push_back(next);
                return;
            }
            _nested_array_element_count.top() -= 1;
            if (_nested_array_element_count.top() == 0) {
                _nested_array_element_count.pop();
                _on_element(next);
            }
        }

        InputIterator _on_simple_str(InputIterator begin, InputIterator end)
        {
            while (begin != end) {
                byte b = *begin++;
                if (b == '\r') {
                    if (begin == end) {
                        throw MessageInterrupted();
                    }
                    return ++begin;
                }
            }
            throw MessageInterrupted();
        }
    public:
        explicit BulkMessageSplitter(InputIterator begin)
            : _interrupted(false)
        {
            _split_points.push_back(begin);
        }

        BulkMessageSplitter(BulkMessageSplitter&& rhs)
            : _split_points(std::move(rhs._split_points))
            , _nested_array_element_count(
                std::move(rhs._nested_array_element_count))
            , _interrupted(rhs._interrupted)
        {}

        InputIterator on_int(rint, InputIterator next)
        {
            _on_element(next);
            return next;
        }

        InputIterator on_sstr(InputIterator begin, InputIterator end)
        {
            InputIterator next = _on_simple_str(begin, end);
            _on_element(next);
            return next;
        }

        InputIterator on_str(rint size, InputIterator begin, InputIterator end)
        {
            size += 2; /* length of \r\n */
            for (; 0 < size && begin != end; ++begin, --size)
                ;
            if (size != 0) {
                _interrupted = true;
            }
            _on_element(begin);
            return begin;
        }

        void on_arr(rint size)
        {
            _nested_array_element_count.push(size);
        }

        InputIterator on_nil(InputIterator next)
        {
            _on_element(next);
            return next;
        }

        InputIterator on_err(InputIterator begin, InputIterator end)
        {
            InputIterator next = _on_simple_str(begin, end);
            _on_element(next);
            return next;
        }

        void interrupted()
        {
            _interrupted = true;
        }

        bool finished() const
        {
            return _nested_array_element_count.empty() && !_interrupted;
        }

        InputIterator interrupt_point() const
        {
            return _split_points.back();
        }

        class MessageIterator {
            typename std::vector<InputIterator>::iterator _cursor;
        public:
            explicit MessageIterator(
                    typename std::vector<InputIterator>::iterator const& cur)
                : _cursor(cur)
            {}

            MessageIterator(MessageIterator const& rhs)
                : MessageIterator(rhs._cursor)
            {}

            InputIterator range_begin() const
            {
                return *_cursor;
            }

            InputIterator range_end() const
            {
                return *(_cursor + 1);
            }

            MessageIterator& operator++()
            {
                ++_cursor;
                return *this;
            }

            MessageIterator operator++(int)
            {
                return MessageIterator(*_cursor++);
            }

            rint size() const
            {
                return range_end() - range_begin();
            }

            bool operator==(MessageIterator const& rhs) const
            {
                return _cursor == rhs._cursor;
            }

            bool operator!=(MessageIterator const& rhs) const
            {
                return !(*this == rhs);
            }
        };

        typedef MessageIterator iterator;

        MessageIterator begin()
        {
            return MessageIterator(_split_points.begin());
        }

        MessageIterator end()
        {
            return MessageIterator(_split_points.end() - 1);
        }

        rint size() const
        {
            return rint(_split_points.size() - 1);
        }
    };

    template <typename InputIterator>
    BulkMessageSplitter<InputIterator> split(
        InputIterator begin, InputIterator end)
    {
        BulkMessageSplitter<InputIterator> s(begin);
        try {
            while (begin != end) {
                begin = parse(begin, end, s);
            }
        } catch (MessageInterrupted) {
            s.interrupted();
        }
        return std::move(s);
    }

} }

#endif /* __CERBERUS_MESSAGE_HPP__ */
