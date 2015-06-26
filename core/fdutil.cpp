#include "fdutil.hpp"
#include "syscalls/cio.h"

using namespace cerb;

FDWrapper::~FDWrapper()
{
    this->close();
}

bool FDWrapper::closed() const
{
    return this->fd == -1;
}

void FDWrapper::close()
{
    if (!this->closed()) {
        cio::close(this->fd);
        this->fd = -1;
    }
}
