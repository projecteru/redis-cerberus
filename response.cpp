#include "response.hpp"
#include "command.hpp"
#include "proxy.hpp"
#include "message.hpp"
#include "utils/string.h"
#include "utils/address.hpp"
#include "utils/logging.hpp"

using namespace cerb;

namespace {

    class NormalResponse
        : public Response
    {
    public:
        Buffer rsp;

        explicit NormalResponse(Buffer r)
            : rsp(std::move(r))
        {}

        void rsp_to(util::sref<Command> cmd, util::sref<Proxy>)
        {
            cmd->copy_response(std::move(this->rsp));
        }

        Buffer const& dump_buffer() const
        {
            return rsp;
        }
    };

    class RetryMovedAskResponse
        : public Response
    {
        static Buffer const dump;
    public:
        void rsp_to(util::sref<Command> cmd, util::sref<Proxy> p)
        {
            p->retry_move_ask_command_later(cmd);
        }

        Buffer const& dump_buffer() const
        {
            return dump;
        }
    };
    Buffer const RetryMovedAskResponse::dump(
        Buffer::from_string("$ RETRY MOVED OR ASK $"));

    class ServerResponseSplitter
        : public cerb::msg::MessageSplitterBase<
            Buffer::iterator, ServerResponseSplitter>
    {
        typedef cerb::msg::MessageSplitterBase<
            Buffer::iterator, ServerResponseSplitter> BaseType;

        std::string _last_error;

        void _push_retry_rsp()
        {
            responses.push_back(util::mkptr(new RetryMovedAskResponse));
        }

        void _push_normal_rsp(Buffer::iterator begin, Buffer::iterator end)
        {
            responses.push_back(util::mkptr(
                new NormalResponse(Buffer(begin, end))));
        }

        void _push_rsp(Buffer::iterator i)
        {
            if (!_last_error.empty()) {
                if (util::stristartswith(_last_error, "MOVED") ||
                    util::stristartswith(_last_error, "ASK") ||
                    util::stristartswith(_last_error, "CLUSTERDOWN"))
                {
                    LOG(DEBUG) << "Retry due to " << _last_error;
                    return _push_retry_rsp();
                }
            }
            _push_normal_rsp(_split_points.back(), i);
        }

        void _on_element(Buffer::iterator i)
        {
            _push_rsp(i);
            _last_error.clear();
            BaseType::on_element(i);
        }
    public:
        std::vector<util::sptr<Response>> responses;
        std::function<void(Buffer::iterator)> on_element;

        void on_byte(byte) {}

        explicit ServerResponseSplitter(Buffer::iterator i)
            : BaseType(i)
            , on_element([&](Buffer::iterator i)
                         {
                             this->_on_element(i);
                         })
        {}

        Buffer::iterator on_err(Buffer::iterator begin, Buffer::iterator end)
        {
            auto next = msg::parse_simple_str(
                begin, end,
                [&](byte b)
                {
                    this->_last_error += b;
                });
            this->on_element(next);
            return next;
        }

        void on_arr_end(Buffer::iterator next)
        {
            _push_normal_rsp(_split_points.back(), next);
            BaseType::on_element(next);
            if (this->_nested_array_element_count.size() == 0) {
                this->on_element = [&](Buffer::iterator i)
                                   {
                                       this->_on_element(i);
                                   };
            }
        }

        void on_arr(cerb::rint size, Buffer::iterator next)
        {
            if (size != 0) {
                this->on_element = [&](Buffer::iterator i)
                                   {
                                       BaseType::on_element(i);
                                   };
            }
            BaseType::on_arr(size, next);
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
