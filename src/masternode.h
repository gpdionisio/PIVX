// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2020 The PIVX developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef MASTERNODE_H
#define MASTERNODE_H

#include "base58.h"
#include "key.h"
#include "messagesigner.h"
#include "net.h"
#include "sync.h"
#include "timedata.h"
#include "util.h"

/* Depth of the block pinged by masternodes */
static const unsigned int MNPING_DEPTH = 12;

class CMasternode;
class CMasternodeBroadcast;
class CMasternodePing;

int MasternodeMinPingSeconds();
int MasternodeBroadcastSeconds();
int MasternodeCollateralMinConf();
int MasternodePingSeconds();
int MasternodeExpirationSeconds();
int MasternodeRemovalSeconds();

//
// The Masternode Ping Class : Contains a different serialize method for sending pings from masternodes throughout the network
//

class CMasternodePing : public CSignedMessage
{
public:
    CTxIn vin;
    uint256 blockHash;
    int64_t sigTime; //mnb message times

    CMasternodePing();
    CMasternodePing(const CTxIn& newVin, const uint256& nBlockHash);

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(vin);
        READWRITE(blockHash);
        READWRITE(sigTime);
        READWRITE(vchSig);
        try
        {
            READWRITE(nMessVersion);
        } catch (...) {
            nMessVersion = MessageVersion::MESS_VER_STRMESS;
        }
    }

    uint256 GetHash() const;

    // override CSignedMessage functions
    uint256 GetSignatureHash() const override { return GetHash(); }
    std::string GetStrMessage() const override;
    const CTxIn GetVin() const override  { return vin; };
    bool IsNull() { return blockHash.IsNull() || vin.prevout.IsNull(); }

    bool CheckAndUpdate(int& nDos, bool fRequireEnabled = true, bool fCheckSigTimeOnly = false);
    void Relay();

    void swap(CMasternodePing& first, CMasternodePing& second) // nothrow
    {
        CSignedMessage::swap(first, second);

        // enable ADL (not necessary in our case, but good practice)
        using std::swap;

        // by swapping the members of two classes,
        // the two classes are effectively swapped
        swap(first.vin, second.vin);
        swap(first.blockHash, second.blockHash);
        swap(first.sigTime, second.sigTime);
    }

    CMasternodePing& operator=(CMasternodePing from)
    {
        swap(*this, from);
        return *this;
    }

    friend bool operator==(const CMasternodePing& a, const CMasternodePing& b)
    {
        return a.vin == b.vin && a.blockHash == b.blockHash;
    }
    friend bool operator!=(const CMasternodePing& a, const CMasternodePing& b)
    {
        return !(a == b);
    }
};

//
// The Masternode Class. It contains the input of the 10000 PIV, signature to prove
// it's the one who own that ip address and code for calculating the payment election.
//
class CMasternode : public CSignedMessage
{
private:
    // critical section to protect the inner data structures
    mutable RecursiveMutex cs;
    int64_t lastTimeChecked;

public:
    enum state {
        MASTERNODE_PRE_ENABLED,
        MASTERNODE_ENABLED,
        MASTERNODE_EXPIRED,
        MASTERNODE_REMOVE,
        MASTERNODE_WATCHDOG_EXPIRED,
        MASTERNODE_POSE_BAN,
        MASTERNODE_VIN_SPENT,
        MASTERNODE_POS_ERROR,
        MASTERNODE_MISSING
    };

    CTxIn vin;
    CService addr;
    CPubKey pubKeyCollateralAddress;
    CPubKey pubKeyMasternode;
    int activeState;
    int64_t sigTime; //mnb message time
    bool unitTest;
    bool allowFreeTx;
    int protocolVersion;
    int64_t nLastDsq; //the dsq count from the last dsq broadcast of this node
    int nScanningErrorCount;
    int nLastScanningErrorBlockHeight;
    CMasternodePing lastPing;

    CMasternode();
    CMasternode(const CMasternode& other);

    // override CSignedMessage functions
    uint256 GetSignatureHash() const override;
    std::string GetStrMessage() const override;
    const CTxIn GetVin() const override { return vin; };
    const CPubKey GetPublicKey(std::string& strErrorRet) const override { return pubKeyCollateralAddress; }

    void swap(CMasternode& first, CMasternode& second) // nothrow
    {
        CSignedMessage::swap(first, second);

        // enable ADL (not necessary in our case, but good practice)
        using std::swap;

        // by swapping the members of two classes,
        // the two classes are effectively swapped
        swap(first.vin, second.vin);
        swap(first.addr, second.addr);
        swap(first.pubKeyCollateralAddress, second.pubKeyCollateralAddress);
        swap(first.pubKeyMasternode, second.pubKeyMasternode);
        swap(first.activeState, second.activeState);
        swap(first.sigTime, second.sigTime);
        swap(first.lastPing, second.lastPing);
        swap(first.unitTest, second.unitTest);
        swap(first.allowFreeTx, second.allowFreeTx);
        swap(first.protocolVersion, second.protocolVersion);
        swap(first.nLastDsq, second.nLastDsq);
        swap(first.nScanningErrorCount, second.nScanningErrorCount);
        swap(first.nLastScanningErrorBlockHeight, second.nLastScanningErrorBlockHeight);
    }

    CMasternode& operator=(CMasternode from)
    {
        swap(*this, from);
        return *this;
    }
    friend bool operator==(const CMasternode& a, const CMasternode& b)
    {
        return a.vin == b.vin;
    }
    friend bool operator!=(const CMasternode& a, const CMasternode& b)
    {
        return !(a.vin == b.vin);
    }

    uint256 CalculateScore(const uint256& hash) const;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        LOCK(cs);

        READWRITE(vin);
        READWRITE(addr);
        READWRITE(pubKeyCollateralAddress);
        READWRITE(pubKeyMasternode);
        READWRITE(vchSig);
        READWRITE(sigTime);
        READWRITE(protocolVersion);
        READWRITE(activeState);
        READWRITE(lastPing);
        READWRITE(unitTest);
        READWRITE(allowFreeTx);
        READWRITE(nLastDsq);
        READWRITE(nScanningErrorCount);
        READWRITE(nLastScanningErrorBlockHeight);
    }

    int64_t SecondsSincePayment();

    bool UpdateFromNewBroadcast(CMasternodeBroadcast& mnb);

    void Check(bool forceCheck = false);

    bool IsBroadcastedWithin(int seconds)
    {
        return (GetAdjustedTime() - sigTime) < seconds;
    }

    bool IsPingedWithin(int seconds, int64_t now = -1)
    {
        now == -1 ? now = GetAdjustedTime() : now;

        return lastPing.IsNull() ? false : now - lastPing.sigTime < seconds;
    }

    void Disable()
    {
        LOCK(cs);
        sigTime = 0;
        lastPing = CMasternodePing();
    }

    bool IsEnabled()
    {
        return WITH_LOCK(cs, return activeState == MASTERNODE_ENABLED);
    }

    bool IsPreEnabled()
    {
        return WITH_LOCK(cs, return activeState == MASTERNODE_PRE_ENABLED );
    }

    std::string Status() const
    {
        LOCK(cs);
        if (activeState == CMasternode::MASTERNODE_PRE_ENABLED) return "PRE_ENABLED";
        if (activeState == CMasternode::MASTERNODE_ENABLED)     return "ENABLED";
        if (activeState == CMasternode::MASTERNODE_EXPIRED)     return "EXPIRED";
        if (activeState == CMasternode::MASTERNODE_VIN_SPENT)   return "VIN_SPENT";
        if (activeState == CMasternode::MASTERNODE_REMOVE)      return "REMOVE";
        if (activeState == CMasternode::MASTERNODE_POS_ERROR)   return "POS_ERROR";
        if (activeState == CMasternode::MASTERNODE_MISSING)     return "MISSING";

        return strprintf("INVALID_%d", activeState);
    }

    int64_t GetLastPaid();
    bool IsValidNetAddr();

    /// Is the input associated with collateral public key? (and there is 10000 PIV - checking if valid masternode)
    bool IsInputAssociatedWithPubkey() const;
};


//
// The Masternode Broadcast Class : Contains a different serialize method for sending masternodes through the network
//

class CMasternodeBroadcast : public CMasternode
{
public:
    CMasternodeBroadcast();
    CMasternodeBroadcast(CService newAddr, CTxIn newVin, CPubKey newPubkey, CPubKey newPubkey2, int protocolVersionIn);
    CMasternodeBroadcast(const CMasternode& mn);

    bool CheckAndUpdate(int& nDoS);
    bool CheckInputsAndAdd(int& nDos);

    uint256 GetHash() const;

    void Relay();

    // special sign/verify
    bool Sign(const CKey& key, const CPubKey& pubKey);
    bool Sign(const std::string strSignKey);
    bool CheckSignature() const;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(vin);
        READWRITE(addr);
        READWRITE(pubKeyCollateralAddress);
        READWRITE(pubKeyMasternode);
        READWRITE(vchSig);
        READWRITE(sigTime);
        READWRITE(protocolVersion);
        READWRITE(lastPing);
        READWRITE(nMessVersion);    // abuse nLastDsq (which will be removed) for old serialization
        if (ser_action.ForRead())
            nLastDsq = 0;
    }

    /// Create Masternode broadcast, needs to be relayed manually after that
    static bool Create(const CTxIn& vin, const CService& service, const CKey& keyCollateralAddressNew, const CPubKey& pubKeyCollateralAddressNew, const CKey& keyMasternodeNew, const CPubKey& pubKeyMasternodeNew, std::string& strErrorRet, CMasternodeBroadcast& mnbRet);
    static bool Create(const std::string& strService, const std::string& strKey, const std::string& strTxHash, const std::string& strOutputIndex, std::string& strErrorRet, CMasternodeBroadcast& mnbRet, bool fOffline = false);
    static bool CheckDefaultPort(CService service, std::string& strErrorRet, const std::string& strContext);
};

#endif
