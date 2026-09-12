// Copyright (c) 2018-2019 The Blocknet developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <xrouter/xrouterconnectorbtc.h>

#include <xrouter/xroutererror.h>

#include <bloom.h>
#include <util/strencodings.h>


namespace xrouter
{

static UniValue getResult(const std::string & obj)
{
    UniValue obj_val;
    if (!obj_val.read(obj) || obj_val.isNull())
        return UniValue(obj);
    const UniValue & r = find_value(obj_val.get_obj(), "result");
    if (r.isNull())
        return UniValue(obj);
    return r;
}

static bool hasError(const std::string & data)
{
    UniValue val;
    if (!val.read(data) || !val.isObject())
        return false;

    const auto & o = val.get_obj();
    const UniValue & error = find_value(o, "error");
    return !error.isNull();
}

static std::string checkError(const std::string & data, int code)
{
    UniValue val;
    if (!val.read(data) || !val.isObject())
        return data;

    const UniValue & error = find_value(val.get_obj(), "error");
    if (error.isNull())
        return data;

    UniValue o = val.get_obj();
    const UniValue & code_val = find_value(o, "code");
    if (code_val.isNull())
        o.pushKV("code", code);
    return o.write();
}

static double parseVout(UniValue vout, std::string account)
{
    double result = 0.0;
    double val = find_value(vout.get_obj(), "value").get_real();
    UniValue src = find_value(vout.get_obj(), "scriptPubKey").get_obj();
    const UniValue & addr_val = find_value(src, "addresses");
    if (addr_val.isNull())
        return 0.0;
    UniValue addr = addr_val.get_array();

    for (unsigned int k = 0; k != addr.size(); k++ ) {
        std::string cur_addr = addr[k].get_str();
        if (cur_addr == account)
            result += val;
    }

    return result;
}

std::string BtcWalletConnectorXRouter::getBlockCount() const
{
    std::string command("getblockcount");
    UniValue params(UniValue::VARR);
    return CallRPC(m_user, m_passwd, m_ip, m_port, command, params, jsonver, contenttype);
}

std::string BtcWalletConnectorXRouter::getBlockHash(const int & block) const
{
    std::string command("getblockhash");
UniValue params(UniValue::VARR); params.push_back(block);
    return CallRPC(m_user, m_passwd, m_ip, m_port, command, params, jsonver, contenttype);
}

std::string BtcWalletConnectorXRouter::getBlock(const std::string & blockHash) const
{
    static const std::string command("getblock");
UniValue params(UniValue::VARR); params.push_back(blockHash);
    return CallRPC(m_user, m_passwd, m_ip, m_port, command, params, jsonver, contenttype);
}

std::vector<std::string> BtcWalletConnectorXRouter::getBlocks(const std::vector<std::string> & blockHashes) const
{
    static const std::string commandGB("getblock");

    std::set<std::string> unique{blockHashes.begin(), blockHashes.end()};
    std::map<std::string, std::string> results;
    std::vector<std::string> list;

    for (const auto & hash : unique)
        results[hash] = getBlock(hash);

    for (const auto & hash : blockHashes)
        list.push_back(results[hash]);

    return list;
}

std::string BtcWalletConnectorXRouter::getTransaction(const std::string & hash) const
{
    static const std::string commandGRT("getrawtransaction");
    UniValue params(UniValue::VARR); params.push_back(hash);
    const auto & rawTr = CallRPC(m_user, m_passwd, m_ip, m_port, commandGRT, params, jsonver, contenttype);

    if (hasError(rawTr)) {
        return rawTr;
    } else {
        const auto & rawTr_val = getResult(rawTr);
        std::string hex;
        if (!rawTr_val.isStr())
            return "";
        hex = rawTr_val.get_str();
        static const std::string commandDRT("decoderawtransaction");
        UniValue drtparams(UniValue::VARR); drtparams.push_back(hex);
        return CallRPC(m_user, m_passwd, m_ip, m_port, commandDRT, drtparams, jsonver, contenttype);
    }
}

std::vector<std::string> BtcWalletConnectorXRouter::getTransactions(const std::vector<std::string> & txHashes) const
{
    static const std::string commandGRT("getrawtransaction");
    static const std::string commandDRT("decoderawtransaction");

    std::set<std::string> unique{txHashes.begin(), txHashes.end()};
    std::map<std::string, std::string> results;
    std::vector<std::string> list;

    for (const auto & hash : unique)
        results[hash] = getTransaction(hash);

    for (const auto & hash : txHashes)
        list.push_back(results[hash]);

    return list;
}

std::string BtcWalletConnectorXRouter::decodeRawTransaction(const std::string & hex) const
{
    static const std::string commandDRT("decoderawtransaction");
    UniValue params(UniValue::VARR); params.push_back(hex);
    return CallRPC(m_user, m_passwd, m_ip, m_port, commandDRT, params, jsonver, contenttype);
}

std::vector<std::string> BtcWalletConnectorXRouter::getTransactionsBloomFilter(const int & number, CDataStream & stream, const int & fetchlimit) const
{
    static const std::string commandGBC("getblockcount");
    static const std::string commandGBH("getblockhash");
    static const std::string commandGB("getblock");
    static const std::string commandGRT("getrawtransaction");
    static const std::string commandDRT("decoderawtransaction");

    CBloomFilter ft;
    stream >> ft;
    CBloomFilter filter(ft);
    filter.UpdateEmptyFull();

    std::vector<std::string> results;

    const auto & blockCountObj = CallRPC(m_user, m_passwd, m_ip, m_port, commandGBC, UniValue(UniValue::VARR), jsonver, contenttype);
    int blockcount = getResult(blockCountObj).get_int();

    if ((fetchlimit > 0) && (blockcount - number > fetchlimit)) {
        throw XRouterError("Too many blocks requested", xrouter::INVALID_PARAMETERS);
    }
    
    for (int id = number; id <= blockcount; id++)
    {
        UniValue bhparams(UniValue::VARR); bhparams.push_back(id);
        const auto & blockHashObj = CallRPC(m_user, m_passwd, m_ip, m_port, commandGBH, bhparams, jsonver, contenttype);
        const auto & hash = getResult(blockHashObj).get_str();
        UniValue gbparams(UniValue::VARR); gbparams.push_back(hash);
        const auto & blockObj = CallRPC(m_user, m_passwd, m_ip, m_port, commandGB, gbparams, jsonver, contenttype);
        UniValue block = getResult(blockObj).get_obj();

        UniValue txs = find_value(block, "tx").get_array();

        for (const auto & j : txs.getValues()) {
            const auto & txid = j.get_str();
            UniValue grtparams(UniValue::VARR); grtparams.push_back(txid);
            const auto & rawTrObj = CallRPC(m_user, m_passwd, m_ip, m_port, commandGRT, grtparams, jsonver, contenttype);
            const auto & txData_str = getResult(rawTrObj).get_str();

            std::vector<unsigned char> txData(ParseHex(txData_str));
            CDataStream ssData(txData, SER_NETWORK, PROTOCOL_VERSION);
            CMutableTransaction mtx;
            ssData >> mtx;

            const CTransaction ctx(mtx);
            if (filter.IsRelevantAndUpdate(ctx)) {
                results.push_back(txData_str);
            }
        }
    }
    
    return results;
}

std::string BtcWalletConnectorXRouter::sendTransaction(const std::string & transaction) const
{
    static const std::string command("sendrawtransaction");
    UniValue params(UniValue::VARR); params.push_back(transaction);
    return checkError(CallRPC(m_user, m_passwd, m_ip, m_port, command, params, jsonver, contenttype), BAD_REQUEST);
}

std::string BtcWalletConnectorXRouter::convertTimeToBlockCount(const std::string & timestamp) const
{
//    const auto & blockCountObj = CallRPC(m_user, m_passwd, m_ip, m_port, "getblockcount", {}, jsonver, contenttype);
//    Value res = getResult(blockCountObj);
//    int blockcount = res.get_int();
//    if (blockcount <= 51)
//        return "0";
//
//    // Estimate average block creation time from the last 10 blocks
//    Array paramsGBH { blockcount };
//    Object blockHashObj = CallRPC(m_user, m_passwd, m_ip, m_port, "getblockhash", paramsGBH, jsonver, contenttype);
//    std::string hash = getResult(blockHashObj).get_str();
//    Array paramsGB { hash };
//    Object blockObj = CallRPC(m_user, m_passwd, m_ip, m_port, "getblock", paramsGB, jsonver, contenttype);
//    Object block = getResult(blockObj).get_obj();
//    int curblocktime = find_value(block, "time").get_int();
//
//    Array paramsGBH2 { blockcount-50 };
//    blockHashObj = CallRPC(m_user, m_passwd, m_ip, m_port, "getblockhash", paramsGBH2, jsonver, contenttype);
//    hash = getResult(blockHashObj).get_str();
//    Array paramsGB2 { hash };
//    blockObj = CallRPC(m_user, m_passwd, m_ip, m_port, "getblock", paramsGB2, jsonver, contenttype);
//    block = getResult(blockObj).get_obj();
//    int blocktime50ago = find_value(block, "time").get_int();
//
//    int averageBlockTime = (curblocktime - blocktime50ago) / 50;
//
//    int time = std::stoi(timestamp);
//
//    if (time >= curblocktime)
//        return std::to_string(blockcount);
//
//    int blockEstimate = blockcount - (curblocktime - time) / averageBlockTime;
//    if (blockEstimate <= 1)
//        return "0";
//
//    Array paramsGBH3 { blockEstimate };
//    blockHashObj = CallRPC(m_user, m_passwd, m_ip, m_port, "getblockhash", paramsGBH3, jsonver, contenttype);
//    hash = getResult(blockHashObj).get_str();
//    Array paramsGB3 { hash };
//    blockObj = CallRPC(m_user, m_passwd, m_ip, m_port, "getblock", paramsGB3, jsonver, contenttype);
//    block = getResult(blockObj).get_obj();
//    int cur_estimate = find_value(block, "time").get_int();
//
//    int sign = 1;
//    if (cur_estimate > time)
//        sign = -1;
//
//    while (true) {
//        blockEstimate += sign;
//        Array paramsGBH { blockEstimate };
//        Object blockHashObj = CallRPC(m_user, m_passwd, m_ip, m_port, "getblockhash", paramsGBH, jsonver, contenttype);
//        std::string hash = getResult(blockHashObj).get_str();
//        Array paramsGB { hash };
//        Object blockObj = CallRPC(m_user, m_passwd, m_ip, m_port, "getblock", paramsGB, jsonver, contenttype);
//        Object block = getResult(blockObj).get_obj();
//        int new_estimate = find_value(block, "time").get_int();
//        //std::cout << blockEstimate << " " << new_estimate << " " << cur_estimate << " " << (new_estimate - time) << " " << (cur_estimate - time) << " " << (cur_estimate - time) * (new_estimate - time) << std::endl << std::flush;
//        int sign1 = (cur_estimate - time > 0) ? 1 : -1;
//        if (sign1 * (new_estimate - time) < 0) {
//            if (sign > 0)
//                return std::to_string(blockEstimate - 1);
//            else
//                return std::to_string(blockEstimate);
//        }
//
//        cur_estimate = new_estimate;
//    }
    
    return "0"; // TODO Implement
}

std::string BtcWalletConnectorXRouter::getBalance(const std::string & address) const
{
    return "0"; // TODO Implement
}

} // namespace xrouter
