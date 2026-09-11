macOS Build Instructions and Notes
====================================
The commands in this guide should be executed in a Terminal application.
The built-in one is located in `/Applications/Utilities/Terminal.app`.

Preparation
-----------
Install the macOS command line tools:

`xcode-select --install`

When the popup appears, click `Install`.

Then install [Homebrew](https://brew.sh).

Dependencies
----------------------

    brew install automake berkeley-db4 libtool boost miniupnpc openssl pkg-config protobuf python qt libevent qrencode

See [dependencies.md](dependencies.md) for a complete overview.

If you want to build the distributable app package with `make deploy` (.zip / optional), you need RSVG:

    brew install librsvg

Berkeley DB
-----------
It is recommended to use Berkeley DB 4.8. If you have to build it yourself,
you can use [the installation script included in contrib/](/contrib/install_db4.sh)
like so:

```shell
./contrib/install_db4.sh .
```

from the root of the repository.

**Note**: You only need Berkeley DB if the wallet is enabled (see [*Disable-wallet mode*](/doc/build-osx.md#disable-wallet-mode)).

Build Bitcoin Core
------------------------

1. Clone the Bitcoin Core source code:

        git clone https://github.com/bitcoin/bitcoin
        cd bitcoin

2.  Build Bitcoin Core:

    Configure and build the headless Bitcoin Core binaries as well as the GUI (if Qt is found).

    You can disable the GUI build by passing `--without-gui` to configure.

        ./autogen.sh
        ./configure
        make

3.  It is recommended to build and run the unit tests:

        make check

4.  You can also package the .app bundle as a .zip (optional):

        make deploy

Disable-wallet mode
--------------------
When the intention is to run only a P2P node without a wallet, Bitcoin Core may be compiled in
disable-wallet mode with:

    ./configure --disable-wallet

In this case there is no dependency on Berkeley DB 4.8.

Mining is also possible in disable-wallet mode using the `getblocktemplate` RPC call.

Running
-------

Bitcoin Core is now available at `./src/bitcoind`

Before running, you may create an empty configuration file:

    mkdir -p "/Users/${USER}/Library/Application Support/Bitcoin"

    touch "/Users/${USER}/Library/Application Support/Bitcoin/bitcoin.conf"

    chmod 600 "/Users/${USER}/Library/Application Support/Bitcoin/bitcoin.conf"

The first time you run bitcoind, it will start downloading the blockchain. This process could take many hours, or even days on slower than average systems.

You can monitor the download process by looking at the debug.log file:

    tail -f $HOME/Library/Application\ Support/Bitcoin/debug.log

Other commands:
-------

    ./src/bitcoind -daemon # Starts the bitcoin daemon.
    ./src/bitcoin-cli --help # Outputs a list of command-line options.
    ./src/bitcoin-cli help # Outputs a list of RPC commands when the daemon is running.

Notes
-----

* Tested on macOS 14+ (x86_64 and arm64). Minimum deployment target 14.0 (see `depends/hosts/darwin.mk`).

* Building with downloaded Qt binaries is not officially supported. See the notes in [#7714](https://github.com/bitcoin/bitcoin/issues/7714)

Deterministic macOS DMG Notes
-----------------------------

Working macOS DMGs are created in Linux by combining a recent clang/LLVM,
the LLD Mach-O linker (driven through a `${HOST}-ld` → `ld64.lld` PATH shim
created by the Guix build scripts, `-mlinker-version=711`) and DMG authoring
tools. See `depends/hosts/darwin.mk`:
`OSX_MIN_VERSION=14.0`, `OSX_SDK_VERSION=14.0`, `XCODE_VERSION=26.1.1`.

Apple uses clang extensively for development and has upstreamed the necessary
functionality so that a vanilla clang can take advantage. It supports the use
of -F, -target, -mmacosx-version-min, and --sysroot, which are all necessary
when building for macOS.

These tools inject timestamps by default, which produce non-deterministic
binaries. The ZERO_AR_DATE environment variable is used to disable that.

All builds must target an Apple SDK. The pinned SDK tarball is hosted by
bitcoincore.org (hash-verified on fetch — see `contrib/guix/macos-sdk.env`):

```
source contrib/guix/macos-sdk.env
mkdir -p depends/SDKs
curl --location --fail "$MACOS_SDK_URL/$MACOS_SDK_NAME.tar" -o /tmp/macos-sdk.tar
printf '%s %s\n' "$MACOS_SDK_SHA256" /tmp/macos-sdk.tar | sha256sum --check
tar -C depends/SDKs -xf /tmp/macos-sdk.tar
```

which places it at:

```
depends/SDKs/Xcode-26.1.1-17B100-extracted-SDK-with-libcxx-headers
```

Alternatively, with a Mac + developer account, download Xcode 26.1.1, extract
the SDK (see `contrib/macdeploy/README.md#sdk-extraction`) and place it there
(the exact name must match `depends/hosts/darwin.mk`, or set `SDK_PATH`).

On macOS, find your local SDK with `xcrun --show-sdk-path` and create the
tarball with:

```
  $ tar -C $(dirname $(xcrun --show-sdk-path)) -czf <sdk-name>.tar.gz <sdk-name>
```

The Guix build (`contrib/guix/guix-build`) skips darwin hosts with a warning
when no SDK is present; provide one to enable FULL macOS builds.

genisoimage is used to create the initial DMG. It is not deterministic as-is,
so it has been patched. A system genisoimage will work fine, but it will not
be deterministic because the file-order will change between invocations.
The patch can be seen here:  [theuni/osx-cross-depends](https://raw.githubusercontent.com/theuni/osx-cross-depends/master/patches/cdrtools/genisoimage.diff).
No effort was made to fix this cleanly, so it likely leaks memory badly. But
it's only used for a single invocation, so that's no real concern.

genisoimage cannot compress DMGs, so afterwards, the 'dmg' tool from the
libdmg-hfsplus project is used to compress it. There are several bugs in this
tool and its maintainer has seemingly abandoned the project. It has been forked
and is available (with fixes) here: [theuni/libdmg-hfsplus](https://github.com/theuni/libdmg-hfsplus).

The 'dmg' tool has the ability to create DMGs from scratch as well, but this
functionality is broken. Only the compression feature is currently used.
Ideally, the creation could be fixed and genisoimage would no longer be necessary.

Background images and other features can be added to DMG files by inserting a
.DS_Store before creation. This is generated by the script
contrib/macdeploy/custom_dsstore.py.

As of OS X 10.9 Mavericks, using an Apple-blessed key to sign binaries is a
requirement in order to satisfy the new Gatekeeper requirements. Because this
private key cannot be shared, we'll have to be a bit creative in order for the
build process to remain somewhat deterministic. Here's how it works:

- Builders use Gitian to create an unsigned release. This outputs an unsigned
  dmg which users may choose to bless and run. It also outputs an unsigned app
  structure in the form of a tarball, which also contains all of the tools
  that have been previously (deterministically) built in order to create a
  final dmg.
- The Apple keyholder uses this unsigned app to create a detached signature,
  using the script that is also included there. Detached signatures are available from this [repository](https://github.com/bitcoin-core/bitcoin-detached-sigs).
- Builders feed the unsigned app + detached signature back into Gitian. It
  uses the pre-built tools to recombine the pieces into a deterministic dmg.

