FROM debian:trixie-slim

ARG DEBIAN_FRONTEND=noninteractive

# Prevents OOM on low-end machines or memory-restricted docker/podman as
# unrestricred number of zstd worker threads consumes a lot of memory during
# "apt-get install" packages decompression
ARG ZSTD_NBTHREADS=2

COPY common-packages.txt dev-packages.txt /tmp/

RUN apt-get update \
    && cat /tmp/common-packages.txt /tmp/dev-packages.txt \
        | xargs apt-get install -y --no-install-recommends \
    && apt-get clean \
    && rm -rf /var/lib/apt/lists/*

ENV CC=gcc
ENV CXX=g++

RUN useradd --shell /bin/bash --create-home vscode \
    && echo "vscode ALL=(ALL) NOPASSWD:ALL" >> /etc/sudoers

USER vscode
