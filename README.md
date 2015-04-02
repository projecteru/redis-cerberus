A Redis cluster proxy.

Build
===

Requirements:

* UNIX-like system with `SO_REUSEPORT | SO_REUSEADDR` support
* `epoll` support
* pthread
* C++ compiler & lib with C++11 features, like g++ 4.8 or clang++ 3.2
* Google Test (for test)

To build, just

    make

turn on all debug logs

    make MODE=debug

or compile with g艹

    make COMPILER=g++

To link libstdc++ statically, use

    make STATIC_LINK=1

to run test (just cover message parsing parts)

    make runtest

run test with valgrind checking

    make runtest CHECK_MEM=1

Run
===

    ./cerberus example.conf

The argument is path of a configuration file, which should contains at least

* bind : (integer) local port to listen
* node : (address) one of active node in a cluster; format should be host:port
* thread: (integer) number of threads

Commands in Particular
===

Restricted Commands Bypass
---

* `MGET` : execute multiple GETs
* `MSET` : execute multiple SETs
* `DEL` : execute multiple DELs
* `RENAME` : if source and destination are not in the same slot, execute a GET-SET-DEL sequence

Extra Commands
---

* `PROXY`: shows proxy information, including threads count, clients counts
* `KEYSINSLOT slot count`: list keys in a specified slot, same as `CLUSTER GETKEYSINSLOT slot count`

Not Implemented
---

* keys: `KEYS`, `MIGRATE`, `MOVE`, `OBJECT`, `RANDOMKEY`, `RENAMENX`, `SCAN`, `BITOP`,
* list: `BLPOP`, `BRPOP`, `BRPOPLPUSH`, `RPOPLPUSH`,
* set: `SINTERSTORE`, `SDIFFSTORE`, `SINTER`, `SMOVE`, `SUNIONSTORE`,
* sorted set: `ZINTERSTORE`, `ZUNIONSTORE`,
* pub/sub: `PUBSUB`, `PUNSUBSCRIBE`, `UNSUBSCRIBE`,

others: `PFADD`, `PFCOUNT`, `PFMERGE`,
`EVAL`, `EVALSHA`, `SCRIPT`,
`WATCH`, `UNWATCH`, `EXEC`, `DISCARD`, `MULTI`,
`SELECT`, `QUIT`, `ECHO`, `AUTH`,
`CLUSTER`, `BGREWRITEAOF`, `BGSAVE`, `CLIENT`, `COMMAND`, `CONFIG`,
`DBSIZE`, `DEBUG`, `FLUSHALL`, `FLUSHDB`, `INFO`, `LASTSAVE`, `MONITOR`,
`ROLE`, `SAVE`, `SHUTDOWN`, `SLAVEOF`, `SLOWLOG`, `SYNC`, `TIME`,

For more information please read [here (CN)](https://github.com/HunanTV/redis-cerberus/wiki/Redis-%E9%9B%86%E7%BE%A4%E4%BB%A3%E7%90%86%E5%9F%BA%E6%9C%AC%E5%8E%9F%E7%90%86%E4%B8%8E%E4%BD%BF%E7%94%A8).
