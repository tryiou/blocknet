// Copyright (c) 2017-2019 The Blocknet developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

//*****************************************************************************
//*****************************************************************************

#ifndef BLOCKNET_XBRIDGE_UTIL_XUTIL_H
#define BLOCKNET_XBRIDGE_UTIL_XUTIL_H

#include <xbridge/util/logger.h>
#include <xbridge/util/xbridgeerror.h>
#include <xbridge/xbridgedef.h>

#include <amount.h>
#include <uint256.h>
#include <univalue.h>

#include <string>

#include <boost/date_time/posix_time/ptime.hpp>

#define BEGIN(a) ((char*)&(a))
#define END(a) ((char*)&((&(a))[1]))

//*****************************************************************************
//*****************************************************************************
namespace xbridge
{
    void init();

    std::wstring wide_string(std::string const & s);//, std::locale const &loc);
    // std::string narrow_string(std::wstring const &s, char default_char = '?');//, std::locale const &loc, char default_char = '?');

    std::string mb_string(std::string const & s);
    std::string mb_string(std::wstring const & s);

    std::string base64_encode(const std::vector<unsigned char> & s);
    std::string base64_encode(const std::string & s);
    std::string base64_decode(const std::string & s);

    std::string to_str(const std::vector<unsigned char> & obj);

    template<class _T> std::string to_str(const _T & obj)
    {
        return base64_encode(std::string((const char *)(obj.begin()),
                                         (const char *)(obj.end())));
    }


    /**
     * @brief iso8601 - converted boost posix time to string in ISO 8061 format
     * @param time - boost posix time
     * @return string in ISO 8061 format
     */
    std::string iso8601(const boost::posix_time::ptime &time);

    /**
     * @brief tranactionPrice - calculated transaction price in terms of bid price - toAmount/fromAmount
     * @param ptr - pointer to transaction description
     * @return price of transaction
     */
    double price(const xbridge::TransactionDescrPtr ptr);

    /**
     * @brief priceAsk - the inverted price calculation. Used by asks to calculate price in terms of bid price.
     * askFromAmount/askToAmount.
     * @param ptr - pointer to transaction description
     * @return price of transaction
     */
    double priceBid(const xbridge::TransactionDescrPtr ptr);

    boost::uint64_t timeToInt(const boost::posix_time::ptime &time);
    boost::posix_time::ptime intToTime(const uint64_t& number);

    constexpr double xBridgeMaxPriceDeviation = 1.0 / 100000000.0;
    constexpr int xBridgePartialOrderMaxUtxos = 10;
    double xBridgeValueFromAmount(CAmount amount);
    CAmount xBridgeIntFromReal(double val);
    CAmount xBridgeAmountFromReal(double val);
    std::string xBridgeAmountToString(CAmount descrAmount);
    // Exact integer conversion from descr units (TransactionDescr::COIN = 1e6
    // per coin) to wallet base units (walletCoin per coin, usually 1e8).
    // Pure integer math, no double involved.
    CAmount xBridgeDescrToSats(CAmount descrAmount, uint64_t walletCoin);
    // Convert a wallet coin-denominated double (e.g. UTXO amount, minTxFee)
    // to integer base units, rounding to nearest. Safe because wallet RPC
    // values carry at most 8 decimals and magnitudes are far below 2^53.
    CAmount xBridgeWalletSatsFromReal(double coinAmount, uint64_t walletCoin);
    // Redeem excess rule (integer sats): the excess over amount + redeem fee,
    // paid only on strict cover. Single definition shared by the payment
    // builder and the deposit verifier so the two sides cannot drift apart.
    CAmount xBridgeExcessSats(uint64_t p2shSats, CAmount toSats, CAmount fee2Sats);
    bool xBridgeFundsSufficient(CAmount inDescr, CAmount requirementDescr);
    // Redeem-retry decision for the ConfirmA/ConfirmB payTx blocks. errCode is
    // the callee contract: 0 = pre-broadcast permanent failure (bad secret:
    // counterparty misbehaving; nothing broadcast yet),
    // RPC_VERIFY_ERROR / RPC_CLIENT_NOT_CONNECTED = transient (dependency not
    // yet visible, wallet down, deterministic build fail that cannot be
    // distinguished from transient; bounded re-drive, never fast-cancel),
    // anything else = send-stage failure with uncertain broadcast state.
    // Every `return false` path in the callee sets errCode explicitly, and
    // call sites initialize it to REDEEM_ERR_UNSET (never 0), so CancelOrder
    // can only come from the deliberate bad-secret assignment, never from an
    // unset or forgotten code. Transient retries are bounded by
    // tries/maxTries and the watching flag; permanent pre-broadcast cancels
    // via the standard abort path; uncertain send failures expire to the
    // watch loop (cancel is unsafe once the pay tx may be out). Pre-secret
    // expiry must keep the packet alive via processLater (the watch loop only
    // recovers post-secret orders).
    // Sentinel for "callee did not set a code": classifies as Expire (safe
    // default — never fast-cancel on an unknown).
    static constexpr int32_t REDEEM_ERR_UNSET = INT32_MIN;
    enum class RedeemRetryClass { Retry, CancelOrder, Expire };
    RedeemRetryClass xBridgeRedeemRetryClass(int32_t errCode, uint32_t tries, uint32_t maxTries, bool doneWatching);
    std::string xBridgeStringValueFromPrice(double price);
    std::string xBridgeStringValueFromPrice(double price, uint64_t denomination);
    std::string xBridgeStringValueFromAmount(CAmount amount);

    /**
     * Return the counterparty destination amount from maker/taker price.
     * @param counterpartySourceAmount
     * @param sourceAmount
     * @param destAmount
     * @return
     */
    CAmount xBridgeDestAmountFromPrice(const CAmount counterpartySourceAmount, const CAmount sourceAmount, const CAmount destAmount);
    /**
     * Return the counterparty source amount from maker/taker price.
     * @param counterpartyDestAmount
     * @param sourceAmount
     * @param destAmount
     * @return
     */
    CAmount xBridgeSourceAmountFromPrice(const CAmount counterpartyDestAmount, const CAmount sourceAmount, const CAmount destAmount);

    /**
     * Responsible for checking for an acceptable drift in partial orders.
     * @param makerSource
     * @param makerDest
     * @param otherSource
     * @param otherDest
     * @return
     */
    bool xBridgePartialOrderDriftCheck(CAmount makerSource, CAmount makerDest, CAmount otherSource, CAmount otherDest);

    /**
     * @brief Returns true if the input precision is supported by xbridge.
     * @param coin Coin amount as string
     * @return true if valid, false if invalid
     * Example:<br>
     * \verbatim￼
        xBridgeValidCoin("0.000001")
        // returns true
     * \endverbatim
     */
    bool xBridgeValidCoin(std::string coin);

    /**
     * @brief Returns the number of digits in base 10 integer not including the most significant.
     * @param amount Coin amount as 64bit integer.
     * @return Number of digits (i.e. length of digits past decimal in 1/amount)
     * Example:<br>
     * \verbatim￼
        xBridgeSignificantDigits(1000000)
        // returns 6
     * \endverbatim
     */
    unsigned int xBridgeSignificantDigits(int64_t amount);
     /** @brief makeError - generate standard json_sprit object with error description
     * @param statusCode - error code
     * @param function - nome of called function
     * @param message - additional error description
     * @return  UniValue object with error description
     */
     UniValue makeError(const xbridge::Error statusCode, const std::string &function, const std::string &message = "");

    void LogOrderMsg(const std::string & orderId, const std::string & msg, const std::string & func);
    void LogOrderMsg(UniValue o, const std::string & msg, const std::string & func);
    void LogOrderMsg(xbridge::TransactionDescrPtr & ptr, const std::string & func);
    void LogOrderMsg(xbridge::TransactionPtr & ptr, const std::string & func);

} // namespace xbridge

#endif // BLOCKNET_XBRIDGE_UTIL_XUTIL_H
