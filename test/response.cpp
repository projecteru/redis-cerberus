#include <gtest/gtest.h>

#include "../core/response.hpp"

using cerb::Buffer;
using cerb::Response;
using cerb::split_server_response;

TEST(Response, String)
{
    {
        Buffer b("+OK\r\n");
        std::vector<util::sptr<Response>> r(split_server_response(b));
        ASSERT_TRUE(b.empty());
        ASSERT_EQ(1, r.size());
        ASSERT_EQ("+OK\r\n", r[0]->get_buffer().to_string());
    }
    {
        Buffer b("$2\r\nOK\r\n$4\r\nabcd\r\n");
        std::vector<util::sptr<Response>> r(split_server_response(b));
        ASSERT_TRUE(b.empty());
        ASSERT_EQ(2, r.size());
        ASSERT_EQ("$2\r\nOK\r\n", r[0]->get_buffer().to_string());
        ASSERT_EQ("$4\r\nabcd\r\n", r[1]->get_buffer().to_string());
    }
}

TEST(Response, Array)
{
    {
        Buffer b("+OK\r\n" "*0\r\n");
        std::vector<util::sptr<Response>> r(split_server_response(b));
        ASSERT_TRUE(b.empty());
        ASSERT_EQ(2, r.size());
        ASSERT_EQ("+OK\r\n", r[0]->get_buffer().to_string());
        ASSERT_EQ("*0\r\n", r[1]->get_buffer().to_string());
    }
    {
        Buffer b("+OK\r\n"
                 "*2\r\n"
                     "$1\r\na\r\n"
                     "$1\r\nb\r\n");
        std::vector<util::sptr<Response>> r(split_server_response(b));
        ASSERT_TRUE(b.empty());
        ASSERT_EQ(2, r.size());
        ASSERT_EQ("+OK\r\n", r[0]->get_buffer().to_string());
        ASSERT_EQ("*2\r\n$1\r\na\r\n$1\r\nb\r\n",
                  r[1]->get_buffer().to_string());
    }
}
