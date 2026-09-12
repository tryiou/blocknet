// Copyright (c) 2018-2020 The Blocknet developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BLOCKNET_XBRIDGE_UTIL_SECPCTX_H
#define BLOCKNET_XBRIDGE_UTIL_SECPCTX_H

#include <secp256k1.h>

/**
 * Returns the process-wide secp256k1 context (SIGN|VERIFY, randomized on
 * first use). The context lives for the duration of the process; callers
 * must not destroy it. Replaces per-TU/per-instance contexts that were
 * previously duplicated in xbridgepacket, xrouterpacket and
 * xbridgecryptoproviderbtc.
 * @return shared secp256k1 context
 */
secp256k1_context * Secp256k1Ctx();

#endif // BLOCKNET_XBRIDGE_UTIL_SECPCTX_H
