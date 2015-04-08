#ifndef __CERBERUS_UTILITY_ALGORITHM_HPP__
#define __CERBERUS_UTILITY_ALGORITHM_HPP__

#include <algorithm>

namespace util {

    template <typename C, typename F>
    static void erase_if(C& what, F f)
    {
        what.erase(
            std::remove_if(
                what.begin(), what.end(),
                [&](typename C::value_type const& cmd)
                {
                    return f(cmd);
                }),
            what.end());
    }

}

#endif /* __CERBERUS_UTILITY_ALGORITHM_HPP__ */
