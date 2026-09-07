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

Common `host-platform-triplets` for cross compilation are (CI pins in `.github/workflows/ci.yml`):

- `i686-w64-mingw32` for Win32 (legacy)
- `x86_64-w64-mingw32` for Win64 — CI `windows-2022`
- `aarch64-w64-mingw32` for Win ARM64 — CI `windows-11-arm` (experimental, needs `qt` arm64 patch)
- `x86_64-apple-darwin` / `arm64-apple-darwin` for macOS — CI `macos-15-intel` / `macos-15` (SDK 14.5/15.2, not 10.11)
- `arm-linux-gnueabihf` for Linux ARM 32 bit (legacy)
- `aarch64-linux-gnu` for Linux ARM 64 bit — CI `ubuntu-24.04-arm`
- `x86_64-linux-gnu` / `x86_64-pc-linux-gnu` for Linux x64 — CI `ubuntu-22.04`
- `riscv32-linux-gnu` / `riscv64-linux-gnu` for RISC-V (kept, not in CI matrix)

No other options are needed, the paths are automatically configured.

### Install the required dependencies: Ubuntu & Debian (22.04 jammy / 24.04 noble)

> CI: `ubuntu-22.04` (x64) + `ubuntu-24.04-arm` (aarch64) (see `ci.yml`). Releases via Guix on `ubuntu-22.04` with glibc 2.27 floor (`--enable-glibc-back-compat`, Docker `FROM ubuntu:22.04`).

#### For macOS cross compilation (SDK 14.5/15.2 — old 10.11 deprecated)

    sudo apt-get install curl librsvg2-bin libtiff-tools bsdmainutils cmake imagemagick libcap-dev libz-dev libbz2-dev python3-setuptools
    # plus: automake libtool pkg-config; SDK in depends/SDKs/ (see release.yml / contrib/guix)

#### For Win32/Win64/WinARM64 cross compilation

- see [build-windows.md](../doc/build-windows.md#cross-compilation-for-ubuntu-and-windows-subsystem-for-linux)
- CI: `windows-2022` (x64) + `windows-11-arm` (arm64 via `aarch64-w64-mingw32`, experimental)

#### For linux (including i386, ARM) cross compilation

Common linux dependencies:

    sudo apt-get install make automake cmake curl g++-multilib libtool binutils-gold bsdmainutils pkg-config python3 patch

For linux ARM cross compilation:

    sudo apt-get install g++-arm-linux-gnueabihf binutils-arm-linux-gnueabihf

For linux AARCH64 cross compilation:

    sudo apt-get install g++-aarch64-linux-gnu binutils-aarch64-linux-gnu

For linux RISC-V 64-bit cross compilation (there are no packages for 32-bit):

    sudo apt-get install g++-riscv64-linux-gnu binutils-riscv64-linux-gnu

RISC-V known issue: gcc-7.3.0 and gcc-7.3.1 result in a broken `test_bitcoin` executable (see https://github.com/bitcoin/bitcoin/pull/13543),
this is apparently fixed in gcc-8.1.0.

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

