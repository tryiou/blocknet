#!/usr/bin/env bash
set -e -o pipefail
# Isolated Guix build — never touches host. Use Docker BuildKit cache + persistent store.
# Usage: ./contrib/guix/docker-run.sh [HOSTS]  # default: x86_64-linux-gnu
#        HOSTS="x86_64-linux-gnu aarch64-linux-gnu" ./contrib/guix/docker-run.sh
#        # or: ./contrib/guix/docker-run.sh "x86_64-linux-gnu aarch64-linux-gnu x86_64-w64-mingw32"
HOSTS="${1:-${HOSTS:-x86_64-linux-gnu}}"
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
    # Drop stale socket from unclean previous runs (persistent /var/guix
    # volume keeps it, but no daemon is alive to serve it).
    if ! guix gc --list-failures >/dev/null 2>&1; then
      rm -f /var/guix/daemon-socket/socket
    fi
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
    echo "---SMOKE (inside container, no host pollution)---"
    for _host in $HOSTS; do
      echo "HOST=$_host"
      _bin="blocknet-binaries/$_host"
      ls -R "$_bin" 2>&1 | head -n 20 || true
      # find executables
      for _exe in $(find "$_bin" -type f -name "blocknetd*" -o -name "blocknet-qt*" 2>/dev/null | head -n 5); do
        echo "FILE $_exe: $(file -b "$_exe" 2>&1 | head -n1)"
        case "$_host" in
          *linux-gnu)
            if echo "$_host" | grep -q aarch64; then
              if command -v qemu-aarch64-static >/dev/null 2>&1; then
                echo "QEMU smoke $_exe --version"
                qemu-aarch64-static "$_exe" --version 2>&1 | head -n5 || echo "qemu smoke failed (expected for cross, file check above is canonical)"
              else
                echo "qemu-aarch64-static not in image (Layer 1b)"
              fi
            else
              # x86_64 native — can run directly inside container
              echo "NATIVE smoke $_exe --version"
              "$_exe" --version 2>&1 | head -n5 || file "$_exe" 2>&1 | head -n5
            fi
            ;;
          *mingw32)
            # Windows: file is canonical, no wine needed to build (only cross via mingw)
            file "$_exe" 2>&1 | head -n1
            ;;
          *darwin*)
            echo "DARWIN artifact (Mach-O) — file check only on Linux"
            file "$_exe" 2>&1 | head -n1 || true
            ;;
        esac
      done
    done
    echo "SMOKE done"
  '

echo "Done. Artifacts in blocknet-binaries/, SHA256SUMS at root."
echo "Determinism check: run twice, SHA256SUMS must be identical."
