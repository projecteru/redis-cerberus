#ifndef __BACKTRACPP_SINGAL_HANDLER_H__
#define __BACKTRACPP_SINGAL_HANDLER_H__

namespace trac {

    void set_output(std::ostream& os);

    void trace_on_seg_fault();
    void trace_on_div_0();

}

#endif /* __BACKTRACPP_SINGAL_HANDLER_H__ */
