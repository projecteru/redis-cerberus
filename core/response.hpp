#ifndef __CERBERUS_RESPONSE_HPP__
#define __CERBERUS_RESPONSE_HPP__

#include <vector>

#include "utils/pointer.h"
#include "buffer.hpp"

namespace cerb {

    class DataCommand;
    class Proxy;

    class Response {
    public:
        Response() {}
        virtual ~Response() {}
        Response(Response const&) = delete;

        virtual void rsp_to(util::sref<DataCommand> c, util::sref<Proxy> p) = 0;
        virtual Buffer const& get_buffer() const = 0;
        virtual bool server_moved() const { return false; }

        static std::string const NIL_STR;
        static Buffer const NIL;
    };

    std::vector<util::sptr<Response>> split_server_response(Buffer& buffer);

}

#endif /* __CERBERUS_RESPONSE_HPP__ */
