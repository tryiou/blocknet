# Guix deterministic builds — successor to Gitian

Deterministic Blocknet builds — ported from Bitcoin Core `contrib/guix`.
Runs on `ubuntu-22.04` (CI) / `ubuntu-24.04` local, produces all 6 targets via Guix cross-compilation
(`x86_64-linux-gnu`, `aarch64-linux-gnu`, `x86_64-w64-mingw32`,
`aarch64-w64-mingw32` experimental, `x86_64-apple-darwin`, `arm64-apple-darwin`).
`glibc` floor `2.27` (compatible with `18.04`+) via Guix `glibc@2.27`; Docker runtime `FROM ubuntu:22.04` (`2.35`) for wider compatibility.

**Clean Guix-only — no depends fallback.** Release determinism comes solely
from Guix. Per-OS `depends` sanity is `CI` (`.github/workflows/ci.yml`, 6-arch
matrix on modern runners), not Release. This replaces the green-first
fallback that previously did plain `depends` cross-build.

Migration:
- Port `bitcoin/contrib/guix/libexec/build.sh` + `manifest.scm` (pinned
  `channels.scm` matching `depends` versions: `boost@1.64.0` via
  `archives.boost.io`, `openssl@1.0.1k`, `qt@5.9.7`, `glibc@2.27`).
- `contrib/guix/guix-build` is the wrapper (`guix time-machine -- build …`).
  Until `manifest.scm` lands, Release job fails fast with instructions —
  it does not silently fall back to `depends`.

Legacy Gitian files archived in:
- `contrib/gitian-descriptors.legacy/`
- `contrib/gitian-build.py.legacy`

Local deterministic build (no host Guix needed):

    docker run --rm -v "$PWD":/work -w /work nixos/guix:latest \
      guix time-machine --url=https://git.savannah.gnu.org/git/guix.git --commit=160f78a4d92205df986ed9efcce7d3aac188cb24 -- build -m contrib/guix/manifest.scm
    # alternatively, using the pinned channel file:
    # guix time-machine --channels=contrib/guix/channels.scm -- build -m contrib/guix/manifest.scm

See `doc/release-process.md` (Guix section) and `.github/workflows/release.yml` `guix-build`.
