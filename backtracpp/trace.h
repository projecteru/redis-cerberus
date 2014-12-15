#ifndef __BACKTRACPP_TRACE_H__
#define __BACKTRACPP_TRACE_H__

#include <vector>
#include <ostream>

#include "demangle.h"

namespace trac {

    std::vector<frame> stacktrace();

    void print_trace();
    std::ostream& print_trace(std::ostream& os);

    void print_trace_br();
    std::ostream& print_trace_br(std::ostream& os);

}

#endif /* __BACKTRACPP_TRACE_H__ */
