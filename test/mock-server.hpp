#ifndef __CERBERUS_TEST_MOCK_SERVER_HPP__
#define __CERBERUS_TEST_MOCK_SERVER_HPP__

#include "core/server.hpp"

std::set<cerb::Server*> const& created_servers();
std::set<cerb::Server*> const& closed_servers();
void clear_all_servers();

#endif /* __CERBERUS_TEST_MOCK_SERVER_HPP__ */
