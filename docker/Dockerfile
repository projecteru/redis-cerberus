FROM debian:buster-slim

# add our user and group first to make sure their IDs get assigned consistently, regardless of whatever dependencies get added
RUN groupadd -r -g 999 cerberus && useradd -r -g cerberus -u 999 cerberus

# grab gosu for easy step-down from root
# https://github.com/tianon/gosu/releases
ENV GOSU_VERSION 1.11
RUN set -eux; \
# save list of currently installed packages for later so we can clean up
	sed -i "s@http://deb.debian.org@http://mirrors.huaweicloud.com@g" /etc/apt/sources.list; \
	sed -i "s@http://security.debian.org@http://mirrors.huaweicloud.com@g" /etc/apt/sources.list; \
	savedAptMark="$(apt-mark showmanual)"; \
	apt-get update; \
	apt-get install -y --no-install-recommends \
		ca-certificates \
		dirmngr \
		wget \
	; \
	rm -rf /var/lib/apt/lists/*; \
	\
	dpkgArch="$(dpkg --print-architecture | awk -F- '{ print $NF }')"; \
	wget -O /usr/local/bin/gosu "https://github.com/tianon/gosu/releases/download/$GOSU_VERSION/gosu-$dpkgArch"; \
	# wget -O /usr/local/bin/gosu.asc "https://github.com/tianon/gosu/releases/download/$GOSU_VERSION/gosu-$dpkgArch.asc"; \
	\
# verify the signature
	# export GNUPGHOME="$(mktemp -d)"; \
	# gpg --batch --keyserver hkps://keys.openpgp.org --recv-keys B42F6819007F00F88E364FD4036A9C25BF357DD4; \
	# gpg --batch --verify /usr/local/bin/gosu.asc /usr/local/bin/gosu; \
	# gpgconf --kill all; \
	# rm -rf "$GNUPGHOME" /usr/local/bin/gosu.asc; \
	# \
# clean up fetch dependencies
	apt-mark auto '.*' > /dev/null; \
	[ -z "$savedAptMark" ] || apt-mark manual $savedAptMark > /dev/null; \
	apt-get purge -y --auto-remove -o APT::AutoRemove::RecommendsImportant=false; \
	\
	chmod +x /usr/local/bin/gosu; \
# verify that the binary works
	gosu --version; \
	gosu nobody true

# ENV CERBERUS_VERSION 4.0.14
ENV CERBERUS_DOWNLOAD_URL https://github.com/totemofwolf/redis-cerberus.git
# ENV CERBERUS_DOWNLOAD_SHA 1e1e18420a86cfb285933123b04a82e1ebda20bfb0a289472745a087587e93a7

RUN set -eux; \
	\
	savedAptMark="$(apt-mark showmanual)"; \
	apt-get update; \
	apt-get install -y --no-install-recommends \
		ca-certificates \
        git \
		g++ \
		make \
		cmake \
        bash \
        tzdata \
        build-essential \
		\
		libc6-dev \
	; \
	rm -rf /var/lib/apt/lists/*; \
	\
    git config --global http.sslVerify false; \
    mkdir -pv /usr/local/src/ && cd /usr/local/src/; \
    git clone -b master "$CERBERUS_DOWNLOAD_URL"; \
    cd redis-cerberus; \
	\
    # for higher version g++
    # CFLAGS=-fPIC
    make COMPILER=g++; \
	cp -fv ./cerberus /usr/local/bin/; \
    make clean; \
	\
	rm -r /usr/local/src/redis-cerberus; \
	cd -;\
	\
	apt-mark auto '.*' > /dev/null; \
	[ -z "$savedAptMark" ] || apt-mark manual $savedAptMark > /dev/null; \
	apt-get purge -y --auto-remove -o APT::AutoRemove::RecommendsImportant=false

RUN mkdir /data && chown cerberus:cerberus /data
VOLUME /data
WORKDIR /data

COPY docker-entrypoint.sh /usr/local/bin/
ENTRYPOINT ["docker-entrypoint.sh"]

EXPOSE 6379
CMD ["cerberus"]
