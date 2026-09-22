// Copyright (c) 2017-2019 The Blocknet developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <rpc/server.h>
#include <algorithm>
#include <iterator>

#include <xbridge/util/logger.h>
#include <xbridge/util/settings.h>
#include <xbridge/util/xbridgeerror.h>
#include <xbridge/util/xseries.h>
#include <xbridge/util/xutil.h>
#include <xbridge/xbridgeapp.h>
#include <xbridge/xbridgeexchange.h>
#include <xbridge/xbridgetransaction.h>
#include <xbridge/xbridgetransactiondescr.h>
#include <xbridge/xuiconnector.h>

#include <init.h>
#include <rpc/util.h>
#include <shutdown.h>
#include <validation.h>

#include <array>
#include <atomic>
#include <math.h>
#include <numeric>
#include <stdio.h>

#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/iostreams/concepts.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/algorithm/string/predicate.hpp>

using namespace std;
using namespace boost;

using TransactionMap    = std::map<uint256, xbridge::TransactionDescrPtr>;
using TransactionPair   = std::pair<uint256, xbridge::TransactionDescrPtr>;
using RealVector        = std::vector<double>;
using TransactionVector = std::vector<xbridge::TransactionDescrPtr>;


std::string parseParentId(const uint256 & parentId) {
    if (parentId.IsNull())
        return "";
    return parentId.GetHex();
}

/**
 * @brief TxOutToCurrencyPair inspects a CTxOut and returns currency pair transaction info
 * @param tx.vout - transaction outpoints with possible multisig/op_return
 * @param snode_pubkey - (output) the service node public key
 * @return - currency pair transaction details
 */
CurrencyPair TxOutToCurrencyPair(const std::vector<CTxOut> & vout, std::string& snode_pubkey)
{
    snode_pubkey.clear();

    if (vout.empty())
        return {};

    bool foundOpData{false};
    std::string json;

    for (const CTxOut & out : vout) {
        if (out.scriptPubKey.empty())
            continue;

        std::vector<std::vector<unsigned char> > solutions;
        txnouttype type = Solver(out.scriptPubKey, solutions);

        if (type == TX_MULTISIG) {
            if (solutions.size() < 4)
                continue;

            snode_pubkey = EncodeDestination(CTxDestination(CPubKey(solutions[1]).GetID()));
            for (size_t i = 2; i < solutions.size()-1; ++i) {
                const auto& sol = solutions[i];
                if (sol.size() != 65)
                    break;
                std::copy(sol.begin()+1, sol.end(), std::back_inserter(json));
            }
        } else if (type == TX_NULL_DATA) {
            if (out.nValue != 0 || !out.scriptPubKey.IsUnspendable())
                continue;
            std::vector<unsigned char> data;
            CScript::const_iterator pc = out.scriptPubKey.begin();
            while (pc < out.scriptPubKey.end()) { // look for order data
                opcodetype opcode;
                if (!out.scriptPubKey.GetOp(pc, opcode, data))
                    break;
                if (data.size() != 0) {
                    std::copy(data.begin(), data.end(), std::back_inserter(json));
                    foundOpData = true;
                    break;
                }
            }
        }
    }

    if (json.empty())
        return {}; // no data found

    if (foundOpData && vout.size() >= 2) {
        CTxDestination snodeAddr;
        if (ExtractDestination(vout[1].scriptPubKey, snodeAddr))
            snode_pubkey = EncodeDestination(snodeAddr);
    }

    UniValue val;
    if (!val.read(json) || !val.isArray())
        return {}; // not order data, ignore
    UniValue xtx = val.get_array();
    if (xtx.size() != 5)
        return {"Unknown chain data, bad records count"};
    // validate chain inputs
    try { xtx[0].get_str(); } catch(...) {
        return {"Bad ID" }; }
    try { xtx[1].get_str(); } catch(...) {
        return {"Bad from token" }; }
    // Amounts arrive as JSON numbers and convert to uint64_t: reject
    // negatives before the cast (a negative would wrap to a huge amount).
    int64_t fromAmount{0}, toAmount{0};
    try { fromAmount = xtx[2].get_int64(); } catch(...) {
        return {"Bad from amount" }; }
    if (fromAmount < 0) {
        return {"Bad from amount" }; }
    try { xtx[3].get_str(); } catch(...) {
        return {"Bad to token" }; }
    try { toAmount = xtx[4].get_int64(); } catch(...) {
        return {"Bad to amount" }; }
    if (toAmount < 0) {
        return {"Bad to amount" }; }

    return CurrencyPair{
            xtx[0].get_str(),    // xid
            {ccy::Currency{xtx[1].get_str(),xbridge::TransactionDescr::COIN}, // fromCurrency
             static_cast<ccy::Amount>(fromAmount)},                   // fromAmount
            {ccy::Currency{xtx[3].get_str(),xbridge::TransactionDescr::COIN}, // toCurrency
             static_cast<ccy::Amount>(toAmount)}                      // toAmount
    };
}

UniValue dxGetNewTokenAddress(const JSONRPCRequest& request)
{
    if (request.fHelp)
        throw std::runtime_error(
            RPCHelpMan{"dxGetNewTokenAddress",
                "\nReturns a new address for the specified asset.\n",
                {
                    {"ticker", RPCArg::Type::STR, RPCArg::Optional::NO, "The ticker symbol of the asset you want to generate an address for (e.g. LTC)."},
                },
                RPCResult{
                R"(
    [
        "SVTbaYZ8oApVn3uNyimst3GKyvvfzXQgdK"
    ]

    Key                    | Type | Description
    -----------------------|------|----------------------------------------------
    Array                  | arr  | An array containing the newly generated
                           |      | address for the given asset.
                )"
                },
                RPCExamples{
                    HelpExampleCli("dxGetNewTokenAddress", "BTC")
                  + HelpExampleRpc("dxGetNewTokenAddress", "\"BTC\"")
                },
            }.ToString());
    const UniValue &params = request.params;

    if (params.size() != 1)
        return xbridge::makeError(xbridge::INVALID_PARAMETERS, __FUNCTION__, "(ticker)");

    const auto currency = params[0].get_str();
    UniValue res(UniValue::VARR);

    xbridge::WalletConnectorPtr conn = xbridge::App::instance().connectorByCurrency(currency);

    if (conn) {
        const auto addr = conn->getNewTokenAddress();
        if (!addr.empty())
            res.push_back(addr);
    }

    return res;
}

UniValue dxLoadXBridgeConf(const JSONRPCRequest& request)
{
    if (request.fHelp)
        throw std::runtime_error(
            RPCHelpMan{"dxLoadXBridgeConf",
                "\nHot loads the xbridge.conf file. Note, this may disrupt trades in progress.\n",
                {},
                RPCResult{
                R"(
    true

    Type | Description
    -----|----------------------------------------------
    bool | `true`: Successfully reloaded file.
                )"
                },
                RPCExamples{
                    HelpExampleCli("dxLoadXBridgeConf", "")
                  + HelpExampleRpc("dxLoadXBridgeConf", "")
                },
            }.ToString());
    const UniValue &params = request.params;

    if (params.size() > 0)
        return xbridge::makeError(xbridge::INVALID_PARAMETERS, __FUNCTION__,
                               "This function does not accept any parameter.");

    if (ShutdownRequested())
        throw runtime_error("dxLoadXBridgeConf\nFailed to reload the config because a shutdown request is in progress.");

    auto & app = xbridge::App::instance();
    if (app.isUpdatingWallets()) // let the user know if wallets are being actively updated
        throw runtime_error("dxLoadXBridgeConf\nAn existing wallet update is currently in progress, please wait until it is completed.");

    auto success = app.loadSettings();
    app.clearBadWallets(); // clear any bad wallet designations b/c user is explicitly requesting a wallet update
    app.updateActiveWallets();
    if (!settings().showAllOrders())
        app.clearNonLocalOrders();
    return success;
}

UniValue dxGetLocalTokens(const JSONRPCRequest& request)
{
    if (request.fHelp)
        throw std::runtime_error(
            RPCHelpMan{"dxGetLocalTokens",
                "\nReturns a list of assets supported by your node. "
                "You can only trade on markets with assets returned in both dxGetNetworkTokens and dxGetLocalTokens.\n",
                {},
                RPCResult{
                R"(
    [
        "BLOCK",
        "LTC",
        "MONA",
        "SYS"
    ]

    Key                    | Type | Description
    -----------------------|------|----------------------------------------------
    Array                  | arr  | An array of all the assets supported by the
                           |      | local client.
                )"
                },
                RPCExamples{
                    HelpExampleCli("dxGetLocalTokens", "")
                  + HelpExampleRpc("dxGetLocalTokens", "")
                },
            }.ToString());
    const UniValue &params = request.params;

    if (params.size() > 0) {
        return xbridge::makeError(xbridge::INVALID_PARAMETERS, __FUNCTION__,
                               "This function does not accept any parameter.");
    }

    UniValue r(UniValue::VARR);

    std::vector<std::string> currencies = xbridge::App::instance().availableCurrencies();
    for (std::string currency : currencies) {
        r.push_back(currency);
    }
    return r;
}

UniValue dxGetNetworkTokens(const JSONRPCRequest& request)
{
    if (request.fHelp)
        throw std::runtime_error(
            RPCHelpMan{"dxGetNetworkTokens",
                "\nReturns a list of all the assets currently supported by the network. "
                "You can only trade on markets with assets returned in both dxGetNetworkTokens and dxGetLocalTokens.\n",
                {},
                RPCResult{
                R"(
    [
        "BLOCK",
        "BTC",
        "DGB",
        "LTC",
        "MONA",
        "PIVX",
        "SYS"
    ]

    Key                    | Type | Description
    -----------------------|------|----------------------------------------------
    Array                  | arr  | An array of all the assets supported by the
                           |      | network.
                )"
                },
                RPCExamples{
                    HelpExampleCli("dxGetNetworkTokens", "")
                  + HelpExampleRpc("dxGetNetworkTokens", "")
                },
            }.ToString());
    const UniValue &params = request.params;

    if (params.size() > 0) {
        return xbridge::makeError(xbridge::INVALID_PARAMETERS, __FUNCTION__,
                               "This function does not accept any parameters.");
    }

    std::set<std::string> services;
    auto ws = xbridge::App::instance().walletServices();
    for (auto & serviceItem : ws) {
        auto s = serviceItem.second.services();
        services.insert(s.begin(), s.end());
    }

    UniValue arr(UniValue::VARR);
    for (const auto & service : services)
        arr.push_back(service);
    return arr;
}

/** \brief Returns the list of open and pending transactions
  * \param params A list of input params.
  * \param request.fHelp For debug purposes, throw the exception describing parameters.
  * \return A list of open(they go first) and pending transactions.
  *
  * Returns the list of open and pending transactions as JSON structures.
  * The open transactions go first.
  */
UniValue dxGetOrders(const JSONRPCRequest& request)
{
    if (request.fHelp)
        throw std::runtime_error(
            RPCHelpMan{"dxGetOrders",
                "\nReturns a list of all orders of every market pair. \n"
                "It will only return orders for assets returned in dxGetLocalTokens.\n",
                {},
                RPCResult{
                R"(
    [
        {
            "id": "91d0ea83edc79b9a2041c51d08037cff87c181efb311a095dfdd4edbcc7993a9",
            "maker": "SYS",
            "maker_size": "100.000000",
            "taker": "LTC",
            "taker_size": "10.500000",
            "updated_at": "2018-01-15T18:25:05.12345Z",
            "created_at": "2018-01-15T18:15:30.12345Z",
            "order_type": "partial",
            "partial_minimum": "10.000000",
            "partial_orig_maker_size": "100.000000",
            "partial_orig_taker_size": "10.500000",
            "partial_repost": false,
            "partial_parent_id": "",
            "status": "open"
        },
        {
            "id": "a1f40d53f75357eb914554359b207b7b745cf096dbcb028eb77b7b7e4043c6b4",
            "maker": "SYS",
            "maker_size": "0.100000",
            "taker": "LTC",
            "taker_size": "0.010000",
            "updated_at": "2018-01-15T18:25:05.12345Z",
            "created_at": "2018-01-15T18:15:30.12345Z",
            "order_type": "exact",
            "partial_minimum": "0.000000",
            "partial_orig_maker_size": "0.000000",
            "partial_orig_taker_size": "0.000000",
            "partial_repost": false,
            "partial_parent_id": "",
            "status": "open"
        }
    ]

    Key                     | Type | Description
    ------------------------|------|---------------------------------------------
    Array                   | arr  | An array of all orders with each order
                            |      | having the following parameters.
    id                      | str  | The order ID.
    maker                   | str  | Maker trading asset; the ticker of the asset
                            |      | being sold by the maker.
    maker_size              | str  | Maker trading size. String is used to
                            |      | preserve precision.
    maker_address           | str  | Address for sending the outgoing asset.
    taker                   | str  | Taker trading asset; the ticker of the asset
                            |      | being sold by the taker.
    taker_size              | str  | Taker trading size. String is used to
                            |      | preserve precision.
    taker_address           | str  | Address for receiving the incoming asset.
    updated_at              | str  | ISO 8601 datetime, with microseconds, of the
                            |      | last time the order was updated.
    created_at              | str  | ISO 8601 datetime, with microseconds, of
                            |      | when the order was created.
    order_type              | str  | The order type.
    partial_minimum*        | str  | The minimum amount that can be taken.
    partial_orig_maker_size*| str  | The partial order original maker_size.
    partial_orig_taker_size*| str  | The partial order original taker_size.
    partial_repost          | str  | Whether the order will be reposted or not.
                            |      | This applies to `partial` order types and
                            |      | will show `false` for `exact` order types.
    partial_parent_id       | str  | The previous order id of a reposted partial
                            |      | order. This will return an empty string if
                            |      | there is no parent order.
    status                  | str  | The order status.

    * This only applies to `partial` order types and will show `0` on `exact`
      order types.
                )"
                },
                RPCExamples{
                    HelpExampleCli("dxGetOrders", "")
                  + HelpExampleRpc("dxGetOrders", "")
                },
            }.ToString());
    const UniValue &params = request.params;

    if (!params.empty()) {
        return xbridge::makeError(xbridge::INVALID_PARAMETERS, __FUNCTION__,
                               "This function does not accept any parameters.");
    }

    auto &xapp = xbridge::App::instance();
    TransactionMap trlist = xapp.transactions();
    auto currentTime = boost::posix_time::second_clock::universal_time();
    bool nowalletswitch = gArgs.GetBoolArg("-dxnowallets", settings().showAllOrders());
    UniValue result(UniValue::VARR);
    for (const auto& trEntry : trlist) {

        const auto &tr = trEntry.second;

        // Skip canceled, finished, and expired orders older than 1 minute
        if ((currentTime - tr->txtime).total_seconds() > 60) {
            if (tr->state == xbridge::TransactionDescr::trCancelled
              || tr->state == xbridge::TransactionDescr::trFinished
              || tr->state == xbridge::TransactionDescr::trExpired)
            continue;
        }

        xbridge::WalletConnectorPtr connFrom = xapp.connectorByCurrency(tr->fromCurrency);
        xbridge::WalletConnectorPtr connTo   = xapp.connectorByCurrency(tr->toCurrency);
        if ((!connFrom || !connTo) && !nowalletswitch ){
            continue;
        }

        UniValue jtr(UniValue::VOBJ);
        jtr.pushKV("id",             tr->id.GetHex());
        jtr.pushKV("maker",          tr->fromCurrency);
        jtr.pushKV("maker_size",     xbridge::xBridgeStringValueFromAmount(tr->fromAmount));
        jtr.pushKV("taker",          tr->toCurrency);
        jtr.pushKV("taker_size",     xbridge::xBridgeStringValueFromAmount(tr->toAmount));
        jtr.pushKV("updated_at",     xbridge::iso8601(tr->txtime));
        jtr.pushKV("created_at",     xbridge::iso8601(tr->created));
        jtr.pushKV("order_type",     tr->orderType());
        jtr.pushKV("partial_minimum", xbridge::xBridgeStringValueFromAmount(tr->minFromAmount));
        jtr.pushKV("partial_orig_maker_size", xbridge::xBridgeStringValueFromAmount(tr->origFromAmount));
        jtr.pushKV("partial_orig_taker_size", xbridge::xBridgeStringValueFromAmount(tr->origToAmount));
        jtr.pushKV("partial_repost", tr->repostOrder);
        jtr.pushKV("partial_parent_id", parseParentId(tr->getParentOrder()));
        jtr.pushKV("status",         tr->strState());
        result.push_back(jtr);

    }


    return result;
}

UniValue dxGetOrderFills(const JSONRPCRequest& request)
{
    if (request.fHelp)
        throw std::runtime_error(
            RPCHelpMan{"dxGetOrderFills",
                "\nReturns all the recent trades by trade pair that have been filled (i.e. completed). "
                "This will only return orders that have been filled in your current session.\n",
                {
                    {"maker", RPCArg::Type::STR, RPCArg::Optional::NO, "The symbol of the asset sold by the maker (e.g. LTC)."},
                    {"taker", RPCArg::Type::STR, RPCArg::Optional::NO, "The symbol of the asset sold by the taker (e.g. BLOCK)."},
                    {"combined", RPCArg::Type::BOOL, "true", "If true, combines the results to return orders with the maker and taker as specified as well as orders of the inverse market. If false, only returns filled orders with the maker and taker assets as specified."},
                },
                RPCResult{
                R"(
    [
        {
            "id": "a1f40d53f75357eb914554359b207b7b745cf096dbcb028eb77b7b7e4043c6b4",
            "time": "2018-01-16T13:15:05.12345Z",
            "maker": "SYS",
            "maker_size": "101.00000000",
            "taker": "LTC",
            "taker_size": "0.01000000"
        },
        {
            "id": "91d0ea83edc79b9a2041c51d08037cff87c181efb311a095dfdd4edbcc7993a9",
            "time": "2018-01-16T13:15:05.12345Z",
            "maker": "LTC",
            "maker_size": "0.01000000",
            "taker": "SYS",
            "taker_size": "101.00000000"
        }
    ]

    Key             | Type | Description
    ----------------|------|-----------------------------------------------------
    Array           | arr  | Array of orders sorted by date descending.
    id              | str  | The order ID.
    time            | str  | Time the order was filled.
    maker           | str  | Maker trading asset; the ticker of the asset being
                    |      | sold by the maker.
    maker_size      | str  | Maker trading size. String is used to preserve
                    |      | precision.
    taker           | str  | Taker trading asset; the ticker of the asset being
                    |      | sold by the taker.
    taker_size      | str  | Taker trading size. String is used to preserve
                    |      | precision.
                )"
                },
                RPCExamples{
                    HelpExampleCli("dxGetOrderFills", "BLOCK LTC")
                  + HelpExampleRpc("dxGetOrderFills", "\"BLOCK\", \"LTC\"")
                  + HelpExampleCli("dxGetOrderFills", "BLOCK LTC true")
                  + HelpExampleRpc("dxGetOrderFills", "\"BLOCK\", \"LTC\", true")
                },
            }.ToString());
    const UniValue &params = request.params;

    bool invalidParams = ((params.size() != 2) &&
                          (params.size() != 3));
    if (invalidParams) {
        return xbridge::makeError(xbridge::INVALID_PARAMETERS, __FUNCTION__,
                               "(maker) (taker) (combined, default=true)[optional]");
    }

    bool combined = params.size() == 3 ? params[2].get_bool() : true;

    const auto maker = params[0].get_str();
    const auto taker = params[1].get_str();



    TransactionMap history = xbridge::App::instance().history();



    TransactionVector result;

    for (auto &item : history) {
        const xbridge::TransactionDescrPtr &ptr = item.second;
        if ((ptr->state == xbridge::TransactionDescr::trFinished) &&
            (combined ? ((ptr->fromCurrency == maker && ptr->toCurrency == taker) || (ptr->toCurrency == maker && ptr->fromCurrency == taker)) : (ptr->fromCurrency == maker && ptr->toCurrency == taker))) {
            result.push_back(ptr);
        }
    }

    std::sort(result.begin(), result.end(),
              [](const xbridge::TransactionDescrPtr &a,  const xbridge::TransactionDescrPtr &b)
    {
         return (a->txtime) > (b->txtime);
    });

    UniValue arr(UniValue::VARR);
    for(const auto &transaction : result) {

        UniValue tmp(UniValue::VOBJ);
        tmp.pushKV("id",         transaction->id.GetHex());
        tmp.pushKV("time",       xbridge::iso8601(transaction->txtime));
        tmp.pushKV("maker",      transaction->fromCurrency);
        tmp.pushKV("maker_size", xbridge::xBridgeStringValueFromAmount(transaction->fromAmount));
        tmp.pushKV("taker",      transaction->toCurrency);
        tmp.pushKV("taker_size", xbridge::xBridgeStringValueFromAmount(transaction->toAmount));
        tmp.pushKV("order_type", transaction->orderType());
        tmp.pushKV("partial_minimum", xbridge::xBridgeStringValueFromAmount(transaction->minFromAmount));
        tmp.pushKV("partial_orig_maker_size", xbridge::xBridgeStringValueFromAmount(transaction->origFromAmount));
        tmp.pushKV("partial_orig_taker_size", xbridge::xBridgeStringValueFromAmount(transaction->origToAmount));
        tmp.pushKV("partial_repost", transaction->repostOrder);
        tmp.pushKV("partial_parent_id", parseParentId(transaction->getParentOrder()));
        arr.push_back(tmp);

    }
    return arr;
}

UniValue dxGetOrderHistory(const JSONRPCRequest& request)
{
    if (request.fHelp)
        throw std::runtime_error(
            RPCHelpMan{"dxGetOrderHistory",
                "\nReturns the OHLCV data by trade pair for a specified time range and interval. "
                "It can return the order history for any asset since all trade history is stored on-chain.\n",
                {
                    {"maker", RPCArg::Type::STR, RPCArg::Optional::NO, "The symbol of the asset sold by the maker (e.g. LTC)."},
                    {"taker", RPCArg::Type::STR, RPCArg::Optional::NO, "The symbol of the asset sold by the taker (e.g. BLOCK)."},
                    {"start_time", RPCArg::Type::NUM, RPCArg::Optional::NO, "The Unix time in seconds for the start time boundary to search."},
                    {"end_time", RPCArg::Type::NUM, RPCArg::Optional::NO, "The Unix time in seconds for the end time boundary to search."},
                    {"granularity", RPCArg::Type::NUM, RPCArg::Optional::NO, "Time interval slice in seconds. The slice options are: " + xQuery::supported_seconds_csv()},
                    {"order_ids", RPCArg::Type::BOOL, "false", "If true, returns the IDs of all filled orders in each slice. If false, IDs are omitted."},
                    {"with_inverse", RPCArg::Type::BOOL, "false", "If false, returns the order history for the specified market. If true, also returns the orders in the inverse pair too (e.g. if LTC SYS then SYS LTC would be returned as well)."},
                    {"limit", RPCArg::Type::NUM, std::to_string(xQuery::IntervalLimit{}.count()), "The max number of interval slices returned. maximum=" + std::to_string(xQuery::IntervalLimit::max())},
                    // {"interval_timestamp", RPCArg::Type::STR, "at_start", "The timestamp at start of the interval. The options are [at_start | at_end]."},
                },
                RPCResult{
                R"(
    [
        //[ time, low, high, open, close, volume, id(s) ],
        [ "2018-01-16T13:15:05.12345Z", 1.10, 2.0, 1.10, 1.4, 1000, [ "0cc2e8a7222f1416cda996031ca21f67b53431614e89651887bc300499a6f83e" ] ],
        [ "2018-01-16T14:15:05.12345Z", 0, 0, 0, 0, 0, [] ],
        [ "2018-01-16T15:15:05.12345Z", 1.12, 2.2, 1.10, 1.4, 1000, [ "91d0ea83edc79b9a2041c51d08037cff87c181efb311a095dfdd4edbcc7993a9", "0cc2e8a7222f1416cda996031ca21f67b53431614e89651887bc300499a6f83e", "a1f40d53f75357eb914554359b207b7b745cf096dbcb028eb77b7b7e4043c6b4" ] ],
        [ "2018-01-16T16:15:05.12345Z", 1.14, 2.0, 1.10, 1.4, 1000, [ "a1f40d53f75357eb914554359b207b7b745cf096dbcb028eb77b7b7e4043c6b4" ] ],
        [ "2018-01-16T17:15:05.12345Z", 1.15, 2.0, 1.10, 1.4, 1000, [ "6be548bc46a3dcc69b6d56529948f7e679dd96657f85f5870a017e005caa050a" ] ]
    ]

    Key           | Type  | Description
    --------------|-------|------------------------------------------------------
    time          | str   | ISO 8601 datetime, with microseconds, of the time at
                  |       | the beginning of the time slice.
    low           | float | Exchange rate lower bound within the time slice.
    high          | float | Exchange rate upper bound within the time slice.
    open          | float | Exchange rate of first filled order at the beginning
                  |       | of the time slice.
    close         | float | Exchange rate of last filled order at the end of the
                  |       | time slice.
    volume        | int   | Total volume of the taker asset within the time
                  |       | slice.
    order_ids     | arr   | Array of GUIDs of all filled orders within the time
                  |       | slice.
                )"
                },
                RPCExamples{
                    HelpExampleCli("dxGetOrderHistory", "SYS LTC 1540660180 1540660420 60")
                  + HelpExampleRpc("dxGetOrderHistory", "\"SYS\", \"LTC\", 1540660180, 1540660420, 60")
                  + HelpExampleCli("dxGetOrderHistory", "SYS LTC 1540660180 1540660420 60 true false 18000")
                  + HelpExampleRpc("dxGetOrderHistory", "\"SYS\", \"LTC\", 1540660180, 1540660420, 60, true, false, 18000")
                },
            }.ToString());
    const UniValue &params = request.params;

    //--Validate query parameters
    if (params.size() < 5 || params.size() > 8)
        return xbridge::makeError(xbridge::INVALID_PARAMETERS, __FUNCTION__,
                               "(maker) (taker) (start time) (end time) (granularity) "
                               "(order_ids, default=false)[optional] "
                               "(with_inverse, default=false)[optional] "
                               "(limit, default="+std::to_string(xQuery::IntervalLimit{}.count())+")[optional]"
                               // "(interval_timestamp, one of [at_start | at_end])[optional] "
                               );
    const xQuery query{
        params[0].get_str(),    // maker
        params[1].get_str(),    // taker
        params[4].get_int(),    // granularity (need before start/end time)
        params[2].get_int64(),  // start time
        params[3].get_int64(),  // end time
        params.size() > 5 && params[5].get_bool()
            ? xQuery::WithTxids::Included
            : xQuery::WithTxids::Excluded,
        params.size() > 6 && params[6].get_bool()
            ? xQuery::WithInverse::Included
            : xQuery::WithInverse::Excluded,
        params.size() > 7
            ? xQuery::IntervalLimit{params[7].get_int()}
            : xQuery::IntervalLimit{},
        params.size() > 8
            ? xQuery::IntervalTimestamp{params[8].get_str()}
            : xQuery::IntervalTimestamp{}
    };

    if (query.error())
        return xbridge::makeError(xbridge::INVALID_PARAMETERS, __FUNCTION__, query.what() );
    try {
        //--Process query, get result
        auto& xseries = xbridge::App::instance().getXSeriesCache();
        std::vector<xAggregate> result = xseries.getXAggregateSeries(query);

        //--Serialize result
        UniValue arr(UniValue::VARR);
        const boost::posix_time::time_duration offset = query.interval_timestamp.at_start()
            ? query.granularity
            : boost::posix_time::seconds{0};
        for (const auto& x : result) {
            double volume = x.fromVolume.amount<double>();
            UniValue ohlc(UniValue::VARR);
            ohlc.push_back(xbridge::iso8601(x.timeEnd - offset)); // time
            ohlc.push_back(x.low);
            ohlc.push_back(x.high);
            ohlc.push_back(x.open);
            ohlc.push_back(x.close);
            ohlc.push_back(volume);
            if (query.with_txids == xQuery::WithTxids::Included) {
                UniValue orderIds(UniValue::VARR);
                for (const auto& id : x.orderIds)
                    orderIds.push_back(id);
                ohlc.push_back(orderIds);
            }
            arr.push_back(ohlc);
        }
        return arr;
    } catch(const std::exception& e) {
        return xbridge::makeError(xbridge::UNKNOWN_ERROR, __FUNCTION__, e.what() );
    } catch( ... ) {
        return xbridge::makeError(xbridge::UNKNOWN_ERROR, __FUNCTION__, "unknown exception" );
    }
}

UniValue dxGetOrder(const JSONRPCRequest& request)
{
    if (request.fHelp)
        throw std::runtime_error(
            RPCHelpMan{"dxGetOrder",
                "\nReturns order info by order ID.\n",
                {
                    {"id", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "The order ID."},
                },
                RPCResult{
                R"(
    {
        "id": "6be548bc46a3dcc69b6d56529948f7e679dd96657f85f5870a017e005caa050a",
        "maker": "SYS",
        "maker_size": "0.100",
        "taker": "LTC",
        "taker_size": "0.01",
        "updated_at": "1970-01-01T00:00:00.00000Z",
        "created_at": "2018-01-15T18:15:30.12345Z",
        "order_type": "exact",
        "partial_minimum": "0.000000",
        "partial_orig_maker_size": "0.000000",
        "partial_orig_taker_size": "0.000000",
        "partial_repost": false,
        "partial_parent_id": "",
        "status": "open"
    }

    Key                     | Type | Description
    ------------------------|------|---------------------------------------------
    Array                   | arr  | An array of all orders with each order
                            |      | having the following parameters.
    id                      | str  | The order ID.
    maker                   | str  | Maker trading asset; the ticker of the asset
                            |      | being sold by the maker.
    maker_size              | str  | Maker trading size. String is used to
                            |      | preserve precision.
    maker_address           | str  | Address for sending the outgoing asset.
    taker                   | str  | Taker trading asset; the ticker of the asset
                            |      | being sold by the taker.
    taker_size              | str  | Taker trading size. String is used to
                            |      | preserve precision.
    taker_address           | str  | Address for receiving the incoming asset.
    updated_at              | str  | ISO 8601 datetime, with microseconds, of the
                            |      | last time the order was updated.
    created_at              | str  | ISO 8601 datetime, with microseconds, of
                            |      | when the order was created.
    order_type              | str  | The order type.
    partial_minimum*        | str  | The minimum amount that can be taken.
    partial_orig_maker_size*| str  | The partial order original maker_size.
    partial_orig_taker_size*| str  | The partial order original taker_size.
    partial_repost          | str  | Whether the order will be reposted or not.
                            |      | This applies to `partial` order types and
                            |      | will show `false` for `exact` order types.
    partial_parent_id       | str  | The previous order id of a reposted partial
                            |      | order. This will return an empty string if
                            |      | there is no parent order.
    status                  | str  | The order status.

    * This only applies to `partial` order types and will show `0` on `exact`
      order types.
                )"
                },
                RPCExamples{
                    HelpExampleCli("dxGetOrder", "524137449d9a35fa707ee395abab32bedae91aa2aefb6e3611fcd8574863e432")
                  + HelpExampleRpc("dxGetOrder", "\"524137449d9a35fa707ee395abab32bedae91aa2aefb6e3611fcd8574863e432\"")
                },
            }.ToString());
    const UniValue &params = request.params;

    if (params.size() != 1) {
        return xbridge::makeError(xbridge::INVALID_PARAMETERS, __FUNCTION__, "(id)");
    }

    uint256 id = uint256S(params[0].get_str());

    auto &xapp = xbridge::App::instance();

    const xbridge::TransactionDescrPtr order = xapp.transaction(uint256(id));

    if(order == nullptr) {
        return xbridge::makeError(xbridge::TRANSACTION_NOT_FOUND, __FUNCTION__, id.ToString());
    }

    xbridge::WalletConnectorPtr connFrom = xapp.connectorByCurrency(order->fromCurrency);
    xbridge::WalletConnectorPtr connTo   = xapp.connectorByCurrency(order->toCurrency);
    if(!connFrom) {
        return xbridge::makeError(xbridge::NO_SESSION, __FUNCTION__, order->fromCurrency);
    }
    if (!connTo) {
        return xbridge::makeError(xbridge::NO_SESSION, __FUNCTION__, order->toCurrency);
    }

    UniValue result(UniValue::VOBJ);
    result.pushKV("id",          order->id.GetHex());
    result.pushKV("maker",       order->fromCurrency);
    result.pushKV("maker_size",  xbridge::xBridgeStringValueFromAmount(order->fromAmount));
    result.pushKV("taker",       order->toCurrency);
    result.pushKV("taker_size",  xbridge::xBridgeStringValueFromAmount(order->toAmount));
    result.pushKV("updated_at",  xbridge::iso8601(order->txtime));
    result.pushKV("created_at",  xbridge::iso8601(order->created));
    result.pushKV("order_type", order->orderType());
    result.pushKV("partial_minimum", xbridge::xBridgeStringValueFromAmount(order->minFromAmount));
    result.pushKV("partial_orig_maker_size", xbridge::xBridgeStringValueFromAmount(order->origFromAmount));
    result.pushKV("partial_orig_taker_size", xbridge::xBridgeStringValueFromAmount(order->origToAmount));
    result.pushKV("partial_repost", order->repostOrder);
    result.pushKV("partial_parent_id", parseParentId(order->getParentOrder()));
    result.pushKV("status",      order->strState());
    return result;
}

UniValue dxMakeOrder(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 7)
        throw std::runtime_error(
            RPCHelpMan{"dxMakeOrder",
                "\nCreate a new exact order. Exact orders must be taken for the full order amount. "
                "For partial orders, see dxMakePartialOrder.\n"
                "You can only create orders for markets with assets supported by your node (view with dxGetLocalTokens) "
                "and the network (view with dxGetNetworkTokens). There are no fees to make orders.\n"
                "\nNote:\n"
                "XBridge will first attempt to use funds from the specified maker address. "
                "If this address does not have sufficient funds to cover the order and "
                "`use_all_funds` is true, then it will pull funds from other addresses in "
                "the wallet. Change is deposited to the address with the largest input used.\n",
                {
                    {"maker", RPCArg::Type::STR, RPCArg::Optional::NO, "The symbol of the asset being sold by the maker (e.g. LTC)."},
                    {"maker_size", RPCArg::Type::STR, RPCArg::Optional::NO, "The amount of the maker asset being sent."},
                    {"maker_address", RPCArg::Type::STR, RPCArg::Optional::NO, "The maker address containing asset being sent."},
                    {"taker", RPCArg::Type::STR, RPCArg::Optional::NO, "The symbol of the asset being bought by the maker (e.g. BLOCK)."},
                    {"taker_size", RPCArg::Type::STR, RPCArg::Optional::NO, "The amount of the taker asset to be received."},
                    {"taker_address", RPCArg::Type::STR, RPCArg::Optional::NO, "The taker address for the receiving asset."},
                    {"type", RPCArg::Type::STR, RPCArg::Optional::NO, "The order type. Options: exact"},
                    {"use_all_funds", RPCArg::Type::BOOL, /* default */ "true", "Use funds from all available addresses in the wallet as opposed to just the maker_address."},
                    {"dryrun", RPCArg::Type::STR, RPCArg::Optional::OMITTED, "Simulate the order submission without actually submitting the order, i.e. a test run. Options: dryrun"},
                },
                RPCResult{
                R"(
    {
        "id": "4306a107113c4562afa6273ecd9a3990ead53a0227f74ddd9122272e453ae07d",
        "maker": "SYS",
        "maker_size": "1.000000",
        "maker_address": "SVTbaYZ8olpVn3uNyImst3GKyrvfzXQgdK",
        "taker": "LTC",
        "taker_size": "0.100000",
        "taker_address": "LVvFhZroMRGTtg1hHp7jVew3YoZRX8y35Z",
        "updated_at": "2018-01-16T00:00:00.00000Z",
        "created_at": "2018-01-15T18:15:30.12345Z",
        "block_id": "38729344720548447445023782734923740427863289632489723984723",
        "order_type": "exact",
        "partial_minimum": "0.000000",
        "partial_orig_maker_size": "0.000000",
        "partial_orig_taker_size": "0.000000",
        "partial_repost": false,
        "partial_parent_id": "",
        "status": "created"
    }

    Key                     | Type | Description
    ------------------------|------|---------------------------------------------
    Array                   | arr  | An array of all orders with each order
                            |      | having the following parameters.
    id                      | str  | The order ID.
    maker                   | str  | Maker trading asset; the ticker of the asset
                            |      | being sold by the maker.
    maker_size              | str  | Maker trading size. String is used to
                            |      | preserve precision.
    maker_address           | str  | Address for sending the outgoing asset.
    taker                   | str  | Taker trading asset; the ticker of the asset
                            |      | being sold by the taker.
    taker_size              | str  | Taker trading size. String is used to
                            |      | preserve precision.
    taker_address           | str  | Address for receiving the incoming asset.
    updated_at              | str  | ISO 8601 datetime, with microseconds, of the
                            |      | last time the order was updated.
    created_at              | str  | ISO 8601 datetime, with microseconds, of
                            |      | when the order was created.
    order_type              | str  | The order type.
    partial_minimum*        | str  | The minimum amount that can be taken.
    partial_orig_maker_size*| str  | The partial order original maker_size.
    partial_orig_taker_size*| str  | The partial order original taker_size.
    partial_repost          | str  | Whether the order will be reposted or not.
                            |      | This applies to `partial` order types and
                            |      | will show `false` for `exact` order types.
    partial_parent_id       | str  | The previous order id of a reposted partial
                            |      | order. This will return an empty string if
                            |      | there is no parent order.
    status                  | str  | The order status.

    * This only applies to `partial` order types and will show `0` on `exact`
      order types.
                )"
                },
                RPCExamples{
                    HelpExampleCli("dxMakeOrder", "LTC 25 LLZ1pgb6Jqx8hu84fcr5WC5HMoKRUsRE8H BLOCK 1000 BWQrvmuHB4C68KH5V7fcn9bFtWN8y5hBmR exact")
                  + HelpExampleRpc("dxMakeOrder", "\"LTC\", \"25\", \"LLZ1pgb6Jqx8hu84fcr5WC5HMoKRUsRE8H\", \"BLOCK\", \"1000\", \"BWQrvmuHB4C68KH5V7fcn9bFtWN8y5hBmR\", \"exact\"")
                  + HelpExampleCli("dxMakeOrder", "LTC 25 LLZ1pgb6Jqx8hu84fcr5WC5HMoKRUsRE8H BLOCK 1000 BWQrvmuHB4C68KH5V7fcn9bFtWN8y5hBmR exact true dryrun")
                  + HelpExampleRpc("dxMakeOrder", "\"LTC\", \"25\", \"LLZ1pgb6Jqx8hu84fcr5WC5HMoKRUsRE8H\", \"BLOCK\", \"1000\", \"BWQrvmuHB4C68KH5V7fcn9bFtWN8y5hBmR\", \"exact\", \"true\", \"dryrun\"")
                },
            }.ToString());
    const UniValue &params = request.params;

    if (!xbridge::xBridgeValidCoin(params[1].get_str())) {
        UniValue error(UniValue::VOBJ);
        error.pushKV("error",    xbridge::xbridgeErrorText(xbridge::INVALID_PARAMETERS,
                      "The maker_size is too precise. The maximum precision supported is " +
                              std::to_string(xbridge::xBridgeSignificantDigits(xbridge::TransactionDescr::COIN)) + " digits."));
        error.pushKV("code",     xbridge::INVALID_PARAMETERS);
        error.pushKV("name",     __FUNCTION__);
        return error;
    }

    if (!xbridge::xBridgeValidCoin(params[4].get_str())) {
        UniValue error(UniValue::VOBJ);
        error.pushKV("error",    xbridge::xbridgeErrorText(xbridge::INVALID_PARAMETERS,
                      "The taker_size is too precise. The maximum precision supported is " +
                              std::to_string(xbridge::xBridgeSignificantDigits(xbridge::TransactionDescr::COIN)) + " digits."));
        error.pushKV("code",     xbridge::INVALID_PARAMETERS);
        error.pushKV("name",     __FUNCTION__);
        return error;
    }

    std::string fromCurrency    = params[0].get_str();
    double      fromAmount      = boost::lexical_cast<double>(params[1].get_str());
    std::string fromAddress     = params[2].get_str();

    std::string toCurrency      = params[3].get_str();
    double      toAmount        = boost::lexical_cast<double>(params[4].get_str());
    std::string toAddress       = params[5].get_str();

    std::string type            = params[6].get_str();

    // Validate the order type
    if (type != "exact") {
        return xbridge::makeError(xbridge::INVALID_PARAMETERS, __FUNCTION__,
                               "Only the exact type is supported at this time.");
    }

    // Check that addresses are not the same
    if (fromAddress == toAddress) {
        return xbridge::makeError(xbridge::INVALID_PARAMETERS, __FUNCTION__,
                               "The maker_address and taker_address cannot be the same: " + fromAddress);
    }

    // Check upper limits
    if (fromAmount > (double)xbridge::TransactionDescr::MAX_COIN ||
            toAmount > (double)xbridge::TransactionDescr::MAX_COIN) {
        return xbridge::makeError(xbridge::INVALID_PARAMETERS, __FUNCTION__,
                               "The maximum supported size is " + std::to_string(xbridge::TransactionDescr::MAX_COIN));
    }
    // Check lower limits
    if (fromAmount <= 0 || toAmount <= 0) {
        return xbridge::makeError(xbridge::INVALID_PARAMETERS, __FUNCTION__,
                               "The minimum supported size is " + xbridge::xBridgeStringValueFromPrice(1.0/xbridge::TransactionDescr::COIN));
    }

    // Validate addresses
    xbridge::WalletConnectorPtr connFrom = xbridge::App::instance().connectorByCurrency(fromCurrency);
    xbridge::WalletConnectorPtr connTo   = xbridge::App::instance().connectorByCurrency(toCurrency);
    if (!connFrom) return xbridge::makeError(xbridge::NO_SESSION, __FUNCTION__, "Unable to connect to wallet: " + fromCurrency);
    if (!connTo) return xbridge::makeError(xbridge::NO_SESSION, __FUNCTION__, "Unable to connect to wallet: " + toCurrency);

    xbridge::App &app = xbridge::App::instance();

    if (!app.isValidAddress(fromAddress, connFrom)) {
        return xbridge::makeError(xbridge::INVALID_ADDRESS, __FUNCTION__, fromAddress);
    }
    if (!app.isValidAddress(toAddress, connTo)) {
        return xbridge::makeError(xbridge::INVALID_ADDRESS, __FUNCTION__, toAddress);
    }
    if(fromAmount <= .0) {
        return xbridge::makeError(xbridge::INVALID_PARAMETERS, __FUNCTION__,
                               "The maker_size must be greater than 0.");
    }
    if(toAmount <= .0) {
        return xbridge::makeError(xbridge::INVALID_PARAMETERS, __FUNCTION__,
                               "The taker_size must be greater than 0.");
    }

    bool useAllFunds = true;
    if (request.params.size() >= 8)
        useAllFunds = request.params[7].get_bool();

    // Perform explicit check on dryrun to avoid executing order on bad spelling
    bool dryrun = false;
    if (params.size() == 9) {
        std::string dryrunParam = params[8].get_str();
        if (dryrunParam != "dryrun") {
            return xbridge::makeError(xbridge::INVALID_PARAMETERS, __FUNCTION__, dryrunParam);
        }
        dryrun = true;
    }


    UniValue result(UniValue::VOBJ);
    auto statusCode = app.checkCreateParams(fromCurrency, toCurrency,
                                       xbridge::xBridgeAmountFromReal(fromAmount), fromAddress);
    switch (statusCode) {
    case xbridge::SUCCESS:{
        // If dryrun
        if (dryrun) {
            result.pushKV("id", uint256().GetHex());
            result.pushKV("maker", fromCurrency);
            result.pushKV("maker_size",
                                     xbridge::xBridgeStringValueFromAmount(xbridge::xBridgeAmountFromReal(fromAmount)));
            result.pushKV("maker_address", fromAddress);
            result.pushKV("taker", toCurrency);
            result.pushKV("taker_size",
                                     xbridge::xBridgeStringValueFromAmount(xbridge::xBridgeAmountFromReal(toAmount)));
            result.pushKV("taker_address", toAddress);
            result.pushKV("order_type", "exact");
            result.pushKV("partial_minimum","0");
            result.pushKV("partial_orig_maker_size", "0");
            result.pushKV("partial_orig_taker_size", "0");
            result.pushKV("partial_repost", false);
            result.pushKV("partial_parent_id", parseParentId(uint256()));
            result.pushKV("status", "created");
            return result;
        }
        break;
    }

    case xbridge::INVALID_CURRENCY: {
        return xbridge::makeError(statusCode, __FUNCTION__, fromCurrency);
    }
    case xbridge::NO_SESSION:{
        return xbridge::makeError(statusCode, __FUNCTION__, fromCurrency);
    }
    case xbridge::INSUFFICIENT_FUNDS:{
        return xbridge::makeError(statusCode, __FUNCTION__, fromAddress);
    }

    default:
        return xbridge::makeError(statusCode, __FUNCTION__);
    }

    uint256 id = uint256();
    uint256 blockHash = uint256();
    statusCode = xbridge::App::instance().sendXBridgeTransaction
          (fromAddress, fromCurrency, xbridge::xBridgeAmountFromReal(fromAmount),
           toAddress, toCurrency, xbridge::xBridgeAmountFromReal(toAmount), useAllFunds, id, blockHash);

    if (statusCode == xbridge::SUCCESS) {

        UniValue obj(UniValue::VOBJ);
        obj.pushKV("id",             id.GetHex());
        obj.pushKV("maker_address",  fromAddress);
        obj.pushKV("maker",          fromCurrency);
        obj.pushKV("maker_size",     xbridge::xBridgeStringValueFromAmount(xbridge::xBridgeAmountFromReal(fromAmount)));
        obj.pushKV("taker_address",  toAddress);
        obj.pushKV("taker",          toCurrency);
        obj.pushKV("taker_size",     xbridge::xBridgeStringValueFromAmount(xbridge::xBridgeAmountFromReal(toAmount)));
        const auto &createdTime = xbridge::App::instance().transaction(id)->created;
        obj.pushKV("created_at",     xbridge::iso8601(createdTime));
        obj.pushKV("updated_at",     xbridge::iso8601(boost::posix_time::microsec_clock::universal_time())); // TODO Need actual updated time, this is just estimate
        obj.pushKV("block_id",       blockHash.GetHex());
        obj.pushKV("order_type",     "exact");
        obj.pushKV("partial_minimum","0");
        obj.pushKV("partial_orig_maker_size", "0");
        obj.pushKV("partial_orig_taker_size", "0");
        obj.pushKV("partial_repost", false);
        obj.pushKV("partial_parent_id", parseParentId(uint256()));
        obj.pushKV("status",         "created");
        return obj;

    } else if (statusCode == xbridge::INSUFFICIENT_FUNDS) {
        return xbridge::makeError(statusCode, __FUNCTION__, fromAddress);
    } else {
        return xbridge::makeError(statusCode, __FUNCTION__);
    }
}

UniValue dxTakeOrder(const JSONRPCRequest& request) {
    if (request.fHelp || request.params.size() < 3 || request.params.size() > 5)
        throw std::runtime_error(
            RPCHelpMan{"dxTakeOrder",
                "\nThis call is used to take an order. You can only take orders for assets supported "
                "by your node (view with dxGetLocalTokens). Taking your own order is not supported. "
                "Taking an order has a 0.015 BLOCK fee.\n"
                "\nNote:\n"
                "XBridge will first attempt to use funds from the specified from_address. "
                "If this address does not have sufficient funds to cover the order, then "
                "it will pull funds from other addresses in the wallet. Change is "
                "deposited to the address with the largest input used.\n",
                {
                    {"id", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "The ID of the order being filled."},
                    {"from_address", RPCArg::Type::STR, RPCArg::Optional::NO, "The address containing asset being sent."},
                    {"to_address", RPCArg::Type::STR, RPCArg::Optional::NO, "The address for the receiving asset."},
                    {"amount", RPCArg::Type::STR, RPCArg::Optional::OMITTED, "The amount to take (allowed only on partial orders)"},
                    {"dryrun", RPCArg::Type::STR, RPCArg::Optional::OMITTED, "Simulate the order submission without actually submitting the order, i.e. a test run. Options: dryrun"},
                },
                RPCResult{
                R"(
    {
        "id": "4306aa07113c4562ffa6278ecd9a3990ead53a0227f74ddd9122272e453ae07d",
        "maker": "SYS",
        "maker_size": "0.100",
        "taker": "LTC",
        "taker_size": "0.01",
        "updated_at": "1970-01-01T00:00:00.00000Z",
        "created_at": "2018-01-15T18:15:30.12345Z",
        "order_type": "exact",
        "partial_minimum": "0.000000",
        "partial_repost": false,
        "status": "accepting"
    }

    Key             | Type | Description
    ----------------|------|-----------------------------------------------------
    id              | str  | The order ID.
    maker           | str  | Maker trading asset; the ticker of the asset being
                    |      | sold by the maker.
    maker_size      | str  | Maker trading size. String is used to preserve
                    |      | precision.
    taker           | str  | Taker trading asset; the ticker of the asset being
                    |      | sold by the taker.
    taker_size      | str  | Taker trading size. String is used to preserve
                    |      | precision.
    updated_at      | str  | ISO 8601 datetime, with microseconds, of the last
                    |      | time the order was updated.
    created_at      | str  | ISO 8601 datetime, with microseconds, of when the
                    |      | order was created.
    status          | str  | The order status.
                )"
                },
                RPCExamples{
                    HelpExampleCli("dxTakeOrder", "524137449d9a35fa707ee395abab32bedae91aa2aefb6e3611fcd8574863e432 LLZ1pgb6Jqx8hu84fcr5WC5HMoKRUsRE8H BWQrvmuHB4C68KH5V7fcn9bFtWN8y5hBmR")
                  + HelpExampleRpc("dxTakeOrder", "\"524137449d9a35fa707ee395abab32bedae91aa2aefb6e3611fcd8574863e432\", \"LLZ1pgb6Jqx8hu84fcr5WC5HMoKRUsRE8H\", \"BWQrvmuHB4C68KH5V7fcn9bFtWN8y5hBmR\"")
                  + HelpExampleCli("dxTakeOrder", "524137449d9a35fa707ee395abab32bedae91aa2aefb6e3611fcd8574863e432 LLZ1pgb6Jqx8hu84fcr5WC5HMoKRUsRE8H BWQrvmuHB4C68KH5V7fcn9bFtWN8y5hBmR 0.5")
                  + HelpExampleRpc("dxTakeOrder", "\"524137449d9a35fa707ee395abab32bedae91aa2aefb6e3611fcd8574863e432\", \"LLZ1pgb6Jqx8hu84fcr5WC5HMoKRUsRE8H\", \"BWQrvmuHB4C68KH5V7fcn9bFtWN8y5hBmR\", \"0.5\"")
                  + HelpExampleCli("dxTakeOrder", "524137449d9a35fa707ee395abab32bedae91aa2aefb6e3611fcd8574863e432 LLZ1pgb6Jqx8hu84fcr5WC5HMoKRUsRE8H BWQrvmuHB4C68KH5V7fcn9bFtWN8y5hBmR 0.5 dryrun")
                  + HelpExampleRpc("dxTakeOrder", "\"524137449d9a35fa707ee395abab32bedae91aa2aefb6e3611fcd8574863e432\", \"LLZ1pgb6Jqx8hu84fcr5WC5HMoKRUsRE8H\", \"BWQrvmuHB4C68KH5V7fcn9bFtWN8y5hBmR\", \"0.5\", \"dryrun\"")
                },
            }.ToString());

    uint256 id = uint256S(request.params[0].get_str());
    std::string fromAddress = request.params[1].get_str();
    std::string toAddress = request.params[2].get_str();

    xbridge::App &app = xbridge::App::instance();

    // Check that addresses are not the same
    if (fromAddress == toAddress) {
        return xbridge::makeError(xbridge::INVALID_PARAMETERS, __FUNCTION__,
                               "The from_address and to_address cannot be the same: " + fromAddress);
    }

    double amount{0};
    if (request.params.size() >= 4) {
        const auto amountStr = request.params[3].get_str();
        if (!amountStr.empty()) {
            amount = boost::lexical_cast<double>(amountStr);
            if (amount <= 0) {
                return xbridge::makeError(xbridge::INVALID_PARAMETERS, __FUNCTION__,
                        "The amount cannot be less than or equal to 0: " + request.params[3].get_str());
            }
        }
    }

    // Perform explicit check on dryrun to avoid executing order on bad spelling
    bool dryrun = false;
    if (request.params.size() == 5) {
        std::string dryrunParam = request.params[4].get_str();
        if (dryrunParam != "dryrun") {
            return xbridge::makeError(xbridge::INVALID_PARAMETERS, __FUNCTION__, dryrunParam);
        }
        dryrun = true;
    }

    UniValue result(UniValue::VOBJ);
    xbridge::Error statusCode;
    xbridge::TransactionDescrPtr txDescr = app.transaction(id);
    if (!txDescr) {
        WARN() << "transaction not found " << __FUNCTION__;
        return xbridge::makeError(xbridge::TRANSACTION_NOT_FOUND, __FUNCTION__);
    }

    CAmount fromSize = txDescr->toAmount;
    CAmount toSize = txDescr->fromAmount;

    // If no amount is specified on a partial order by default use the full
    // order sizes (will result in the entire partial order being taken).
    if (txDescr->isPartialOrderAllowed() && xbridge::xBridgeAmountFromReal(amount) > 0) {
        if (xbridge::xBridgeAmountFromReal(amount) < txDescr->minFromAmount) {
            return xbridge::makeError(xbridge::INVALID_PARAMETERS, __FUNCTION__, "The minimum amount for this order is: " +
                        xbridge::xBridgeStringValueFromAmount(txDescr->minFromAmount));
        } else if (xbridge::xBridgeAmountFromReal(amount) > txDescr->fromAmount) {
            return xbridge::makeError(xbridge::INVALID_PARAMETERS, __FUNCTION__, "The maximum amount for this order is: " +
                        xbridge::xBridgeStringValueFromAmount(txDescr->fromAmount));
        }
        if (xbridge::xBridgeAmountFromReal(amount) < toSize) {
            toSize = xbridge::xBridgeAmountFromReal(amount);
            fromSize = xbridge::xBridgeSourceAmountFromPrice(toSize, txDescr->toAmount, txDescr->fromAmount);
        }
    } else if (amount > 0) {
        WARN() << "partial orders are not allowed for this order " << __FUNCTION__;
        return xbridge::makeError(xbridge::INVALID_PARTIAL_ORDER, __FUNCTION__);
    }

    // Check taker sending coin balance (toCurrency here because the swap frame of reference hasn't occurred yet)
    statusCode = app.checkAcceptParams(txDescr->toCurrency, fromSize);

    switch (statusCode)
    {
    case xbridge::SUCCESS: {
        if (txDescr->isLocal()) // no self trades
            return xbridge::makeError(xbridge::INVALID_PARAMETERS, __FUNCTION__, "Unable to accept your own order.");

        // taker [to] will match order [from] currency (due to pair swap happening later)
        xbridge::WalletConnectorPtr connTo = xbridge::App::instance().connectorByCurrency(txDescr->fromCurrency);
        // taker [from] will match order [to] currency (due to pair swap happening later)
        xbridge::WalletConnectorPtr connFrom = xbridge::App::instance().connectorByCurrency(txDescr->toCurrency);
        if (!connFrom) return xbridge::makeError(xbridge::NO_SESSION, __FUNCTION__, "Unable to connect to wallet: " + txDescr->toCurrency);
        if (!connTo) return xbridge::makeError(xbridge::NO_SESSION, __FUNCTION__, "Unable to connect to wallet: " + txDescr->fromCurrency);
        // Check for valid toAddress
        if (!app.isValidAddress(toAddress, connTo))
            return xbridge::makeError(xbridge::INVALID_ADDRESS, __FUNCTION__,
                                   ": " + txDescr->fromCurrency + " address is bad. Are you using the correct address?");
        // Check for valid fromAddress
        if (!app.isValidAddress(fromAddress, connFrom))
            return xbridge::makeError(xbridge::INVALID_ADDRESS, __FUNCTION__,
                                   ": " + txDescr->toCurrency + " address is bad. Are you using the correct address?");

        if (dryrun) {
            result.pushKV("id", uint256().GetHex());
            result.pushKV("maker", txDescr->fromCurrency);
            result.pushKV("maker_size", xbridge::xBridgeStringValueFromAmount(fromSize));
            result.pushKV("taker", txDescr->toCurrency);
            result.pushKV("taker_size", xbridge::xBridgeStringValueFromAmount(toSize));
            result.pushKV("updated_at", xbridge::iso8601(boost::posix_time::microsec_clock::universal_time()));
            result.pushKV("created_at", xbridge::iso8601(txDescr->created));
            result.pushKV("order_type", txDescr->orderType());
            result.pushKV("partial_minimum", xbridge::xBridgeStringValueFromAmount(txDescr->minFromAmount));
            result.pushKV("partial_orig_maker_size", xbridge::xBridgeStringValueFromAmount(txDescr->origFromAmount));
            result.pushKV("partial_orig_taker_size", xbridge::xBridgeStringValueFromAmount(txDescr->origToAmount));
            result.pushKV("partial_repost", txDescr->repostOrder);
            result.pushKV("partial_parent_id", parseParentId(txDescr->getParentOrder()));
            result.pushKV("status", "filled");
            return result;
        }

        break;
    }
    case xbridge::TRANSACTION_NOT_FOUND:
    {
        return xbridge::makeError(xbridge::TRANSACTION_NOT_FOUND, __FUNCTION__, id.ToString());
    }

    case xbridge::NO_SESSION:
    {
        return xbridge::makeError(xbridge::NO_SESSION, __FUNCTION__, txDescr->toCurrency);
    }

    case xbridge::INSUFFICIENT_FUNDS:
    {
        return xbridge::makeError(xbridge::INSUFFICIENT_FUNDS, __FUNCTION__, fromAddress);
    }

    default:
        return xbridge::makeError(statusCode, __FUNCTION__);
    }

    // TODO swap is destructive on state (also complicates historical data)
    std::swap(txDescr->fromCurrency, txDescr->toCurrency);
    std::swap(txDescr->fromAmount, txDescr->toAmount);

    statusCode = app.acceptXBridgeTransaction(id, fromAddress, toAddress, fromSize, toSize);
    if (statusCode == xbridge::SUCCESS) {
        result.pushKV("id", id.GetHex());
        result.pushKV("maker", txDescr->fromCurrency);
        result.pushKV("maker_size", xbridge::xBridgeStringValueFromAmount(fromSize));
        result.pushKV("taker", txDescr->toCurrency);
        result.pushKV("taker_size", xbridge::xBridgeStringValueFromAmount(toSize));
        result.pushKV("updated_at", xbridge::iso8601(boost::posix_time::microsec_clock::universal_time()));
        result.pushKV("created_at", xbridge::iso8601(txDescr->created));
        result.pushKV("order_type", txDescr->orderType());
        result.pushKV("partial_minimum", xbridge::xBridgeStringValueFromAmount(txDescr->minFromAmount));
        result.pushKV("partial_orig_maker_size", xbridge::xBridgeStringValueFromAmount(txDescr->origFromAmount));
        result.pushKV("partial_orig_taker_size", xbridge::xBridgeStringValueFromAmount(txDescr->origToAmount));
        result.pushKV("partial_repost", txDescr->repostOrder);
        result.pushKV("partial_parent_id", parseParentId(txDescr->getParentOrder()));
        result.pushKV("status", txDescr->strState());
        return result;
    } else {
        // restore state on error
        txDescr->fromCurrency = txDescr->origFromCurrency;
        txDescr->fromAmount = txDescr->origFromAmount;
        txDescr->toCurrency = txDescr->origToCurrency;
        txDescr->toAmount = txDescr->origToAmount;
        return xbridge::makeError(statusCode, __FUNCTION__);
    }
}

UniValue dxCancelOrder(const JSONRPCRequest& request)
{
    if(request.fHelp)
        throw std::runtime_error(
            RPCHelpMan{"dxCancelOrder",
                "\nThis call is used to cancel one of your own orders. This automatically "
                "rolls back the order if a trade is in process.\n",
                {
                    {"id", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "The ID of the order to cancel."},
                },
                RPCResult{
                R"(
    {
        "id": "91d0ea83edc79b9a2041c51d08037cff87c181efb311a095dfdd4edbcc7993a9",
        "maker": "SYS",
        "maker_size": "0.100",
        "maker_address": "SVTbaYZ8oApVn3uNyimst3GKyvvfzXQgdK",
        "taker": "LTC",
        "taker_size": "0.01",
        "taker_address": "LVvFhzRoMRGTtGihHp7jVew3YoZRX8y35Z",
        "updated_at": "1970-01-01T00:00:00.00000Z",
        "created_at": "2018-01-15T18:15:30.12345Z",
        "status": "canceled"
    }

    Key             | Type | Description
    ----------------|------|-----------------------------------------------------
    id              | str  | The order ID.
    maker           | str  | Sending asset of party cancelling the order.
    maker_size      | str  | Sending trading size. String is used to preserve
                    |      | precision.
    maker_address   | str  | Address for sending the outgoing asset.
    taker           | str  | Receiving asset of party cancelling the order.
    taker_size      | str  | Receiving trading size. String is used to preserve
                    |      | precision.
    taker_address   | str  | Address for receiving the incoming asset.
    updated_at      | str  | ISO 8601 datetime, with microseconds, of the last
                    |      | time the order was updated.
    created_at      | str  | ISO 8601 datetime, with microseconds, of when the
                    |      | order was created.
    status          | str  | The order status (canceled).
                )"
                },
                RPCExamples{
                    HelpExampleCli("dxCancelOrder", "524137449d9a35fa707ee395abab32bedae91aa2aefb6e3611fcd8574863e432")
                  + HelpExampleRpc("dxCancelOrder", "\"524137449d9a35fa707ee395abab32bedae91aa2aefb6e3611fcd8574863e432\"")
                },
            }.ToString());
    const UniValue &params = request.params;

    if (params.size() != 1)
    {
        return xbridge::makeError(xbridge::INVALID_PARAMETERS, __FUNCTION__, "(id)");
    }

    LOG() << "rpc cancel order " << __FUNCTION__;
    const auto sid = params[0].get_str();
    if (uint256S(sid).IsNull())
        return xbridge::makeError(xbridge::INVALID_PARAMETERS, __FUNCTION__, strprintf("Invalid order id [%s]", sid));

    uint256 id = uint256S(sid);

    xbridge::TransactionDescrPtr tx = xbridge::App::instance().transaction(id);
    if (!tx)
    {
        return xbridge::makeError(xbridge::TRANSACTION_NOT_FOUND, __FUNCTION__, id.ToString());
    }

    if (tx->state >= xbridge::TransactionDescr::trCreated)
    {
        return xbridge::makeError(xbridge::INVALID_STATE, __FUNCTION__, "The order is already " + tx->strState());
    }

    const auto res = xbridge::App::instance().cancelXBridgeTransaction(id, crRpcRequest);
    if (res != xbridge::SUCCESS)
    {
        return xbridge::makeError(res, __FUNCTION__);
    }

    xbridge::WalletConnectorPtr connFrom = xbridge::App::instance().connectorByCurrency(tx->fromCurrency);
    xbridge::WalletConnectorPtr connTo   = xbridge::App::instance().connectorByCurrency(tx->toCurrency);
    if (!connFrom) {
        return xbridge::makeError(xbridge::NO_SESSION, __FUNCTION__, tx->fromCurrency);
    }

    if (!connTo) {
        return xbridge::makeError(xbridge::NO_SESSION, __FUNCTION__, tx->toCurrency);
    }
    UniValue obj(UniValue::VOBJ);
    obj.pushKV("id", id.GetHex());

    obj.pushKV("maker", tx->fromCurrency);
    obj.pushKV("maker_size", xbridge::xBridgeStringValueFromAmount(tx->fromAmount));
    obj.pushKV("maker_address", connFrom->fromXAddr(tx->from));

    obj.pushKV("taker", tx->toCurrency);
    obj.pushKV("taker_size", xbridge::xBridgeStringValueFromAmount(tx->toAmount));
    obj.pushKV("taker_address", connTo->fromXAddr(tx->to));
    obj.pushKV("refund_tx", tx->refTx);

    obj.pushKV("updated_at", xbridge::iso8601(tx->txtime));
    obj.pushKV("created_at", xbridge::iso8601(tx->created));

    obj.pushKV("status", tx->strState());
    return obj;
}

UniValue dxFlushCancelledOrders(const JSONRPCRequest& request)
{
    if(request.fHelp)
        throw std::runtime_error(
            RPCHelpMan{"dxFlushCancelledOrders",
                "\nThis call is used to remove your cancelled orders that are older than the specified amount of time.\n",
                {
                    {"ageMillis", RPCArg::Type::NUM, "0", "Remove cancelled orders older than this amount of milliseconds."},
                },
                RPCResult{
                R"(
    {
        "ageMillis": 0,
        "now": "20191126T024005.352285",
        "durationMicrosec": 0,
        "flushedOrders": [
            {
                "id": "582a02ada05c8a4bb39b34de0eb54767bcb95a7792e5865d3a0babece4715f47",
                "txtime": "20191126T023945.855058",
                "use_count": 1
            },
            {
                "id": "a508cd8d110bdc0b1fd819a89d94cdbf702e3aa40edbe654af5d556ff3c43a0a",
                "txtime": "20191126T023956.270409",
                "use_count": 1
            }
        ]
    }

    Key               | Type | Description
    ------------------|------|---------------------------------------------------
    ageMillis         | int  | Millisecond value specified when making the call.
    now               | str  | ISO 8601 datetime, with microseconds, of when the
                      |      | call was executed.
    durationMicrosec* | int  | The amount of time in milliseconds it took to
                      |      | process the call.
    flushedOrders     | arr  | Array of cancelled orders that were removed.
    id                | str  | The order ID.
    txtime            | str  | ISO 8601 datetime, with microseconds, of when the
                      |      | order was created.
    use_count*        | int  | This value is strictly for debugging purposes.
                )"
                },
                RPCExamples{
                    HelpExampleCli("dxFlushCancelledOrders", "")
                  + HelpExampleRpc("dxFlushCancelledOrders", "")
                  + HelpExampleCli("dxFlushCancelledOrders", "600000")
                  + HelpExampleRpc("dxFlushCancelledOrders", "600000")
                },
            }.ToString());
    const UniValue &params = request.params;

    const int ageMillis = params.size() == 0
        ? 0
        : (params.size() == 1 ? params[0].get_int() : -1);

    if (ageMillis < 0)
    {
        return xbridge::makeError(xbridge::INVALID_PARAMETERS, __FUNCTION__,
                               "ageMillis must be an integer >= 0");
    }

    const auto minAge = boost::posix_time::millisec{ageMillis};

    LOG() << "rpc flush cancelled orders older than " << minAge << ": " << __FUNCTION__;

    const auto now = boost::posix_time::microsec_clock::universal_time();
    const auto list = xbridge::App::instance().flushCancelledOrders(minAge);
    const auto micros = boost::posix_time::time_duration{ boost::posix_time::microsec_clock::universal_time() - now };

    UniValue result(UniValue::VOBJ);
    result.pushKV("ageMillis",        ageMillis);
    result.pushKV("now",              xbridge::iso8601(now));
    result.pushKV("durationMicrosec", static_cast<int>(micros.total_microseconds()));
    UniValue a(UniValue::VARR);
    for(const auto & it : list) {
        UniValue o(UniValue::VOBJ);
        o.pushKV("id",        it.id.GetHex());
        o.pushKV("txtime",    xbridge::iso8601(it.txtime));
        o.pushKV("use_count", it.use_count);
        a.push_back(o);
    }
    result.pushKV("flushedOrders", a);
    return result;
}

UniValue dxGetOrderBook(const JSONRPCRequest& request)
{
    if (request.fHelp)
        throw std::runtime_error(
            RPCHelpMan{"dxGetOrderBook",
                "\nThis call is used to retrieve open orders at various detail levels:\n"
                "\nDetail 1 - Returns the best bid and ask.\n"
                "Detail 2 - Returns a list of aggregated orders. This is useful for charting.\n"
                "Detail 3 - Returns a list of non-aggregated orders. This is useful for bot trading.\n"
                "Detail 4 - Returns the best bid and ask with the order IDs.\n"
                "\nNote:\n"
                "This call will only return orders for markets with both assets supported by your "
                "node (view with dxGetLocalTokens). To view all orders, set ShowAllOrders=true in "
                "your xbridge.conf header and reload it with dxLoadXBridgeConf.\n",
                {
                    {"detail", RPCArg::Type::NUM, RPCArg::Optional::NO, "The detail level."},
                    {"maker", RPCArg::Type::STR, RPCArg::Optional::NO, "The symbol of the token being sold by the maker (e.g. LTC)."},
                    {"taker", RPCArg::Type::STR, RPCArg::Optional::NO, "The symbol of the token being sold by the taker (e.g. BLOCK)."},
                    {"max_orders", RPCArg::Type::NUM, "50", "The maximum total orders to display for bids and asks combined."},
                },
                RPCResult{
                "\n"
                },
                RPCExamples{
                    HelpExampleCli("dxGetOrderBook", "3 BLOCK LTC")
                  + HelpExampleRpc("dxGetOrderBook", "3, \"BLOCK\", \"LTC\"")
                  + HelpExampleCli("dxGetOrderBook", "3 BLOCK LTC 60")
                  + HelpExampleRpc("dxGetOrderBook", "3, \"BLOCK\", \"LTC\", 60")
                },
            }.ToString());
    const UniValue &params = request.params;

    if ((params.size() < 3 || params.size() > 4))
    {
        return xbridge::makeError(xbridge::INVALID_PARAMETERS, __FUNCTION__,
                               "(detail, 1-4) (maker) (taker) (max_orders, default=50)[optional]");
    }

    UniValue res(UniValue::VOBJ);
    TransactionMap trList = xbridge::App::instance().transactions();
    {
        /**
         * @brief detaiLevel - Get a list of open orders for a product.
         * The amount of detail shown can be customized with the level parameter.
         */
        const auto detailLevel  = params[0].get_int();
        const auto fromCurrency = params[1].get_str();
        const auto toCurrency   = params[2].get_str();

        std::size_t maxOrders = 50;

        if (params.size() == 4)
            maxOrders = params[3].get_int();

        if (maxOrders < 1)
            maxOrders = 1;

        if (detailLevel < 1 || detailLevel > 4)
        {
            return xbridge::makeError(xbridge::INVALID_DETAIL_LEVEL, __FUNCTION__);
        }

        res.pushKV("detail", detailLevel);
        res.pushKV("maker", fromCurrency);
        res.pushKV("taker", toCurrency);

        /**
         * @brief bids - array with bids
         */
        UniValue bids(UniValue::VARR);
        /**
         * @brief asks - array with asks
         */
        UniValue asks(UniValue::VARR);

        if(trList.empty())
        {
            LOG() << "empty transactions list";
            res.pushKV("asks", asks);
            res.pushKV("bids", bids);
            return res;
        }

        TransactionMap asksList;
        TransactionMap bidsList;

        //copy all transactions in currencies specified in the parameters

        // ask orders are based in the first token in the trading pair
        std::copy_if(trList.begin(), trList.end(), std::inserter(asksList, asksList.end()),
                     [&toCurrency, &fromCurrency](const TransactionPair &transaction)
        {
            if(transaction.second == nullptr)
                return false;
            if (transaction.second->fromAmount <= 0 || transaction.second->toAmount <= 0)
                return false;
            if (transaction.second->state != xbridge::TransactionDescr::trPending)
                return false;

            return  ( boost::iequals(transaction.second->toCurrency, toCurrency) &&
                      boost::iequals(transaction.second->fromCurrency, fromCurrency) );
        });

        // bid orders are based in the second token in the trading pair (inverse of asks)
        std::copy_if(trList.begin(), trList.end(), std::inserter(bidsList, bidsList.end()),
                     [&toCurrency, &fromCurrency](const TransactionPair &transaction)
        {
            if(transaction.second == nullptr)
                return false;
            if (transaction.second->fromAmount <= 0 || transaction.second->toAmount <= 0)
                return false;
            if (transaction.second->state != xbridge::TransactionDescr::trPending)
                return false;

            return  ( boost::iequals(transaction.second->toCurrency, fromCurrency) &&
                      boost::iequals(transaction.second->fromCurrency, toCurrency));
        });

        std::vector<xbridge::TransactionDescrPtr> asksVector;
        std::vector<xbridge::TransactionDescrPtr> bidsVector;

        for (const auto &trEntry : asksList)
            asksVector.emplace_back(trEntry.second);

        for (const auto &trEntry : bidsList)
            bidsVector.emplace_back(trEntry.second);

        // sort asks descending
        std::sort(asksVector.begin(), asksVector.end(),
                  [](const xbridge::TransactionDescrPtr &a, const xbridge::TransactionDescrPtr &b)
        {
            const auto priceA = xbridge::price(a);
            const auto priceB = xbridge::price(b);
            return priceA > priceB;
        });

        //sort bids descending
        std::sort(bidsVector.begin(), bidsVector.end(),
                  [](const xbridge::TransactionDescrPtr &a, const xbridge::TransactionDescrPtr &b)
        {
            const auto priceA = xbridge::priceBid(a);
            const auto priceB = xbridge::priceBid(b);
            return priceA > priceB;
        });

        // floating point comparisons
        // see Knuth 4.2.2 Eq 36
        auto floatCompare = [](const double a, const double b) -> bool
        {
            const auto epsilon = std::numeric_limits<double>::epsilon();
            return (fabs(a - b) / fabs(a) <= epsilon) && (fabs(a - b) / fabs(b) <= epsilon);
        };

        switch (detailLevel)
        {
        case 1:
        {
            //return only the best bid and ask
            if (!bidsList.empty()) {
                const auto bidsItem = std::max_element(bidsList.begin(), bidsList.end(),
                                       [](const TransactionPair &a, const TransactionPair &b)
                {
                    //find transaction with best bids
                    const auto &tr1 = a.second;
                    const auto &tr2 = b.second;

                    if(tr1 == nullptr)
                        return true;

                    if(tr2 == nullptr)
                        return false;

                    const auto priceA = xbridge::priceBid(tr1);
                    const auto priceB = xbridge::priceBid(tr2);

                    return priceA < priceB;
                });

                const auto bidsCount = std::count_if(bidsList.begin(), bidsList.end(),
                                                     [bidsItem, floatCompare](const TransactionPair &a)
                {
                    const auto &tr = a.second;

                    if(tr == nullptr)
                        return false;

                    const auto price = xbridge::priceBid(tr);

                    const auto &bestTr = bidsItem->second;
                    if (bestTr != nullptr)
                    {
                        const auto bestBidPrice = xbridge::priceBid(bestTr);
                        return floatCompare(price, bestBidPrice);
                    }

                    return false;
                });

                const auto &tr = bidsItem->second;
                if (tr != nullptr)
                {
                    const auto bidPrice = xbridge::priceBid(tr);
                    UniValue bid(UniValue::VARR);
                    bid.push_back(xbridge::xBridgeStringValueFromPrice(bidPrice));
                    bid.push_back(xbridge::xBridgeStringValueFromAmount(tr->toAmount));
                    bid.push_back(static_cast<int64_t>(bidsCount));
                    bids.push_back(bid);
                }
            }

            if (!asksList.empty()) {
                const auto asksItem = std::min_element(asksList.begin(), asksList.end(),
                                                   [](const TransactionPair &a, const TransactionPair &b)
                {
                    //find transactions with best asks
                    const auto &tr1 = a.second;
                    const auto &tr2 = b.second;

                    if(tr1 == nullptr)
                        return true;

                    if(tr2 == nullptr)
                        return false;

                    const auto priceA = xbridge::price(tr1);
                    const auto priceB = xbridge::price(tr2);
                    return priceA < priceB;
                });

                const auto asksCount = std::count_if(asksList.begin(), asksList.end(),
                                                     [asksItem, floatCompare](const TransactionPair &a)
                {
                    const auto &tr = a.second;

                    if(tr == nullptr)
                        return false;

                    const auto price = xbridge::price(tr);

                    const auto &bestTr = asksItem->second;
                    if (bestTr != nullptr)
                    {
                        const auto bestAskPrice = xbridge::price(bestTr);
                        return floatCompare(price, bestAskPrice);
                    }

                    return false;
                });

                const auto &tr = asksItem->second;
                if (tr != nullptr)
                {
                    const auto askPrice = xbridge::price(tr);
                    UniValue ask(UniValue::VARR);
                    ask.push_back(xbridge::xBridgeStringValueFromPrice(askPrice));
                    ask.push_back(xbridge::xBridgeStringValueFromAmount(tr->fromAmount));
                    ask.push_back(static_cast<int64_t>(asksCount));
                    asks.push_back(ask);
                }
            }

            res.pushKV("asks", asks);
            res.pushKV("bids", bids);
            return res;
        }
        case 2:
        {
            //Top X bids and asks (aggregated)

            /**
             * @brief bound - calculate upper bound
             */
            auto bound = std::min<int32_t>(maxOrders, bidsVector.size());
            for (size_t i = 0; i < bound; ++i) // Best bids are at the beginning of the stack (sorted descending, highest price better)
            {
                if(bidsVector[i] == nullptr)
                    continue;

                UniValue bid(UniValue::VARR);
                //calculate bids and push to array
                const auto bidAmount    = bidsVector[i]->toAmount;
                const auto bidPrice     = xbridge::priceBid(bidsVector[i]);
                auto bidSize            = bidAmount;
                const auto bidsCount    = std::count_if(bidsList.begin(), bidsList.end(),
                                                     [bidPrice, floatCompare](const TransactionPair &a)
                {
                    const auto &tr = a.second;

                    if(tr == nullptr)
                        return false;

                    const auto price = xbridge::priceBid(tr);

                    return floatCompare(price, bidPrice);
                });
                //array sorted by bid price, we can to skip the transactions with equals bid price
                while((++i < bound) && floatCompare(xbridge::priceBid(bidsVector[i]), bidPrice)) {
                    bidSize += bidsVector[i]->toAmount;
                }
                bid.push_back(xbridge::xBridgeStringValueFromPrice(bidPrice));
                bid.push_back(xbridge::xBridgeStringValueFromAmount(bidSize));
                bid.push_back(static_cast<int64_t>(bidsCount));
                bids.push_back(bid);
            }

            bound = std::min<int32_t>(maxOrders, asksVector.size());
            const auto asks_len = static_cast<int32_t>(asksVector.size());
            for (int32_t i = asks_len - bound; i < asks_len; ++i) // Best asks are at the back of the stack (sorted descending, lowest price better)
            {
                if(asksVector[i] == nullptr)
                    continue;

                UniValue ask(UniValue::VARR);
                //calculate asks and push to array
                const auto askAmount    = asksVector[i]->fromAmount;
                const auto askPrice     = xbridge::price(asksVector[i]);
                auto askSize            = askAmount;
                const auto asksCount    = std::count_if(asksList.begin(), asksList.end(),
                                                     [askPrice, floatCompare](const TransactionPair &a)
                {
                    const auto &tr = a.second;

                    if(tr == nullptr)
                        return false;

                    const auto price = xbridge::price(tr);

                    return floatCompare(price, askPrice);
                });

                //array sorted by price, we can to skip the transactions with equals price
                while((++i < bound) && floatCompare(xbridge::price(asksVector[i]), askPrice)){
                    askSize += asksVector[i]->fromAmount;
                }
                ask.push_back(xbridge::xBridgeStringValueFromPrice(askPrice));
                ask.push_back(xbridge::xBridgeStringValueFromAmount(askSize));
                ask.push_back(static_cast<int64_t>(asksCount));
                asks.push_back(ask);
            }

            res.pushKV("asks", asks);
            res.pushKV("bids", bids);
            return res;
        }
        case 3:
        {
            //Full order book (non aggregated)
            auto bound = std::min<int32_t>(maxOrders, bidsVector.size());
            for (size_t i = 0; i < bound; ++i) // Best bids are at the beginning of the stack (sorted descending, highest price better)
            {
                if(bidsVector[i] == nullptr)
                    continue;

                UniValue bid(UniValue::VARR);
                const auto bidAmount   = bidsVector[i]->toAmount;
                const auto bidPrice    = xbridge::priceBid(bidsVector[i]);
                bid.push_back(xbridge::xBridgeStringValueFromPrice(bidPrice));
                bid.push_back(xbridge::xBridgeStringValueFromAmount(bidAmount));
                bid.push_back(bidsVector[i]->id.GetHex());

                bids.push_back(bid);
            }

            bound = std::min<int32_t>(maxOrders, asksVector.size());
            const auto asks_len = static_cast<int32_t>(asksVector.size());
            for (int32_t i = asks_len - bound; i < asks_len; ++i) // Best asks are at the back of the stack (sorted descending, lowest price better)
            {
                if(asksVector[i] == nullptr)
                    continue;

                UniValue ask(UniValue::VARR);
                const auto bidAmount    = asksVector[i]->fromAmount;
                const auto askPrice     = xbridge::price(asksVector[i]);
                ask.push_back(xbridge::xBridgeStringValueFromPrice(askPrice));
                ask.push_back(xbridge::xBridgeStringValueFromAmount(bidAmount));
                ask.push_back(asksVector[i]->id.GetHex());

                asks.push_back(ask);
            }

            res.pushKV("asks", asks);
            res.pushKV("bids", bids);
            return res;
        }
        case 4:
        {
            //return Only the best bid and ask
            if (!bidsList.empty()) {
                const auto bidsItem = std::max_element(bidsList.begin(), bidsList.end(),
                                           [](const TransactionPair &a, const TransactionPair &b)
                {
                    //find transaction with best bids
                    const auto &tr1 = a.second;
                    const auto &tr2 = b.second;

                    if(tr1 == nullptr)
                        return true;

                    if(tr2 == nullptr)
                        return false;

                    const auto priceA = xbridge::priceBid(tr1);
                    const auto priceB = xbridge::priceBid(tr2);

                    return priceA < priceB;
                });

                const auto &tr = bidsItem->second;
                if (tr != nullptr)
                {
                    const auto bidPrice = xbridge::priceBid(tr);
                    bids.push_back(xbridge::xBridgeStringValueFromPrice(bidPrice));
                    bids.push_back(xbridge::xBridgeStringValueFromAmount(tr->toAmount));

                    UniValue bidsIds(UniValue::VARR);
                    bidsIds.push_back(tr->id.GetHex());

                    for(const TransactionPair &tp : bidsList)
                    {
                        const auto &otherTr = tp.second;

                        if(otherTr == nullptr)
                            continue;

                        if(tr->id == otherTr->id)
                            continue;

                        const auto otherTrBidPrice = xbridge::priceBid(otherTr);

                        if(!floatCompare(bidPrice, otherTrBidPrice))
                            continue;

                        bidsIds.push_back(otherTr->id.GetHex());
                    }

                    bids.push_back(bidsIds);
                }
            }

            if (!asksList.empty()) {
                const auto asksItem = std::min_element(asksList.begin(), asksList.end(),
                                                   [](const TransactionPair &a, const TransactionPair &b)
                {
                    //find transactions with best asks
                    const auto &tr1 = a.second;
                    const auto &tr2 = b.second;

                    if(tr1 == nullptr)
                        return true;

                    if(tr2 == nullptr)
                        return false;

                    const auto priceA = xbridge::price(tr1);
                    const auto priceB = xbridge::price(tr2);
                    return priceA < priceB;
                });

                const auto &tr = asksItem->second;
                if (tr != nullptr)
                {
                    const auto askPrice = xbridge::price(tr);
                    asks.push_back(xbridge::xBridgeStringValueFromPrice(askPrice));
                    asks.push_back(xbridge::xBridgeStringValueFromAmount(tr->fromAmount));

                    UniValue asksIds(UniValue::VARR);
                    asksIds.push_back(tr->id.GetHex());

                    for(const TransactionPair &tp : asksList)
                    {
                        const auto &otherTr = tp.second;

                        if(otherTr == nullptr)
                            continue;

                        if(tr->id == otherTr->id)
                            continue;

                        const auto otherTrAskPrice = xbridge::price(otherTr);

                        if(!floatCompare(askPrice, otherTrAskPrice))
                            continue;

                        asksIds.push_back(otherTr->id.GetHex());
                    }

                    asks.push_back(asksIds);
                }
            }

            res.pushKV("asks", asks);
            res.pushKV("bids", bids);
            return res;
        }

        default:
            return xbridge::makeError(xbridge::INVALID_DETAIL_LEVEL, __FUNCTION__);
        }
    }
}

UniValue dxGetMyOrders(const JSONRPCRequest& request)
{
    if (request.fHelp)
        throw std::runtime_error(
            RPCHelpMan{"dxGetMyOrders",
                "\nReturns a list of all of your orders (of all states). "
                "It will only return orders from your current session.\n",
                {},
                RPCResult{
                R"(
    [
        {
            "id": "91d0ea83edc79b9a2041c51d08037cff87c181efb311a095dfdd4edbcc7993a9",
            "maker": "SYS",
            "maker_size": "100.000000",
            "maker_address": "SVTbaYZ8olpVn3uNyImst3GKyrvfzXQgdK",
            "taker": "LTC",
            "taker_size": "10.500000",
            "taker_address": "LVvFhZroMRGTtg1hHp7jVew3YoZRX8y35Z",
            "updated_at": "2018-01-15T18:25:05.12345Z",
            "created_at": "2018-01-15T18:15:30.12345Z",
            "order_type": "partial",
            "partial_minimum": "10.000000",
            "partial_orig_maker_size": "100.000000",
            "partial_orig_taker_size": "10.500000",
            "partial_repost": true,
            "partial_parent_id": "",
            "status": "open"
        },
        {
            "id": "6be548bc46a3dcc69b6d56529948f7e679dd96657f85f5870a017e005caa050a",
            "maker": "SYS",
            "maker_size": "4.000000",
            "maker_address": "SVTbaYZ8olpVn3uNyImst3GKyrvfzXQgdK",
            "taker": "LTC",
            "taker_size": "0.400000",
            "taker_address": "LVvFhZroMRGTtg1hHp7jVew3YoZRX8y35Z",
            "updated_at": "2018-01-15T18:25:05.12345Z",
            "created_at": "2018-01-15T18:15:30.12345Z",
            "order_type": "partial",
            "partial_minimum": "0.400000",
            "partial_orig_maker_size": "4.000000",
            "partial_orig_taker_size": "0.400000",
            "partial_repost": true,
            "partial_parent_id": "91d0ea83edc79b9a2041c51d08037cff87c181efb311a095dfdd4edbcc7993a9",
            "status": "open"
        }
    ]

    Key                     | Type | Description
    ------------------------|------|---------------------------------------------
    Array                   | arr  | An array of all orders with each order
                            |      | having the following parameters.
    id                      | str  | The order ID.
    maker                   | str  | Maker trading asset; the ticker of the asset
                            |      | being sold by the maker.
    maker_size              | str  | Maker trading size. String is used to
                            |      | preserve precision.
    maker_address           | str  | Address for sending the outgoing asset.
    taker                   | str  | Taker trading asset; the ticker of the asset
                            |      | being sold by the taker.
    taker_size              | str  | Taker trading size. String is used to
                            |      | preserve precision.
    taker_address           | str  | Address for receiving the incoming asset.
    updated_at              | str  | ISO 8601 datetime, with microseconds, of the
                            |      | last time the order was updated.
    created_at              | str  | ISO 8601 datetime, with microseconds, of
                            |      | when the order was created.
    order_type              | str  | The order type.
    partial_minimum*        | str  | The minimum amount that can be taken.
    partial_orig_maker_size*| str  | The partial order original maker_size.
    partial_orig_taker_size*| str  | The partial order original taker_size.
    partial_repost          | str  | Whether the order will be reposted or not.
                            |      | This applies to `partial` order types and
                            |      | will show `false` for `exact` order types.
    partial_parent_id       | str  | The previous order id of a reposted partial
                            |      | order. This will return an empty string if
                            |      | there is no parent order.
    status                  | str  | The order status.

    * This only applies to `partial` order types and will show `0` on `exact`
      order types.
                )"
                },
                RPCExamples{
                    HelpExampleCli("dxGetMyOrders", "")
                  + HelpExampleRpc("dxGetMyOrders", "")
                },
            }.ToString());
    const UniValue &params = request.params;

    if (!params.empty()) {

        UniValue error(UniValue::VOBJ);
        error.pushKV("error",
                                            xbridge::xbridgeErrorText(xbridge::INVALID_PARAMETERS,
                                                                      "This function does not accept any parameters."));
        error.pushKV("code",     xbridge::INVALID_PARAMETERS);
        error.pushKV("name",     __FUNCTION__);
        return error;

    }

    xbridge::App & xapp = xbridge::App::instance();

    UniValue r(UniValue::VARR);
    TransactionVector orders;

    TransactionMap trList = xbridge::App::instance().transactions();

    // Filter local orders
    for (auto i : trList) {
        const xbridge::TransactionDescrPtr &t = i.second;
        if(!t->isLocal())
            continue;
        orders.push_back(t);
    }

    // Add historical orders
    TransactionMap history = xbridge::App::instance().history();

    // Filter local orders only
    for (auto &item : history) {
        const xbridge::TransactionDescrPtr &ptr = item.second;
        if (ptr->isLocal() &&
                (ptr->state == xbridge::TransactionDescr::trFinished ||
                 ptr->state == xbridge::TransactionDescr::trCancelled)) {
            orders.push_back(ptr);
        }
    }

    // Return if no records
    if (orders.empty())
        return r;

    // sort ascending by updated time
    std::sort(orders.begin(), orders.end(),
        [](const xbridge::TransactionDescrPtr &a,  const xbridge::TransactionDescrPtr &b) {
            return (a->txtime) < (b->txtime);
        });

    std::map<std::string, bool> seen;
    for (const auto &t : orders) {
        // do not process already seen orders
        if (seen.count(t->id.GetHex()))
            continue;
        seen[t->id.GetHex()] = true;

        xbridge::WalletConnectorPtr connFrom = xapp.connectorByCurrency(t->fromCurrency);
        xbridge::WalletConnectorPtr connTo   = xapp.connectorByCurrency(t->toCurrency);

        std::string makerAddress;
        std::string takerAddress;
        if (connFrom)
            makerAddress = connFrom->fromXAddr(t->from);
        if (connTo)
            takerAddress = connTo->fromXAddr(t->to);

        UniValue o(UniValue::VOBJ);
        o.pushKV("id", t->id.GetHex());

        // maker data
        o.pushKV("maker", t->fromCurrency);
        o.pushKV("maker_size", xbridge::xBridgeStringValueFromAmount(t->fromAmount));
        o.pushKV("maker_address", makerAddress);
        // taker data
        o.pushKV("taker", t->toCurrency);
        o.pushKV("taker_size", xbridge::xBridgeStringValueFromAmount(t->toAmount));
        o.pushKV("taker_address", takerAddress);
        // dates
        o.pushKV("updated_at", xbridge::iso8601(t->txtime));
        o.pushKV("created_at", xbridge::iso8601(t->created));
        // partial order details
        o.pushKV("order_type", t->orderType());
        o.pushKV("partial_minimum", xbridge::xBridgeStringValueFromAmount(t->minFromAmount));
        o.pushKV("partial_orig_maker_size", xbridge::xBridgeStringValueFromAmount(t->origFromAmount));
        o.pushKV("partial_orig_taker_size", xbridge::xBridgeStringValueFromAmount(t->origToAmount));
        o.pushKV("partial_repost", t->repostOrder);
        o.pushKV("partial_parent_id", parseParentId(t->getParentOrder()));
        o.pushKV("status", t->strState());

        r.push_back(o);
    }

    return r;
}

UniValue dxGetMyPartialOrderChain(const JSONRPCRequest& request) {
    if (request.fHelp || request.params.empty() || request.params.size() > 1)
        throw std::runtime_error(
            RPCHelpMan{"dxGetMyPartialOrderChain",
                "\nReturns a list of all orders related to the specified "
                "order id. This includes partial orders that were repost "
                "from a parent order.\n",
                {
                    {"order_id", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "Order id"},
                },
                RPCResult{
                R"(
    [
        {
            "id": "91d0ea83edc79b9a2041c51d08037cff87c181efb311a095dfdd4edbcc7993a9",
            "maker": "SYS",
            "maker_size": "100.000000",
            "maker_address": "SVTbaYZ8olpVn3uNyImst3GKyrvfzXQgdK",
            "taker": "LTC",
            "taker_size": "10.500000",
            "taker_address": "LVvFhZroMRGTtg1hHp7jVew3YoZRX8y35Z",
            "updated_at": "2018-01-15T18:25:05.12345Z",
            "created_at": "2018-01-15T18:15:30.12345Z",
            "order_type": "partial",
            "partial_minimum": "10.000000",
            "partial_orig_maker_size": "100.000000",
            "partial_orig_taker_size": "10.500000",
            "partial_repost": true,
            "partial_parent_id": "",
            "status": "open"
        },
        {
            "id": "6be548bc46a3dcc69b6d56529948f7e679dd96657f85f5870a017e005caa050a",
            "maker": "SYS",
            "maker_size": "4.000000",
            "maker_address": "SVTbaYZ8olpVn3uNyImst3GKyrvfzXQgdK",
            "taker": "LTC",
            "taker_size": "0.400000",
            "taker_address": "LVvFhZroMRGTtg1hHp7jVew3YoZRX8y35Z",
            "updated_at": "2018-01-15T18:25:05.12345Z",
            "created_at": "2018-01-15T18:15:30.12345Z",
            "order_type": "partial",
            "partial_minimum": "0.400000",
            "partial_orig_maker_size": "4.000000",
            "partial_orig_taker_size": "0.400000",
            "partial_repost": true,
            "partial_parent_id": "91d0ea83edc79b9a2041c51d08037cff87c181efb311a095dfdd4edbcc7993a9",
            "status": "open"
        }
    ]

    Key                     | Type | Description
    ------------------------|------|---------------------------------------------
    Array                   | arr  | An array of all orders with each order
                            |      | having the following parameters.
    id                      | str  | The order ID.
    maker                   | str  | Maker trading asset; the ticker of the asset
                            |      | being sold by the maker.
    maker_size              | str  | Maker trading size. String is used to
                            |      | preserve precision.
    maker_address           | str  | Address for sending the outgoing asset.
    taker                   | str  | Taker trading asset; the ticker of the asset
                            |      | being sold by the taker.
    taker_size              | str  | Taker trading size. String is used to
                            |      | preserve precision.
    taker_address           | str  | Address for receiving the incoming asset.
    updated_at              | str  | ISO 8601 datetime, with microseconds, of the
                            |      | last time the order was updated.
    created_at              | str  | ISO 8601 datetime, with microseconds, of
                            |      | when the order was created.
    order_type              | str  | The order type.
    partial_minimum*        | str  | The minimum amount that can be taken.
    partial_orig_maker_size*| str  | The partial order original maker_size.
    partial_orig_taker_size*| str  | The partial order original taker_size.
    partial_repost          | str  | Whether the order will be reposted or not.
                            |      | This applies to `partial` order types and
                            |      | will show `false` for `exact` order types.
    partial_parent_id       | str  | The previous order id of a reposted partial
                            |      | order. This will return an empty string if
                            |      | there is no parent order.
    status                  | str  | The order status.

    * This only applies to `partial` order types and will show `0` on `exact`
      order types.
                )"
                },
                RPCExamples{
                    HelpExampleCli("dxGetMyPartialOrderChain", "\"6be548bc46a3dcc69b6d56529948f7e679dd96657f85f5870a017e005caa050a\"")
                  + HelpExampleRpc("dxGetMyPartialOrderChain", "6be548bc46a3dcc69b6d56529948f7e679dd96657f85f5870a017e005caa050a")
                },
            }.ToString());

    RPCTypeCheck(request.params, {UniValue::VSTR});
    const auto orderid = uint256S(request.params[0].get_str());
    if (orderid.IsNull())
        return xbridge::makeError(xbridge::INVALID_PARAMETERS, __FUNCTION__, "bad order id");

    UniValue r(UniValue::VARR);

    xbridge::App & xapp = xbridge::App::instance();
    auto orderChain = xapp.getPartialOrderChain(orderid);

    std::map<std::string, bool> seen;
    for (const auto & t : orderChain) {
        // do not process already seen orders
        if (seen.count(t->id.GetHex()))
            continue;
        seen[t->id.GetHex()] = true;

        xbridge::WalletConnectorPtr connFrom = xapp.connectorByCurrency(t->fromCurrency);
        xbridge::WalletConnectorPtr connTo   = xapp.connectorByCurrency(t->toCurrency);

        std::string makerAddress;
        std::string takerAddress;
        if (connFrom)
            makerAddress = connFrom->fromXAddr(t->from);
        if (connTo)
            takerAddress = connTo->fromXAddr(t->to);

        UniValue o(UniValue::VOBJ);
        o.pushKV("id", t->id.GetHex());

        // maker data
        o.pushKV("maker", t->fromCurrency);
        o.pushKV("maker_size", xbridge::xBridgeStringValueFromAmount(t->fromAmount));
        o.pushKV("maker_address", makerAddress);
        // taker data
        o.pushKV("taker", t->toCurrency);
        o.pushKV("taker_size", xbridge::xBridgeStringValueFromAmount(t->toAmount));
        o.pushKV("taker_address", takerAddress);
        // dates
        o.pushKV("updated_at", xbridge::iso8601(t->txtime));
        o.pushKV("created_at", xbridge::iso8601(t->created));
        // partial order details
        o.pushKV("order_type", t->orderType());
        o.pushKV("partial_minimum", xbridge::xBridgeStringValueFromAmount(t->minFromAmount));
        o.pushKV("partial_orig_maker_size", xbridge::xBridgeStringValueFromAmount(t->origFromAmount));
        o.pushKV("partial_orig_taker_size", xbridge::xBridgeStringValueFromAmount(t->origToAmount));
        o.pushKV("partial_repost", t->repostOrder);
        o.pushKV("partial_parent_id", parseParentId(t->getParentOrder()));
        o.pushKV("status", t->strState());

        r.push_back(o);
    }

    return r;
}

UniValue dxPartialOrderChainDetails(const JSONRPCRequest& request) {
    if (request.fHelp || request.params.empty() || request.params.size() > 1)
        throw std::runtime_error(
            RPCHelpMan{"dxPartialOrderChainDetails",
                "\nReturns detailed information about a partial order "
                "chain. This includes original amounts, total amount, "
                "reported amounts sent and received and other information.\n",
                {
                    {"order_id", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "Order id"},
                },
                RPCResult{
                R"(
    {
        "first_order_id": "0b28e7c7de9a048dd2cb28b7d91062a052d16adf6d1a2154aa99ab2321c29770",
        "maker": "BLOCK",
        "maker_address": "y4Fn5z58KFA4qLcktBFCrKc8UHrWnNaVym",
        "taker": "LTC",
        "taker_address": "LWvt2ygq8QDkVEcCkMWHR4qXCqL2gC9D2B",
        "partial_minimum": "0.100000",
        "partial_orig_maker_size": "0.100000",
        "partial_orig_taker_size": "0.000100",
        "first_order_time": "2020-07-23T23:52:05.999Z",
        "last_order_time": "2020-07-23T23:58:34.604Z",
        "total_reported_sent": "0.200000",
        "total_reported_received": "0.000200",
        "total_reported_notsent": "0.800000",
        "total_reported_notreceived": "0.000800",
        "total_orders_open": 0,
        "total_orders_finished": 2,
        "total_orders_canceled": 1,
        "orders": [
          "0b28e7c7de9a048dd2cb28b7d91062a052d16adf6d1a2154aa99ab2321c29770",
          "d3afd3b5faf604245a6962214bd0460bec88ff275236480d24b9e5cd45d44c41",
          "5d4bde2de3d6982ce40da82da3b55803f82e11672b2292c611aec9b54cc4c4c9"
        ],
        "p2sh_deposits": [
          "a3bd9b849696946a06ad90b5e03337dba326400192d5b9b96ce0faf2cb513377",
          "a29c4d06941877b501d0b5fe6dc054ca177f723e30a987f67a5871df8b14bfa5",
          ""
        ],
        "p2sh_deposits_counterparty": [
          "41e106c3668d097166cc4a5cce283a9079e769859c4a5467826506fc2547725e",
          "c2f86465d26b3f90f559e3fe56a4a0aa44ee01e07f1e27b2236f16be92991f25",
          ""
        ]
    }

    Key                        | Type | Description
    ---------------------------|------|-----------------------------------------------------
    first_order_id             | str  | The order ID.
    maker                      | str  | Maker trading asset; the ticker of the asset being
                               |      | sold by the maker.
    maker_address              | str  | Address for sending the outgoing asset.
    taker                      | str  | Taker trading asset; the ticker of the asset being
                               |      | sold by the taker.
    taker_address              | str  | Address for receiving the incoming asset.
    partial_minimum            | str  | The minimum amount that can be taken. This applies
                               |      | to `partial` order types and will show `0` on
                               |      | `exact` order types.
    partial_orig_maker_size    | str  | The partial order original maker_size.
    partial_orig_taker_size    | str  | The partial order original taker_size.
    first_order_time           | str  | ISO 8601 datetime, with microseconds, of the last
                               |      | time the order was updated.
    last_order_time            | str  | ISO 8601 datetime, with microseconds, of when the
                               |      | order was created.
    total_reported_sent        | str  | Total amount of maker coin sent to traders.
    total_reported_received    | str  | Total amount of taker coin received from traders.
    total_reported_notsent     | str  | Total amount of maker coin not yet sent to traders.
    total_reported_notreceived | str  | Total amount of taker coin not yet received from traders.
    total_orders_open          | int  | Total number of open orders.
    total_orders_finished      | int  | Total number of completed orders.
    total_orders_canceled      | int  | Total number of canceled orders.
    orders                     | arr  | All orders in the partial order chain.
    p2sh_deposits              | arr  | All p2sh deposit txids sorted by "orders" data (1 for each order)
    p2sh_deposits_counterparty | arr  | All p2sh counterparty deposit txids sorted by "orders" data (1 for each order)

                )"
                },
                RPCExamples{
                    HelpExampleCli("dxPartialOrderChainDetails", "\"6be548bc46a3dcc69b6d56529948f7e679dd96657f85f5870a017e005caa050a\"")
                  + HelpExampleRpc("dxPartialOrderChainDetails", "6be548bc46a3dcc69b6d56529948f7e679dd96657f85f5870a017e005caa050a")
                },
            }.ToString());

    RPCTypeCheck(request.params, {UniValue::VSTR});
    const auto orderid = uint256S(request.params[0].get_str());
    if (orderid.IsNull())
        return xbridge::makeError(xbridge::INVALID_PARAMETERS, __FUNCTION__, "bad order id");

    xbridge::App & xapp = xbridge::App::instance();
    auto orderChain = xapp.getPartialOrderChain(orderid);
    if (orderChain.empty())
        return UniValue(UniValue::VOBJ);

    const auto firstOrder = orderChain[0];
    const auto lastOrder = orderChain[orderChain.size()-1];
    xbridge::WalletConnectorPtr connFrom = xapp.connectorByCurrency(firstOrder->fromCurrency);
    xbridge::WalletConnectorPtr connTo = xapp.connectorByCurrency(firstOrder->toCurrency);
    const auto firstOrderId = firstOrder->id;
    const auto maker = firstOrder->fromCurrency;
    const auto taker = firstOrder->toCurrency;
    const auto makerAddress = connFrom ? connFrom->fromXAddr(firstOrder->from) : "";
    const auto takerAddress = connTo ? connTo->fromXAddr(firstOrder->to) : "";
    const auto partialMinimum = xbridge::xBridgeStringValueFromAmount(firstOrder->minFromAmount);
    const auto makerOrigSize = xbridge::xBridgeStringValueFromAmount(firstOrder->origFromAmount);
    const auto takerOrigSize = xbridge::xBridgeStringValueFromAmount(firstOrder->origToAmount);
    const auto firstOrderTime = xbridge::iso8601(firstOrder->created);
    const auto lastOrderTime = xbridge::iso8601(lastOrder->txtime);
    int64_t totalSent{0}, totalReceived{0}, totalNotSent{0}, totalNotReceived{0};
    int totalOpen{0}, totalInProgress{0}, totalFinished{0}, totalCanceled{0};
    UniValue uvorders(UniValue::VARR);
    UniValue uvp2sh(UniValue::VARR);
    UniValue uvp2shcparty(UniValue::VARR);
    for (const auto & t : orderChain) {
        if (t->state == xbridge::TransactionDescr::trFinished) {
            totalSent += t->fromAmount;
            totalReceived += t->toAmount;
            ++totalFinished;
        } else {
            totalNotSent += t->fromAmount;
            totalNotReceived += t->toAmount;
        }
        if (t->state <= xbridge::TransactionDescr::trPending)
            ++totalOpen;
        if (t->state > xbridge::TransactionDescr::trPending && t->state < xbridge::TransactionDescr::trFinished)
            ++totalInProgress;
        if (t->state == xbridge::TransactionDescr::trCancelled)
            ++totalCanceled;
        uvorders.push_back(t->id.GetHex());
        uvp2sh.push_back(t->binTxId);
        uvp2shcparty.push_back(t->oBinTxId);
    }

    UniValue o(UniValue::VOBJ);
    o.pushKV("first_order_id", firstOrderId.GetHex());
    o.pushKV("maker", maker);
    o.pushKV("maker_address", makerAddress);
    o.pushKV("taker", taker);
    o.pushKV("taker_address", takerAddress);
    o.pushKV("partial_minimum", partialMinimum);
    o.pushKV("partial_orig_maker_size", makerOrigSize);
    o.pushKV("partial_orig_taker_size", takerOrigSize);
    o.pushKV("first_order_time", firstOrderTime);
    o.pushKV("last_order_time", lastOrderTime);
    o.pushKV("total_reported_sent", xbridge::xBridgeStringValueFromAmount(totalSent));
    o.pushKV("total_reported_received", xbridge::xBridgeStringValueFromAmount(totalReceived));
    o.pushKV("total_reported_notsent", xbridge::xBridgeStringValueFromAmount(totalNotSent));
    o.pushKV("total_reported_notreceived", xbridge::xBridgeStringValueFromAmount(totalNotReceived));
    o.pushKV("total_orders_open", totalOpen);
    o.pushKV("total_orders_finished", totalFinished);
    o.pushKV("total_orders_canceled", totalCanceled);
    o.pushKV("orders", uvorders);
    o.pushKV("p2sh_deposits", uvp2sh);
    o.pushKV("p2sh_deposits_counterparty", uvp2shcparty);
    return o;
}

UniValue dxGetTokenBalances(const JSONRPCRequest& request)
{
    if (request.fHelp)
        throw std::runtime_error(
            RPCHelpMan{"dxGetTokenBalances",
                "\nReturns a list of available balances for all connected wallets on your "
                "node (view with dxGetLocalTokens).\n"
                "\nNote:\n"
                "These balances do not include Segwit UTXOs or those being used in open or in process orders. "
                "XBridge works best with pre-sliced UTXOs so that your entire wallet balance is capable of "
                "multiple simultaneous trades. Use dxSplitInputs or dxSplitAddress to generate trading inputs.\n",
                {},
                RPCResult{
                R"(
    {
        "BLOCK": "250.83492174",
        "LTC": "0.568942",
        "MONA": "3.452",
        "SYS": "1050.128493"
    }

    Key          | Type | Description
    -------------|------|--------------------------------------------------------
    Object       | obj  | Key-value object of the assets and respective balances.
    -- key       | str  | The asset symbol.
    -- value     | str  | The available wallet balance amount.
                )"
                },
                RPCExamples{
                    HelpExampleCli("dxGetTokenBalances", "")
                  + HelpExampleRpc("dxGetTokenBalances", "")
                },
            }.ToString());
    const UniValue &params = request.params;

    if (params.size() != 0)
    {
        UniValue error(UniValue::VOBJ);
        error.pushKV("error",    xbridge::xbridgeErrorText(xbridge::INVALID_PARAMETERS, "This function does not accept any parameters."));
        error.pushKV("code",     xbridge::INVALID_PARAMETERS);
        error.pushKV("name",     __FUNCTION__);
        return error;
    }

    UniValue res(UniValue::VOBJ);

    // Wallet balance
    double walletBalance = boost::numeric_cast<double>(xbridge::availableBalance()) / boost::numeric_cast<double>(COIN);
    res.pushKV("Wallet", xbridge::xBridgeStringValueFromPrice(walletBalance));

    // Add connected wallet balances (fetch balances concurrently)
    const auto &connectors = xbridge::App::instance().connectors();
    std::condition_variable cv;
    Mutex cv_mu;
    Mutex mu; // lock writes to res
    int cores = GetNumCores()/2;
    if (cores > connectors.size())
        cores = connectors.size();
    if (cores <= 0)
        cores = 1;
    int count = 0;
    boost::thread_group tg;
    for(const auto &connector : connectors)
    {
        count++;
        tg.create_thread([&cv,&mu,&count,&connector,&res]() {
            RenameThread("blocknet-balance-check");
            const auto & excluded = xbridge::App::instance().getAllLockedUtxos(connector->currency);
            const auto balance = connector->getWalletBalance(excluded);
            {
                LOCK(mu);
                if (balance >= 0) // Ignore results from disconnected wallets
                    res.pushKV(connector->currency, xbridge::xBridgeStringValueFromPrice(balance));
                count--;
            }
            cv.notify_one();
        });
        while (count >= cores) { // block when queue is full
            WAIT_LOCK(cv_mu, lock);
            cv.wait(lock);
        }
    }
    tg.join_all(); // wait for all to complete

    return res;
}

UniValue dxGetLockedUtxos(const JSONRPCRequest& request)
{
    if (request.fHelp)
        throw std::runtime_error(
            RPCHelpMan{"dxGetLockedUtxos",
                "\nReturns a list of locked UTXOs used in orders. You can only use "
                "this call if you have a Service Node setup.\n",
                {
                    {"id", RPCArg::Type::STR_HEX, RPCArg::Optional::OMITTED, "The order ID. If omitted, a list of UTXOs used in all orders will be returned."},
                },
                RPCResult{
                R"(
    [
        {
            "id" : "91d0ea83edc79b9a2041c51d08037cff87c181efb311a095dfdd4edbcc7993a9",
            "LTC" : [
                6be548bc46a3dcc69b6d56529948f7e679dd96657f85f5870a017e005caa050a,
                6be548bc46a3dcc69b6d56529948f7e679dd96657f85f5870a017e005caa050a,
                6be548bc46a3dcc69b6d56529948f7e679dd96657f85f5870a017e005caa050a
            ]
        }
    ]

    Key             | Type | Description
    ----------------|------|-----------------------------------------------------
    id              | str  | The order ID.
    Object          | obj  | Key-value object of the asset and UTXOs for the
                    |      | forementioned order.
    -- key          | str  | The asset symbol.
    -- value        | arr  | The UTXOs locked for the given order ID.
                )"
                },
                RPCExamples{
                    HelpExampleCli("dxGetLockedUtxos", "")
                  + HelpExampleRpc("dxGetLockedUtxos", "")
                  + HelpExampleCli("dxGetLockedUtxos", "524137449d9a35fa707ee395abab32bedae91aa2aefb6e3611fcd8574863e432")
                  + HelpExampleRpc("dxGetLockedUtxos", "\"524137449d9a35fa707ee395abab32bedae91aa2aefb6e3611fcd8574863e432\"")
                },
            }.ToString());
    const UniValue &params = request.params;

    if (params.size() > 1)
    {
        UniValue error(UniValue::VOBJ);
        error.pushKV("error",    xbridge::xbridgeErrorText(xbridge::INVALID_PARAMETERS, "Too many parameters."));
        error.pushKV("code",     xbridge::INVALID_PARAMETERS);
        error.pushKV("name",     __FUNCTION__);
        return error;
    }

    xbridge::Exchange & e = xbridge::Exchange::instance();
    if (!e.isStarted())
    {
        UniValue error(UniValue::VOBJ);
        error.pushKV("error",    xbridge::xbridgeErrorText(xbridge::Error::NOT_EXCHANGE_NODE));
        error.pushKV("code",     xbridge::Error::NOT_EXCHANGE_NODE);
        error.pushKV("name",     __FUNCTION__);
        return error;
    }

    uint256 id;

    if(params.size() == 1)
        id = uint256S(params[0].get_str());

    std::vector<xbridge::wallet::UtxoEntry> items;
    if(!e.getUtxoItems(id, items))
    {

        UniValue error(UniValue::VOBJ);
        error.pushKV("error",    xbridge::xbridgeErrorText(xbridge::Error::TRANSACTION_NOT_FOUND, id.GetHex()));
        error.pushKV("code",     xbridge::Error::TRANSACTION_NOT_FOUND);
        error.pushKV("name",     __FUNCTION__);
        return error;
    }

    UniValue utxo(UniValue::VARR);

    for(const xbridge::wallet::UtxoEntry & entry : items)
        utxo.push_back(entry.toString());

    UniValue obj(UniValue::VOBJ);
    if(id.IsNull())
    {
        obj.pushKV("all_locked_utxo", utxo);

        return obj;
    }

    xbridge::TransactionPtr pendingTx = e.pendingTransaction(id);
    xbridge::TransactionPtr acceptedTx = e.transaction(id);

    if (!pendingTx->isValid() && !acceptedTx->isValid())
    {
        UniValue error(UniValue::VOBJ);
        error.pushKV("error",    xbridge::xbridgeErrorText(xbridge::Error::TRANSACTION_NOT_FOUND, id.GetHex()));
        error.pushKV("code",     xbridge::Error::TRANSACTION_NOT_FOUND);
        error.pushKV("name",     __FUNCTION__);
        return error;
    }

    obj.pushKV("id", id.GetHex());

    if(pendingTx->isValid())
        obj.pushKV(pendingTx->a_currency(), utxo);
    else if(acceptedTx->isValid())
        obj.pushKV(acceptedTx->a_currency() + "_and_" + acceptedTx->b_currency(), utxo);

    return obj;
}

UniValue gettradingdata(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() > 2)
        throw std::runtime_error(
            RPCHelpMan{"gettradingdata",
                "\nReturns an object of XBridge trading records. This information is "
                "pulled from on-chain history so pulling a large amount of blocks will "
                "result in longer response times.\n",
                {
                    {"blocks", RPCArg::Type::NUM, "43200", "The number of blocks to return trade records for (60s block time)."},
                    {"errors", RPCArg::Type::BOOL, "false", "show errors"},
                },
                RPCResult{
                "{\n"
                "  \"timestamp\":  \"1559970139\",                          (uint64) Unix epoch timestamp in seconds of when the trade took place.\n"
                "  \"txid\":       \"4b409r5c5fb1986p30cf7c19afec2c8\",     (string) The Blocknet trade fee transaction ID.\n"
                "  \"to\":         \"Bqtes8j14rE65kcpsEors5JDzDaHiaMtLG\",  (string) The address of the Service Node that received the trade fee.\n"
                "  \"xid\":        \"9eb57bas331eab3zf3daefd8364cdbL\",     (string) The XBridge transaction ID.\n"
                "  \"from\":       \"BLOCK\",                               (string) The symbol of the token bought by the maker.\n"
                "  \"fromAmount\": 0.001111,                              (uint64) The amount of the token that was bought by the maker.\n"
                "  \"to\":         \"SYS\",                                 (string) The symbol of the token sold by the maker.\n"
                "  \"toAmount\":   0.001000,                              (uint64) The amount of the token that was sold by the maker.\n"
                "}\n"
                },
                RPCExamples{
                    HelpExampleCli("gettradingdata", "")
                  + HelpExampleRpc("gettradingdata", "")
                  + HelpExampleCli("gettradingdata", "86400")
                  + HelpExampleRpc("gettradingdata", "86400")
                  + HelpExampleCli("gettradingdata", "86400 true")
                  + HelpExampleRpc("gettradingdata", "86400, true")
                },
            }.ToString());
    const UniValue &params = request.params;

    uint32_t countOfBlocks = 43200;
    bool showErrors = false;
    if (params.size() >= 1) {
        if (params.size() == 2) {
            RPCTypeCheck(request.params, {UniValue::VNUM, UniValue::VBOOL});
            showErrors = params[1].get_bool();
        } else
            RPCTypeCheck(request.params, {UniValue::VNUM});
        countOfBlocks = params[0].get_int();
    }

    LOCK(cs_main);

    UniValue records(UniValue::VARR);

    CBlockIndex * pindex = chainActive.Tip();
    int64_t timeBegin = chainActive.Tip()->GetBlockTime();
    for (; pindex->pprev && pindex->GetBlockTime() > (timeBegin-30*24*60*60) && countOfBlocks > 0;
             pindex = pindex->pprev, --countOfBlocks)
    {
        CBlock block;
        if (!ReadBlockFromDisk(block, pindex, Params().GetConsensus()))
        {
            // throw
            continue;
        }
        const auto timestamp = block.GetBlockTime();
        for (const CTransactionRef & tx : block.vtx)
        {
            const auto txid = tx->GetHash().GetHex();
            std::string snode_pubkey{};

            const CurrencyPair p = TxOutToCurrencyPair(tx->vout, snode_pubkey);
            switch(p.tag) {
            case CurrencyPair::Tag::Error:
                // Show errors
                if (showErrors) {
                    UniValue o(UniValue::VOBJ);
                    o.pushKV("timestamp",  timestamp);
                    o.pushKV("txid",       txid);
                    o.pushKV("xid",        p.error());
                    records.push_back(o);
                }
                break;
            case CurrencyPair::Tag::Valid: {
                UniValue o(UniValue::VOBJ);
                o.pushKV("timestamp",  timestamp);
                o.pushKV("txid",       txid);
                o.pushKV("to",         snode_pubkey);
                o.pushKV("xid",        p.xid());
                o.pushKV("from",       p.from.currency().to_string());
                o.pushKV("fromAmount", p.from.amount<double>());
                o.pushKV("to",         p.to.currency().to_string());
                o.pushKV("toAmount",   p.to.amount<double>());
                records.push_back(o);
                break;
            }
            case CurrencyPair::Tag::Empty:
            default:
                break;
            }
        }
    }

    return records;
}

UniValue dxGetTradingData(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() > 2)
        throw std::runtime_error(
            RPCHelpMan{"dxGetTradingData",
                "\nReturns an object of XBridge trading records. This information is "
                "pulled from on-chain history so pulling a large amount of blocks will "
                "result in longer response times.\n",
                {
                    {"blocks", RPCArg::Type::NUM, "43200", "The number of blocks to return trade records for (60s block time)."},
                    {"errors", RPCArg::Type::BOOL, "false", "Shows an error if an error is detected."},
                },
                RPCResult{
                R"(
    [
      {
        "timestamp": 1559970139,
        "fee_txid": "4b409e5c5fb1986930cf7c19afec2c89ac2ad4fddc13c1d5479b66ddf4a8fefb",
        "nodepubkey": "Bqtms8j1zrE65kcpsEorE5JDzDaHidMtLG",
        "id": "9eb57bac331eab34f3daefd8364cdb2bb05259c407d805d0bd0c",
        "taker": "BLOCK",
        "taker_size": 0.001111,
        "maker": "SYS",
        "maker_size": 0.001000
      },
      {
        "timestamp": 1559970139,
        "fee_txid": "3de7479e8a88ebed986d3b7e7e135291d3fd10e4e6d4c6238663db42c5019286",
        "nodepubkey": "Bqtms8j1zrE65kcpsEorE5JDzDaHidMtLG",
        "id": "fd0fed3ee9fe557d5735768c9bdcd4ab2908165353e0f0cef0d5",
        "taker": "BLOCK",
        "taker_size": 0.001577,
        "maker": "SYS",
        "maker_size": 0.001420
      }
    ]

    Key         | Type | Description
    ------------|------|---------------------------------------------------------
    timestamp   | int  | Unix epoch timestamp of when the trade took place.
    fee_txid    | str  | The Blocknet trade fee transaction ID.
    nodepubkey  | str  | The pubkey of the service node that received the trade
                |      | fee.
    id          | str  | The order ID.
    taker       | str  | Taker trading asset; the ticker of the asset being sold
                |      | by the taker.
    taker_size  | int  | Taker trading size.
    maker       | str  | Maker trading asset; the ticker of the asset being sold
                |      | by the maker.
    maker_size  | int  | Maker trading size.
                )"
                },
                RPCExamples{
                    HelpExampleCli("dxGetTradingData", "")
                  + HelpExampleRpc("dxGetTradingData", "")
                  + HelpExampleCli("dxGetTradingData", "43200")
                  + HelpExampleRpc("dxGetTradingData", "43200")
                  + HelpExampleCli("dxGetTradingData", "43200 true")
                  + HelpExampleRpc("dxGetTradingData", "43200, true")
                },
            }.ToString());
    const UniValue &params = request.params;

    uint32_t countOfBlocks = 43200;
    bool showErrors = false;
    if (params.size() >= 1) {
        if (params.size() == 2) {
            RPCTypeCheck(request.params, {UniValue::VNUM, UniValue::VBOOL});
            showErrors = params[1].get_bool();
        } else
            RPCTypeCheck(request.params, {UniValue::VNUM});
        countOfBlocks = params[0].get_int();
    }

    LOCK(cs_main);

    UniValue records(UniValue::VARR);

    CBlockIndex * pindex = chainActive.Tip();
    int64_t timeBegin = chainActive.Tip()->GetBlockTime();
    for (; pindex->pprev && pindex->GetBlockTime() > (timeBegin-30*24*60*60) && countOfBlocks > 0;
             pindex = pindex->pprev, --countOfBlocks)
    {
        CBlock block;
        if (!ReadBlockFromDisk(block, pindex, Params().GetConsensus()))
        {
            // throw
            continue;
        }
        const auto timestamp = block.GetBlockTime();
        for (const CTransactionRef & tx : block.vtx)
        {
            const auto txid = tx->GetHash().GetHex();
            std::string snode_pubkey{};

            const CurrencyPair p = TxOutToCurrencyPair(tx->vout, snode_pubkey);
            switch(p.tag) {
            case CurrencyPair::Tag::Error:
                // Show errors
                if (showErrors) {
                    UniValue o(UniValue::VOBJ);
                    o.pushKV("timestamp",  timestamp);
                    o.pushKV("fee_txid",   txid);
                    o.pushKV("id",         p.error());
                    records.push_back(o);
                }
                break;
            case CurrencyPair::Tag::Valid: {
                UniValue o(UniValue::VOBJ);
                o.pushKV("timestamp",  timestamp);
                o.pushKV("fee_txid",   txid);
                o.pushKV("nodepubkey", snode_pubkey);
                o.pushKV("id",         p.xid());
                o.pushKV("taker",      p.from.currency().to_string());
                o.pushKV("taker_size", p.from.amount<double>());
                o.pushKV("maker",      p.to.currency().to_string());
                o.pushKV("maker_size", p.to.amount<double>());
                records.push_back(o);
                break;
            }
            case CurrencyPair::Tag::Empty:
            default:
                break;
            }
        }
    }

    return records;
}

UniValue dxMakePartialOrder(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 6)
        throw std::runtime_error(
            RPCHelpMan{"dxMakePartialOrder",
                "\nCreate a new partial order. Partial orders don't require the entire order to be filled. "
                "For exact orders, see dxMakeOrder.\n"
                "You can only create orders for markets with assets supported by your node (view with dxGetLocalTokens) "
                "and the network (view with dxGetNetworkTokens). There are no fees to make orders. \n"
                "\nWhen a partial order is created, multiple inputs will be selected or "
                "generated. Using multiple inputs is optimal for allowing partial orders of "
                "varying sizes while minimizing the amount of change (change not reposted). "
                "This maximizes the amount remaining that can be immediately reposted.\n"
                "\nThe way input selection/generation is done depends on your total "
                "`maker_size` and `minimum_size`. XBridge will first attempt to find "
                "existing inputs that are properly sized for the order. If needed, existing "
                "inputs will automatically be split into the proper size at the time the "
                "order is posted. While the inputs are being generated, the order will "
                "remain in the `new` state. Once the generated inputs have 1 confirmation "
                "the order will proceed to the `open` state.\n"
                "\nNote:\n"
                "XBridge will first attempt to use funds from the specified maker address. "
                "If this address does not have sufficient funds to cover the order and "
                "`use_all_funds` is true, then it will pull funds from other addresses in "
                "the wallet. Change is deposited to the address with the largest input used.\n",
                {
                    {"maker", RPCArg::Type::STR, RPCArg::Optional::NO, "The symbol of the asset being sold by the maker (e.g. LTC)."},
                    {"maker_size", RPCArg::Type::STR, RPCArg::Optional::NO, "The amount of the maker asset being sent."},
                    {"maker_address", RPCArg::Type::STR, RPCArg::Optional::NO, "The maker address containing asset being sent."},
                    {"taker", RPCArg::Type::STR, RPCArg::Optional::NO, "The symbol of the asset being bought by the maker (e.g. BLOCK)."},
                    {"taker_size", RPCArg::Type::STR, RPCArg::Optional::NO, "The amount of the taker asset to be received."},
                    {"taker_address", RPCArg::Type::STR, RPCArg::Optional::NO, "The taker address for the receiving asset."},
                    {"minimum_size", RPCArg::Type::STR, RPCArg::Optional::NO, "Minimum maker_size that can be traded in the partial order."},
                    {"repost", RPCArg::Type::BOOL, /* default */ "true", "Repost partial order remainder after taken."},
                    {"use_all_funds", RPCArg::Type::BOOL, /* default */ "true", "Use funds from all available addresses in the wallet as opposed to just the maker_address."},
                    {"auto_split", RPCArg::Type::BOOL, /* default */ "true", "Split funds into multiple UTXOs if needed."},
                    {"dryrun", RPCArg::Type::STR, RPCArg::Optional::OMITTED, "Simulate the order submission without actually submitting the order, i.e. a test run. Options: dryrun"},
                },
                RPCResult{
                R"(
    {
        "id": "4306a107113c4562afa6273ecd9a3990ead53a0227f74ddd9122272e453ae07d",
        "maker": "SYS",
        "maker_size": "1.000000",
        "maker_address": "SVTbaYZ8olpVn3uNyImst3GKyrvfzXQgdK",
        "taker": "LTC",
        "taker_size": "0.100000",
        "taker_address": "LVvFhZroMRGTtg1hHp7jVew3YoZRX8y35Z",
        "updated_at": "2018-01-16T00:00:00.00000Z",
        "created_at": "2018-01-15T18:15:30.12345Z",
        "block_id": "38729344720548447445023782734923740427863289632489723984723",
        "order_type": "partial",
        "partial_minimum": "0.200000",
        "partial_orig_maker_size": "2.000000",
        "partial_orig_taker_size": "0.200000",
        "partial_repost": true,
        "partial_parent_id": "1faeba06827929f16490c61ba633522158e8d44163c47f735078eac0304c5eb6",
        "status": "created"
    }

    Key                     | Type | Description
    ------------------------|------|---------------------------------------------
    Array                   | arr  | An array of all orders with each order
                            |      | having the following parameters.
    id                      | str  | The order ID.
    maker                   | str  | Maker trading asset; the ticker of the asset
                            |      | being sold by the maker.
    maker_size              | str  | Maker trading size. String is used to
                            |      | preserve precision.
    maker_address           | str  | Address for sending the outgoing asset.
    taker                   | str  | Taker trading asset; the ticker of the asset
                            |      | being sold by the taker.
    taker_size              | str  | Taker trading size. String is used to
                            |      | preserve precision.
    taker_address           | str  | Address for receiving the incoming asset.
    updated_at              | str  | ISO 8601 datetime, with microseconds, of the
                            |      | last time the order was updated.
    created_at              | str  | ISO 8601 datetime, with microseconds, of
                            |      | when the order was created.
    order_type              | str  | The order type.
    partial_minimum*        | str  | The minimum amount that can be taken.
    partial_orig_maker_size*| str  | The partial order original maker_size.
    partial_orig_taker_size*| str  | The partial order original taker_size.
    partial_repost          | str  | Whether the order will be reposted or not.
                            |      | This applies to `partial` order types and
                            |      | will show `false` for `exact` order types.
    partial_parent_id       | str  | The previous order id of a reposted partial
                            |      | order. This will return an empty string if
                            |      | there is no parent order.
    status                  | str  | The order status.

    * This only applies to `partial` order types and will show `0` on `exact`
      order types.
                )"
                },
                RPCExamples{
                    HelpExampleCli("dxMakePartialOrder", "LTC 25 LLZ1pgb6Jqx8hu84fcr5WC5HMoKRUsRE8H BLOCK 1000 BWQrvmuHB4C68KH5V7fcn9bFtWN8y5hBmR 100")
                  + HelpExampleRpc("dxMakePartialOrder", "\"LTC\", \"25\", \"LLZ1pgb6Jqx8hu84fcr5WC5HMoKRUsRE8H\", \"BLOCK\", \"1000\", \"BWQrvmuHB4C68KH5V7fcn9bFtWN8y5hBmR\", \"100\"")
                  + HelpExampleCli("dxMakePartialOrder", "LTC 25 LLZ1pgb6Jqx8hu84fcr5WC5HMoKRUsRE8H BLOCK 1000 BWQrvmuHB4C68KH5V7fcn9bFtWN8y5hBmR 100 true true true dryrun")
                  + HelpExampleRpc("dxMakePartialOrder", "\"LTC\", \"25\", \"LLZ1pgb6Jqx8hu84fcr5WC5HMoKRUsRE8H\", \"BLOCK\", \"1000\", \"BWQrvmuHB4C68KH5V7fcn9bFtWN8y5hBmR\", \"100\", \"true\", \"true\", \"true\", \"dryrun\"")
                },
            }.ToString());

    if (!xbridge::xBridgeValidCoin(request.params[1].get_str())) {
        UniValue error(UniValue::VOBJ);
        error.pushKV("error",    xbridge::xbridgeErrorText(xbridge::INVALID_PARAMETERS,
                      "The maker_size is too precise. The maximum precision supported is " +
                              std::to_string(xbridge::xBridgeSignificantDigits(xbridge::TransactionDescr::COIN)) + " digits."));
        error.pushKV("code",     xbridge::INVALID_PARAMETERS);
        error.pushKV("name",     __FUNCTION__);
        return error;
    }

    if (!xbridge::xBridgeValidCoin(request.params[4].get_str())) {
        UniValue error(UniValue::VOBJ);
        error.pushKV("error",    xbridge::xbridgeErrorText(xbridge::INVALID_PARAMETERS,
                      "The taker_size is too precise. The maximum precision supported is " +
                              std::to_string(xbridge::xBridgeSignificantDigits(xbridge::TransactionDescr::COIN)) + " digits."));
        error.pushKV("code",     xbridge::INVALID_PARAMETERS);
        error.pushKV("name",     __FUNCTION__);
        return error;
    }

    std::string fromCurrency    = request.params[0].get_str();
    double      fromAmount      = boost::lexical_cast<double>(request.params[1].get_str());
    std::string fromAddress     = request.params[2].get_str();

    std::string toCurrency      = request.params[3].get_str();
    double      toAmount        = boost::lexical_cast<double>(request.params[4].get_str());
    std::string toAddress       = request.params[5].get_str();
    double      partialMinimum  = boost::lexical_cast<double>(request.params[6].get_str());

    // Check if min_size > maker_size 
    if (partialMinimum > fromAmount) {
        return xbridge::makeError(xbridge::INVALID_PARAMETERS, __FUNCTION__,
                               "The minimum_size can't be more than maker_size");
    }

    // Check that addresses are not the same
    if (fromAddress == toAddress) {
        return xbridge::makeError(xbridge::INVALID_PARAMETERS, __FUNCTION__,
                               "The maker_address and taker_address cannot be the same: " + fromAddress);
    }

    // Check upper limits
    if (fromAmount > (double)xbridge::TransactionDescr::MAX_COIN ||
            toAmount > (double)xbridge::TransactionDescr::MAX_COIN) {
        return xbridge::makeError(xbridge::INVALID_PARAMETERS, __FUNCTION__,
                               "The maximum supported size is " + std::to_string(xbridge::TransactionDescr::MAX_COIN));
    }
    // Check lower limits
    if (fromAmount <= 0 || toAmount <= 0) {
        return xbridge::makeError(xbridge::INVALID_PARAMETERS, __FUNCTION__,
                               "The minimum supported size is " + xbridge::xBridgeStringValueFromPrice(1.0/xbridge::TransactionDescr::COIN));
    }

    // Validate addresses
    xbridge::WalletConnectorPtr connFrom = xbridge::App::instance().connectorByCurrency(fromCurrency);
    xbridge::WalletConnectorPtr connTo   = xbridge::App::instance().connectorByCurrency(toCurrency);
    if (!connFrom) return xbridge::makeError(xbridge::NO_SESSION, __FUNCTION__, "Unable to connect to wallet: " + fromCurrency);
    if (!connTo) return xbridge::makeError(xbridge::NO_SESSION, __FUNCTION__, "Unable to connect to wallet: " + toCurrency);

    xbridge::App &app = xbridge::App::instance();

    if (!app.isValidAddress(fromAddress, connFrom)) {
        return xbridge::makeError(xbridge::INVALID_ADDRESS, __FUNCTION__, fromAddress);
    }
    if (!app.isValidAddress(toAddress, connTo)) {
        return xbridge::makeError(xbridge::INVALID_ADDRESS, __FUNCTION__, toAddress);
    }
    if(fromAmount <= .0) {
        return xbridge::makeError(xbridge::INVALID_PARAMETERS, __FUNCTION__,
                               "The maker_size must be greater than 0.");
    }
    if(toAmount <= .0) {
        return xbridge::makeError(xbridge::INVALID_PARAMETERS, __FUNCTION__,
                               "The taker_size must be greater than 0.");
    }
    if (connFrom->isDustAmount(partialMinimum)) {
        return xbridge::makeError(xbridge::INVALID_PARAMETERS, __FUNCTION__,
                               "The partial minimum_size is dust, i.e. it's too small.");
    }

    bool repost{true};
    if (request.params.size() >= 8)
        repost = request.params[7].get_bool();

    bool useAllFunds = true;
    if (request.params.size() >= 9)
        useAllFunds = request.params[8].get_bool();

    bool autoSplit = true;
    if (request.params.size() >= 10)
        autoSplit = request.params[9].get_bool();

    // Perform explicit check on dryrun to avoid executing order on bad spelling
    bool dryrun = false;
    if (request.params.size() == 11) {
        std::string dryrunParam = request.params[10].get_str();
        if (dryrunParam != "dryrun") {
            return xbridge::makeError(xbridge::INVALID_PARAMETERS, __FUNCTION__, dryrunParam);
        }
        dryrun = true;
    }


    UniValue result(UniValue::VOBJ);
    auto statusCode = app.checkCreateParams(fromCurrency, toCurrency,
                                       xbridge::xBridgeAmountFromReal(fromAmount), fromAddress);
    switch (statusCode) {
    case xbridge::SUCCESS:{
        // If dryrun
        if (dryrun) {
            result.pushKV("id", uint256().GetHex());
            result.pushKV("maker", fromCurrency);
            result.pushKV("maker_size",
                                     xbridge::xBridgeStringValueFromAmount(xbridge::xBridgeAmountFromReal(fromAmount)));
            result.pushKV("maker_address", fromAddress);
            result.pushKV("taker", toCurrency);
            result.pushKV("taker_size",
                                     xbridge::xBridgeStringValueFromAmount(xbridge::xBridgeAmountFromReal(toAmount)));
            result.pushKV("taker_address", toAddress);
            result.pushKV("order_type", "partial");
            result.pushKV("partial_minimum", xbridge::xBridgeStringValueFromAmount(xbridge::xBridgeAmountFromReal(partialMinimum)));
            result.pushKV("partial_orig_maker_size", xbridge::xBridgeStringValueFromAmount(xbridge::xBridgeAmountFromReal(fromAmount)));
            result.pushKV("partial_orig_taker_size", xbridge::xBridgeStringValueFromAmount(xbridge::xBridgeAmountFromReal(toAmount)));
            result.pushKV("partial_repost",  repost);
            result.pushKV("partial_parent_id", parseParentId(uint256()));
            result.pushKV("status", "created");
            return result;
        }
        break;
    }

    case xbridge::INVALID_CURRENCY: {
        return xbridge::makeError(statusCode, __FUNCTION__, fromCurrency);
    }
    case xbridge::NO_SESSION:{
        return xbridge::makeError(statusCode, __FUNCTION__, fromCurrency);
    }
    case xbridge::INSUFFICIENT_FUNDS:{
        return xbridge::makeError(statusCode, __FUNCTION__, fromAddress);
    }
    case xbridge::NO_SERVICE_NODE:{
        return xbridge::makeError(statusCode, __FUNCTION__, fromCurrency + "/" + toCurrency);
    }

    default:
        return xbridge::makeError(statusCode, __FUNCTION__);
    }

    uint256 id = uint256();
    uint256 blockHash = uint256();
    statusCode = xbridge::App::instance().sendXBridgeTransaction(fromAddress, fromCurrency,
            xbridge::xBridgeAmountFromReal(fromAmount), toAddress, toCurrency,
            xbridge::xBridgeAmountFromReal(toAmount), std::vector<xbridge::wallet::UtxoEntry>{},
            true, repost, xbridge::xBridgeAmountFromReal(partialMinimum), autoSplit, useAllFunds, id, blockHash);

    if (statusCode == xbridge::SUCCESS) {
        UniValue obj(UniValue::VOBJ);
        obj.pushKV("id",               id.GetHex());
        obj.pushKV("maker_address",    fromAddress);
        obj.pushKV("maker",            fromCurrency);
        obj.pushKV("maker_size",       xbridge::xBridgeStringValueFromAmount(xbridge::xBridgeAmountFromReal(fromAmount)));
        obj.pushKV("taker_address",    toAddress);
        obj.pushKV("taker",            toCurrency);
        obj.pushKV("taker_size",       xbridge::xBridgeStringValueFromAmount(xbridge::xBridgeAmountFromReal(toAmount)));
        const auto &createdTime = xbridge::App::instance().transaction(id)->created;
        obj.pushKV("created_at",       xbridge::iso8601(createdTime));
        obj.pushKV("updated_at",       xbridge::iso8601(boost::posix_time::microsec_clock::universal_time())); // TODO Need actual updated time, this is just estimate
        obj.pushKV("block_id",         blockHash.GetHex());
        obj.pushKV("order_type",       "partial");
        obj.pushKV("partial_minimum",  xbridge::xBridgeStringValueFromAmount(xbridge::xBridgeAmountFromReal(partialMinimum)));
        obj.pushKV("partial_orig_maker_size", xbridge::xBridgeStringValueFromAmount(xbridge::xBridgeAmountFromReal(fromAmount)));
        obj.pushKV("partial_orig_taker_size", xbridge::xBridgeStringValueFromAmount(xbridge::xBridgeAmountFromReal(toAmount)));
        obj.pushKV("partial_repost",   repost);
        obj.pushKV("partial_parent_id", parseParentId(uint256()));
        obj.pushKV("status",           "created");
        return obj;

    } else if (statusCode == xbridge::INSUFFICIENT_FUNDS) {
        return xbridge::makeError(statusCode, __FUNCTION__, fromAddress);
    } else {
        return xbridge::makeError(statusCode, __FUNCTION__);
    }
}

UniValue dxSplitAddress(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 3 || request.params.size() > 6)
        throw std::runtime_error(
            RPCHelpMan{"dxSplitAddress",
                "\nSplits unused coin in the given address into the specified size. Left over amounts "
                "end up in change. UTXOs being used in existing orders will not be included by the "
                "splitter (view with dxGetUtxos). You can only split UTXOs for assets supported by "
                "your node (view with dxGetLocalTokens).\n",
                {
                   {"token", RPCArg::Type::STR, RPCArg::Optional::NO, "The ticker of the asset you want to split UTXOs for."},
                   {"split_amount", RPCArg::Type::STR, RPCArg::Optional::NO, "The desired UTXO output size."},
                   {"address", RPCArg::Type::STR, RPCArg::Optional::NO, "The address to split UTXOs in. Only coin in this address will be split."},
                   {"include_fees", RPCArg::Type::BOOL, "true", "Include the trade P2SH deposit fees in the split UTXO (add deposit fee to `spit_amount` value."},
                   {"show_rawtx", RPCArg::Type::BOOL, "false", "Include the raw transaction in the response (rawtx can be submitted manually)."},
                   {"submit", RPCArg::Type::BOOL, "true", "Submit the raw transaction to the network."},
                },
                RPCResult{
                R"(
    {
        "token": "BLOCK",
        "include_fees": true,
        "split_amount_requested": "4.0",
        "split_amount_with_fees": "4.00040000",
        "split_utxo_count": 6,
        "split_total": "24.44852981",
        "txid": "7f87cba104b3c19f6e25fbc82b3cde5d73714e01d6a54943d3c8fb07ce315db4",
        "rawtx": ""
    }

    Key                    | Type | Description
    -----------------------|------|----------------------------------------------
    token                  | str  | Asset you are splitting UTXOs for.
    include_fees           | bool | Whether you requested to include the fees.
    split_amount_requested | str  | Requested split amount.
    split_amount_with_fees | str  | Requested split amount with fees included.
    split_utxo_count       | int  | Amount of resulting split UTXOs.
    split_total            | str  | Total amount of in the address prior to
                           |      | splitting.
    txid                   | str  | Hex string of the splitting transaction.
    rawtx                  | str  | Hex string of the raw splitting transaction.
                )"
                },
                RPCExamples{
                   HelpExampleCli("dxSplitAddress", "BLOCK 10.5 BWQrvmuHB4C68KH5V7fcn9bFtWN8y5hBmR")
                   + HelpExampleRpc("dxSplitAddress", "\"BLOCK\", \"10.5\", \"BWQrvmuHB4C68KH5V7fcn9bFtWN8y5hBmR\"")
                   + HelpExampleCli("dxSplitAddress", "BLOCK 10.5 BWQrvmuHB4C68KH5V7fcn9bFtWN8y5hBmR true false true")
                   + HelpExampleRpc("dxSplitAddress", "\"BLOCK\", \"10.5\", \"BWQrvmuHB4C68KH5V7fcn9bFtWN8y5hBmR\", true, false, true")
                },
            }.ToString());

    auto token = request.params[0].get_str();
    auto splitAmount = request.params[1].get_str();
    auto address = request.params[2].get_str();
    bool includeFees{true};
    bool showRawTx{false};
    bool submitTx{true};
    if (!request.params[3].isNull())
        includeFees = request.params[3].get_bool();
    if (!request.params[4].isNull())
        showRawTx = request.params[4].get_bool();
    if (!request.params[5].isNull())
        submitTx = request.params[5].get_bool();

    auto & xapp = xbridge::App::instance();
    xbridge::WalletConnectorPtr conn = xapp.connectorByCurrency(token);
    if (!conn)
        return xbridge::makeError(xbridge::NO_SESSION, __FUNCTION__, token);

    auto utxos = xapp.getAllLockedUtxos(token);
    const CAmount sa = xbridge::xBridgeIntFromReal(boost::lexical_cast<double>(splitAmount));
    std::string txid, rawtx, failReason;
    CAmount totalSplit{0};
    CAmount splitInclFees{0};
    int splitCount{0};
    if (!conn->splitUtxos(sa, address, includeFees, utxos, std::set<COutPoint>{}, totalSplit, splitInclFees, splitCount, txid, rawtx, failReason))
        return xbridge::makeError(xbridge::BAD_REQUEST, __FUNCTION__, failReason);

    int errorcode{0};
    std::string txid2, errmsg;
    if (submitTx && !conn->sendRawTransaction(rawtx, txid2, errorcode, errmsg))
        return xbridge::makeError(xbridge::BAD_REQUEST, __FUNCTION__, errmsg);

    UniValue r(UniValue::VOBJ);
    r.pushKV("token", token);
    r.pushKV("include_fees", includeFees);
    r.pushKV("split_amount_requested", xbridge::xBridgeStringValueFromAmount(sa));
    r.pushKV("split_amount_with_fees", xbridge::xBridgeStringValueFromAmount(splitInclFees));
    r.pushKV("split_utxo_count", splitCount);
    r.pushKV("split_total", xbridge::xBridgeStringValueFromAmount(totalSplit));
    r.pushKV("txid", txid);
    r.pushKV("rawtx", showRawTx ? rawtx : "");
    return r;
}


UniValue dxSplitInputs(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 3 || request.params.size() > 7)
        throw std::runtime_error(
            RPCHelpMan{"dxSplitInputs",
                "\nSplits specified UTXOs into the given size and address. Left over amounts "
                "end up in change. UTXOs being used in existing orders will not be included "
                "by the splitter (view with dxGetUtxos). You can only split UTXOs for assets "
                "supported by your node (view with dxGetLocalTokens).\n",
                {
                   {"token", RPCArg::Type::STR, RPCArg::Optional::NO, "The ticker of the asset you want to split UTXOs for."},
                   {"split_amount", RPCArg::Type::STR, RPCArg::Optional::NO, "The desired UTXO output size."},
                   {"address", RPCArg::Type::STR, RPCArg::Optional::NO, "The address split UTXOs and change will be sent to."},
                   {"include_fees", RPCArg::Type::BOOL, RPCArg::Optional::NO, "Include the trade P2SH deposit fees in the split UTXO (add deposit fee to `spit_amount` value."},
                   {"show_rawtx", RPCArg::Type::BOOL, RPCArg::Optional::NO, "Include the raw transaction in the response (can be submitted manually)."},
                   {"submit", RPCArg::Type::BOOL, RPCArg::Optional::NO, "Submit the raw transaction to the network."},
                   {"utxos", RPCArg::Type::ARR, RPCArg::Optional::NO, "List of UTXO inputs.",
                    {
                        {"", RPCArg::Type::OBJ, RPCArg::Optional::OMITTED, "",
                         {
                             {"txid", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "The UTXO transaction ID."},
                             {"vout", RPCArg::Type::NUM, RPCArg::Optional::NO, "The UTXO output index."},
                         },
                        },
                    }}
                },
                RPCResult{
                R"(
    {
        "token": "BLOCK",
        "include_fees": true,
        "split_amount_requested": "4.0",
        "split_amount_with_fees": "4.00040000",
        "split_utxo_count": 6,
        "split_total": "24.44852981",
        "txid": "7f87cba104b2c19f6e25fbc82b3cde5d73714e01d6a54943d3c8fb07ce315db4",
        "rawtx": ""
    }

    Key                    | Type | Description
    -----------------------|------|----------------------------------------------
    token                  | str  | The asset you are splitting UTXOs for.
    include_fees           | bool | Whether you requested to include the fees.
    split_amount_requested | str  | The requested split amount.
    split_amount_with_fees | str  | The requested split amount with fee included.
    split_utxo_count       | int  | The amount of resulting split UTXOs.
    split_total            | str  | The total amount of in the address prior to
                           |      | splitting.
    txid                   | str  | The hex string of the splitting transaction.
    rawtx                  | str  | The hex string of the raw splitting
                           |      | transaction.
                )"
                },
                RPCExamples{
                     HelpExampleCli("dxSplitInputs", "BLOCK 10.5 BWQrvmuHB4C68KH5V7fcn9bFtWN8y5hBmR true false true [{\"txid\":\"ed7d16abd5c0bf42dec36335d0f63938f1d9c10e7202bc780b888a51d291d3dc\",\"vout\":0},{\"txid\":\"ed7d16abd5c0bf42dec36335d0f63938f1d9c10e7202bc780b888a51d291d3dc\",\"vout\":1}]")
                   + HelpExampleRpc("dxSplitInputs", "\"BLOCK\", \"10.5\", \"BWQrvmuHB4C68KH5V7fcn9bFtWN8y5hBmR\", true, false, true, [{\"txid\":\"ed7d16abd5c0bf42dec36335d0f63938f1d9c10e7202bc780b888a51d291d3dc\",\"vout\":0},{\"txid\":\"ed7d16abd5c0bf42dec36335d0f63938f1d9c10e7202bc780b888a51d291d3dc\",\"vout\":1}]")
                },
            }.ToString());

    auto token = request.params[0].get_str();
    auto splitAmount = request.params[1].get_str();
    auto address = request.params[2].get_str();
    bool includeFees = request.params[3].get_bool();
    bool showRawTx = request.params[4].get_bool();
    bool submitTx = request.params[5].get_bool();
    const auto paramUtxos = request.params[6].get_array();
    if (paramUtxos.empty())
        return xbridge::makeError(xbridge::BAD_REQUEST, __FUNCTION__, "No utxos were specified");

    std::set<COutPoint> userUtxos;
    for (const auto & val : paramUtxos.getValues()) {
        std::map<std::string, UniValue> utxo;
        val.getObjMap(utxo);
        userUtxos.insert(COutPoint{uint256S(utxo["txid"].get_str()), (uint32_t)utxo["vout"].get_int()});
    }

    auto & xapp = xbridge::App::instance();
    xbridge::WalletConnectorPtr conn = xapp.connectorByCurrency(token);
    if (!conn)
        return xbridge::makeError(xbridge::NO_SESSION, __FUNCTION__, token);

    auto excludedUtxos = xapp.getAllLockedUtxos(token);
    for (const auto & utxo : excludedUtxos) {
        COutPoint vout{uint256S(utxo.txId), utxo.vout};
        if (userUtxos.count(vout))
            return xbridge::makeError(xbridge::BAD_REQUEST, __FUNCTION__, "Cannot split utxo already in use: " + vout.ToString());
    }

    const CAmount sa = xbridge::xBridgeIntFromReal(boost::lexical_cast<double>(splitAmount));
    std::string txid, rawtx, failReason;
    CAmount totalSplit{0};
    CAmount splitInclFees{0};
    int splitCount{0};
    if (!conn->splitUtxos(sa, address, includeFees, excludedUtxos, userUtxos, totalSplit, splitInclFees, splitCount, txid, rawtx, failReason))
        return xbridge::makeError(xbridge::BAD_REQUEST, __FUNCTION__, failReason);

    int errorcode{0};
    std::string txid2, errmsg;
    if (submitTx && !conn->sendRawTransaction(rawtx, txid2, errorcode, errmsg))
        return xbridge::makeError(xbridge::BAD_REQUEST, __FUNCTION__, errmsg);

    UniValue r(UniValue::VOBJ);
    r.pushKV("token", token);
    r.pushKV("include_fees", includeFees);
    r.pushKV("split_amount_requested", xbridge::xBridgeStringValueFromAmount(sa));
    r.pushKV("split_amount_with_fees", xbridge::xBridgeStringValueFromAmount(splitInclFees));
    r.pushKV("split_utxo_count", splitCount);
    r.pushKV("split_total", xbridge::xBridgeStringValueFromAmount(totalSplit));
    r.pushKV("txid", txid);
    r.pushKV("rawtx", showRawTx ? rawtx : "");
    return r;
}

UniValue dxGetUtxos(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 1 || request.params.size() > 2)
        throw std::runtime_error(
            RPCHelpMan{"dxGetUtxos",
                "\nReturns all compatible and unlocked UTXOs for the specified asset. "
                "Currently only P2PKH UTXOs are supported (Segwit UTXOs not supported). "
                "You can only view UTXOs for assets supported by your node (view with dxGetLocalTokens).\n",
                {
                   {"token", RPCArg::Type::STR, RPCArg::Optional::NO, "The ticker of the asset you want to view UTXOs for."},
                   {"include_used", RPCArg::Type::BOOL, "false", "Include UTXOs used in existing orders."},
                },
                RPCResult{
                R"(
    [
        {
            "txid": "c019edf2a71efcfc9b1ec50cd0d9db54c55b74acd0bcc81cefd6ffbba359a210",
            "vout": 2,
            "amount": "3.26211780",
            "address": "BrPHj12ZSm7roD2gvrjRG2gD4TzeP1YDXG",
            "scriptPubKey": "7b1ef56a92cec50cd0d147876a914ffd6fcbb4c5724a4057de",
            "confirmations": 11904,
            "orderid": ""
        },
        {
            "txid": "a91c224c0725745cd0bcc81cefd6ffbba3f6cc36956cd566c50cd0d9db5c55b7",
            "vout": 0,
            "amount": "2.44485198",
            "address": "BJYS5dd4Mx5bFxfYDX136SLrv5kGCZaUtF",
            "scriptPubKey": "7e36ab914fc645b2b9fd5ce704f54bc34a59a56c9671eb355b",
            "confirmations": 20690,
            "orderid": "e1b0f4bf05e6c47506abf5d717c95baa1b6de79dd1758673a8cdd171ddad6578"
        }
    ]

    Key             | Type | Description
    ----------------|------|-----------------------------------------------------
    txid            | str  | Transaction ID of the UTXO.
    vout            | int  | Vout index of the UTXO.
    amount          | str  | UTXO amount.
    address         | str  | UTXO address.
    scriptPubKey    | str  | UTXO address script pubkey.
    confirmations   | int  | UTXO blockchain confirmation count.
    orderid         | str  | The order ID if the UTXO is currently being used in
                    |      | an order.
                )"
                },
                RPCExamples{
                     HelpExampleCli("dxGetUtxos", "BLOCK")
                   + HelpExampleRpc("dxGetUtxos", "\"BLOCK\"")
                   + HelpExampleCli("dxGetUtxos", "BTC")
                   + HelpExampleRpc("dxGetUtxos", "\"BTC\"")
                   + HelpExampleCli("dxGetUtxos", "BLOCK true")
                   + HelpExampleRpc("dxGetUtxos", "\"BLOCK\", true")
                },
            }.ToString());

    const auto token = request.params[0].get_str();
    bool includeUsed{false};
    if (!request.params[1].isNull())
        includeUsed = request.params[1].get_bool();

    auto & xapp = xbridge::App::instance();
    xbridge::WalletConnectorPtr conn = xapp.connectorByCurrency(token);
    if (!conn)
        return xbridge::makeError(xbridge::NO_SESSION, __FUNCTION__, token);

    std::set<xbridge::wallet::UtxoEntry> excluded = xapp.getAllLockedUtxos(token);
    std::vector<xbridge::wallet::UtxoEntry> unspent;
    if (!conn->getUnspent(unspent, !includeUsed ? excluded : std::set<xbridge::wallet::UtxoEntry>{}))
        return xbridge::makeError(xbridge::BAD_REQUEST, __FUNCTION__, "failed to get unspent transaction outputs");

    UniValue r(UniValue::VARR);
    for (const auto & utxo : unspent) {
        UniValue o(UniValue::VOBJ);
        o.pushKV("txid", utxo.txId);
        o.pushKV("vout", static_cast<int>(utxo.vout));
        o.pushKV("amount", xbridge::xBridgeStringValueFromPrice(utxo.amount, conn->COIN));
        o.pushKV("address", utxo.address);
        o.pushKV("scriptPubKey", utxo.scriptPubKey);
        o.pushKV("confirmations", static_cast<int>(utxo.confirmations));
        o.pushKV("orderid", "");
        if (excluded.count(utxo) > 0) {
            auto orderid = xapp.orderWithUtxo(utxo);
            o.pushKV("orderid", orderid.IsNull() ? "" : orderid.GetHex());
        }
        r.push_back(o);
    }
    return r;
}

// clang-format off
static const CRPCCommand commands[] =
{ //  category             name                          actor (function)              argNames
  //  -------------------- ----------------------------- ----------------------------- ----------
    { "xbridge",           "dxGetOrderFills",            &dxGetOrderFills,             {} },
    { "xbridge",           "dxGetOrders",                &dxGetOrders,                 {} },
    { "xbridge",           "dxGetOrder",                 &dxGetOrder,                  {} },
    { "xbridge",           "dxGetLocalTokens",           &dxGetLocalTokens,            {} },
    { "xbridge",           "dxLoadXBridgeConf",          &dxLoadXBridgeConf,           {} },
    { "xbridge",           "dxGetNewTokenAddress",       &dxGetNewTokenAddress,        {} },
    { "xbridge",           "dxGetNetworkTokens",         &dxGetNetworkTokens,          {} },
    { "xbridge",           "dxMakeOrder",                &dxMakeOrder,                 {} },
    { "xbridge",           "dxMakePartialOrder",         &dxMakePartialOrder,          {} },
    { "xbridge",           "dxTakeOrder",                &dxTakeOrder,                 {} },
    { "xbridge",           "dxCancelOrder",              &dxCancelOrder,               {} },
    { "xbridge",           "dxGetOrderHistory",          &dxGetOrderHistory,           {} },
    { "xbridge",           "dxGetOrderBook",             &dxGetOrderBook,              {} },
    { "xbridge",           "dxGetTokenBalances",         &dxGetTokenBalances,          {} },
    { "xbridge",           "dxGetMyOrders",              &dxGetMyOrders,               {} },
    { "xbridge",           "dxGetMyPartialOrderChain",   &dxGetMyPartialOrderChain,    {"order_id"} },
    { "xbridge",           "dxPartialOrderChainDetails", &dxPartialOrderChainDetails,  {"order_id"} },
    { "xbridge",           "dxGetLockedUtxos",           &dxGetLockedUtxos,            {} },
    { "xbridge",           "dxFlushCancelledOrders",     &dxFlushCancelledOrders,      {} },
    { "xbridge",           "gettradingdata",             &gettradingdata,              {} },
    { "xbridge",           "dxGetTradingData",           &dxGetTradingData,            {} },
    { "xbridge",           "dxSplitAddress",             &dxSplitAddress,              {"token", "splitamount", "address", "include_fees", "show_rawtx", "submit"} },
    { "xbridge",           "dxSplitInputs",              &dxSplitInputs,               {"token", "splitamount", "address", "include_fees", "show_rawtx", "submit", "utxos"} },
    { "xbridge",           "dxGetUtxos",                 &dxGetUtxos,                  {"token", "include_used"} },
};
// clang-format on

void RegisterXBridgeRPCCommands(CRPCTable &t)
{
    for (const auto & command : commands)
        t.appendCommand(command.name, &command);
}
