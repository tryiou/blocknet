Known issues and deferred work
====================

This file tracks accepted limitations and deliberately deferred work so
they are visible instead of living only in code comments. Each entry
should reference the relevant source location and the reason for the
deferral.

## XRouter TLS certificate verification is disabled

`src/xrouter/utils-network.cpp` (XR client call path) does not verify
remote XRouter/SSL peer certificates: enabling `SSL_VERIFY_PEER` is
commented out in the source with a TODO ("xrclient cert verification").

Accepted for now because enabling it changes connection behavior for
nodes that point at peers with invalid or self-signed certificates.
Revisit with an explicit configuration flag and a deliberate default
(verify-by-default with an opt-out is the likely shape). This entry
records the upstream TODO ("xrclient cert verification") so it is not
forgotten.

## orders.dat v1 → v2 upgrade is one-way (downgrade destroys history)

`src/xbridge/xbridgetransactiondescr.h` (`CURRENT_VERSION=2`) appends
`oRedeemTries` to `orders.dat`. New binaries read v1 files (counter defaults
to `0`); old binaries (4.4.1 and earlier, v1-only) fail to load a v2 file
(checksum/deserialize error).

Mitigations on this branch: (1) one-time auto-backup `orders.dat` →
`orders.dat.pre-v2.bak` in the data directory before the first v2 overwrite
(`src/xbridge/xbridgedb.cpp`, `XBridgeDB::Write`), never overwritten once
created; (2) `saveOrders` refuses to overwrite when the existing file exists
but fails to load (`src/xbridge/xbridgeapp.cpp`, `App::saveOrders`).

Downgrade procedure: stop the node, replace `orders.dat` with
`orders.dat.pre-v2.bak`, then run the old binary. Never copy a v2 `orders.dat`
back to an old binary. The `.bak` is a best-effort pre-upgrade copy, not a
corruption recovery point.

## Boost::filesystem canonical() trailing-separator semantics

As of the depends refresh to Boost 1.81, `boost::filesystem::canonical()`
may preserve a trailing directory separator (`/dir/` canonicalizes to
`/dir/`, not `/dir`). The wallet init code normalizes this for
`-walletdir` (see `src/wallet/init.cpp`, `VerifyWallets`). Any new code
comparing canonical paths as strings should be aware of this behavior.
