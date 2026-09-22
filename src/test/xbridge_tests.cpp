// Copyright (c) 2011-2018 The Bitcoin Core developers
// Copyright (c) 2020 The Blocknet developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <test/test_bitcoin.h>
#include <coinvalidator.h>
#include <primitives/transaction.h>
#include <rpc/protocol.h>
#include <rpc/server.h>
#include <script/script.h>
#include <script/standard.h>
#include <serialize.h>
#include <streams.h>
#include <uint256.h>
#include <util/moneystr.h>
#include <util/system.h>
#include <xbridge/currencypair.h>
#include <xbridge/xbridgeapp.h>
#include <xbridge/xbridgedb.h>
#include <xbridge/util/xutil.h>
#include <boost/test/unit_test.hpp>

#include <fstream>
#include <iomanip>
#include <sstream>

// Declared in rpcxbridge.cpp (same extern precedent as xseries.cpp).
extern CurrencyPair TxOutToCurrencyPair(const std::vector<CTxOut> & vout, std::string& snode_pubkey);
extern UniValue dxGetOrders(const JSONRPCRequest& request); // declared in rpcxbridge.cpp

BOOST_FIXTURE_TEST_SUITE(xbridge_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(xbridge_partialorderdriftcheck) {
    { // full order should pass drift check
        CAmount makerSource{11220000}, makerDest{5000000}, takerSource{5000000}, takerDest{11220000};
        BOOST_CHECK_MESSAGE(xbridge::xBridgePartialOrderDriftCheck(makerSource, makerDest, takerSource, takerDest), "exact order should pass drift check");
        makerSource = 5000000, makerDest = 11220000, takerSource = 11220000, takerDest = 5000000;
        BOOST_CHECK_MESSAGE(xbridge::xBridgePartialOrderDriftCheck(makerSource, makerDest, takerSource, takerDest), "exact order should pass drift check (inverse)");
    }
    { // maker source > maker dest drift should pass
        CAmount makerSource{11220000}, makerDest{100}, takerSource{62}, takerDest{7000000};
        BOOST_CHECK_MESSAGE(xbridge::xBridgePartialOrderDriftCheck(makerSource, makerDest, takerSource, takerDest), "maker source > maker dest should pass");
    }
    { // maker source < maker dest drift should pass
        CAmount makerSource{100}, makerDest{11220000}, takerSource{7000000}, takerDest{62};
        BOOST_CHECK_MESSAGE(xbridge::xBridgePartialOrderDriftCheck(makerSource, makerDest, takerSource, takerDest), "maker source < maker dest should pass");
    }
    { // bad taker price (source) should fail upper limit
        CAmount makerSource{11220000}, makerDest{100}, takerSource{63}, takerDest{7000000};
        BOOST_CHECK_MESSAGE(!xbridge::xBridgePartialOrderDriftCheck(makerSource, makerDest, takerSource, takerDest), "bad taker price (source) should fail upper limit");
        makerSource = 100, makerDest = 11220000, takerSource = 7000000, takerDest = 63;
        BOOST_CHECK_MESSAGE(!xbridge::xBridgePartialOrderDriftCheck(makerSource, makerDest, takerSource, takerDest), "bad taker price (source) should fail upper limit (inverse)");
    }
    { // bad taker price (source) should fail lower limit
        CAmount makerSource{11220000}, makerDest{100}, takerSource{61}, takerDest{7000000};
        BOOST_CHECK_MESSAGE(!xbridge::xBridgePartialOrderDriftCheck(makerSource, makerDest, takerSource, takerDest), "bad taker price (source) should fail lower limit");
        makerSource = 100, makerDest = 11220000, takerSource = 7000000, takerDest = 61;
        BOOST_CHECK_MESSAGE(!xbridge::xBridgePartialOrderDriftCheck(makerSource, makerDest, takerSource, takerDest), "bad taker price (source) should fail lower limit (inverse)");
    }
    { // bad taker price (dest) should fail upper limit
        CAmount makerSource{11220000}, makerDest{100}, takerSource{62}, takerDest{7068600+1};
        BOOST_CHECK_MESSAGE(!xbridge::xBridgePartialOrderDriftCheck(makerSource, makerDest, takerSource, takerDest), "bad taker price (dest) should fail upper limit");
        makerSource = 100, makerDest = 11220000, takerSource = 7068600+1, takerDest = 62;
        BOOST_CHECK_MESSAGE(!xbridge::xBridgePartialOrderDriftCheck(makerSource, makerDest, takerSource, takerDest), "bad taker price (dest) should fail upper limit (inverse)");
    }
    { // bad taker price (dest) should fail lower limit
        CAmount makerSource{11220000}, makerDest{100}, takerSource{62}, takerDest{6844200};
        BOOST_CHECK_MESSAGE(!xbridge::xBridgePartialOrderDriftCheck(makerSource, makerDest, takerSource, takerDest), "bad taker price (dest) should fail lower limit");
        makerSource = 100, makerDest = 11220000, takerSource = 6844200, takerDest = 62;
        BOOST_CHECK_MESSAGE(!xbridge::xBridgePartialOrderDriftCheck(makerSource, makerDest, takerSource, takerDest), "bad taker price (dest) should fail lower limit (inverse)");
    }
    { // taker price should pass upper limit
        CAmount makerSource{11220000}, makerDest{100}, takerSource{62}, takerDest{7068600};
        BOOST_CHECK_MESSAGE(xbridge::xBridgePartialOrderDriftCheck(makerSource, makerDest, takerSource, takerDest), "taker price should pass upper limit");
        makerSource = 100, makerDest = 11220000, takerSource = 7068600, takerDest = 62;
        BOOST_CHECK_MESSAGE(xbridge::xBridgePartialOrderDriftCheck(makerSource, makerDest, takerSource, takerDest), "taker price should pass upper limit (inverse)");
    }
    { // taker price should fail lower limit
        CAmount makerSource{11220000}, makerDest{100}, takerSource{62}, takerDest{6844200};
        BOOST_CHECK_MESSAGE(!xbridge::xBridgePartialOrderDriftCheck(makerSource, makerDest, takerSource, takerDest), "taker price should fail lower limit");
        makerSource = 100, makerDest = 11220000, takerSource = 6844200, takerDest = 62;
        BOOST_CHECK_MESSAGE(!xbridge::xBridgePartialOrderDriftCheck(makerSource, makerDest, takerSource, takerDest), "taker price should fail lower limit (inverse)");
    }
    { // taker price should pass divisible limits
        CAmount makerSource{100000}, makerDest{1000}, takerSource{1}, takerDest{100};
        BOOST_CHECK_MESSAGE(xbridge::xBridgePartialOrderDriftCheck(makerSource, makerDest, takerSource, takerDest), "taker price should pass divisible limits");
        makerSource = 1000, makerDest = 100000, takerSource = 100, takerDest = 1;
        BOOST_CHECK_MESSAGE(xbridge::xBridgePartialOrderDriftCheck(makerSource, makerDest, takerSource, takerDest), "taker price should pass divisible limits (inverse)");
    }
    { // taker price should fail divisible limits
        CAmount makerSource{100000}, makerDest{1000}, takerSource{10}, takerDest{10000};
        BOOST_CHECK_MESSAGE(!xbridge::xBridgePartialOrderDriftCheck(makerSource, makerDest, takerSource, takerDest), "taker price should fail divisible limits");
        makerSource = 1000, makerDest = 100000, takerSource = 10000, takerDest = 10;
        BOOST_CHECK_MESSAGE(!xbridge::xBridgePartialOrderDriftCheck(makerSource, makerDest, takerSource, takerDest), "taker price should fail divisible limits (inverse)");
    }
    { // taker price should fail on bad taker size
        CAmount makerSource{100000}, makerDest{1000}, takerSource{100}, takerDest{990};
        BOOST_CHECK_MESSAGE(!xbridge::xBridgePartialOrderDriftCheck(makerSource, makerDest, takerSource, takerDest), "taker price should fail on bad taker size");
        makerSource = 1000, makerDest = 100000, takerSource = 990, takerDest = 100;
        BOOST_CHECK_MESSAGE(!xbridge::xBridgePartialOrderDriftCheck(makerSource, makerDest, takerSource, takerDest), "taker price should fail on bad taker size (inverse)");
    }
    { // taker price should fail on bad taker size
        CAmount makerSource{100000}, makerDest{1000}, takerSource{10}, takerDest{990};
        BOOST_CHECK_MESSAGE(!xbridge::xBridgePartialOrderDriftCheck(makerSource, makerDest, takerSource, takerDest), "taker price should fail on bad taker size");
        makerSource = 1000, makerDest = 100000, takerSource = 990, takerDest = 10;
        BOOST_CHECK_MESSAGE(!xbridge::xBridgePartialOrderDriftCheck(makerSource, makerDest, takerSource, takerDest), "taker price should fail on bad taker size (inverse)");
    }
}

BOOST_AUTO_TEST_CASE(xbridge_pricecheck) {
    {   // exact price conversions should pass
        BOOST_CHECK_EQUAL(xbridge::xBridgeDestAmountFromPrice      (10000,        10000,      100000),    100000);
        BOOST_CHECK_EQUAL(xbridge::xBridgeSourceAmountFromPrice    (100000,       10000,      100000),    10000);
        BOOST_CHECK_EQUAL(xbridge::xBridgeDestAmountFromPrice      (1,            1,          10),        10);
        BOOST_CHECK_EQUAL(xbridge::xBridgeSourceAmountFromPrice    (10,           1,          10),        1);
        BOOST_CHECK_EQUAL(xbridge::xBridgeDestAmountFromPrice      (62,           62,         100),       100);
        BOOST_CHECK_EQUAL(xbridge::xBridgeSourceAmountFromPrice    (100,          62,         100),       62);
        BOOST_CHECK_EQUAL(xbridge::xBridgeDestAmountFromPrice      (11220000,     11220000,   62),        62);
        BOOST_CHECK_EQUAL(xbridge::xBridgeSourceAmountFromPrice    (62,           11220000,   62),        11220000);
        BOOST_CHECK_EQUAL(xbridge::xBridgeDestAmountFromPrice      (99,           99,         199),       199);
        BOOST_CHECK_EQUAL(xbridge::xBridgeSourceAmountFromPrice    (199,          99,         199),       99);
        BOOST_CHECK_EQUAL(xbridge::xBridgeDestAmountFromPrice      (10999,        10999,      20999),     20999);
        BOOST_CHECK_EQUAL(xbridge::xBridgeSourceAmountFromPrice    (20999,        10999,      20999),     10999);
    }
    {   // partial price conversions should pass
        BOOST_CHECK_EQUAL(xbridge::xBridgeDestAmountFromPrice      (892,          10000,      100000),    8920);
        BOOST_CHECK_EQUAL(xbridge::xBridgeSourceAmountFromPrice    (8920,         10000,      100000),    892);
        BOOST_CHECK_EQUAL(xbridge::xBridgeDestAmountFromPrice      (1,            5,          10),        2);
        BOOST_CHECK_EQUAL(xbridge::xBridgeSourceAmountFromPrice    (2,            5,          10),        1);
        BOOST_CHECK_EQUAL(xbridge::xBridgeDestAmountFromPrice      (99,           1,          10),        990);
        BOOST_CHECK_EQUAL(xbridge::xBridgeSourceAmountFromPrice    (990,          1,          10),        99);
        BOOST_CHECK_EQUAL(xbridge::xBridgeDestAmountFromPrice      (34567,        12345678,   9999),      27);
        BOOST_CHECK_EQUAL(xbridge::xBridgeSourceAmountFromPrice    (27,           12345678,   9999),      33336);
        BOOST_CHECK_EQUAL(xbridge::xBridgeDestAmountFromPrice      (99999999999,  99,         9),         9090909090);
        BOOST_CHECK_EQUAL(xbridge::xBridgeSourceAmountFromPrice    (9090909090,   99,         9),         99999999990);
        BOOST_CHECK_EQUAL(xbridge::xBridgeDestAmountFromPrice      (999,          999,        999),       999);
        BOOST_CHECK_EQUAL(xbridge::xBridgeSourceAmountFromPrice    (1111,         1111,       1111),      1111);
    }
    {   // bad price conversions should fail (changes only 2nd column price, all other values same as previous test set)
        BOOST_CHECK(xbridge::xBridgeDestAmountFromPrice            (892,          100000,     100000)  != 8920);
        BOOST_CHECK(xbridge::xBridgeSourceAmountFromPrice          (8920,         100000,     100000)  != 892);
        BOOST_CHECK(xbridge::xBridgeDestAmountFromPrice            (1,            20,          10)     != 2);
        BOOST_CHECK(xbridge::xBridgeSourceAmountFromPrice          (2,            20,          10)     != 1);
        BOOST_CHECK(xbridge::xBridgeDestAmountFromPrice            (99,           2,          10)      != 990);
        BOOST_CHECK(xbridge::xBridgeSourceAmountFromPrice          (990,          2,          10)      != 99);
        BOOST_CHECK(xbridge::xBridgeDestAmountFromPrice            (34567,        123456789,  9999)    != 27);
        BOOST_CHECK(xbridge::xBridgeSourceAmountFromPrice          (27,           123456789,  9999)    != 33336);
        BOOST_CHECK(xbridge::xBridgeDestAmountFromPrice            (99999999999,  990,        9)       != 9090909090);
        BOOST_CHECK(xbridge::xBridgeSourceAmountFromPrice          (9090909090,   990,        9)       != 99999999990);
        BOOST_CHECK(xbridge::xBridgeDestAmountFromPrice            (999,          9992,       999)     != 999);
        BOOST_CHECK(xbridge::xBridgeSourceAmountFromPrice          (1111,         11112,      1111)    != 1111);
    }
}

/**
 * Pin the on-chain order-info JSON serialization (embedded in OP_RETURN by
 * xbridge::App when preparing the service node fee tx). UniValue output must
 * stay parseable and semantically identical to the pre-migration json_spirit
 * output: a 5-element array [id, fromCurrency, fromAmount, toCurrency,
 * toAmount] with integer amounts serialized as JSON numbers.
 */
BOOST_AUTO_TEST_CASE(xbridge_orderinfo_json_serialization) {
    UniValue info(UniValue::VARR);
    info.push_back("91d0ea83edc79b9a2041c51d08037cff87c181efb311a095dfdd4edbcc7993a9");
    info.push_back("BLOCK");
    info.push_back(static_cast<int64_t>(11220000));
    info.push_back("LTC");
    info.push_back(static_cast<int64_t>(5000000));
    const std::string str = info.write();
    BOOST_CHECK_EQUAL(str,
        "[\"91d0ea83edc79b9a2041c51d08037cff87c181efb311a095dfdd4edbcc7993a9\",\"BLOCK\",11220000,\"LTC\",5000000]");

    // round-trip through the parser used by TxOutToCurrencyPair
    UniValue back;
    BOOST_CHECK(back.read(str));
    BOOST_CHECK(back.isArray());
    BOOST_CHECK_EQUAL(back.size(), 5);
    BOOST_CHECK_EQUAL(back[0].get_str(), "91d0ea83edc79b9a2041c51d08037cff87c181efb311a095dfdd4edbcc7993a9");
    BOOST_CHECK_EQUAL(back[1].get_str(), "BLOCK");
    BOOST_CHECK_EQUAL(back[2].get_int64(), int64_t{11220000});
    BOOST_CHECK_EQUAL(back[3].get_str(), "LTC");
    BOOST_CHECK_EQUAL(back[4].get_int64(), int64_t{5000000});
}

// Replicates the live mainnet crNoMoney cancel of a fundable exact-fit order
// (0.0098 BLOCK order funded by a single 0.01000000 utxo): the deposit
// selector accepts the utxo (its requirement side is unpadded), while the
// session funding check pads the requirement with +1e-8
// (xBridgeValueFromAmount "round up 1 sat"), so the exact fit is deterministically
// rejected and the order cancelled crNoMoney. The contract under test: a utxo
// set returned by selectUtxos must satisfy the session funding check when the
// integer ledger ties (utxoAmount >= orderAmount + fee1 + fee2 in descr units).
BOOST_AUTO_TEST_CASE(xbridge_funds_sufficient_exactfit) {
    // Deterministic connector fees, mirroring MinTxFee=10000 / FeePerByte=20:
    // (192*in + 34*out) * feePerByte, floored at 10000 sats, wallet COIN 1e8.
    auto minTxFee = [](const uint32_t in, const uint32_t out) -> double {
        uint64_t fee = (192 * in + 34 * out) * 20;
        if (fee < 10000)
            fee = 10000;
        return static_cast<double>(fee) / 100000000.0;
    };

    xbridge::wallet::UtxoEntry utxo;
    utxo.txId = "e070c50c5b09407a87af23e53e681b7a37bae3519819726b6fb62b90957e574f";
    utxo.vout = 0;
    utxo.amount = 0.01; // exactly 1,000,000 sats
    utxo.address = "";

    std::vector<xbridge::wallet::UtxoEntry> forUse;
    uint64_t utxoAmount{0}, fee1{0}, fee2{0};

    // Selector accepts the exact-fit utxo (requirement side unpadded).
    BOOST_CHECK(xbridge::App::instance().selectUtxos("", {utxo}, minTxFee, minTxFee,
                 9800, xbridge::TransactionDescr::COIN, forUse, utxoAmount, fee1, fee2));
    BOOST_CHECK_EQUAL(forUse.size(), 1);
    BOOST_CHECK_EQUAL(utxoAmount, uint64_t{10000}); // 0.01 * 1e6 descr
    BOOST_CHECK_EQUAL(fee1, uint64_t{100});
    BOOST_CHECK_EQUAL(fee2, uint64_t{100});

    // The session funding check (xbridgesession.cpp processTransactionCreateA)
    // must accept the very selection the selector returned: the integer
    // ledger ties exactly (10000 = 9800 + 100 + 100). Accumulated the way the
    // session does (running-double total converted per step).
    const CAmount requirement = 9800 + static_cast<CAmount>(fee1) + static_cast<CAmount>(fee2);
    double inAmount = 0;
    CAmount cinAmount = 0;
    for (const auto & u : forUse) {
        inAmount += u.amount;
        cinAmount = xbridge::xBridgeIntFromReal(inAmount);
    }
    BOOST_CHECK_MESSAGE(xbridge::xBridgeFundsSufficient(cinAmount, requirement),
                        "exact-fit utxo selected by selectUtxos must pass the session funding check");

    // Overcorrection guards: a genuinely short selection must still fail,
    // down to a single descr unit (9999 vs 10000).
    BOOST_CHECK(!xbridge::xBridgeFundsSufficient(xbridge::xBridgeIntFromReal(0.0098), requirement));
    BOOST_CHECK(!xbridge::xBridgeFundsSufficient(CAmount{9999}, CAmount{10000}));
    BOOST_CHECK(xbridge::xBridgeFundsSufficient(CAmount{10000}, CAmount{10000}));
    // A comfortable margin must pass.
    BOOST_CHECK(xbridge::xBridgeFundsSufficient(xbridge::xBridgeIntFromReal(0.02), requirement));
}

// orders.dat upgrade compat: the v2 counter is appended at the end of the
// serialization stream. A v2 round-trip preserves it; a version 1 blob
// (simulated by stripping the trailing uint32) must still load with the
// counter defaulted to 0 and earlier fields intact.
BOOST_AUTO_TEST_CASE(xbridge_redeem_tries_ser_compat) {
    xbridge::TransactionDescr d;
    d.fromAmount = 9800;
    d.tryRedeem();
    d.tryRedeem();
    CDataStream ss(SER_DISK, 0);
    ss << d;
    // snapshot before extraction: a fully consumed CDataStream clears its
    // buffer, so the version 1 prefix must be cut from the raw bytes first.
    std::vector<unsigned char> raw(ss.begin(), ss.end());
    xbridge::TransactionDescr d2;
    ss >> d2;
    BOOST_CHECK_EQUAL(d2.fromAmount, uint64_t{9800});
    BOOST_CHECK_EQUAL(d2.redeemTries(), 2u);
    BOOST_CHECK_EQUAL(d2.maxRedeemTries(), 2u);
    std::vector<unsigned char> v1(raw.begin(), raw.end() - 4);
    // a true version 1 blob carries nVersion==1 in its first four bytes
    // (little-endian int); patch the version, not just the length.
    v1[0] = 1; v1[1] = 0; v1[2] = 0; v1[3] = 0;
    CDataStream s1(v1, SER_DISK, 0);
    xbridge::TransactionDescr d1;
    BOOST_CHECK_NO_THROW(s1 >> d1);
    BOOST_CHECK_EQUAL(d1.fromAmount, uint64_t{9800});
    BOOST_CHECK_EQUAL(d1.redeemTries(), 0u);
}

// Documents the session utxo-consumption sequence
// (xbridgesession.cpp processTransactionCreateA loop): fee1 recomputed per
// consumed utxo as minTxFee1(usedCount,3), fee2 fixed minTxFee2(1,1). This is
// a formula reference, NOT gate coverage: the gate itself lives inline in the
// packet handler and is covered by xbridge_funds_sufficient_exactfit (live
// predicate), xbridge_predicate_sequence below (live predicate, multi-step),
// and the live swap matrix. If this replica and the session ever disagree on
// fee math, the independently recomputed expectations below fail first.
struct ConsumeReplicaResult { std::vector<double> used; CAmount requirement; };
static ConsumeReplicaResult consumeReplica(const std::vector<double> & coins,
                                           const uint64_t feePerByte)
{
    const CAmount coutAmount = 9800;
    uint64_t s2 = (192 * 1 + 34 * 1) * feePerByte;
    if (s2 < 10000)
        s2 = 10000;
    const CAmount cfee2 = static_cast<CAmount>(s2 / 100); // floored sats -> descr
    double inAmount = 0;
    CAmount cinAmount = 0, cfee1 = 0, req = 0;
    std::vector<double> used;
    for (const double c : coins) {
        if (!used.empty() && cinAmount >= req)
            break; // non-partial: done
        used.push_back(c);
        inAmount += c;
        cinAmount = xbridge::xBridgeIntFromReal(inAmount);
        uint64_t s = (192 * used.size() + 34 * 3) * feePerByte;
        if (s < 10000)
            s = 10000;
        cfee1 = static_cast<CAmount>(s / 100);
        req = coutAmount + cfee1 + cfee2;
    }
    return {used, req};
}

BOOST_AUTO_TEST_CASE(xbridge_consume_gate_exactfit) {
    // Spare utxo available: the integer gate (now live in
    // processTransactionCreateA) exits on the exact tie after the first utxo
    // instead of over-consuming. (The former padded-double gate consumed both;
    // that failure was captured red before the rewire.)
    ConsumeReplicaResult integer = consumeReplica({0.01, 0.005}, 20);
    BOOST_CHECK_MESSAGE(integer.used.size() == 1,
        "integer loop gate must stop on exact tie, used=" << integer.used.size());
    BOOST_CHECK_EQUAL(integer.requirement, CAmount{10000});
    // Fee-growth lock-in: with feePerByte=100 the requirement grows per input
    // (step1: 9800+294+226=10320, step2: 9800+486+226=10512, independently
    // recomputed); two 0.0053 utxos (10600 descr) must both be consumed and pass.
    ConsumeReplicaResult growth = consumeReplica({0.0053, 0.0053}, 100);
    BOOST_CHECK_EQUAL(growth.used.size(), 2);
    BOOST_CHECK_EQUAL(growth.requirement, CAmount{10512});
    BOOST_CHECK(growth.used.size() == 2 &&
                xbridge::xBridgeIntFromReal(0.0053 + 0.0053) >= growth.requirement);
}

// Drives the LIVE funding predicate through a multi-step accumulation with
// independently recomputed fees (feePerByte=100, floor 10000 sats): after
// each consumed utxo the session's requirement is cout+cfee1(N)+cfee2 and the
// gate must agree step by step (fail, fail, pass). Guards production drift
// that a formula replica cannot catch.
BOOST_AUTO_TEST_CASE(xbridge_predicate_sequence) {
    const CAmount cout = 9800;
    const CAmount cfee2 = 226; // (192+34)*100=22600 sats -> 226 descr, no floor
    // step 1: one 0.0053 utxo; cfee1(1)=(192+102)*100=29400 -> 294
    CAmount req1 = cout + 294 + cfee2; // 10320
    BOOST_CHECK_EQUAL(req1, CAmount{10320});
    BOOST_CHECK(!xbridge::xBridgeFundsSufficient(CAmount{5300}, req1));
    // step 2: second 0.0053 utxo; cfee1(2)=(384+102)*100=48600 -> 486
    CAmount req2 = cout + 486 + cfee2; // 10512
    BOOST_CHECK_EQUAL(req2, CAmount{10512});
    BOOST_CHECK(!xbridge::xBridgeFundsSufficient(CAmount{5300}, req2));
    BOOST_CHECK(xbridge::xBridgeFundsSufficient(CAmount{10600}, req2));
}

// Redeem-retry policy (xbridgesession.cpp processTransactionConfirmA payTx
// block): transient wallet/propagation failures re-drive bounded by tries,
// pre-broadcast permanent failures cancel (nothing broadcast yet, matches the
// long-standing crBadBDepositTx abort path), and post-broadcast-uncertain
// failures expire to the watch loop (cancel unsafe: pay tx may be out).
BOOST_AUTO_TEST_CASE(xbridge_redeem_retry_policy) {
    using R = xbridge::RedeemRetryClass;
    // Transient: missing inputs / wallet not connected -> retry while budget remains.
    BOOST_CHECK(xbridge::xBridgeRedeemRetryClass(RPCErrorCode::RPC_VERIFY_ERROR, 0, 2, false) == R::Retry);
    BOOST_CHECK(xbridge::xBridgeRedeemRetryClass(RPCErrorCode::RPC_VERIFY_ERROR, 1, 2, false) == R::Retry);
    BOOST_CHECK(xbridge::xBridgeRedeemRetryClass(RPCErrorCode::RPC_CLIENT_NOT_CONNECTED, 0, 2, false) == R::Retry);
    // Budget exhausted or watching done -> expire, never loop forever.
    BOOST_CHECK(xbridge::xBridgeRedeemRetryClass(RPCErrorCode::RPC_VERIFY_ERROR, 2, 2, false) == R::Expire);
    BOOST_CHECK(xbridge::xBridgeRedeemRetryClass(RPCErrorCode::RPC_VERIFY_ERROR, 0, 2, true) == R::Expire);
    BOOST_CHECK(xbridge::xBridgeRedeemRetryClass(RPCErrorCode::RPC_CLIENT_NOT_CONNECTED, 2, 2, false) == R::Expire);
    // Pre-broadcast permanent (bad secret: counterparty misbehaving; callee
    // sets errCode 0 only for this case) -> cancel abort path. Local build
    // failures map to a transient code (bounded Retry, then Expire) so a
    // wallet outage never fast-cancels a funded swap.
    BOOST_CHECK(xbridge::xBridgeRedeemRetryClass(0, 0, 2, false) == R::CancelOrder);
    BOOST_CHECK(xbridge::xBridgeRedeemRetryClass(0, 2, 2, true) == R::CancelOrder);
    // Send-stage uncertain (non-verify errors, pay tx possibly broadcast) ->
    // expire to watch loop; cancel would risk stranding funds.
    BOOST_CHECK(xbridge::xBridgeRedeemRetryClass(-26, 0, 2, false) == R::Expire);
    BOOST_CHECK(xbridge::xBridgeRedeemRetryClass(RPCErrorCode::RPC_MISC_ERROR, 0, 2, false) == R::Expire);
    // Malformed send replies (-1) and unset codes (sentinel) must expire in
    // every watch state: never retry-loop a possibly-broadcast pay tx, and
    // never cancel on a code nobody assigned.
    BOOST_CHECK(xbridge::xBridgeRedeemRetryClass(-1, 0, 2, false) == R::Expire);
    BOOST_CHECK(xbridge::xBridgeRedeemRetryClass(-1, 0, 2, true) == R::Expire);
    BOOST_CHECK(xbridge::xBridgeRedeemRetryClass(-1, 2, 2, false) == R::Expire);
    BOOST_CHECK(xbridge::xBridgeRedeemRetryClass(xbridge::REDEEM_ERR_UNSET, 0, 2, false) == R::Expire);
    BOOST_CHECK(xbridge::xBridgeRedeemRetryClass(xbridge::REDEEM_ERR_UNSET, 0, 2, true) == R::Expire);
}

// Exact amount strings for the wallet RPC (replaces double serialization):
// descr units (TransactionDescr::COIN=1e6) formatted as exact decimals with
// six fractional digits. By construction never more than six decimals, so the
// wallet's ParseFixedPoint(8) always accepts them.
BOOST_AUTO_TEST_CASE(xbridge_amount_string_exact) {
    BOOST_CHECK_EQUAL(xbridge::xBridgeAmountToString(CAmount{9900}), "0.009900");
    BOOST_CHECK_EQUAL(xbridge::xBridgeAmountToString(CAmount{100}), "0.000100");
    BOOST_CHECK_EQUAL(xbridge::xBridgeAmountToString(CAmount{9800}), "0.009800");
    BOOST_CHECK_EQUAL(xbridge::xBridgeAmountToString(CAmount{10000}), "0.010000");
    BOOST_CHECK_EQUAL(xbridge::xBridgeAmountToString(CAmount{1}), "0.000001");
    BOOST_CHECK_EQUAL(xbridge::xBridgeAmountToString(CAmount{0}), "0.000000");
    BOOST_CHECK_EQUAL(xbridge::xBridgeAmountToString(CAmount{123456789}), "123.456789");
}

BOOST_AUTO_TEST_CASE(xbridge_amount_string_wallet_parseable) {
    // 1 descr unit = 100 wallet sats (1e6 descr per coin, 1e8 sats per coin).
    struct Case { CAmount descr; CAmount sats; };
    const std::vector<Case> cases = {{9900, 990000}, {100, 10000}, {9800, 980000},
                                     {10000, 1000000}, {1, 100}, {0, 0}};
    for (const auto & c : cases) {
        CAmount sats = -1;
        BOOST_REQUIRE_MESSAGE(ParseFixedPoint(xbridge::xBridgeAmountToString(c.descr), 8, &sats),
                              "descr " << c.descr << " must wallet-parse");
        BOOST_CHECK_EQUAL(sats, c.sats);
    }
}

// Live S10 failure kept as a permanent guard: the former double path feeds
// outAmount+fee2 (padded doubles) through UniValue setprecision(16), which
// emits an 18-decimal string the wallet rejects with code -3 Invalid amount.
BOOST_AUTO_TEST_CASE(xbridge_deposit_double_serialization_rejected) {
    const double deposit = xbridge::xBridgeValueFromAmount(9800) + xbridge::xBridgeValueFromAmount(100);
    std::ostringstream oss;
    oss << std::setprecision(16) << deposit; // mirrors UniValue::setFloat
    CAmount amt = 0;
    BOOST_CHECK_MESSAGE(!ParseFixedPoint(oss.str(), 8, &amt),
                        "old double path must stay unparsable, serialized=" << oss.str());
}

// Integer descr -> wallet sats conversion (refund/payment/prep outputs):
// pure integer math, exact for all magnitudes below 2^53 base units.
BOOST_AUTO_TEST_CASE(xbridge_descr_to_sats) {
    // 1 descr unit = 100 wallet sats (1e6 descr per coin, 1e8 sats per coin).
    BOOST_CHECK_EQUAL(xbridge::xBridgeDescrToSats(CAmount{9800}, 100000000), CAmount{980000});
    BOOST_CHECK_EQUAL(xbridge::xBridgeDescrToSats(CAmount{100}, 100000000), CAmount{10000});
    BOOST_CHECK_EQUAL(xbridge::xBridgeDescrToSats(CAmount{1}, 100000000), CAmount{100});
    BOOST_CHECK_EQUAL(xbridge::xBridgeDescrToSats(CAmount{0}, 100000000), CAmount{0});
    BOOST_CHECK_EQUAL(xbridge::xBridgeDescrToSats(CAmount{123456789}, 100000000), CAmount{12345678900});
    // Heterogeneous wallet COIN (e.g. 1e6 base units per coin): identity.
    BOOST_CHECK_EQUAL(xbridge::xBridgeDescrToSats(CAmount{9800}, 1000000), CAmount{9800});
    BOOST_CHECK_EQUAL(xbridge::xBridgeDescrToSats(CAmount{1}, 1000000), CAmount{1});
}

// Wallet coin-double -> integer sats uses nearest rounding (not truncation):
// 0.0098 * 1e8 truncates to 979999 as double, the exact value is 980000.
BOOST_AUTO_TEST_CASE(xbridge_wallet_sats_from_real) {
    BOOST_CHECK_EQUAL(xbridge::xBridgeWalletSatsFromReal(0.01, 100000000), CAmount{1000000});
    BOOST_CHECK_EQUAL(xbridge::xBridgeWalletSatsFromReal(0.0098, 100000000), CAmount{980000});
    BOOST_CHECK_EQUAL(xbridge::xBridgeWalletSatsFromReal(0.0001, 100000000), CAmount{10000});
    BOOST_CHECK_EQUAL(xbridge::xBridgeWalletSatsFromReal(0.0, 100000000), CAmount{0});
    // Fee-scale values round-trip exactly (minTxFee ratio math is integer
    // sats over COIN, magnitudes far below 2^53).
    BOOST_CHECK_EQUAL(xbridge::xBridgeWalletSatsFromReal(10000.0 / 100000000.0, 100000000), CAmount{10000});
}

// Payment output rule (mirrors redeemOrderCounterpartyDeposit): exact
// on-chain P2SH sats less the integer redeem fee, paying excess only when
// the deposit strictly covers amount + fee (checkDepositTransaction rule).
// Drives the live shared helper (xBridgeExcessSats), so builder and
// verifier cannot drift apart undetected.
BOOST_AUTO_TEST_CASE(xbridge_payment_sats_rule) {
    const CAmount toSats{980000}, fee2sats{10000};
    // Exact deposit: no excess, output is the order amount.
    BOOST_CHECK_EQUAL(xbridge::xBridgeExcessSats(990000, toSats, fee2sats), CAmount{0});
    // Overpaid deposit: excess paid to redeemer.
    BOOST_CHECK_EQUAL(xbridge::xBridgeExcessSats(995000, toSats, fee2sats), CAmount{5000});
    // Marginally short deposit (fee tolerance band): output stays the order
    // amount, never reduced by truncation.
    BOOST_CHECK_EQUAL(xbridge::xBridgeExcessSats(989999, toSats, fee2sats), CAmount{0});
    // Refund rule: order amount converts exactly, input is amount + fee.
    BOOST_CHECK_EQUAL(xbridge::xBridgeDescrToSats(CAmount{9800}, 100000000), toSats);
    BOOST_CHECK_EQUAL(xbridge::xBridgeDescrToSats(CAmount{9800 + 100}, 100000000), CAmount{990000});
}

// On-chain order records with negative amounts must be rejected before the
// uint64_t cast in TxOutToCurrencyPair (a negative would wrap to a huge
// amount). Driven through a real OP_RETURN output like a scanned chain tx.
BOOST_AUTO_TEST_CASE(xbridge_txout_negative_amount_rejected) {
    const auto makeOut = [](const int64_t fromAmt, const int64_t toAmt) {
        UniValue info(UniValue::VARR);
        info.push_back("91d0ea83edc79b9a2041c51d08037cff87c181efb311a095dfdd4edbcc7993a9");
        info.push_back("BLOCK");
        info.push_back(fromAmt);
        info.push_back("LTC");
        info.push_back(toAmt);
        const std::string str = info.write();
        CScript script = CScript() << OP_RETURN
                                   << std::vector<unsigned char>(str.begin(), str.end());
        return CTxOut(0, script);
    };
    std::string snode;
    const CurrencyPair badFrom = TxOutToCurrencyPair({makeOut(-5, 5000000)}, snode);
    BOOST_CHECK(badFrom.tag == CurrencyPair::Tag::Error);
    BOOST_CHECK_EQUAL(badFrom.error(), "Bad from amount");
    const CurrencyPair badTo = TxOutToCurrencyPair({makeOut(11220000, -7)}, snode);
    BOOST_CHECK(badTo.tag == CurrencyPair::Tag::Error);
    BOOST_CHECK_EQUAL(badTo.error(), "Bad to amount");
    const CurrencyPair good = TxOutToCurrencyPair({makeOut(11220000, 5000000)}, snode);
    BOOST_CHECK(good.tag == CurrencyPair::Tag::Valid);
}

// Fail-open tripwire for the coinvalidator infraction list: LoadStatic skips
// unparsable lines instead of aborting, so a blacklist typo would silently
// whitelist an exploited txid. These known-bad txids (getExplList head) must
// stay invalid; any skipped or typo'd line flips this test red.
BOOST_AUTO_TEST_CASE(coinvalidator_static_list_failopen_guard) {
    BOOST_REQUIRE(CoinValidator::instance().LoadStatic());
    BOOST_CHECK_MESSAGE(!CoinValidator::instance().IsCoinValid(
        "00c0a0a887c2663e563494bd87f0ce279698d3e4f60fa3c5c39893f7fce8c336"),
        "blacklisted exploit txid must stay invalid");
    BOOST_CHECK_MESSAGE(!CoinValidator::instance().IsCoinValid(
        "00c2408da7c5d0bbbbc4aeeeae43f25d97fb6ba9c00a0203091af33a2c5276d7"),
        "blacklisted exploit txid must stay invalid");
    BOOST_CHECK_MESSAGE(CoinValidator::instance().IsCoinValid(
        "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"),
        "clean txid must stay valid");
}

// dxGetOrders reply-shape pin (UniValue migration): field names, string
// amount encoding, and status values are downstream-visible (Block DX).
BOOST_AUTO_TEST_CASE(xbridge_dxgetorders_reply_shape) {
    // Isolate from orders left by other cases (no erase API exists; history
    // is invisible to dxGetOrders which lists open transactions only).
    xbridge::App::instance().moveTransactionToHistory(uint256S(std::string(64, '4')));
    gArgs.SoftSetBoolArg("-dxnowallets", true); // list without live connectors
    const auto mkOrder = [](const std::string & idhex, const CAmount fromAmt,
                            const CAmount toAmt, const CAmount minAmt) {
        auto tr = std::make_shared<xbridge::TransactionDescr>();
        tr->id = uint256S(idhex);
        tr->fromCurrency = "BLOCK";
        tr->toCurrency = "LTC";
        tr->from = std::vector<unsigned char>{'f'};
        tr->to = std::vector<unsigned char>{'t'};
        tr->fromAmount = fromAmt;
        tr->toAmount = toAmt;
        tr->minFromAmount = minAmt;
        tr->origFromAmount = fromAmt;
        tr->origToAmount = toAmt;
        return tr;
    };
    auto t1 = mkOrder(std::string(64, '5'), CAmount{11220000}, CAmount{5000000}, CAmount{100});
    auto t2 = mkOrder(std::string(64, '6'), CAmount{9800}, CAmount{4900}, CAmount{0});
    xbridge::App::instance().appendTransaction(t1);
    xbridge::App::instance().appendTransaction(t2);
    JSONRPCRequest req;
    const UniValue result = dxGetOrders(req);
    BOOST_REQUIRE_MESSAGE(result.isArray(), "dxGetOrders must return an array");
    BOOST_REQUIRE_MESSAGE(result.size() >= 2, "both test orders must be listed");
    bool found1{false}, found2{false};
    for (const auto & row : result.getValues()) {
        BOOST_REQUIRE(row.isObject());
        const std::string id = find_value(row.get_obj(), "id").get_str();
        if (id != t1->id.GetHex() && id != t2->id.GetHex())
            continue;
        const auto & tr = (id == t1->id.GetHex()) ? t1 : t2;
        if (id == t1->id.GetHex()) found1 = true; else found2 = true;
        // amounts are decimal strings, never JSON numbers (wallet-parseable)
        BOOST_CHECK_EQUAL(find_value(row.get_obj(), "maker").get_str(), "BLOCK");
        BOOST_CHECK_EQUAL(find_value(row.get_obj(), "taker").get_str(), "LTC");
        BOOST_CHECK_EQUAL(find_value(row.get_obj(), "maker_size").get_str(),
                          xbridge::xBridgeStringValueFromAmount(tr->fromAmount));
        BOOST_CHECK_EQUAL(find_value(row.get_obj(), "taker_size").get_str(),
                          xbridge::xBridgeStringValueFromAmount(tr->toAmount));
        BOOST_CHECK(!find_value(row.get_obj(), "maker_size").isNum());
        BOOST_CHECK_EQUAL(find_value(row.get_obj(), "order_type").get_str(), "exact");
        BOOST_CHECK(find_value(row.get_obj(), "partial_repost").isBool());
        BOOST_CHECK(find_value(row.get_obj(), "partial_parent_id").isStr());
        BOOST_CHECK_EQUAL(find_value(row.get_obj(), "status").get_str(), tr->strState());
        BOOST_CHECK(!find_value(row.get_obj(), "updated_at").get_str().empty());
    }
    BOOST_CHECK_MESSAGE(found1 && found2, "both test rows must be found by id");
    xbridge::App::instance().moveTransactionToHistory(t1->id);
    xbridge::App::instance().moveTransactionToHistory(t2->id);
    gArgs.ForceSetArg("-dxnowallets", "false"); // restore: SoftSet has no unset
}

// makeError shape pin (UniValue migration): code is a JSON number (the
// int cast), name carries the calling function, error text non-empty.
BOOST_AUTO_TEST_CASE(xbridge_makeerror_shape) {
    const UniValue e = xbridge::makeError(xbridge::INVALID_PARAMETERS, "dxGetOrders", "msg");
    BOOST_REQUIRE(e.isObject());
    BOOST_CHECK(find_value(e.get_obj(), "code").isNum());
    BOOST_CHECK_EQUAL(find_value(e.get_obj(), "code").get_int(), 1025);
    BOOST_CHECK_EQUAL(find_value(e.get_obj(), "name").get_str(), "dxGetOrders");
    BOOST_CHECK(!find_value(e.get_obj(), "error").get_str().empty());
}

// XTxIn carries integer wallet sats (no double -> no truncation in
// locally built refund/payment txs or segwit sighash amounts).
BOOST_AUTO_TEST_CASE(xbridge_xtxin_carries_sats) {
    xbridge::XTxIn in("aaabbb", 1, CAmount{980000});
    BOOST_CHECK_EQUAL(in.amountSats, CAmount{980000});
}

static std::vector<unsigned char> readFileBytes(const fs::path & p) {
    std::ifstream f(p.string(), std::ios::binary);
    return std::vector<unsigned char>(std::istreambuf_iterator<char>(f),
                                      std::istreambuf_iterator<char>());
}

// Note on file-backed tests below: SetDataDir alone is not enough.
// GetDataDir caches its result, and earlier suites in a full-binary run
// leave a stale cache pointing at removed temp roots, so the cache must be
// cleared too (ClearDatadirCache after every SetDataDir).

// orders.dat durability: the first overwrite of a pre-existing file leaves a
// byte-identical one-time backup that later writes never touch.
BOOST_AUTO_TEST_CASE(xbridge_orders_backup_once) {
    SetDataDir("orders_backup_once");
    ClearDatadirCache();
    // Hermetic: datadirs persist across test-binary runs; stale files from a
    // previous run would fake the first-write precondition.
    fs::remove(GetDataDir() / "orders.dat");
    fs::remove(GetDataDir() / "orders.dat.pre-v2.bak");
    xbridge::XBridgeDB db;
    xbridge::XOrderSet orders;
    xbridge::TransactionDescr d;
    d.id = uint256S(std::string(64, '1'));
    d.fromAmount = 9800;
    orders[d.id] = d;
    BOOST_REQUIRE(db.Write(orders, true));
    const fs::path dbfile = GetDataDir() / "orders.dat";
    const fs::path bak = GetDataDir() / "orders.dat.pre-v2.bak";
    BOOST_CHECK_MESSAGE(!fs::exists(bak), "no backup on first write of a fresh file");
    const auto before = readFileBytes(dbfile);
    BOOST_REQUIRE(!before.empty());
    d.fromAmount = 11220000;
    orders[d.id] = d;
    BOOST_REQUIRE(db.Write(orders, true));
    BOOST_REQUIRE_MESSAGE(fs::exists(bak), "backup created before the second write");
    BOOST_CHECK_MESSAGE(readFileBytes(bak) == before, "backup is the pre-write content");
    xbridge::XOrderSet back;
    BOOST_REQUIRE(db.Read(back));
    BOOST_CHECK_EQUAL(back[d.id].fromAmount, uint64_t{11220000});
    d.fromAmount = 5000000;
    orders[d.id] = d;
    BOOST_REQUIRE(db.Write(orders, true));
    BOOST_CHECK_MESSAGE(readFileBytes(bak) == before, "later writes never touch the backup");
}

// orders.dat durability: a file that exists but fails to load (foreign
// version, corruption, torn write) must make the real App::saveOrders path
// refuse the overwrite, leaving file bytes untouched and memory authoritative.
BOOST_AUTO_TEST_CASE(xbridge_orders_no_overwrite_on_failed_read) {
    SetDataDir("orders_no_overwrite");
    ClearDatadirCache();
    fs::remove(GetDataDir() / "orders.dat");
    fs::remove(GetDataDir() / "orders.dat.pre-v2.bak");
    // seed a healthy file so the guard has something to protect
    {
        xbridge::XBridgeDB seed;
        xbridge::XOrderSet seedOrders;
        xbridge::TransactionDescr s;
        s.id = uint256S(std::string(64, '3'));
        s.fromAmount = 9800;
        seedOrders[s.id] = s;
        BOOST_REQUIRE(seed.Write(seedOrders, true));
    }
    // corrupt the file in place
    {
        std::ofstream f((GetDataDir() / "orders.dat").string(),
                        std::ios::binary | std::ios::trunc);
        f << "not a valid orders database, corrupt bytes";
    }
    const auto before = readFileBytes(GetDataDir() / "orders.dat");
    BOOST_REQUIRE(!before.empty());
    // non-empty memory: a local order forces saveOrders past the empty check
    // into the guarded Read path
    auto tr = std::make_shared<xbridge::TransactionDescr>();
    tr->id = uint256S(std::string(64, '4'));
    tr->from = std::vector<unsigned char>{'f'};
    tr->to = std::vector<unsigned char>{'t'};
    tr->fromAmount = 11220000;
    BOOST_REQUIRE(tr->isLocal());
    xbridge::App::instance().appendTransaction(tr);
    // drive the real guarded path with force: must refuse before Write (no
    // .bak side effect either — backup lives inside Write)
    xbridge::App::instance().saveOrders(true);
    BOOST_CHECK_MESSAGE(readFileBytes(GetDataDir() / "orders.dat") == before,
                        "refused save must leave the corrupt file untouched");
    BOOST_CHECK_MESSAGE(!fs::exists(GetDataDir() / "orders.dat.pre-v2.bak"),
                        "refused save must not reach Write (no backup taken)");
    BOOST_CHECK_MESSAGE(xbridge::App::instance().transaction(tr->id) != nullptr,
                        "refusal must not drop the in-memory order");
}

// orders.dat v2 round-trip through the real file path (envelope + checksum):
// the persisted retry budget survives Write -> Read with fields intact.
BOOST_AUTO_TEST_CASE(xbridge_orders_v2_roundtrip) {
    SetDataDir("orders_v2_roundtrip");
    ClearDatadirCache();
    fs::remove(GetDataDir() / "orders.dat");
    fs::remove(GetDataDir() / "orders.dat.pre-v2.bak");
    xbridge::XBridgeDB db;
    xbridge::XOrderSet orders;
    xbridge::TransactionDescr d;
    d.id = uint256S(std::string(64, '2'));
    d.fromAmount = 9800;
    d.tryRedeem();
    d.tryRedeem();
    orders[d.id] = d;
    BOOST_REQUIRE(db.Write(orders, true));
    xbridge::XOrderSet back;
    BOOST_REQUIRE(db.Read(back));
    BOOST_CHECK_EQUAL(back[d.id].fromAmount, uint64_t{9800});
    BOOST_CHECK_EQUAL(back[d.id].redeemTries(), 2u);
}

BOOST_AUTO_TEST_SUITE_END()
