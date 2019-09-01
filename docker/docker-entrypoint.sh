#!/bin/sh
set -e

# first arg is `-f` or `--some-option`
# or first arg is `something.conf`
if [ "${1#-}" != "$1" ] || [ "${1%.conf}" != "$1" ]; then
	set -- cerberus "$@"
fi

# allow the container to be started with `--user`
if [ "$1" = 'cerberus' -a "$(id -u)" = '0' ]; then
	find . \! -user cerberus -exec chown cerberus '{}' +
	exec gosu cerberus "$0" "$@"
fi

exec "$@"
