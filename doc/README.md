Blocknet Core
=============

Setup
---------------------
Blocknet Core (`blocknetd`, `blocknet-qt`) is a full node for the Blocknet
network and builds the backbone of the Blocknet Protocol. It downloads and,
by default, stores the entire history of Blocknet transactions, which requires
a few gigabytes of disk space (txindex is mandatory; pruning is not
supported). Depending on the speed of your computer and network connection,
the synchronization process can take anywhere from a few hours to a day or
more.

To download Blocknet Core, visit [blocknet.org](https://blocknet.org).

Running
---------------------
The following are some helpful notes on how to run Blocknet Core on your
native platform.

### Unix

Unpack the files into a directory and run:

- `bin/blocknet-qt` (GUI) or
- `bin/blocknetd` (headless)

### Windows

Unpack the files into a directory, and then run blocknet-qt.exe.

### macOS

Drag Blocknet Core to your applications folder, and then run Blocknet Core.

### Need Help?

* See the documentation at [docs.blocknet.org](https://docs.blocknet.org)
for help and more information.
* Ask for help on the project's [Discord](https://discord.gg/mZ6pTneMx3).

Building
---------------------
The following are developer notes on how to build Blocknet Core on your native platform. They are not complete guides, but include notes on the necessary libraries, compile flags, etc.

- [Dependencies](dependencies.md)
- [macOS Build Notes](build-osx.md)
- [Unix Build Notes](build-unix.md)
- [Windows Build Notes](build-windows.md)
- [FreeBSD Build Notes](build-freebsd.md)
- [OpenBSD Build Notes](build-openbsd.md)
- [NetBSD Build Notes](build-netbsd.md)
- [Guix Deterministic Builds](/contrib/guix/README.md)

Development
---------------------
The Blocknet repo's [root README](/README.md) contains relevant information on the development process and automated testing.

- [Developer Notes](developer-notes.md)
- [Productivity Notes](productivity.md)
- [Release Notes](release-notes.md)
- [Release Process](release-process.md)
- [Known Issues](known-issues.md)
- [Translation Process](translation_process.md)
- [Translation Strings Policy](translation_strings_policy.md)
- [JSON-RPC Interface](JSON-RPC-interface.md)
- [Unauthenticated REST Interface](REST-interface.md)
- [Shared Libraries](shared-libraries.md)
- [BIPS](bips.md)
- [Dnsseed Policy](dnsseed-policy.md)
- [Benchmarking](benchmarking.md)

### Resources
* Discuss on the [Discord](https://discord.gg/mZ6pTneMx3).
* Blocknet API and protocol docs: [docs.blocknet.org](https://docs.blocknet.org).

### Miscellaneous
- [Assets Attribution](assets-attribution.md)
- [blocknet.conf Configuration File](blocknet-conf.md)
- [Files](files.md)
- [Fuzz-testing](fuzzing.md)
- [Reduce Traffic](reduce-traffic.md)
- [Tor Support](tor.md)
- [Init Scripts (systemd/upstart/openrc)](init.md)
- [ZMQ](zmq.md)
- [PSBT support](psbt.md)

License
---------------------
Distributed under the [MIT software license](/COPYING).
This product includes software developed by the OpenSSL Project for use in the [OpenSSL Toolkit](https://www.openssl.org/). This product includes
cryptographic software written by Eric Young ([eay@cryptsoft.com](mailto:eay@cryptsoft.com)), and UPnP software written by Thomas Bernard.
