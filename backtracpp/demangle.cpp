#include <cxxabi.h>
#include <cstdlib>
#include <stdexcept>
#include <sstream>
#include <algorithm>

#include "demangle.h"

using namespace trac;

namespace {

    struct auto_string {
        explicit auto_string(char* string)
            : _string(string)
        {}

        ~auto_string()
        {
            free(_string);
        }

        char const* get() const
        {
            return _string;
        }
    private:
        char* const _string;
    };

    struct ch_finder {
        explicit ch_finder(char ch)
            : c(ch)
        {}

        bool operator()(char ch) const
        {
            return c == ch;
        }

        char const c;
    };

}

static std::string demangle_func_name(std::string const& name)
{
    int status = -1;
    auto_string demangled_name(abi::__cxa_demangle(name.c_str(), NULL, NULL, &status));
    switch (status) {
    case -1:
        throw std::bad_alloc();
    case -2:
        return name;
    default:
        return std::string(demangled_name.get());
    }
}

static std::string find_module(std::string const& str)
{
    std::string::const_reverse_iterator e = std::find_if(str.rbegin(), str.rend(), ch_finder('('));
    if (e == str.rend()) {
        return "";
    }
    std::string reversed(e + 1, str.rend());
    return std::string(reversed.rbegin(), reversed.rend());
}

static std::string find_func_name(std::string const& str)
{
    std::string::const_reverse_iterator e = std::find_if(str.rbegin(), str.rend(), ch_finder('+'));
    if (e == str.rend()) {
        return "";
    }
    std::string::const_reverse_iterator b = std::find_if(e, str.rend(), ch_finder('('));
    std::string reversed(e + 1, b);
    return std::string(reversed.rbegin(), reversed.rend());
}

static std::string find_func_offset(std::string const& str)
{
    std::string::const_reverse_iterator e = std::find_if(str.rbegin(), str.rend(), ch_finder(')'));
    std::string::const_reverse_iterator b = std::find_if(e, str.rend(), ch_finder('+'));
    if (b == str.rend()) {
        return "";
    }
    std::string reversed(e + 1, b);
    return std::string(reversed.rbegin() + 2, reversed.rend());
}

static std::string find_address(std::string const& str)
{
    std::string::const_reverse_iterator e = std::find_if(str.rbegin(), str.rend(), ch_finder(']'));
    std::string::const_reverse_iterator b = std::find_if(e, str.rend(), ch_finder('['));
    std::string reversed(e + 1, b);
    return std::string(reversed.rbegin() + 2, reversed.rend());
}

static int hex_from_str(std::string const& str)
{
    int result = 0;
    for (std::string::const_iterator i = str.begin(); str.end() != i; ++i) {
        result *= 16;
        if ('0' <= *i && *i <= '9') {
            result += (*i - '0');
        } else {
            result += (*i - 'a') + 10;
        }
    }
    return result;
}

std::string frame::str() const
{
    std::stringstream ss;
    ss << std::hex << module << ' ' << address << ' ' << func << ' ' << offset;
    return ss.str();
}

frame trac::demangle(std::string const& frame_info)
{
    std::string module(find_module(frame_info));
    int address(hex_from_str(find_address(frame_info)));
    std::string func(demangle_func_name(find_func_name(frame_info)));
    int offset(hex_from_str(find_func_offset(frame_info)));
    return frame(module, address, func, offset);
}
