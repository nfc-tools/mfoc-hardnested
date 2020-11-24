FROM ubuntu:18.04

WORKDIR /mfoc


RUN set -e; \
    apt-get update; \
    apt-get install -y file build-essential autoconf pkg-config automake libnfc-dev liblzma-dev


# Install gcc
ARG COMPILER=gcc-8

RUN set -e; \
    apt-get update; \
    apt-get install -y $COMPILER


COPY . .

ENV CC=${COMPILER}

RUN autoreconf -vis
RUN ./configure
RUN make
RUN file ./src/mfoc-hardnested
RUN ./src/mfoc-hardnested -h
