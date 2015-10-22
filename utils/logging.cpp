#include "logging.hpp"

INITIALIZE_EASYLOGGINGPP

void logging::init()
{
    el::Configurations c;
    c.setToDefault();
    c.parseFromText("*GLOBAL:\n FORMAT = %datetime %levshort %thread %msg");
    el::Loggers::reconfigureAllLoggers(c);
}
