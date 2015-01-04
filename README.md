A Redis cluster proxy.

Build
===

Requirements:

* UNIX-like system with port-reuse support
* pthread
* C++ compiler & lib with C++11 features
* Google Test (for test)

    make

or compile with g艹

    make COMPILER=g++

To link libstdc++ statically, use

    make STATIC_LINK=1

Run
===

    ./cerberus example.conf

The argument is path of a configuration file, which should contains at least

* bind : (integer) local port to listen
* node : (address) one of active node in a cluster; format should be host:port
* thread: (integer) number of threads

Restricted Command Bypass
===

* `MGET` : execute multiple GETs
* `MSET` : execute multiple SETs
* `DEL` : execute multiple DELs
* `RENAME` : if source and destination are not in the same slot, execute a GET-SET-DEL sequence

For more information please read [here (CN)](https://github.com/hntv/redis-cerberus/wiki/Redis-%E9%9B%86%E7%BE%A4%E4%BB%A3%E7%90%86%E5%9F%BA%E6%9C%AC%E5%8E%9F%E7%90%86%E4%B8%8E%E4%BD%BF%E7%94%A8).
