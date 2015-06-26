#ifndef __CERBERUS_MESSAGE_HPP__
#define __CERBERUS_MESSAGE_HPP__

#include <memory>
#include <utility>
#include <vector>
#include <stack>
#include <iterator>

#include "common.hpp"
#include "except/exceptions.hpp"

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

    template <typename InputIterator, typename F>
    InputIterator parse_simple_str(
        InputIterator begin, InputIterator end, F f)
    {
        while (begin != end) {
            byte b = *begin++;
            if (b == '\r') {
                if (begin == end) {
                    throw MessageInterrupted();
                }
                return ++begin;
            }
            f(b);
        }
        throw MessageInterrupted();
    }

    template <typename InputIterator, typename F>
    InputIterator parse_str(
        rint size, InputIterator begin, InputIterator end, F f)
    {
        for (; 0 < size && begin != end; ++begin, --size) {
            f(*begin);
        }
        /* length of \r\n */
        for (size = 2; 0 < size && begin != end; ++begin, --size)
            ;
        if (size != 0) {
            throw MessageInterrupted();
        }
        return begin;
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
        if (buffer_begin == buffer_end) {
            return buffer_end;
        }
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
                c.on_arr(r.first, r.second);
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

    template <typename InputIterator, typename FinalType>
    class MessageSplitterBase {
    public:
        bool _interrupted;
        std::vector<InputIterator> _split_points;
        std::stack<rint> _nested_array_element_count;

        void on_arr_end(InputIterator next)
        {
            static_cast<FinalType*>(this)->on_element(next);
        }

        void on_element(InputIterator next)
        {
            if (_nested_array_element_count.size() == 0) {
                _split_points.push_back(next);
                return;
            }
            _nested_array_element_count.top() -= 1;
            if (_nested_array_element_count.top() == 0) {
                _nested_array_element_count.pop();
                static_cast<FinalType*>(this)->on_arr_end(next);
            }
        }

        explicit MessageSplitterBase(InputIterator begin)
            : _interrupted(false)
        {
            _split_points.push_back(begin);
        }

        MessageSplitterBase(MessageSplitterBase const&) = delete;

        MessageSplitterBase(MessageSplitterBase&& rhs)
            : _interrupted(rhs._interrupted)
            , _split_points(std::move(rhs._split_points))
            , _nested_array_element_count(
                std::move(rhs._nested_array_element_count))
        {}

        void interrupt()
        {
            _interrupted = true;
        }

        InputIterator on_int(rint, InputIterator next)
        {
            static_cast<FinalType*>(this)->on_element(next);
            return next;
        }

        InputIterator on_sstr(InputIterator begin, InputIterator end)
        {
            auto next = parse_simple_str(
                begin, end,
                [&](byte b)
                {
                    static_cast<FinalType*>(this)->on_byte(b);
                });
            static_cast<FinalType*>(this)->on_element(next);
            return next;
        }

        InputIterator on_str(rint size, InputIterator begin, InputIterator end)
        {
            begin = parse_str(
                size, begin, end,
                [&](byte b)
                {
                    static_cast<FinalType*>(this)->on_byte(b);
                });
            static_cast<FinalType*>(this)->on_element(begin);
            return begin;
        }

        void on_arr(rint size, InputIterator next)
        {
            if (size == 0) {
                static_cast<FinalType*>(this)->on_element(next);
            } else {
                _nested_array_element_count.push(size);
            }
        }

        InputIterator on_nil(InputIterator next)
        {
            static_cast<FinalType*>(this)->on_element(next);
            return next;
        }

        InputIterator on_err(InputIterator begin, InputIterator end)
        {
            auto next = parse_simple_str(
                begin, end,
                [&](byte b)
                {
                    static_cast<FinalType*>(this)->on_byte(b);
                });
            static_cast<FinalType*>(this)->on_element(next);
            return next;
        }

        bool finished() const
        {
            return _nested_array_element_count.empty() && !this->_interrupted;
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
                return !operator==(rhs);
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

    template <typename InputIterator, typename Splitter>
    Splitter split_by(InputIterator begin, InputIterator end, Splitter s)
    {
        try {
            while (begin != end) {
                begin = parse(begin, end, s);
            }
        } catch (MessageInterrupted) {
            s.interrupt();
        }
        return std::move(s);
    }

    template <typename InputIterator>
    class MessageSplitter
        : public MessageSplitterBase<InputIterator,
                                     MessageSplitter<InputIterator>>
    {
        typedef MessageSplitterBase<InputIterator, MessageSplitter> BaseType;
    public:
        typedef typename BaseType::iterator iterator;

        explicit MessageSplitter(InputIterator i)
            : BaseType(i)
        {}

        void on_byte(byte) {}
    };

    template <typename InputIterator>
    MessageSplitter<InputIterator> split(InputIterator begin, InputIterator end)
    {
        return split_by(begin, end, MessageSplitter<InputIterator>(begin));
    }

    std::string format_command(std::string command, std::vector<std::string> const& args);

} }

#endif /* __CERBERUS_MESSAGE_HPP__ */
