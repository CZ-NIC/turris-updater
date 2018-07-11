FROM debian:stable

ENV HOME=/root

RUN \
  apt-get update && \
  apt-get -y upgrade && \
  apt-get -y install --no-install-recommends \
    busybox ca-certificates curl git \
    make pkg-config gcc \
    check cppcheck lua-check valgrind \
    libcurl4-openssl-dev libevent-dev libssl-dev uthash-dev \
    lua5.1 liblua5.1-0-dev \
    asciidoc lcov markdown libcommon-sense-perl \
    wget procps && \
  apt-get clean

CMD [ "bash" ]
