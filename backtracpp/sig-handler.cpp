#include <csignal>
#include <cstring>
#include <iostream>

#include "sig-handler.h"
#include "trace.h"

using namespace trac;

static std::ostream** ostream()
{
    static std::ostream* s = &std::cout;
    return &s;
}

void trac::set_output(std::ostream& os)
{
    *ostream() = &os;
}

namespace {

    struct signal_action {
        typedef void (* handler_type)(int, siginfo_t*, void*);

        explicit signal_action(handler_type handler)
        {
            memset(&_sa, 0, sizeof(struct sigaction));
            _sa.sa_sigaction = handler;
            _sa.sa_flags = SA_SIGINFO;
        }

        struct sigaction const* get_act() const
        {
            return &_sa;
        }
    private:
        struct sigaction _sa;
    };

}

static void install(int sig, signal_action const& action) 
{
    sigaction(sig, action.get_act(), NULL);
}

static void handle_segv(int, siginfo_t*, void*)
{
    (**ostream()) << "Segmentation fault:" << std::endl;
    print_trace(**ostream());
    std::terminate();
}

void trac::trace_on_seg_fault()
{
    install(SIGSEGV, signal_action(handle_segv));
}

static void handle_div_0(int, siginfo_t* info, void*)
{
    if (FPE_INTDIV == info->si_code)
    {
        (**ostream()) << "Divided by 0:" << std::endl;
        print_trace(**ostream());
        std::terminate();
    }
}

void trac::trace_on_div_0()
{
    install(SIGFPE, signal_action(handle_div_0));
}
