FROM debian:unstable

ENV HOME=/root

RUN \
  apt-get update && \
  apt-get -y upgrade && \
  apt-get -y install --no-install-recommends \
    busybox ca-certificates curl git \
    make pkg-config gcc cmake \
    check cppcheck lua-check valgrind \
    libcurl4-openssl-dev libevent-dev libssl-dev liburiparser-dev \
	libb64-dev uthash-dev libarchive-dev \
    lua5.1 liblua5.1-0-dev \
    asciidoc lcov markdown libcommon-sense-perl \
    wget procps && \
  apt-get clean

CMD [ "bash" ]
