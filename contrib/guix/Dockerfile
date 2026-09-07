# syntax=docker/dockerfile:1.6
# Guix deterministic build environment — isolated from host, modern.
# Mirrors release.yml: ubuntu-22.04, Guix 1.5.0+ (official install), glibc 2.35.
# Usage:
#   DOCKER_BUILDKIT=1 docker build -f contrib/guix/Dockerfile -t blocknet-guix:22.04 .
#   docker run --rm --privileged -v $PWD:/blocknet -w /blocknet \
#     -v blocknet-guix-store:/gnu/store -v blocknet-guix-var:/var/guix \
#     blocknet-guix:22.04 ./contrib/guix/guix-build
#   # Or via helper:
#   ./contrib/guix/docker-run.sh x86_64-linux-gnu
FROM ubuntu:22.04
ENV DEBIAN_FRONTEND=noninteractive
ENV GUIX_PROFILE=/root/.guix-profile
ENV PATH=/root/.guix-profile/bin:/var/guix/profiles/per-user/root/current-guix/bin:/usr/local/bin:/usr/bin:/bin

# Layer 1: apt dependencies — cached independently (BuildKit cache mounts)
# This layer only rebuilds when package list changes, not when guix install changes.
RUN --mount=type=cache,target=/var/cache/apt,sharing=locked \
    --mount=type=cache,target=/var/lib/apt,sharing=locked \
    apt-get update -qq && \
    apt-get install -y --no-install-recommends \
        ca-certificates curl wget git netbase xz-utils \
        build-essential pkg-config python3 gnupg && \
    update-ca-certificates 2>/dev/null || true && \
    mkdir -p /gnu/store /var/guix && \
    apt-get clean && \
    rm -rf /var/lib/apt/lists/* 2>/dev/null || true

# Layer 2: Guix binary — separate layer, does not invalidate apt cache
# Uses official guix-binary tarball (no systemd/init required, works in Docker)
RUN mkdir -p /gnu/store /var/guix /root/.config/guix && \
    export PATH="/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin" && \
    (curl -fsSL https://ftp.gnu.org/gnu/guix/guix-binary-1.5.0.x86_64-linux.tar.xz -o /tmp/guix-binary.tar.xz || \
     wget -q https://ftp.gnu.org/gnu/guix/guix-binary-1.5.0.x86_64-linux.tar.xz -O /tmp/guix-binary.tar.xz || \
     curl -fsSL https://ftpmirror.gnu.org/gnu/guix/guix-binary-1.5.0.x86_64-linux.tar.xz -o /tmp/guix-binary.tar.xz) && \
    echo "Extracting guix-binary tarball..." | tee /tmp/guix-install.log && \
    tar -xJf /tmp/guix-binary.tar.xz -C / 2>&1 | tee -a /tmp/guix-install.log && \
    ls -lh /gnu/store/*-guix-1.5.0* 2>&1 | head -n 20 | tee -a /tmp/guix-install.log && \
    ls -R /var/guix 2>&1 | head -n 50 | tee -a /tmp/guix-install.log && \
    # Create guix build users (required for daemon)
    groupadd -r guixbuild 2>/dev/null || groupadd -r _guixbuild 2>/dev/null || true; \
    for i in $(seq 1 10); do \
      id guixbuilder$i >/dev/null 2>&1 || id _guixbuilder$i >/dev/null 2>&1 || \
      useradd -r -g guixbuild -G guixbuild -d /var/empty -s /usr/sbin/nologin guixbuilder$i 2>/dev/null || \
      useradd -r -g _guixbuild -G _guixbuild -d /var/empty -s /usr/sbin/nologin _guixbuilder$i 2>/dev/null || true; \
    done; \
    getent group | grep guix | tee -a /tmp/guix-install.log; \
    getent passwd | grep guix | tee -a /tmp/guix-install.log; \
    # Link profile
    ln -sf /var/guix/profiles/per-user/root/current-guix /root/.guix-profile 2>/dev/null || true; \
    ln -sf /var/guix/profiles/per-user/root/guix-profile /root/.guix-profile 2>/dev/null || true; \
    export PATH="/root/.guix-profile/bin:/var/guix/profiles/per-user/root/current-guix/bin:/gnu/var/guix/profiles/per-user/root/current-guix/bin:$PATH"; \
    . /root/.guix-profile/etc/profile 2>/dev/null || true; \
    which guix 2>&1 | tee -a /tmp/guix-install.log; \
    guix --version 2>&1 | tee /tmp/guix-version.log | tee -a /tmp/guix-install.log || true; \
    guix archive --authorize < /root/.guix-profile/share/guix/ci.guix.gnu.org.pub 2>/dev/null || \
    guix archive --authorize < /var/guix/profiles/per-user/root/current-guix/share/guix/ci.guix.gnu.org.pub 2>/dev/null || true; \
    echo "=== guix-install.log tail ===" && tail -n 100 /tmp/guix-install.log; \
    echo "=== guix-version.log ===" && cat /tmp/guix-version.log 2>/dev/null || true

WORKDIR /blocknet
CMD ["./contrib/guix/guix-build"]
