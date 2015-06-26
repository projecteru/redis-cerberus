#include "utils/string.h"
#include "message.hpp"

std::string cerb::msg::format_command(std::string command, std::vector<std::string> const& args)
{
    std::vector<std::string> r;
    r.push_back('*' + util::str(int(args.size() + 1)));
    r.push_back('$' + util::str(int(command.size())));
    r.push_back(std::move(command));
    for (std::string const& s: args) {
        r.push_back('$' + util::str(int(s.size())));
        r.push_back(s);
    }
    return util::join("\r\n", r) + "\r\n";
}
