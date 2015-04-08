#include "connection.hpp"

using namespace cerb;

void ProxyConnection::on_error()
{
    this->close();
}
