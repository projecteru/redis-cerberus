
## changelog

``` bash
[100901]

*) add docker support

```

## usage:

``` bash
$ docker build . -t redis-cerberus:190901

$ docker run --rm -p 6379:6379 -e TZ=Asia/Shanghai -v $PWD/example.conf:/tmp/example.conf redis-cerberus:190901 cerberus /tmp/example.conf
```
