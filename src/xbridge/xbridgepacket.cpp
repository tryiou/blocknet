// Copyright (c) 2017-2019 The Blocknet developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

//******************************************************************************
//******************************************************************************

#include <xbridge/util/logger.h>
#include <xbridge/xbridgepacket.h>

#include <xbridge/util/secpctx.h>

#include <crypto/sha256.h>
#include <random.h>
#include <secp256k1.h>
#include <support/allocators/secure.h>

//******************************************************************************
//******************************************************************************


//******************************************************************************
//******************************************************************************
bool XBridgePacket::sign(const std::vector<unsigned char> & pubkey,
                         const std::vector<unsigned char> & privkey)
{
    if (pubkey.size() != pubkeySize || privkey.size() != privkeySize)
    {
        LOG() << "incorrect key size " << __FUNCTION__;
        return false;
    }

    memcpy(pubkeyField(), &pubkey[0], pubkeySize);
    memset(signatureField(), 0, rawSignatureSize);

    unsigned char hash[CSHA256::OUTPUT_SIZE];

    {
        CSHA256 sha256;
        sha256.Write(&m_body[0], m_body.size());
        sha256.Finalize(hash);
    }

    secp256k1_ecdsa_signature sig;
    if (secp256k1_ecdsa_sign(Secp256k1Ctx(), &sig, hash, &privkey[0], 0, 0) == 0)
    {
        return false;
    }

    secp256k1_ecdsa_signature_serialize_compact(Secp256k1Ctx(), signatureField(), &sig);

    // TODO verify signature
    return verify();
}

//******************************************************************************
// verify signature
//******************************************************************************
bool XBridgePacket::verify()
{
    unsigned char signature[rawSignatureSize];
    memcpy(signature, signatureField(), rawSignatureSize);
    memset(signatureField(), 0, rawSignatureSize);

    unsigned char hash[CSHA256::OUTPUT_SIZE];

    {
        CSHA256 sha256;
        sha256.Write(&m_body[0], m_body.size());
        sha256.Finalize(hash);
    }

    // restore signature
    memcpy(signatureField(), signature, rawSignatureSize);

    secp256k1_ecdsa_signature sig;
    if (secp256k1_ecdsa_signature_parse_compact(Secp256k1Ctx(), &sig, signatureField()) == 0)
    {
        LOG() << "incorrect or unparseable signature " << __FUNCTION__;
        return false;
    }

    secp256k1_pubkey scpubkey;
    if (secp256k1_ec_pubkey_parse(Secp256k1Ctx(), &scpubkey, pubkeyField(), pubkeySize) == 0)
    {
        LOG() << "the public key could not be parsed or is invalid " << __FUNCTION__;
        return false;
    }

    if (secp256k1_ecdsa_verify(Secp256k1Ctx(), &sig, hash, &scpubkey) != 1)
    {
        LOG() << "bad signature " << __FUNCTION__;
        return false;
    }

    // correct signature, check pubkey
    unsigned char pub[pubkeySize];
    size_t len = pubkeySize;
    secp256k1_ec_pubkey_serialize(Secp256k1Ctx(), pub, &len, &scpubkey, SECP256K1_EC_COMPRESSED);

    if (memcmp(pub, pubkeyField(), pubkeySize))
    {
        LOG() << "signature correct, but different pubkeys " << __FUNCTION__;
        return false;
    }
    if (len != pubkeySize)
    {
        LOG() << "incorrect pubkey lengtn returned " << __FUNCTION__;
        return false;
    }

    // all correct
    return true;
}

//******************************************************************************
// verify signature and pubkey
//******************************************************************************
bool XBridgePacket::verify(const std::vector<unsigned char> & pubkey)
{
    if (pubkey.size() != pubkeySize || memcmp(pubkeyField(), &pubkey[0], pubkeySize))
    {
        return false;
    }

    return verify();
}
