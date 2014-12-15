#ifndef __BACKTRACPP_DEMANGLE_H__
#define __BACKTRACPP_DEMANGLE_H__

#include <string>

namespace trac {

    struct frame {
        std::string const module;
        int const address;
        std::string const func;
        int const offset;

        frame(std::string const& m, int a, std::string const& f, int o)
            : module(m)
            , address(a)
            , func(f)
            , offset(o)
        {}

        std::string str() const;
    };

    frame demangle(std::string const& frame_info);

}

#endif /* __BACKTRACPP_DEMANGLE_H__ */
