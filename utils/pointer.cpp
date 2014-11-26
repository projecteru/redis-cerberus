#include <sstream>

#include "pointer.h"
#include "string.h"

using namespace util;

std::string id::str() const
{
    return util::str(_id);
}

bool id::operator<(id const& rhs) const
{
    return _id < rhs._id;
}

bool id::operator==(id const& rhs) const
{
    return _id == rhs._id;
}

bool id::operator!=(id const& rhs) const
{
    return !operator==(rhs);
}
