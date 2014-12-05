#ifndef __STEKIN_UTILITY_STRING_H__
#define __STEKIN_UTILITY_STRING_H__

#include <vector>
#include <string>

namespace util {

    std::string replace_all(std::string src
                          , std::string const& origin_text
                          , std::string const& replacement);
    std::string join(std::string const& sep, std::vector<std::string> const& values);

    std::string str(int i);
    std::string str(long i);
    std::string str(long long i);
    std::string str(double d);
    std::string str(bool b);
    std::string str(void const* p);

}

#endif /* __STEKIN_UTILITY_STRING_H__ */
