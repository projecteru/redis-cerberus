#ifndef __CERBERUS_TEST_MOCK_ACCEPTOR_HPP__
#define __CERBERUS_TEST_MOCK_ACCEPTOR_HPP__

#include <functional>

#include "core/acceptor.hpp"

void set_acceptor_fd_gen(std::function<int()> fd_gen);
cerb::Acceptor* get_acceptor();
int last_client_fd();

#endif /* __CERBERUS_TEST_MOCK_ACCEPTOR_HPP__ */
