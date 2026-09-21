// Copyright (c) 2017-2019 The Blocknet developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

//******************************************************************************
//******************************************************************************

#include <xbridge/xbridgewalletconnectordgb.h>

#include <xbridge/util/logger.h>
#include <xbridge/util/xutil.h>
#include <xbridge/xbitcointransaction.h>


//*****************************************************************************
//*****************************************************************************
namespace xbridge
{

//*****************************************************************************
//*****************************************************************************
namespace rpc
{


//*****************************************************************************
//*****************************************************************************
bool createRawTransaction(const std::string & rpcuser,
                          const std::string & rpcpasswd,
                          const std::string & rpcip,
                          const std::string & rpcport,
                          const std::vector<XTxIn> & inputs,
                          const std::vector<std::pair<std::string, std::string> > & outputs,
                          const uint32_t lockTime,
                          std::string & tx,
                          bool cltv);

//*****************************************************************************
//*****************************************************************************
bool decodeRawTransaction(const std::string & rpcuser, const std::string & rpcpasswd,
                          const std::string & rpcip, const std::string & rpcport,
                          const std::string & rawtx,
                          std::string & txid, std::string & tx);

//*****************************************************************************
//*****************************************************************************
namespace
{

//*****************************************************************************
//*****************************************************************************
bool signRawTransactionWithWallet(const std::string & rpcuser,
                                  const std::string & rpcpasswd,
                                  const std::string & rpcip,
                                  const std::string & rpcport,
                                  std::string & rawtx,
                                  bool & complete)
{
    try
    {
        LOG() << "rpc call <signrawtransactionwithwallet>";

        UniValue params(UniValue::VARR);
        params.push_back(rawtx);

        UniValue reply = CallRPC(rpcuser, rpcpasswd, rpcip, rpcport, "signrawtransactionwithwallet", params);

        // Parse reply
        const UniValue & result = find_value(reply, "result");
        const UniValue & error  = find_value(reply, "error");

        if (!error.isNull())
        {
            // Error
            LOG() << "error: " << error.write();
            // int code = find_value(error.get_obj(), "code").get_int();
            return false;
        }
        else if (!result.isObject())
        {
            // Result
            LOG() << "result not an object " <<
                     (result.isNull() ? "" :
                      result.isStr()  ? result.get_str() :
                                                   result.write(4, 1));
            return false;
        }

        UniValue obj = result.get_obj();
        const UniValue & tx = find_value(obj, "hex");
        const UniValue & cpl = find_value(obj, "complete");

        if (!tx.isStr() || !cpl.isBool())
        {
            LOG() << "bad hex " <<
                     (tx.isNull() ? "" :
                      tx.isStr()  ? tx.get_str() :
                                                   tx.write(4, 1));
            return false;
        }

        rawtx    = tx.get_str();
        complete = cpl.get_bool();

    }
    catch (std::exception & e)
    {
        LOG() << "signrawtransactionwithwallet exception " << e.what();
        return false;
    }

    return true;
}

} // namespace
} // namespace rpc

//******************************************************************************
//******************************************************************************
DgbWalletConnector::DgbWalletConnector()
    : BtcWalletConnector()
{

}

//******************************************************************************
//******************************************************************************
bool DgbWalletConnector::createDepositTransaction(const std::vector<XTxIn> & inputs,
                                                  const std::vector<std::pair<std::string, std::string> > & outputs,
                                                  std::string & txId,
                                                  uint32_t & txVout,
                                                  std::string & rawTx)
{
    if (!rpc::createRawTransaction(m_user, m_passwd, m_ip, m_port,
                                   inputs, outputs, 0, rawTx, true))
    {
        // cancel transaction
        LOG() << "create transaction error, transaction canceled " << __FUNCTION__;
        return false;
    }

    // sign
    bool complete = false;
    if (!rpc::signRawTransactionWithWallet(m_user, m_passwd, m_ip, m_port, rawTx, complete))
    {
        // do not sign, cancel
        LOG() << "sign transaction error, transaction canceled " << __FUNCTION__;
        return false;
    }

    if(!complete)
    {
        LOG() << "transaction not fully signed " << __FUNCTION__;
        return false;
    }

    std::string txid;
    std::string json;
    if (!rpc::decodeRawTransaction(m_user, m_passwd, m_ip, m_port, rawTx, txid, json))
    {
        LOG() << "decode signed transaction error, transaction canceled " << __FUNCTION__;
        return false;
    }

    txId = txid;
    txVout = 0;

    return true;
}

} // namespace xbridge
