// Copyright (c) 2018-2019 The Blocknet developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <xrouter/xrouterconnectoreth.h>

#include <tinyformat.h>
#include <uint256.h>

#include <univalue.h>

#include <boost/lexical_cast.hpp>

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

static std::string dec2hex(const unsigned int & n) {
    std::stringstream ss;
    ss << std::hex << n;
    return "0x" + ss.str();
}

static std::string dec2hex(const std::string & s) {
    return dec2hex(boost::lexical_cast<unsigned int>(s));
}

static unsigned int hex2dec(const std::string & s) {
    unsigned int result;
    std::stringstream ss;
    ss << std::hex << s;
    ss >> result;
    return result;
}

std::string EthWalletConnectorXRouter::getBlockCount() const
{
    static const std::string command("eth_blockNumber");
    const auto & data = CallRPC(m_user, m_passwd, m_ip, m_port, command, UniValue(UniValue::VARR), jsonver, contenttype);

    UniValue data_val;
    if (!data_val.read(data) || !data_val.isObject())
        return data;

    const auto & result_val = getResult(data);
    if (!result_val.isStr())
        return data;

    auto blockCount = hex2dec(result_val.get_str());

    // Replace the "result" hex string with the decimal block count
    UniValue o = data_val.get_obj();
    const auto & keys = o.getKeys();
    const auto & values = o.getValues();
    UniValue nret(UniValue::VOBJ);
    for (size_t i = 0; i < keys.size(); ++i) {
        if (keys[i] == "result")
            nret.pushKV("result", static_cast<int>(blockCount));
        else if (i < values.size())
            nret.pushKV(keys[i], values[i]);
    }
    return nret.write();
}

std::string EthWalletConnectorXRouter::getBlockHash(const int & block) const
{
    static const std::string command("eth_getBlockByNumber");
    UniValue params(UniValue::VARR);
    params.push_back(dec2hex(block));
    params.push_back(false);
    return CallRPC(m_user, m_passwd, m_ip, m_port, command, params, jsonver, contenttype);
}

std::string EthWalletConnectorXRouter::getBlock(const std::string & blockHash) const
{
    static const std::string command("eth_getBlockByHash");
    UniValue params(UniValue::VARR);
    params.push_back(blockHash);
    params.push_back(false);
    return CallRPC(m_user, m_passwd, m_ip, m_port, command, params, jsonver, contenttype);
}

std::vector<std::string> EthWalletConnectorXRouter::getBlocks(const std::vector<std::string> & blockHashes) const
{
    std::vector<std::string> results;
    for (const auto & hash : blockHashes)
        results.push_back(getBlock(hash));
    return results;
}

std::string EthWalletConnectorXRouter::getTransaction(const std::string & trHash) const
{
    static const std::string command("eth_getTransactionByHash");
    UniValue params(UniValue::VARR);
    params.push_back(trHash);
    return CallRPC(m_user, m_passwd, m_ip, m_port, command, params, jsonver, contenttype);
}

std::string EthWalletConnectorXRouter::decodeRawTransaction(const std::string & trHash) const
{
    UniValue unsupported(UniValue::VOBJ);
    unsupported.pushKV("error", "Unsupported");
    return unsupported.write(/*prettyIndent=*/4, /*indentLevel=*/1);
}

std::vector<std::string> EthWalletConnectorXRouter::getTransactions(const std::vector<std::string> & txHashes) const
{
    std::vector<std::string> results;
    for (const auto & hash : txHashes)
        results.push_back(getTransaction(hash));
    return results;
}

std::vector<std::string> EthWalletConnectorXRouter::getTransactionsBloomFilter(const int &, CDataStream &, const int &) const
{
    UniValue unsupported(UniValue::VOBJ);
    unsupported.pushKV("error", "Unsupported");
    return std::vector<std::string>{unsupported.write(/*prettyIndent=*/4, /*indentLevel=*/1)};
}

std::string EthWalletConnectorXRouter::sendTransaction(const std::string & rawtx) const
{
    static const std::string command("eth_sendRawTransaction");
    UniValue params(UniValue::VARR);
    params.push_back(rawtx);
    return CallRPC(m_user, m_passwd, m_ip, m_port, command, params, jsonver, contenttype);
}

std::string EthWalletConnectorXRouter::convertTimeToBlockCount(const std::string & timestamp) const
{
    UniValue unsupported(UniValue::VOBJ);
    unsupported.pushKV("error", "Unsupported");
    return unsupported.write(/*prettyIndent=*/4, /*indentLevel=*/1);
}

std::string EthWalletConnectorXRouter::getBalance(const std::string & address) const
{
    UniValue unsupported(UniValue::VOBJ);
    unsupported.pushKV("error", "Unsupported");
    return unsupported.write(/*prettyIndent=*/4, /*indentLevel=*/1);
}

} // namespace xrouter
