WINDOWS BUILD NOTES
====================

Release builds for both Windows architectures (`x86_64-w64-mingw32`,
`aarch64-w64-mingw32`) are produced by the deterministic Guix build
inside Docker, using a single pinned llvm-mingw Clang toolchain (UCRT,
libc++) — see `depends/README.md` ("Windows" section) and
`contrib/guix/docker-run.sh`. That is the only supported path for
release artifacts; only 64-bit Windows targets are built.

Manual local builds (e.g. for development/testing on Linux or WSL) can
use the instructions below.

Below are some notes on how to build Blocknet Core for Windows.

The options known to work for building Blocknet Core on Windows are:

* On Linux, using the pinned llvm-mingw toolchain via the depends
  system (the release path; see `depends/README.md`), or a distro
  mingw-w64 cross compiler tool chain for quick local builds.
* On Windows, using [Windows
  Subsystem for Linux (WSL)](https://msdn.microsoft.com/commandline/wsl/about) and a
  mingw-w64 cross compiler tool chain.

Other options which may work, but which have not been extensively tested are (please contribute instructions):

* On Windows, using a POSIX compatibility layer application such as [cygwin](http://www.cygwin.com/) or [msys2](http://www.msys2.org/).

Installing Windows Subsystem for Linux
---------------------------------------

With Windows 10, Microsoft has released a new feature named the [Windows
Subsystem for Linux (WSL)](https://msdn.microsoft.com/commandline/wsl/about). This
feature allows you to run a bash shell directly on Windows in an Ubuntu-based
environment. Within this environment you can cross compile for Windows without
the need for a separate Linux VM or server. Note that while WSL can be installed with
other Linux variants, such as OpenSUSE, the following instructions have only been
tested with Ubuntu.

Full instructions to install WSL are available on the above link.

After the bash shell is active, you can follow the instructions below.

Cross-compilation for Ubuntu and Windows Subsystem for Linux
------------------------------------------------------------

The steps below can be performed on Ubuntu (including in a VM) or WSL. The depends system
will also work on other Linux distributions, however the commands for
installing the toolchain will be different.

First, install the general dependencies:

    sudo apt update
    sudo apt upgrade
    sudo apt install build-essential libtool autotools-dev automake pkg-config bsdmainutils curl git

A host toolchain (`build-essential`) is necessary because some dependency
packages need to build host utilities that are used in the build process.

See [dependencies.md](dependencies.md) for a complete overview.

If you want to build the windows installer with `make deploy` you need [NSIS](https://nsis.sourceforge.io/Main_Page):

    sudo apt install nsis

## Building for 64-bit Windows

The first step is to install the mingw-w64 cross-compilation tool chain:

    sudo apt install g++-mingw-w64-x86-64

Once the toolchain is installed the build steps are common:

Note that for WSL the Blocknet Core source path MUST be somewhere in the default mount file system, for
example /usr/src/blocknet, AND not under /mnt/d/. If this is not the case the dependency autoconf scripts will fail.
This means you cannot use a directory that is located directly on the host Windows file system to perform the build.

Acquire the source in the usual way:

    git clone https://github.com/blocknetdx/blocknet.git

Once the source code is ready the build steps are below:

    PATH=$(echo "$PATH" | sed -e 's/:\/mnt.*//g') # strip out problematic Windows %PATH% imported var
    cd depends
    make HOST=x86_64-w64-mingw32
    cd ..
    ./autogen.sh # not required when building from tarball
    CONFIG_SITE=$PWD/depends/x86_64-w64-mingw32/share/config.site ./configure --prefix=/
    make

## Depends system

For further documentation on the depends system — including the pinned
llvm-mingw toolchain used for releases — see
[README.md](../depends/README.md) in the depends directory.

Installation
-------------

After building using the Windows subsystem it can be useful to copy the compiled
executables to a directory on the Windows drive in the same directory structure
as they appear in the release `.zip` archive. This can be done in the following
way. This will install to `c:\workspace\blocknet`, for example:

    make install DESTDIR=/mnt/c/workspace/blocknet

You can also create an installer using:

    make deploy
