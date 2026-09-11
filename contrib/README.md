Repository Tools
---------------------

### [Developer tools](/contrib/devtools) ###
Specific tools for developers working on this repository.
Contains the script `github-merge.py` for merging GitHub pull requests securely and signing them using GPG.

### [Linearize](/contrib/linearize) ###
Construct a linear, no-fork, best version of the blockchain.

### [Qos](/contrib/qos) ###

A Linux bash script that will set up traffic control (tc) to limit the outgoing bandwidth for connections to the Blocknet network. This means one can have an always-on blocknetd instance running, and another local blocknetd/blocknet-qt instance which connects to this node and receives blocks from it.

### [Seeds](/contrib/seeds) ###
Utility to generate the pnSeed[] array that is compiled into the client (see `src/chainparamsseeds.h`).

### [ZMQ](/contrib/zmq) ###
Example scripts for consuming ZeroMQ notifications from a node.

Build Tools
---------------------

### [Guix](/contrib/guix) ###
The canonical deterministic build system. Builds reproducible release
binaries for all supported hosts inside a pinned Docker image
(`blocknet-guix:22.04`, Guix 1.5.0). See `contrib/guix/README.md` and
`.github/workflows/release.yml` (the release flow) / `ci.yml` (CI).

### [Containers](/contrib/containers) ###
Dockerfile used by the release flow to publish node images (see
`.github/workflows/release.yml`).

### [MacDeploy](/contrib/macdeploy) ###
Scripts and notes for macOS deployment and detached signatures (used by
`make deploy` and the Guix darwin builds).

### [WinDeploy](/contrib/windeploy) ###
Scripts for Windows detached signatures (used by the Guix mingw builds).

### [Init](/contrib/init) ###
Service scripts (systemd, OpenRC, upstart) for running a node. Note: the
units still use legacy `bitcoind` naming and need adaptation before use.

### Bash completions ###
`bitcoind.bash-completion`, `bitcoin-cli.bash-completion`,
`bitcoin-tx.bash-completion` — completion scripts (legacy bitcoin naming;
adapt to `blocknetd`/`blocknet-cli`/`blocknet-tx` before use).

### Packaging ###
The [Debian](/contrib/debian) subfolder contains the copyright file.

### [install_db4.sh](/contrib/install_db4.sh) ###
Fallback helper to build Berkeley DB 4.8 for source builds that do not use
the `depends` system (the preferred path; see `depends/README.md` and
`doc/build-unix.md`).

### [filter-lcov.py](/contrib/filter-lcov.py) ###
Used by the `make lcov*` targets to strip coverage output.

Test Tools
---------------------

### [TestGen](/contrib/testgen) ###
Utilities to generate test vectors for the data-driven Bitcoin tests.
