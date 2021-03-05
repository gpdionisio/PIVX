// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2020 The PIVX developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef MASTERNODE_PAYMENTS_H
#define MASTERNODE_PAYMENTS_H

#include "key.h"
#include "masternode.h"


extern RecursiveMutex cs_vecPayments;
extern RecursiveMutex cs_mapMasternodeBlocks;
extern RecursiveMutex cs_mapMasternodePayeeVotes;

class CMasternodePayments;
class CMasternodePaymentWinner;
class CMasternodeBlockPayees;

extern CMasternodePayments masternodePayments;

#define MNPAYMENTS_SIGNATURES_REQUIRED 6
#define MNPAYMENTS_SIGNATURES_TOTAL 10

void ProcessMessageMasternodePayments(CNode* pfrom, std::string& strCommand, CDataStream& vRecv);
bool IsBlockPayeeValid(const CBlock& block, const CBlockIndex* pindexPrev);
std::string GetRequiredPaymentsString(int nBlockHeight);
bool IsBlockValueValid(int nHeight, CAmount& nExpectedValue, CAmount nMinted, CAmount& nBudgetAmt);
void FillBlockPayee(CMutableTransaction& txCoinbase, CMutableTransaction& txCoinstake, const CBlockIndex* pindexPrev, bool fProofOfStake);

/**
 * Check coinbase output value for blocks v10+.
 * It must pay the masternode for regular blocks and a proposal during superblocks.
 */
bool IsCoinbaseValueValid(const CTransactionRef& tx, CAmount nBudgetAmt);

void DumpMasternodePayments();

/** Save Masternode Payment Data (mnpayments.dat)
 */
class CMasternodePaymentDB
{
private:
    fs::path pathDB;
    std::string strMagicMessage;

public:
    enum ReadResult {
        Ok,
        FileError,
        HashReadError,
        IncorrectHash,
        IncorrectMagicMessage,
        IncorrectMagicNumber,
        IncorrectFormat
    };

    CMasternodePaymentDB();
    bool Write(const CMasternodePayments& objToSave);
    ReadResult Read(CMasternodePayments& objToLoad);
};

class CMasternodePayee
{
public:
    CScript scriptPubKey;
    int nVotes;

    CMasternodePayee()
    {
        scriptPubKey = CScript();
        nVotes = 0;
    }

    CMasternodePayee(CScript payee, int nVotesIn)
    {
        scriptPubKey = payee;
        nVotes = nVotesIn;
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(*(CScriptBase*)(&scriptPubKey));
        READWRITE(nVotes);
    }
};

// Keep track of votes for payees from masternodes
class CMasternodeBlockPayees
{
public:
    int nBlockHeight;
    std::vector<CMasternodePayee> vecPayments;

    CMasternodeBlockPayees()
    {
        nBlockHeight = 0;
        vecPayments.clear();
    }
    CMasternodeBlockPayees(int nBlockHeightIn)
    {
        nBlockHeight = nBlockHeightIn;
        vecPayments.clear();
    }

    void AddPayee(const CScript& payeeIn, int nIncrement)
    {
        LOCK(cs_vecPayments);

        for (CMasternodePayee& payee : vecPayments) {
            if (payee.scriptPubKey == payeeIn) {
                payee.nVotes += nIncrement;
                return;
            }
        }

        CMasternodePayee c(payeeIn, nIncrement);
        vecPayments.push_back(c);
    }

    bool GetPayee(CScript& payee) const
    {
        LOCK(cs_vecPayments);

        int nVotes = -1;
        for (const CMasternodePayee& p : vecPayments) {
            if (p.nVotes > nVotes) {
                payee = p.scriptPubKey;
                nVotes = p.nVotes;
            }
        }

        return (nVotes > -1);
    }

    bool HasPayeeWithVotes(const CScript& payee, int nVotesReq)
    {
        LOCK(cs_vecPayments);

        for (CMasternodePayee& p : vecPayments) {
            if (p.nVotes >= nVotesReq && p.scriptPubKey == payee) return true;
        }

        return false;
    }

    bool IsTransactionValid(const CTransaction& txNew);
    std::string GetRequiredPaymentsString();

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(nBlockHeight);
        READWRITE(vecPayments);
    }
};

// for storing the winning payments
class CMasternodePaymentWinner : public CSignedMessage
{
public:
    CTxIn vinMasternode;
    int nBlockHeight;
    CScript payee;

    CMasternodePaymentWinner() :
        CSignedMessage(),
        vinMasternode(),
        nBlockHeight(0),
        payee()
    {}

    CMasternodePaymentWinner(CTxIn vinIn, int nHeight) :
        CSignedMessage(),
        vinMasternode(vinIn),
        nBlockHeight(nHeight),
        payee()
    {}

    uint256 GetHash() const;

    // override CSignedMessage functions
    uint256 GetSignatureHash() const override { return GetHash(); }
    std::string GetStrMessage() const override;
    CTxIn GetVin() const { return vinMasternode; };

    bool IsValid(CNode* pnode, std::string& strError);
    void Relay();

    void AddPayee(const CScript& payeeIn)
    {
        payee = payeeIn;
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(vinMasternode);
        READWRITE(nBlockHeight);
        READWRITE(*(CScriptBase*)(&payee));
        READWRITE(vchSig);
        try
        {
            READWRITE(nMessVersion);
        } catch (...) {
            nMessVersion = MessageVersion::MESS_VER_STRMESS;
        }
    }

    std::string ToString()
    {
        std::string ret = "";
        ret += vinMasternode.ToString();
        ret += ", " + std::to_string(nBlockHeight);
        ret += ", " + HexStr(payee);
        ret += ", " + std::to_string((int)vchSig.size());
        return ret;
    }
};

//
// Masternode Payments Class
// Keeps track of who should get paid for which blocks
//

class CMasternodePayments
{
private:
    int nLastBlockHeight;

public:
    std::map<uint256, CMasternodePaymentWinner> mapMasternodePayeeVotes;
    std::map<int, CMasternodeBlockPayees> mapMasternodeBlocks;
    std::map<COutPoint, int> mapMasternodesLastVote; //prevout, nBlockHeight

    CMasternodePayments()
    {
        nLastBlockHeight = 0;
    }

    void Clear()
    {
        LOCK2(cs_mapMasternodeBlocks, cs_mapMasternodePayeeVotes);
        mapMasternodeBlocks.clear();
        mapMasternodePayeeVotes.clear();
    }

    bool AddWinningMasternode(CMasternodePaymentWinner& winner);
    bool ProcessBlock(int nBlockHeight);

    void Sync(CNode* node, int nCountNeeded);
    void CleanPaymentList(int mnCount, int nHeight);

    // get the masternode payment outs for block built on top of pindexPrev
    bool GetMasternodeTxOuts(const CBlockIndex* pindexPrev, std::vector<CTxOut>& voutMasternodePaymentsRet) const;

    // can be removed after transition to DMN
    bool GetLegacyMasternodeTxOut(int nHeight, std::vector<CTxOut>& voutMasternodePaymentsRet) const;
    bool GetBlockPayee(int nBlockHeight, CScript& payee) const;

    bool IsTransactionValid(const CTransaction& txNew, const CBlockIndex* pindexPrev);
    bool IsScheduled(const CMasternode& mn, int nNotBlockHeight);

    bool CanVote(const COutPoint& outMasternode, int nBlockHeight)
    {
        LOCK(cs_mapMasternodePayeeVotes);

        if (mapMasternodesLastVote.count(outMasternode)) {
            if (mapMasternodesLastVote[outMasternode] == nBlockHeight) {
                return false;
            }
        }

        //record this masternode voted
        mapMasternodesLastVote[outMasternode] = nBlockHeight;
        return true;
    }

    void ProcessMessageMasternodePayments(CNode* pfrom, std::string& strCommand, CDataStream& vRecv);
    std::string GetRequiredPaymentsString(int nBlockHeight);
    void FillBlockPayee(CMutableTransaction& txCoinbase, CMutableTransaction& txCoinstake, const CBlockIndex* pindexPrev, bool fProofOfStake) const;
    std::string ToString() const;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(mapMasternodePayeeVotes);
        READWRITE(mapMasternodeBlocks);
    }
};


#endif
