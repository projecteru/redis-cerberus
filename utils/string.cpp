#include <algorithm>
#include <sstream>
#include <stdexcept>

#include "string.h"
#include "except/exceptions.hpp"

using namespace util;

bool util::strnieq(std::string const& lhs, std::string const& rhs, ssize_type n)
{
    for (ssize_type i = 0; i < n; ++i) {
        if (lhs.size() == i || rhs.size() == i) {
            return lhs.size() == rhs.size();
        }
        if (std::toupper(lhs[i]) != std::toupper(rhs[i])) {
            return false;
        }
    }
    return true;
}

bool util::stristartswith(std::string const& s, std::string const& pre)
{
    return strnieq(s, pre, pre.size());
}

int util::atoi(std::string const& a)
{
    std::istringstream ss(a);
    int r;
    ss >> std::ws >> r >> std::ws;
    if (!ss.eof()) {
        throw cerb::BadRedisMessage("Invalid integer literal: " + a);
    }
    return r;
}

template <typename T>
static std::string str_from_something(T const& t)
{
    std::stringstream ss;
    ss << t;
    return ss.str();
}

std::string util::str(int i)
{
    return str_from_something(i);
}

std::string util::str(long i)
{
    return str_from_something(i);
}

std::string util::str(long long i)
{
    return str_from_something(i);
}

std::string util::str(double d)
{
    return str_from_something(d);
}

std::string util::str(bool b)
{
    return b ? "true" : "false";
}

std::string util::str(void const* p)
{
    return str_from_something(p);
}

std::string util::str(cerb::msize_t s)
{
    return str_from_something(s);
}

std::string util::str(cerb::Interval i)
{
    return str_from_something(i.count());
}

std::vector<std::string> util::split_str(std::string const& str,
                                         std::string const& delimiters,
                                         bool trimEmpty)
{
    std::vector<std::string> r;
    std::string::size_type lastPos = 0;

    while (true) {
       std::string::size_type pos = str.find_first_of(delimiters, lastPos);
       if (pos == std::string::npos) {
           pos = str.length();
           if (pos != lastPos || !trimEmpty) {
               r.push_back(std::string(str.data() + lastPos, pos - lastPos));
           }
           return r;
       } else {
           if (pos != lastPos || !trimEmpty) {
               r.push_back(std::string(str.data() + lastPos, pos - lastPos));
           }
       }
       lastPos = pos + 1;
    }
}

std::string util::join(std::string const& sep, std::vector<std::string> const& values)
{
    if (values.empty()) {
        return "";
    }
    std::string result;
    result.reserve(sep.size() * (values.size() - 1) + std::accumulate(
        values.begin(), values.end(), 0,
        [](std::string::size_type len, std::string const& s)
        {
            return len + s.size();
        }));
    result += values[0];
    std::for_each(++values.begin()
                , values.end()
                , [&](std::string const& v)
                  {
                      result += sep;
                      result += v;
                  });
    return std::move(result);
}
