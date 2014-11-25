#include <sys/socket.h>
#include <sys/wait.h>
#include <sys/epoll.h>
#include <sys/sendfile.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <memory.h>
#include <netdb.h>
#include <netinet/tcp.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <fcntl.h>
#include <errno.h> 
#include <signal.h>
#include <algorithm>

#include "proxy.hpp"

using cerb::Connection;
using cerb::Client;
using cerb::Server;

int const PORT = 8889;

void exit_on_int(int s)
{
    fprintf(stderr, "Exit %d\n", s);
    exit(0);
}

char* copy_message(char* dst, char* src, char* src_end, int* size)
{
    *size = 0;
    while (src != src_end && ++*size && ('\n' != (*(dst++) = *(src++))))
        ;
    return src;
}

int split_message(std::vector<Client*>& clients, char* message, char* message_end)
{
    int i;
    for (i = 0; i < clients.size() && message != message_end; ++i) {
        message = copy_message(clients[i]->buf, message, message_end, &clients[i]->write_size);
    }
    return i;
}

int main()
{
    signal(SIGINT, exit_on_int);

    cerb::Proxy p;
    p.run(PORT);

    return 0;
}
