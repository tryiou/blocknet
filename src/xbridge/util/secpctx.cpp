// Copyright (c) 2018-2020 The Blocknet developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <xbridge/util/secpctx.h>

#include <logging.h>
#include <random.h>
#include <support/allocators/secure.h>

#include <cassert>
#include <mutex>
#include <vector>

secp256k1_context * Secp256k1Ctx()
{
    static secp256k1_context * ctx = [] {
        secp256k1_context * c = secp256k1_context_create(SECP256K1_CONTEXT_SIGN | SECP256K1_CONTEXT_VERIFY);
        assert(c != nullptr);

        {
            // Pass in a random blinding seed to the secp256k1 context.
            std::vector<unsigned char, secure_allocator<unsigned char>> vseed(32);
            GetRandBytes(vseed.data(), 32);
            if (!secp256k1_context_randomize(c, vseed.data()))
                LogPrintf("Secp256k1Ctx: failed to randomize context\n");
        }

        return c;
    }();
    (void)ctx;
    return ctx;
}
