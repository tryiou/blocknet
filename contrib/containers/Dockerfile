# syntax=docker/dockerfile:1.6
# ubuntu:22.04 (jammy, glibc 2.35 floor), digest-pinned to match the CI
# runner base and the Guix Dockerfile (contrib/guix/Dockerfile).
# Built by .github/workflows/release.yml with context: <repo root>;
# the Guix artifacts are staged into blocknet-binaries/ before the build.
FROM ubuntu:22.04@sha256:2edbbc5dc405e9612ba3584ce95480277e3eb374407b5505fe26f17df77c7dbc
LABEL maintainer="Blocknet Developers <dev@blocknet.org>"
LABEL description="Dockerised Blocknet"

ARG TAG
ARG TARGETARCH

RUN apt-get update && \
    apt-get -y install --no-install-recommends \
    wget \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

RUN mkdir -p /opt/blockchain && \
    mkdir -p /opt/blockchain/data && \
    mkdir -p /opt/blockchain/config && \
    ln -s /opt/blockchain/config /root/.blocknet

COPY blocknet-binaries/ /opt/blockchain/binaries/

RUN set -eux; \
    # Prefer TARGETARCH (buildx), fall back to uname -m for plain docker build
    arch=""; \
    if [ -n "${TARGETARCH:-}" ]; then \
      case "$TARGETARCH" in amd64) arch="x86_64-linux-gnu";; arm64) arch="aarch64-linux-gnu";; *) echo "ERROR: TARGETARCH $TARGETARCH not supported"; exit 1;; esac; \
    else \
      mach=$(uname -m); \
      case "$mach" in aarch64|arm64) arch="aarch64-linux-gnu";; x86_64|amd64) arch="x86_64-linux-gnu";; *) echo "ERROR: Machine type $mach not supported"; exit 1;; esac; \
    fi; \
    cd /opt/blockchain/binaries; \
    # Pick the matching tarball; fail loudly if missing
    set -- blocknet-*-"$arch".tar.gz; \
    if [ ! -f "$1" ]; then echo "ERROR: no tarball for $arch in $(pwd):"; ls -l; exit 1; fi; \
    tar xvzf "$1"; \
    echo "Extracted $1"; \
    cp blocknet-*/bin/* /usr/local/bin; \
    cd /opt/blockchain && rm -rf binaries

# Write default blocknet.conf (can be overridden on commandline)
RUN echo "datadir=/opt/blockchain/data    \n\
                                          \n\
maxmempoolxbridge=128                     \n\
                                          \n\
port=41412    # testnet: 41474            \n\
rpcport=41414 # testnet: 41419            \n\
                                          \n\
listen=1                                  \n\
txindex=1                                 \n\
server=1                                  \n\
logtimestamps=1                           \n\
logips=1                                  \n\
                                          \n\
rpcbind=0.0.0.0                           \n\
rpcallowip=127.0.0.1                      \n\
rpctimeout=60                             \n\
rpcclienttimeout=30" > /opt/blockchain/config/blocknet.conf

WORKDIR /opt/blockchain/
VOLUME ["/opt/blockchain/config", "/opt/blockchain/data"]

# Port, RPC, Test Port, Test RPC
EXPOSE 41412 41414 41474 41419

CMD ["blocknetd", "-daemon=0"]

