// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2018 The Bitcoin Core developers
// Copyright (c) 2020 The Blocknet developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BLOCKNET_XBRIDGE_XBRIDGEDB_H
#define BLOCKNET_XBRIDGE_XBRIDGEDB_H

#include <xbridge/xbridgetransactiondescr.h>

#include <fs.h>
#include <serialize.h>
#include <string>
#include <uint256.h>

namespace xbridge {

typedef std::map<uint256, TransactionDescr> XOrderSet;

/** XBridge order db (orders.dat) */
class XBridgeDB
{
public:
    explicit XBridgeDB();
    bool Write(const XOrderSet & orderSet, bool force = false);
    bool Read(XOrderSet & orderSet);
    bool Exists();
    bool Create();
    bool ShouldSave();
private:
    // Best-effort one-time backup orders.dat -> orders.dat.pre-v2.bak before
    // the first overwrite. Version-agnostic (no format peek): runs at most
    // once per datadir, so the pre-upgrade copy is never overwritten.
    bool BackupOnce();
    const fs::path pathDB;
    boost::posix_time::ptime lastsave;
    bool lastOrdersEmpty{false};
};

}

#endif // BLOCKNET_XBRIDGE_XBRIDGEDB_H
