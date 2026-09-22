# Guix deterministic builds — successor to Gitian

Deterministic Blocknet builds — ported from Bitcoin Core `contrib/guix`.
Runs on `ubuntu-22.04` (CI) / `ubuntu-24.04` local, produces all 6 targets via Guix cross-compilation
(`x86_64-linux-gnu`, `aarch64-linux-gnu`, `x86_64-w64-mingw32`,
`aarch64-w64-mingw32`, `x86_64-apple-darwin`, `arm64-apple-darwin`).
`glibc` floor is stock Guix `2.35` (compatible with `ubuntu-22.04` runtime);
Docker runtime `FROM ubuntu:22.04`.

**Clean Guix-only — no depends fallback.** Release determinism comes solely
from Guix. Per-OS `depends` sanity is `CI` (`.github/workflows/ci.yml`, 6-arch
matrix on modern runners), not Release.

**FULL GUI policy:** `libexec/build.sh` configures with `--with-gui=qt5`.
A missing `blocknet-qt` / `blocknet-qt.exe` is a hard error — Guix never
ships silent daemon-only artifacts.

Toolchain (see `manifest.scm` + `channels.scm` pin):
- `gcc-12`, `linux-libre-headers-6.1`, `clang-toolchain-18` + `lld-18` (darwin)
- pinned `llvm-mingw` (Clang, UCRT) for all `*-mingw32` targets (see `depends/README.md`)
- `nsis`, `osslsigncode` (tests disabled), `python-lief`

Depends versions (see `depends/packages/*.mk`, `doc/dependencies.md`):
- `boost@1.81.0`, `openssl@3.5.8 LTS`, `qt@5.15.14`, `protobuf@21.12`,
  `qrencode@4.1.1`, `zeromq@4.3.4`, `libevent@2.1.12-stable`
- C++17 throughout (`configure.ac`, all compiled depends)

macOS cross builds require an Apple SDK (non-redistributable):
place `Xcode-<ver>-<build>-extracted-SDK-with-libcxx-headers`
(see `depends/hosts/darwin.mk`) under `depends/SDKs/` or set `SDK_PATH`.
Without an SDK, darwin hosts are skipped with a warning.

Local deterministic build (no host Guix needed):

    ./contrib/guix/docker-run.sh "x86_64-linux-gnu"
    # or: HOSTS="x86_64-linux-gnu aarch64-linux-gnu" ./contrib/guix/docker-run.sh

See `doc/release-process.md` (Guix section) and `.github/workflows/release.yml` `guix-build`.
