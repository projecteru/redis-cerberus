#ifndef __CERBERUS_UTILITY_ALGORITHM_HPP__
#define __CERBERUS_UTILITY_ALGORITHM_HPP__

#include <algorithm>

namespace util {

    template <typename C, typename F>
    void erase_if(C& what, F f)
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

    template <typename C, typename F>
    typename C::const_iterator max_element(C const& what, F f)
    {
        typedef typename C::const_reference rtype;
        return std::max_element(
            what.begin(), what.end(),
            [&](rtype lhs, rtype rhs)
            {
                return f(lhs) < f(rhs);
            });
    }

}

#endif /* __CERBERUS_UTILITY_ALGORITHM_HPP__ */
