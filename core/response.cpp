#include "response.hpp"
#include "command.hpp"
#include "proxy.hpp"
#include "message.hpp"
#include "utils/string.h"
#include "utils/address.hpp"
#include "utils/logging.hpp"

using namespace cerb;

std::string const Response::NIL_STR("$-1\r\n");
Buffer const Response::NIL(NIL_STR);

namespace {

    class NormalResponse
        : public Response
    {
    public:
        Buffer rsp;
        bool error;

        NormalResponse(Buffer r, bool e)
            : rsp(std::move(r))
            , error(e)
        {}

        void rsp_to(util::sref<DataCommand> cmd, util::sref<Proxy>)
        {
            cmd->on_remote_responsed(std::move(this->rsp), error);
        }

        Buffer const& get_buffer() const
        {
            return rsp;
        }
    };

    class RetryMovedAskResponse
        : public Response
    {
        static Buffer const dump;
    public:
        void rsp_to(util::sref<DataCommand> cmd, util::sref<Proxy> p)
        {
            p->retry_move_ask_command_later(cmd);
        }

        Buffer const& get_buffer() const
        {
            return dump;
        }

        bool server_moved() const
        {
            return true;
        }
    };
    Buffer const RetryMovedAskResponse::dump("$ RETRY MOVED OR ASK $");

    class ServerResponseSplitter
        : public cerb::msg::MessageSplitterBase<
            Buffer::iterator, ServerResponseSplitter>
    {
        typedef Buffer::iterator Iterator;
        typedef cerb::msg::MessageSplitterBase<Iterator, ServerResponseSplitter> BaseType;

        std::string _last_error;

        void _push_retry_rsp()
        {
            this->responses.push_back(util::mkptr(new RetryMovedAskResponse));
        }

        void _push_normal_rsp(Iterator begin, Iterator end)
        {
            this->responses.push_back(util::mkptr(
                new NormalResponse(Buffer(begin, end), !this->_last_error.empty())));
        }

        void _push_rsp(Iterator i)
        {
            if (!_last_error.empty()) {
                if (util::stristartswith(_last_error, "MOVED") ||
                    util::stristartswith(_last_error, "ASK") ||
                    util::stristartswith(_last_error, "CLUSTERDOWN"))
                {
                    LOG(DEBUG) << "Retry due to " << _last_error;
                    return this->_push_retry_rsp();
                }
            }
            this->_push_normal_rsp(this->_split_points.back(), i);
        }
    public:
        std::vector<util::sptr<Response>> responses;

        explicit ServerResponseSplitter(Iterator i)
            : BaseType(i)
        {}

        void on_split_point(Iterator next)
        {
            this->_push_rsp(next);
            this->_last_error.clear();
        }

        void on_error(Iterator begin, Iterator end)
        {
            this->_last_error = std::string(begin, end);
        }
    };

}

std::vector<util::sptr<Response>> cerb::split_server_response(Buffer& buffer)
{
    ServerResponseSplitter r(msg::split_by(
        buffer.begin(), buffer.end(), ServerResponseSplitter(buffer.begin())));
    if (r.finished()) {
        buffer.clear();
    } else {
        buffer.truncate_from_begin(r.interrupt_point());
    }
    return std::move(r.responses);
}
