#include "globals.hpp"

std::vector<cerb::ListenThread> cerb_global::all_threads;
thread_local util::sref<cerb::Proxy const> cerb_global::current_proxy(nullptr);
