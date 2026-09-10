### Usage

To build dependencies for the current arch+OS:

    make

To build for another arch/OS:

    make HOST=host-platform-triplet

For example:

    make HOST=x86_64-w64-mingw32 -j4

A prefix will be generated that's suitable for plugging into Bitcoin's
configure. In the above example, a dir named x86_64-w64-mingw32 will be
created. To use it for Bitcoin:

    ./configure --prefix=`pwd`/depends/x86_64-w64-mingw32

Supported `host-platform-triplets` (only 64-bit; 32-bit `i686`/`armhf`, `mips`, `powerpc`, `riscv32` removed):

- `x86_64-w64-mingw32` for Win64 (Guix, llvm-mingw Clang, `--with-gui=qt5` required)
- `aarch64-w64-mingw32` for Win ARM64 (Guix, llvm-mingw Clang, `--with-gui=qt5` required)
- `x86_64-apple-darwin` / `arm64-apple-darwin` for macOS (requires Apple SDK 14 + `SDK_PATH`, see below)
- `aarch64-linux-gnu` for Linux ARM 64 bit
- `x86_64-linux-gnu` / `x86_64-pc-linux-gnu` for Linux x64

No other options are needed, the paths are automatically configured.

### Windows (`x86_64` + `aarch64-w64-mingw32`) — one toolchain: llvm-mingw (Clang), Qt5 GUI

All Windows hosts use the single pinned `llvm-mingw` toolchain
(`contrib/guix/manifest.scm`: `llvm-mingw-toolchain`, release `20260908`,
LLVM 23.1.1, UCRT). Rationale: stock GCC has no `aarch64-w64-mingw32`
target (`gcc/config.gcc`: only `i*86/x86_64-*-mingw*`; verified:
`Configuration aarch64-w64-mingw32 not supported`), so ARM64 required a
non-GCC toolchain — and running two compilers for one OS doubles every
future Windows diagnosis, dep bump, and Qt quirk. One toolchain also
matches the ecosystem direction (Qt's own MinGW→LLVM migration,
QTBUG-107516; MSYS2 `CLANG64`/`CLANGARM64` ship production Qt 5.15 built
with `win32-clang-g++`; hebasto's `bitcoin-core-nightly`
`windows-llvm-*.yml` cross-builds both arches with the same toolchain;
Bitcoin upstream conditions win-ARM64 on serving both arches,
`bitcoin/bitcoin#31388`). Side effect: Win64 moved MSVCRT→UCRT with the
toolchain. Consequences, all handled in-tree:

- Qt uses `-xplatform win32-clang-g++` for all mingw hosts
  (`depends/packages/qt.mk`, single `mingw32` block); OpenSSL x64 keeps
  its `mingw64` config (compiler-agnostic) while aarch64 uses the
  backported `mingwarm64` target
  (`depends/patches/openssl/mingwarm64-target.patch`).
- Everything (Qt, Boost, our code) builds against **libc++**, not
  libstdc++: sources must include what they use (`<iterator>`,
  `<algorithm>`, `<new>`, ...). libstdc++-transitive includes do not exist
  here. See `depends/patches/zeromq/`, `depends/patches/bdb/`.
- zeromq needs `__builtin_readcyclecounter()` for ARM64 (`rdtsc` is x86-only
  and llvm-mingw has no `clock_gettime`); BDB needs the `yield` spin hint
  (`depends/patches/bdb/winarm64-mutex-pause.patch`). Both patches are
  arch-guarded, so x86_64 builds take the original paths.
- `build.sh` scrubs Guix's native `CPLUS_INCLUDE_PATH`/`C_INCLUDE_PATH`
  for the two Clang drivers only (via `depends/llvm-shims/`, regenerated
  each run, git-ignored): Clang searches those *before* its own libc++,
  while native `g++` probes (e.g. protobuf's `CXX_FOR_BUILD`) still need
  them. `configure.ac` probes libzmq with `-DZMQ_STATIC` (lld is strict
  about `dllimport`; GNU ld tolerates it).
- The `llvm-mingw` input is a hash-pinned prebuilt tarball: build outputs
  stay reproducible, but the toolchain blob itself is not bootstrappable
  (same trade-off Bitcoin Core weighs in `bitcoin/bitcoin#31388`).

Both Windows hosts are full matrix members (see `release.yml`). No GCC
mingw path remains; the old `make-mingw-pthreads-cross-toolchain` /
`mingw-w64-base-gcc` machinery was deleted from `manifest.scm`.

### Install the required dependencies: Ubuntu & Debian (22.04 jammy / 24.04 noble)

> Releases via Guix inside Docker `FROM ubuntu:22.04` (Guix stock `glibc 2.35` floor). C++17 required.

#### For macOS cross compilation (SDK 14 / Xcode 15, LLD-based toolchain)

    sudo apt-get install curl librsvg2-bin libtiff-tools bsdmainutils cmake imagemagick libcap-dev libz-dev libbz2-dev python3-setuptools
    # plus: automake libtool pkg-config clang lld llvm; SDK in depends/SDKs/ (see release.yml / contrib/guix)
    # SDK layout: depends/SDKs/Xcode-<ver>-<build>-extracted-SDK-with-libcxx-headers (see depends/hosts/darwin.mk)

#### For Win64/WinARM64 cross compilation

- see [build-windows.md](../doc/build-windows.md#cross-compilation-for-ubuntu-and-windows-subsystem-for-linux)

#### For linux (including AARCH64) cross compilation

Common linux dependencies:

    sudo apt-get install make automake cmake curl g++-multilib libtool binutils-gold bsdmainutils pkg-config python3 patch

For linux AARCH64 cross compilation:

    sudo apt-get install g++-aarch64-linux-gnu binutils-aarch64-linux-gnu

### Dependency Options
The following can be set when running make: make FOO=bar

    SOURCES_PATH: downloaded sources will be placed here
    BASE_CACHE: built packages will be placed here
    SDK_PATH: Path where sdk's can be found (used by macOS)
    FALLBACK_DOWNLOAD_PATH: If a source file can't be fetched, try here before giving up
    NO_QT: Don't download/build/cache qt and its dependencies
    NO_WALLET: Don't download/build/cache libs needed to enable the wallet
    NO_UPNP: Don't download/build/cache packages needed for enabling upnp
    DEBUG: disable some optimizations and enable more runtime checking
    RAPIDCHECK: build rapidcheck (experimental)
    HOST_ID_SALT: Optional salt to use when generating host package ids
    BUILD_ID_SALT: Optional salt to use when generating build package ids

If some packages are not built, for example `make NO_WALLET=1`, the appropriate
options will be passed to bitcoin's configure. In this case, `--disable-wallet`.

### Additional targets

    download: run 'make download' to fetch all sources without building them
    download-osx: run 'make download-osx' to fetch all sources needed for macOS builds
    download-win: run 'make download-win' to fetch all sources needed for win builds
    download-linux: run 'make download-linux' to fetch all sources needed for linux builds

### Other documentation

- [description.md](description.md): General description of the depends system
- [packages.md](packages.md): Steps for adding packages

