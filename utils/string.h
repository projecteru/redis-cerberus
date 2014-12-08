#ifndef __STEKIN_UTILITY_STRING_H__
#define __STEKIN_UTILITY_STRING_H__

#include <vector>
#include <string>

namespace util {

    std::string str(int i);
    std::string str(long i);
    std::string str(long long i);
    std::string str(double d);
    std::string str(bool b);
    std::string str(void const* p);

    std::vector<std::string> split_str(std::string const& str,
                                       std::string const& delimiters=" ",
                                       bool trimEmpty=false);

}

#endif /* __STEKIN_UTILITY_STRING_H__ */
