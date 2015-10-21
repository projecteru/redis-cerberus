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

    static rint const LENGTH_OF_CR_LF = 2;

    class MessageInterrupted
        : std::range_error
    {
    public:
        MessageInterrupted()
            : std::range_error("")
        {}
    };

    template <typename Iterator>
    std::pair<rint, Iterator> btou(Iterator begin, Iterator end)
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

    template <typename Iterator>
    std::pair<rint, Iterator> btoi(Iterator begin, Iterator end)
    {
        if (*begin == '-') {
            auto r = btou(++begin, end);
            r.first = -r.first;
            return r;
        }
        return btou(begin, end);
    }

    template <typename Iterator>
    Iterator parse_simple_str(Iterator begin, Iterator end)
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

    template <typename Iterator>
    Iterator parse_str(rint size, Iterator begin, Iterator end)
    {
        Iterator last = begin + size + LENGTH_OF_CR_LF;
        if (end < last) {
            throw msg::MessageInterrupted();
        }
        return last;
    }

    template <typename Iterator>
    Iterator pass_nil(Iterator begin, Iterator end)
    {
        int size;
        for (size = 1 + LENGTH_OF_CR_LF; 0 < size && begin != end; --size, ++begin)
            ;
        if (size != 0) {
            throw MessageInterrupted();
        }
        return begin;
    }

    template <typename Iterator, typename Consumer>
    Iterator parse(Iterator buffer_begin, Iterator buffer_end, Consumer& c)
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
                return c.on_lstr(r.first, r.second, buffer_end);
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

    template <typename Iterator, typename FinalType>
    class MessageSplitterBase {
    protected:
        bool _interrupted;
        std::vector<Iterator> _split_points;
        std::stack<rint> _nested_array_element_count;
    public:
        void on_element(Iterator next)
        {
            if (this->_nested_array_element_count.size() == 0) {
                static_cast<FinalType*>(this)->on_split_point(next);
                this->_split_points.push_back(next);
                return;
            }
            this->_nested_array_element_count.top() -= 1;
            if (this->_nested_array_element_count.top() == 0) {
                this->_nested_array_element_count.pop();
                this->on_element(next);
            }
        }

        void on_error(Iterator /* str begin */, Iterator /* str end */) {}
        void on_string(Iterator /* str begin */, Iterator /* str end */) {}
        void on_split_point(Iterator /* split point */) {}
        void on_array(cerb::rint /* array size */) {}

        explicit MessageSplitterBase(Iterator begin)
            : _interrupted(false)
        {
            _split_points.push_back(begin);
        }

        MessageSplitterBase(MessageSplitterBase const&) = delete;

        MessageSplitterBase(MessageSplitterBase&& rhs)
            : _interrupted(rhs._interrupted)
            , _split_points(std::move(rhs._split_points))
            , _nested_array_element_count(std::move(rhs._nested_array_element_count))
        {}

        void interrupt()
        {
            _interrupted = true;
        }

        Iterator on_int(rint, Iterator next)
        {
            this->on_element(next);
            return next;
        }

        Iterator on_sstr(Iterator begin, Iterator end)
        {
            auto next = parse_simple_str(begin, end);
            static_cast<FinalType*>(this)->on_string(begin, next - LENGTH_OF_CR_LF);
            this->on_element(next);
            return next;
        }

        Iterator on_lstr(rint size, Iterator begin, Iterator end)
        {
            auto next = parse_str(size, begin, end);
            static_cast<FinalType*>(this)->on_string(begin, next - LENGTH_OF_CR_LF);
            this->on_element(next);
            return next;
        }

        void on_arr(rint size, Iterator next)
        {
            static_cast<FinalType*>(this)->on_array(size);
            if (size == 0) {
                this->on_element(next);
            } else {
                this->_nested_array_element_count.push(size);
            }
        }

        Iterator on_nil(Iterator next)
        {
            this->on_element(next);
            return next;
        }

        Iterator on_err(Iterator begin, Iterator end)
        {
            auto next = parse_simple_str(begin, end);
            static_cast<FinalType*>(this)->on_error(begin, next - LENGTH_OF_CR_LF);
            this->on_element(next);
            return next;
        }

        bool finished() const
        {
            return this->_nested_array_element_count.empty() && !this->_interrupted;
        }

        Iterator interrupt_point() const
        {
            return this->_split_points.back();
        }

        class MessageIterator {
            typename std::vector<Iterator>::iterator _cursor;
        public:
            explicit MessageIterator(
                    typename std::vector<Iterator>::iterator const& cur)
                : _cursor(cur)
            {}

            MessageIterator(MessageIterator const& rhs)
                : MessageIterator(rhs._cursor)
            {}

            Iterator range_begin() const
            {
                return *_cursor;
            }

            Iterator range_end() const
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

    template <typename Iterator, typename Splitter>
    Splitter split_by(Iterator begin, Iterator end, Splitter s)
    {
        try {
            while (begin != end) {
                begin = parse(begin, end, s);
            }
        } catch (MessageInterrupted&) {
            s.interrupt();
        }
        return std::move(s);
    }

    template <typename Iterator>
    class MessageSplitter
        : public MessageSplitterBase<Iterator, MessageSplitter<Iterator>>
    {
        typedef MessageSplitterBase<Iterator, MessageSplitter> BaseType;
    public:
        typedef typename BaseType::iterator iterator;

        explicit MessageSplitter(Iterator i)
            : BaseType(i)
        {}
    };

    template <typename Iterator>
    MessageSplitter<Iterator> split(Iterator begin, Iterator end)
    {
        return split_by(begin, end, MessageSplitter<Iterator>(begin));
    }

    std::string format_command(std::string command, std::vector<std::string> const& args);

} }

#endif /* __CERBERUS_MESSAGE_HPP__ */
