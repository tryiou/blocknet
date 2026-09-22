Dependencies
============

These are the dependencies currently used by Blocknet Core. You can find instructions for installing them in the `build-*.md` file for your platform.
Requires a C++17 compiler (GCC >= 9, Clang >= 10).

| Dependency | Version used | Minimum required | CVEs | Shared | [Bundled Qt library](https://doc.qt.io/qt-5/configure-options.html#third-party-libraries) |
| --- | --- | --- | --- | --- | --- |
| Berkeley DB | [4.8.30](https://www.oracle.com/technetwork/database/database-technologies/berkeleydb/downloads/index.html) | 4.8.x | No |  |  |
| Boost | [1.81.0](https://archives.boost.io/release/1.81.0/source/) | [1.73.0](https://github.com/boostorg/boost) | No |  |  |
| Clang |  | [10.0+](https://llvm.org/releases/download.html) (C++17 support) |  |  |  |
| D-Bus | [1.10.18](https://cgit.freedesktop.org/dbus/dbus/tree/NEWS?h=dbus-1.10) |  | No | Yes |  |
| Expat | [2.6.4](https://libexpat.github.io/) |  | No | Yes |  |
| fontconfig | [2.12.6](https://www.freedesktop.org/software/fontconfig/release/) |  | No | Yes |  |
| FreeType | [2.11.0](https://download.savannah.gnu.org/releases/freetype) |  | No |  |  |
| GCC |  | [9.1+](https://gcc.gnu.org/) (C++17 support) |  |  |  |
| HarfBuzz-NG |  |  |  |  |  |
| libevent | [2.1.12-stable](https://github.com/libevent/libevent/releases) | 2.1.8 | No |  |  |
| libjpeg |  |  |  |  | [Yes](../depends/packages/qt.mk) |
| libpng |  |  |  |  | [Yes](../depends/packages/qt.mk) |
| librsvg | |  |  |  |  |
| MiniUPnPc | [2.2.2](https://miniupnp.tuxfamily.org/files) |  | No |  |  |
| OpenSSL | [3.5.8](https://www.openssl.org/source) (LTS to 2030-04) | 3.0 | No |  |  |
| PCRE |  |  |  |  | [Yes](../depends/packages/qt.mk) |
| Python (tests) |  | [3.10](https://www.python.org/downloads) |  |  |  |
| qrencode | [4.1.1](https://fukuchi.org/works/qrencode) |  | No |  |  |
| Qt | [5.15.14](https://download.qt.io/archive/qt/5.15/5.15.14/submodules) | 5.11.3 | No |  |  |
| XCB |  |  |  |  | [Yes](../depends/packages/qt.mk) (Linux only) |
| xkbcommon |  |  |  |  | [Yes](../depends/packages/qt.mk) (Linux only) |
| ZeroMQ | [4.3.5](https://github.com/zeromq/libzmq/releases) | 4.0.0 | No |  |  |
| zlib | [1.3.1](https://github.com/madler/zlib/releases) |  |  |  | No |

Controlling dependencies
------------------------
Some dependencies are not needed in all configurations. The following are some factors that affect the dependency list.

#### Options passed to `./configure`
* MiniUPnPc is not needed with  `--with-miniupnpc=no`.
* Berkeley DB is not needed with `--disable-wallet`.
* Qt is not needed with `--without-gui` (Guix release builds always use `--with-gui=qt5`; missing Qt is a hard error).
* If the qrencode dependency is absent, QR support won't be added. To force an error when that happens, pass `--with-qrencode`.
* ZeroMQ is needed only with the `--with-zmq` option.

#### Other
* librsvg is only needed if you need to run `make deploy` on (cross-compilation to) macOS.
