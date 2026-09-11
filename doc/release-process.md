Release Process
====================

Blocknet releases are produced by the deterministic Guix build, run
automatically by GitHub Actions. The canonical automation lives in
`.github/workflows/release.yml`; the build itself lives in
[`contrib/guix/`](/contrib/guix) (see `contrib/guix/README.md` for
toolchain details). Gitian is fully removed from this tree and must not
be reintroduced.

There are two supported ways to produce a release:

1. **Automated (preferred):** push a tag `v*` — the `release.yml`
   workflow builds all hosts, publishes the Docker image, and creates
   the GitHub release with artifacts and `SHA256SUMS`.
2. **Manual/verification build:** run the same Guix tooling locally to
   reproduce artifacts (or to verify determinism) before/after a tag.

---

## Pre-release checklist

Before every release candidate:

* Update release candidate version in `configure.ac` (`CLIENT_VERSION_RC`).
* Update translations if applicable (see
  [translation_process.md](translation_process.md)).

Before every minor and major release:

* Update version in `configure.ac` (set `CLIENT_VERSION_IS_RELEASE` to
  `true`, `CLIENT_VERSION_RC` to `0`).
* Write release notes (see below) and place them in
  `doc/release-notes/release-notes-<version>.md`.
* Update `src/chainparams.cpp` `nMinimumChainWork` with information from
  the `getblockchaininfo` RPC.
* Update `src/chainparams.cpp` `defaultAssumeValid` with information
  from the `getblockhash` RPC.
  - The selected value must not be orphaned so it may be useful to set
    the value two blocks back from the tip.
  - Testnet should be set some tens of thousands back from the tip due
    to reorgs there.
  - Verify with a `reindex-chainstate` with `assumevalid=0` to catch any
    defect that causes rejection of blocks in the past history.

Before every major release:

* Update hardcoded [seeds](/contrib/seeds/README.md) if the seed
  infrastructure changed.
* Update `src/chainparams.cpp` `m_assumed_blockchain_size` and
  `m_assumed_chain_state_size` with the current size plus some overhead.
* Update `src/chainparams.cpp` `chainTxData` with statistics about the
  transaction count and rate (use the `getchaintxstats` RPC).
* Check that the `contrib/guix` pins (channels, llvm-mingw, macOS SDK)
  are current; bump deliberately, never implicitly.

## Release notes

Write release notes describing user-visible changes, consensus changes,
and upgrade instructions. A good starting point:

    git shortlog --no-merges v(current version)..v(new version)

Generate the author list:

    git log --format='- %aN' v(current)..v(new) | sort -fiu

Archive the notes as `doc/release-notes/release-notes-<version>.md` and
link them from the GitHub release.

---

## Automated release (tag push)

1. Make sure the version in `configure.ac` matches the tag and that
   `CLIENT_VERSION_IS_RELEASE` is `true`.
2. Tag and push:

       git tag -s v4.4.2 -m "Blocknet Core 4.4.2"
       git push origin v4.4.2

3. GitHub Actions (`.github/workflows/release.yml`) then:
   - builds all hosts deterministically inside the pinned
     `blocknet-guix:22.04` Docker image (per-host artifacts land in
     `blocknet-binaries/<host>/`, each with a `SHA256SUMS.part`);
   - builds and pushes the multi-arch Docker image
     (`blocknetdx/<docker repo>`, tags derived from the semver);
   - creates the GitHub release with the artifacts and `SHA256SUMS`
     (prerelease flag is set automatically for `alpha`/`beta`/`rc` tags).
4. Watch the workflow run. If the Guix build fails, the fastest triage is
   to download the per-host build log artifacts.

## Manual / verification build (local Guix)

No host Guix installation is required; the tooling runs everything
inside the pinned Docker image:

    # single host
    ./contrib/guix/docker-run.sh "x86_64-linux-gnu"

    # subset or all hosts
    HOSTS="x86_64-linux-gnu aarch64-linux-gnu" ./contrib/guix/docker-run.sh
    HOSTS="x86_64-linux-gnu aarch64-linux-gnu x86_64-w64-mingw32 aarch64-w64-mingw32 x86_64-apple-darwin arm64-apple-darwin" ./contrib/guix/docker-run.sh

Artifacts land in `blocknet-binaries/<host>/`.

macOS darwin hosts require the pinned Apple SDK (non-redistributable):
source `contrib/guix/macos-sdk.env` and fetch/extract the SDK into
`depends/SDKs/` (the exact commands are in that file's header comment).
Without an SDK, darwin hosts are skipped with a warning.

Notes:

* `guix-build` aborts if `distsrc-<commit>-<host>/` build directories
  already exist for the target commit; the error message suggests
  `./contrib/guix/guix-clean`. If that script is not present in your
  checkout, remove the `distsrc-*`, `guix-build-*` directories and
  `blocknet-binaries/` manually (all are gitignored).
* The Docker wrapper sets `FORCE_DIRTY_WORKTREE=1`, so local Docker
  builds use the worktree contents rather than a `git archive` tarball —
  handy for testing a dirty tree, but remember that a real release is
  built by CI from a clean checkout of the tag.

### Expected artifacts

For version `${VERSION}` the Guix build produces:

1. source tarball (`blocknet-${VERSION}.tar.gz`)
2. linux dist tarballs (`blocknet-${VERSION}-aarch64-linux-gnu.tar.gz`,
   `blocknet-${VERSION}-x86_64-linux-gnu.tar.gz`)
3. windows unsigned installers and dist zips
   (`blocknet-${VERSION}-win64-setup-unsigned.exe`,
   `blocknet-${VERSION}-win64.zip`; host-qualified arm64 variants when
   the aarch64 mingw host is built)
4. macOS unsigned dist tarball (`blocknet-${VERSION}-osx64.tar.gz`)
   and `.dmg`/`.zip` per host
5. per-host `SHA256SUMS.part` files (aggregated into a top-level
   `SHA256SUMS` by the release workflow)

`*-debug*` files contain debug symbols for developer troubleshooting;
do not attach them to the release page.

### Determinism verification

To verify that a release is reproducible, build the same tag locally
(two runs, or on a different machine) and compare the per-host
artifacts:

    find blocknet-binaries -type f -exec sha256sum {} \; | sort -k2

Artifacts must match the `SHA256SUMS` published with the release
byte-for-byte. Any mismatch is a release-blocking bug.

---

## After the build

* Check that the GitHub release page lists every expected artifact plus
  `SHA256SUMS` and the release notes.
* Verify the Docker image tags were pushed and `blocknetd -version`
  inside the image matches the release.
* Archive the release notes to `doc/release-notes/` (if not done above).
* Announce the release through the project's usual channels
  (website, Discord, explorer/social announcements).
* Celebrate.
