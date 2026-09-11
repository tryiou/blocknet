Known issues and deferred work
====================

This file tracks accepted limitations and deliberately deferred work so
they are visible instead of living only in code comments. Each entry
should reference the relevant source location and the reason for the
deferral.

## XRouter TLS certificate verification is disabled

`src/xrouter/utils-network.cpp` (XR client call path) currently uses
`SSL_CTX_set_verify(ssl_ctx, SSL_VERIFY_NONE, nullptr)` — certificate
verification of remote XRouter/SSL peers is disabled, and enabling
`SSL_VERIFY_PEER` is commented out in the source with a TODO.

Accepted for now because enabling it changes connection behavior for
nodes that point at peers with invalid or self-signed certificates.
Revisit with an explicit configuration flag and a deliberate default
(verify-by-default with an opt-out is the likely shape). This entry
records the upstream TODO ("xrclient cert verification") so it is not
forgotten.

## Boost::filesystem canonical() trailing-separator semantics

As of the depends refresh to Boost 1.81, `boost::filesystem::canonical()`
may preserve a trailing directory separator (`/dir/` canonicalizes to
`/dir/`, not `/dir`). The wallet init code normalizes this for
`-walletdir` (see `src/wallet/init.cpp`, `VerifyWallets`). Any new code
comparing canonical paths as strings should be aware of this behavior.
