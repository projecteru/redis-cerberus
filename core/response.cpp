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
        bool error;

        NormalResponse(Buffer r, bool e)
            : rsp(std::move(r))
            , error(e)
        {}

        void rsp_to(util::sref<DataCommand> cmd, util::sref<Proxy>)
        {
            cmd->on_remote_responsed(std::move(this->rsp), error);
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
        void rsp_to(util::sref<DataCommand> cmd, util::sref<Proxy> p)
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

        void _base_on_element(Buffer::iterator i)
        {
            BaseType::on_element(i);
        }

        void _push_retry_rsp()
        {
            responses.push_back(util::mkptr(new RetryMovedAskResponse));
        }

        void _push_normal_rsp(Buffer::iterator begin, Buffer::iterator end,
                              bool error)
        {
            responses.push_back(util::mkptr(
                new NormalResponse(Buffer(begin, end), error)));
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
            _push_normal_rsp(_split_points.back(), i, !_last_error.empty());
        }

        static void _default_on_element(ServerResponseSplitter* me, Buffer::iterator i)
        {
            me->_push_rsp(i);
            me->_last_error.clear();
            me->_base_on_element(i);
        }

        std::function<void(ServerResponseSplitter*, Buffer::iterator)> _on_element;
    public:
        std::vector<util::sptr<Response>> responses;

        void on_byte(byte) {}

        void on_element(Buffer::iterator i)
        {
            this->_on_element(this, i);
        }

        explicit ServerResponseSplitter(Buffer::iterator i)
            : BaseType(i)
            , _on_element(_default_on_element)
        {}

        Buffer::iterator on_err(Buffer::iterator begin, Buffer::iterator end)
        {
            auto next = msg::parse_simple_str(
                begin, end,
                [this](byte b)
                {
                    this->_last_error += b;
                });
            this->on_element(next);
            return next;
        }

        void on_arr_end(Buffer::iterator next)
        {
            _push_normal_rsp(_split_points.back(), next, false);
            BaseType::on_element(next);
            if (this->_nested_array_element_count.size() == 0) {
                this->_on_element = _default_on_element;
            }
        }

        void on_arr(cerb::rint size, Buffer::iterator next)
        {
            if (size != 0) {
                this->_on_element =
                    [](ServerResponseSplitter* me, Buffer::iterator i)
                    {
                        me->_base_on_element(i);
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
