Blocknet Core 4.4.1 Release Notes
==================================

See `doc/release-notes/release-notes-4.3.1.md` for the previous
release. Historical Bitcoin Core 0.x release notes (inherited from the
upstream codebase) were removed from `doc/release-notes/`; the git
history preserves them.

This release is primarily a modernization and maintainability release:
the toolchain, dependency set, CI, and release engineering were brought
up to date with no consensus or protocol changes.

Toolchain and build
--------------------

- Ported the source tree to C++17 and OpenSSL 3; unified Windows builds
  on a pinned llvm-mingw (Clang/UCRT/libc++) toolchain for x86_64 and
  aarch64.
- macOS builds moved to a pure-LLVM toolchain (clang-18 + ld64.lld,
  Xcode 26.1.1 / SDK 14.0).
- Refreshed `depends`: zlib, libevent, miniupnpc (CVE/compat), Qt
  5.15.14, X stack for Qt, toolchain autotools files, expat/fontconfig
  fixes for the Guix environment.
- Release binaries are produced exclusively by the deterministic Guix
  build (`contrib/guix/`, `--with-gui=qt5` hard-fail) across six hosts:
  x86_64-linux-gnu, aarch64-linux-gnu, x86_64-w64-mingw32,
  aarch64-w64-mingw32, x86_64-apple-darwin, arm64-apple-darwin.

CI and release engineering
---------------------------

- Canonical CI (`.github/workflows/ci.yml`): pinned Docker Guix build
  matrix with per-host artifacts, native sanity build, lint job.
- Canonical release flow (`.github/workflows/release.yml`): Guix build
  → Docker Hub image → GitHub release with artifacts and SHA256SUMS.
- Removed legacy Gitian tooling, MSVC project files, and Bitcoin
  heritage release notes from the tree.

Fixes
-----

- wallet: normalize a trailing separator in the canonical `-walletdir`
  path (boost ≥1.80 `canonical()` semantics change made
  `-walletdir=<path>/` with a trailing slash fail validation).
- test: drain validation callbacks before wallet teardown in the PoS
  test fixture (intermittent crash).
- test: fix proposal max-size boundary in governance tests.
- src: assorted missing-include fixes for libc++/C++17 strictness
  (`util/bip32.h`, `lockedpool.cpp`, explicit instantiation of xrouter
  `PushXRouterMessage`).
- configure: dropped EOL Python interpreters from the PATH probe.
