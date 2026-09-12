// Copyright (c) 2018-2019 The Blocknet developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <xrouter/xrouterpacket.h>

#include <xbridge/util/secpctx.h>

#include <xrouter/xrouterlogger.h>

#include <crypto/sha256.h>
#include <secp256k1.h>
#include <random.h>

#include <iostream>

namespace xrouter
{



//******************************************************************************
//******************************************************************************
bool XRouterPacket::sign(const std::vector<unsigned char> & pubkey,
                         const std::vector<unsigned char> & privkey)
{
    if (pubkey.size() != pubkeySize || privkey.size() != privkeySize)
    {
        ERR() << "incorrect key size " << __FUNCTION__;
        return false;
    }

    memcpy(pubkeyField(), &pubkey[0], pubkeySize);
    memset(signatureField(), 0, rawSignatureSize); // unset

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

    return verify();
}

//******************************************************************************
// verify signature
//******************************************************************************
bool XRouterPacket::verify()
{
    unsigned char signature[rawSignatureSize];
    memcpy(signature, signatureField(), rawSignatureSize);
    memset(signatureField(), 0, rawSignatureSize); // unset sig before compute hash

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
        ERR() << "incorrect or unparseable signature " << __FUNCTION__;
        return false;
    }

    secp256k1_pubkey scpubkey;
    if (secp256k1_ec_pubkey_parse(Secp256k1Ctx(), &scpubkey, pubkeyField(), pubkeySize) == 0)
    {
        ERR() << "the public key could not be parsed or is invalid " << __FUNCTION__;
        return false;
    }

    if (secp256k1_ecdsa_verify(Secp256k1Ctx(), &sig, hash, &scpubkey) != 1)
    {
        ERR() << "bad signature " << __FUNCTION__;
        return false;
    }

    // correct signature, check pubkey
    unsigned char pub[pubkeySize];
    size_t len = pubkeySize;
    secp256k1_ec_pubkey_serialize(Secp256k1Ctx(), pub, &len, &scpubkey, SECP256K1_EC_COMPRESSED);

    if (len != pubkeySize)
    {
        ERR() << "incorrect pubkey length returned " << __FUNCTION__;
        return false;
    }

    if (memcmp(pub, pubkeyField(), pubkeySize) != 0)
    {
        ERR() << "signature correct, but different pubkeys " << __FUNCTION__;
        return false;
    }

    // all correct
    return true;
}

//******************************************************************************
// verify signature and pubkey
//******************************************************************************
bool XRouterPacket::verify(const std::vector<unsigned char> & pubkey)
{
    if (pubkey.size() != pubkeySize || memcmp(pubkeyField(), &pubkey[0], pubkeySize) != 0)
    {
        return false;
    }

    return verify();
}

bool XRouterPacket::copyFrom(const std::vector<unsigned char> & data)
{
    if (data.size() < headerSize)
    {
        ERR() << "received data size less than packet header size " << __FUNCTION__;
        return false;
    }

    m_body = data;

    if (sizeField() != static_cast<uint32_t>(data.size())-headerSize)
    {
        ERR() << "incorrect data size " << __FUNCTION__;
        return false;
    }

    return true;
}
} // namespace xrouter
