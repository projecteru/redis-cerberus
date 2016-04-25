#include "fdutil.hpp"
#include "syscalls/cio.h"
#include "utils/logging.hpp"

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
        LOG(DEBUG) << "CLOSE fd=" << this->fd;
        cio::close(this->fd);
        this->fd = -1;
    }
}
