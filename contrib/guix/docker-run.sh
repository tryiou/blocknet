#!/usr/bin/env bash
set -e -o pipefail
# Isolated Guix build — never touches host. Use Docker BuildKit cache + persistent store.
# Usage: ./contrib/guix/docker-run.sh [HOSTS]  # default: x86_64-linux-gnu
HOSTS="${1:-x86_64-linux-gnu}"
JOBS="${JOBS:-$(nproc)}"

# Build image once (cached)
if ! docker image inspect blocknet-guix:22.04 >/dev/null 2>&1; then
  echo "Building blocknet-guix:22.04..."
  DOCKER_BUILDKIT=1 docker build -f contrib/guix/Dockerfile -t blocknet-guix:22.04 .
fi

# Persistent Guix store (survives daemon restarts, no --disable-chroot needed)
VOLUME_FLAGS="-v $PWD:/blocknet -w /blocknet -v blocknet-guix-store:/gnu/store -v blocknet-guix-var:/var/guix"

echo "Host HOSTS=$HOSTS JOBS=$JOBS"
echo "Running Guix single-arch smoke inside Docker (--privileged for guix-daemon, chroot enabled)..."
docker run --rm --privileged \
  $VOLUME_FLAGS \
  -e HOSTS="$HOSTS" -e JOBS="$JOBS" -e FORCE_DIRTY_WORKTREE=1 \
  -e OUTDIR_BASE="/blocknet/blocknet-binaries" \
  blocknet-guix:22.04 bash -c '
    set -e
    git config --global --add safe.directory /blocknet 2>/dev/null || true
    # Fix ownership for Guix container user namespace (host 1000 -> container 65534)
    chmod -R a+w /blocknet 2>/dev/null || true
    chown -R 0:0 /blocknet/depends 2>/dev/null || chmod -R 777 /blocknet/depends 2>/dev/null || true
    # Clean previous Guix build state (profile symlink exists from previous run)
    rm -rf /blocknet/guix-build-*/var 2>/dev/null || true
    rm -rf /blocknet/guix-build-*/distsrc-* 2>/dev/null || true
    rm -rf /blocknet/blocknet-binaries 2>/dev/null || true
    mkdir -p /blocknet/blocknet-binaries
    . /root/.guix-profile/etc/profile 2>/dev/null || true
    export PATH="/root/.guix-profile/bin:$PATH"
    # Start daemon with chroot (proper isolation)
    if getent group _guixbuild >/dev/null; then BUILD_GROUP=_guixbuild; else BUILD_GROUP=guixbuild; fi
    if ! guix gc --list-failures >/dev/null 2>&1; then
      echo "Starting guix-daemon --build-users-group=$BUILD_GROUP ..."
      guix-daemon --build-users-group="$BUILD_GROUP" &
      for i in $(seq 1 15); do guix gc --list-failures >/dev/null 2>&1 && break; sleep 2; done
      guix gc --list-failures >/dev/null || (echo "daemon failed"; cat /tmp/guix-daemon.log 2>&1 | head; exit 1)
    fi
    export SSL_CERT_FILE=/etc/ssl/certs/ca-certificates.crt
    export GIT_SSL_CAINFO=/etc/ssl/certs/ca-certificates.crt
    ./contrib/guix/guix-build
    echo "---SHA256SUMS---"
    find blocknet-binaries -type f -exec sha256sum {} \; | tee SHA256SUMS
  '

echo "Done. Artifacts in blocknet-binaries/, SHA256SUMS at root."
echo "Determinism check: run twice, SHA256SUMS must be identical."
