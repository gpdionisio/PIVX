// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2020 The PIVX developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef MASTERNODE_BUDGET_H
#define MASTERNODE_BUDGET_H

#include "base58.h"
#include "init.h"
#include "key.h"
#include "main.h"
#include "masternode.h"
#include "net.h"
#include "sync.h"
#include "util.h"

#include <atomic>
#include <univalue.h>

class CBudgetManager;
class CFinalizedBudgetBroadcast;
class CFinalizedBudget;
class CBudgetProposal;
class CBudgetProposalBroadcast;
class CTxBudgetPayment;

enum class TrxValidationStatus {
    InValid,         /** Transaction verification failed */
    Valid,           /** Transaction successfully verified */
    DoublePayment,   /** Transaction successfully verified, but includes a double-budget-payment */
    VoteThreshold    /** If not enough masternodes have voted on a finalized budget */
};

static const CAmount PROPOSAL_FEE_TX = (50 * COIN);
static const CAmount BUDGET_FEE_TX_OLD = (50 * COIN);
static const CAmount BUDGET_FEE_TX = (5 * COIN);
static const int64_t BUDGET_VOTE_UPDATE_MIN = 60 * 60;
static std::map<uint256, int> mapPayment_History;

extern CBudgetManager budget;
void DumpBudgets();

//Check the collateral transaction for the budget proposal/finalized budget
bool IsBudgetCollateralValid(const uint256& nTxCollateralHash, const uint256& nExpectedHash, std::string& strError, int64_t& nTime, int& nConf, bool fBudgetFinalization=false);

//
// CBudgetVote - Allow a masternode node to vote and broadcast throughout the network
//

class CBudgetVote : public CSignedMessage
{
public:
    enum VoteDirection {
        VOTE_ABSTAIN = 0,
        VOTE_YES = 1,
        VOTE_NO = 2
    };

private:
    bool fValid;  //if the vote is currently valid / counted
    bool fSynced; //if we've sent this to our peers
    uint256 nProposalHash;
    VoteDirection nVote;
    int64_t nTime;
    CTxIn vin;

public:
    CBudgetVote();
    CBudgetVote(CTxIn vin, uint256 nProposalHash, VoteDirection nVoteIn);

    void Relay() const;

    std::string GetVoteString() const
    {
        std::string ret = "ABSTAIN";
        if (nVote == VOTE_YES) ret = "YES";
        if (nVote == VOTE_NO) ret = "NO";
        return ret;
    }

    uint256 GetHash() const;

    // override CSignedMessage functions
    uint256 GetSignatureHash() const override { return GetHash(); }
    std::string GetStrMessage() const override;
    const CTxIn GetVin() const override { return vin; };

    UniValue ToJSON() const;

    VoteDirection GetDirection() const { return nVote; }
    uint256 GetProposalHash() const { return nProposalHash; }
    int64_t GetTime() const { return nTime; }
    bool IsSynced() const { return fSynced; }
    bool IsValid() const { return fValid; }

    void SetSynced(bool _fSynced) { fSynced = _fSynced; }
    void SetTime(const int64_t& _nTime) { nTime = _nTime; }
    void SetValid(bool _fValid) { fValid = _fValid; }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(vin);
        READWRITE(nProposalHash);
        int nVoteInt = (int) nVote;
        READWRITE(nVoteInt);
        if (ser_action.ForRead())
            nVote = (VoteDirection) nVoteInt;
        READWRITE(nTime);
        READWRITE(vchSig);
        try
        {
            READWRITE(nMessVersion);
        } catch (...) {
            nMessVersion = MessageVersion::MESS_VER_STRMESS;
        }
    }
};

//
// CFinalizedBudgetVote - Allow a masternode node to vote and broadcast throughout the network
//

class CFinalizedBudgetVote : public CSignedMessage
{
private:
    bool fValid;  //if the vote is currently valid / counted
    bool fSynced; //if we've sent this to our peers
    CTxIn vin;
    uint256 nBudgetHash;
    int64_t nTime;

public:
    CFinalizedBudgetVote();
    CFinalizedBudgetVote(CTxIn vinIn, uint256 nBudgetHashIn);

    void Relay() const;
    uint256 GetHash() const;

    // override CSignedMessage functions
    uint256 GetSignatureHash() const override { return GetHash(); }
    std::string GetStrMessage() const override;
    const CTxIn GetVin() const override { return vin; };

    UniValue ToJSON() const;

    uint256 GetBudgetHash() const { return nBudgetHash; }
    int64_t GetTime() const { return nTime; }
    bool IsSynced() const { return fSynced; }
    bool IsValid() const { return fValid; }

    void SetSynced(bool _fSynced) { fSynced = _fSynced; }
    void SetTime(const int64_t& _nTime) { nTime = _nTime; }
    void SetValid(bool _fValid) { fValid = _fValid; }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(vin);
        READWRITE(nBudgetHash);
        READWRITE(nTime);
        READWRITE(vchSig);
        try
        {
            READWRITE(nMessVersion);
        } catch (...) {
            nMessVersion = MessageVersion::MESS_VER_STRMESS;
        }
    }
};

/** Save Budget Manager (budget.dat)
 */
class CBudgetDB
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

    CBudgetDB();
    bool Write(const CBudgetManager& objToSave);
    ReadResult Read(CBudgetManager& objToLoad, bool fDryRun = false);
};


//
// Budget Manager : Contains all proposals for the budget
//
class CBudgetManager
{
private:
    //hold txes until they mature enough to use
    std::map<uint256, uint256> mapCollateralTxids;

    std::map<uint256, CBudgetProposal> mapProposals;                        // guarded by cs_proposals
    std::map<uint256, CFinalizedBudget> mapFinalizedBudgets;                // guarded by cs_budgets

    std::map<uint256, CBudgetProposalBroadcast> mapSeenProposals;           // guarded by cs_proposals
    std::map<uint256, CFinalizedBudgetBroadcast> mapSeenFinalizedBudgets;   // guarded by cs_budgets
    std::map<uint256, CBudgetVote> mapSeenProposalVotes;                    // guarded by cs_votes
    std::map<uint256, CBudgetVote> mapOrphanProposalVotes;                  // guarded by cs_votes
    std::map<uint256, CFinalizedBudgetVote> mapSeenFinalizedBudgetVotes;    // guarded by cs_finalizedvotes
    std::map<uint256, CFinalizedBudgetVote> mapOrphanFinalizedBudgetVotes;  // guarded by cs_finalizedvotes

    // Memory only
    std::vector<CBudgetProposalBroadcast> vecImmatureProposals;             // guarded by cs_proposals
    std::vector<CFinalizedBudgetBroadcast> vecImmatureFinalizedBudgets;     // guarded by cs_budgets

    // Memory Only. Updated in NewBlock (blocks arrive in order)
    std::atomic<int> nBestHeight;

    // Returns a const pointer to the budget with highest vote count
    const CFinalizedBudget* GetBudgetWithHighestVoteCount(int chainHeight) const;
    // Get the payee and amount for the budget with the highest vote count
    bool GetPayeeAndAmount(int chainHeight, CScript& payeeRet, CAmount& nAmountRet) const;
    // Marks synced all votes in proposals and finalized budgets
    void SetSynced(bool synced);

public:
    // critical sections to protect the inner data structures (must be locked in this order)
    mutable RecursiveMutex cs_budgets;
    mutable RecursiveMutex cs_proposals;
    mutable RecursiveMutex cs_finalizedvotes;
    mutable RecursiveMutex cs_votes;

    CBudgetManager()
    {
        LOCK2(cs_budgets, cs_proposals);
        mapProposals.clear();
        mapFinalizedBudgets.clear();
    }

    void ClearSeen()
    {
        WITH_LOCK(cs_proposals, mapSeenProposals.clear(); );
        WITH_LOCK(cs_votes, mapSeenProposalVotes.clear(); );
        WITH_LOCK(cs_budgets, mapSeenFinalizedBudgets.clear(); );
        WITH_LOCK(cs_finalizedvotes, mapSeenFinalizedBudgetVotes.clear(); );
    }

    bool HaveSeenProposal(const uint256& propHash) const { LOCK(cs_proposals); return mapSeenProposals.count(propHash); }
    bool HaveSeenProposalVote(const uint256& voteHash) const { LOCK(cs_votes); return mapSeenProposalVotes.count(voteHash); }
    bool HaveSeenFinalizedBudget(const uint256& budgetHash) const { LOCK(cs_budgets); return mapSeenFinalizedBudgets.count(budgetHash); }
    bool HaveSeenFinalizedBudgetVote(const uint256& voteHash) const { LOCK(cs_finalizedvotes); return mapSeenFinalizedBudgetVotes.count(voteHash); }

    void AddSeenProposal(const CBudgetProposalBroadcast& prop);
    void AddSeenProposalVote(const CBudgetVote& vote);
    void AddSeenFinalizedBudget(const CFinalizedBudgetBroadcast& bud);
    void AddSeenFinalizedBudgetVote(const CFinalizedBudgetVote& vote);

    // Use const operator std::map::at(), thus existence must be checked before calling.
    CDataStream GetProposalVoteSerialized(const uint256& voteHash) const;
    CDataStream GetProposalSerialized(const uint256& propHash) const;
    CDataStream GetFinalizedBudgetVoteSerialized(const uint256& voteHash) const;
    CDataStream GetFinalizedBudgetSerialized(const uint256& budgetHash) const;

    bool AddAndRelayProposalVote(const CBudgetVote& vote, std::string& strError);

    void ResetSync() { SetSynced(false); }
    void MarkSynced() { SetSynced(true); }
    void Sync(CNode* node, const uint256& nProp, bool fPartial = false);
    void SetBestHeight(int height) { nBestHeight.store(height, std::memory_order_release); };
    int GetBestHeight() const { return nBestHeight.load(std::memory_order_acquire); }

    void ProcessMessage(CNode* pfrom, std::string& strCommand, CDataStream& vRecv);
    void NewBlock(int height);
    CBudgetProposal* FindProposal(const uint256& nHash);
    // finds the proposal with the given name, with highest net yes count.
    const CBudgetProposal* FindProposalByName(const std::string& strProposalName) const;
    CFinalizedBudget* FindFinalizedBudget(const uint256& nHash);

    static CAmount GetTotalBudget(int nHeight);
    std::vector<CBudgetProposal*> GetBudget();
    std::vector<CBudgetProposal*> GetAllProposals();
    std::vector<CFinalizedBudget*> GetFinalizedBudgets();
    bool IsBudgetPaymentBlock(int nBlockHeight) const;
    bool AddProposal(CBudgetProposal& budgetProposal);
    bool AddFinalizedBudget(CFinalizedBudget& finalizedBudget);
    void SubmitFinalBudget();

    bool UpdateProposal(const CBudgetVote& vote, CNode* pfrom, std::string& strError);
    bool UpdateFinalizedBudget(CFinalizedBudgetVote& vote, CNode* pfrom, std::string& strError);
    TrxValidationStatus IsTransactionValid(const CTransaction& txNew, int nBlockHeight) const;
    std::string GetRequiredPaymentsString(int nBlockHeight);
    void FillBlockPayee(CMutableTransaction& txNew, bool fProofOfStake) const;

    void CheckOrphanVotes();
    void Clear()
    {
        {
            LOCK(cs_proposals);
            mapProposals.clear();
            mapSeenProposals.clear();
        }
        {
            LOCK(cs_budgets);
            mapFinalizedBudgets.clear();
            mapSeenFinalizedBudgets.clear();
        }
        {
            LOCK(cs_votes);
            mapSeenProposalVotes.clear();
            mapOrphanProposalVotes.clear();
        }
        {
            LOCK(cs_finalizedvotes);
            mapSeenFinalizedBudgetVotes.clear();
            mapOrphanFinalizedBudgetVotes.clear();
        }
        LogPrintf("Budget object cleared\n");
    }
    void CheckAndRemove();
    std::string ToString() const;

    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        WITH_LOCK(cs_proposals, READWRITE(mapSeenProposals); );
        WITH_LOCK(cs_votes, READWRITE(mapSeenProposalVotes); );
        WITH_LOCK(cs_budgets, READWRITE(mapSeenFinalizedBudgets); );
        WITH_LOCK(cs_finalizedvotes, READWRITE(mapSeenFinalizedBudgetVotes); );
        WITH_LOCK(cs_votes, READWRITE(mapOrphanProposalVotes); );
        WITH_LOCK(cs_votes, READWRITE(mapOrphanFinalizedBudgetVotes); );
        WITH_LOCK(cs_proposals, READWRITE(mapProposals); );
        WITH_LOCK(cs_budgets, READWRITE(mapFinalizedBudgets); );
    }
};


class CTxBudgetPayment
{
public:
    uint256 nProposalHash;
    CScript payee;
    CAmount nAmount;

    CTxBudgetPayment()
    {
        payee = CScript();
        nAmount = 0;
        nProposalHash = UINT256_ZERO;
    }

    ADD_SERIALIZE_METHODS;

    //for saving to the serialized db
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(*(CScriptBase*)(&payee));
        READWRITE(nAmount);
        READWRITE(nProposalHash);
    }

    // compare payments by proposal hash
    inline bool operator>(const CTxBudgetPayment& other) const { return nProposalHash > other.nProposalHash; }

};

//
// Finalized Budget : Contains the suggested proposals to pay on a given block
//

class CFinalizedBudget
{
private:
    bool fAutoChecked; //If it matches what we see, we'll auto vote for it (masternode only)
    bool fValid;
    std::string strInvalid;

    // Functions used inside IsWellFormed/UpdateValid - setting strInvalid
    bool IsExpired(int nCurrentHeight);
    bool CheckStartEnd();
    bool CheckAmount(const CAmount& nTotalBudget);
    bool CheckName();
    bool CheckRequiredConfs(int nCurrentHeight);

protected:
    std::map<uint256, CFinalizedBudgetVote> mapVotes;
    std::string strBudgetName;
    int nBlockStart;
    std::vector<CTxBudgetPayment> vecBudgetPayments;
    uint256 nFeeTXHash;

public:
    // Set in CBudgetManager::AddFinalizedBudget via CheckCollateral
    int64_t nTime;
    int nBlockFeeTx;

    CFinalizedBudget();
    CFinalizedBudget(const CFinalizedBudget& other);

    void CleanAndRemove();
    bool AddOrUpdateVote(const CFinalizedBudgetVote& vote, std::string& strError);
    UniValue GetVotesObject() const;
    void SetSynced(bool synced);    // sets fSynced on votes (true only if valid)

    // sync budget votes with a node
    void SyncVotes(CNode* pfrom, bool fPartial, int& nInvCount) const;

    // sets fValid and strInvalid, returns fValid
    bool UpdateValid(int nHeight);
    // Static checks that should be done only once - sets strInvalid
    bool IsWellFormed(const CAmount& nTotalBudget);
    bool IsValid() const  { return fValid; }
    std::string IsInvalidReason() const { return strprintf("%s (%s)", strBudgetName, strInvalid); }

    std::string GetName() const { return strBudgetName; }
    std::string GetProposals() const;
    int GetBlockStart() const { return nBlockStart; }
    int GetBlockEnd() const { return nBlockStart + (int)(vecBudgetPayments.size() - 1); }
    const uint256& GetFeeTXHash() const { return nFeeTXHash;  }
    int GetVoteCount() const { return (int)mapVotes.size(); }
    bool IsPaidAlready(uint256 nProposalHash, int nBlockHeight) const;
    TrxValidationStatus IsTransactionValid(const CTransaction& txNew, int nBlockHeight) const;
    bool GetBudgetPaymentByBlock(int64_t nBlockHeight, CTxBudgetPayment& payment) const;
    bool GetPayeeAndAmount(int64_t nBlockHeight, CScript& payee, CAmount& nAmount) const;

    // Verify and vote on finalized budget
    void CheckAndVote();
    //total pivx paid out by this budget
    CAmount GetTotalPayout() const;
    //vote on this finalized budget as a masternode
    void SubmitVote();

    //checks the hashes to make sure we know about them
    std::string GetStatus() const;

    uint256 GetHash() const
    {
        CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
        ss << strBudgetName;
        ss << nBlockStart;
        ss << vecBudgetPayments;
        return ss.GetHash();
    }

    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(LIMITED_STRING(strBudgetName, 20));
        READWRITE(nFeeTXHash);
        READWRITE(nBlockFeeTx);
        READWRITE(nTime);
        READWRITE(nBlockStart);
        READWRITE(vecBudgetPayments);
        READWRITE(fAutoChecked);
        READWRITE(mapVotes);
    }

    // compare finalized budget by votes (sort tie with feeHash)
    bool operator>(const CFinalizedBudget& other) const;
    // compare finalized budget pointers
    static bool PtrGreater(CFinalizedBudget* a, CFinalizedBudget* b) { return *a > *b; }
};

// FinalizedBudget are cast then sent to peers with this object, which leaves the votes out
class CFinalizedBudgetBroadcast : public CFinalizedBudget
{
public:
    CFinalizedBudgetBroadcast();
    CFinalizedBudgetBroadcast(const CFinalizedBudget& other);
    CFinalizedBudgetBroadcast(std::string strBudgetNameIn, int nBlockStartIn, const std::vector<CTxBudgetPayment>& vecBudgetPaymentsIn, uint256 nFeeTXHashIn);

    void swap(CFinalizedBudgetBroadcast& first, CFinalizedBudgetBroadcast& second) // nothrow
    {
        // enable ADL (not necessary in our case, but good practice)
        using std::swap;
        // by swapping the members of two classes,
        // the two classes are effectively swapped
        swap(first.strBudgetName, second.strBudgetName);
        swap(first.nBlockStart, second.nBlockStart);
        first.mapVotes.swap(second.mapVotes);
        first.vecBudgetPayments.swap(second.vecBudgetPayments);
        swap(first.nFeeTXHash, second.nFeeTXHash);
        swap(first.nTime, second.nTime);
    }

    CFinalizedBudgetBroadcast& operator=(CFinalizedBudgetBroadcast from)
    {
        swap(*this, from);
        return *this;
    }

    void Relay();

    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        //for syncing with other clients
        READWRITE(LIMITED_STRING(strBudgetName, 20));
        READWRITE(nBlockStart);
        READWRITE(vecBudgetPayments);
        READWRITE(nFeeTXHash);
    }
};


//
// Budget Proposal : Contains the masternode votes for each budget
//

class CBudgetProposal
{
private:
    CAmount nAlloted;
    bool fValid;
    std::string strInvalid;

    // Functions used inside UpdateValid()/IsWellFormed - setting strInvalid
    bool IsHeavilyDownvoted();
    bool IsExpired(int nCurrentHeight);
    bool CheckStartEnd();
    bool CheckAmount(const CAmount& nTotalBudget);
    bool CheckAddress();
    bool CheckRequiredConfs(int nCurrentHeight);

protected:
    std::map<uint256, CBudgetVote> mapVotes;
    std::string strProposalName;
    std::string strURL;
    int nBlockStart;
    int nBlockEnd;
    CAmount nAmount;
    CScript address;
    uint256 nFeeTXHash;

public:
    // Set in CBudgetManager::AddProposal via CheckCollateral
    int64_t nTime;
    int nBlockFeeTx;

    CBudgetProposal();
    CBudgetProposal(const CBudgetProposal& other);
    CBudgetProposal(std::string strProposalNameIn, std::string strURLIn, int nBlockStartIn, int nBlockEndIn, CScript addressIn, CAmount nAmountIn, uint256 nFeeTXHashIn);

    bool AddOrUpdateVote(const CBudgetVote& vote, std::string& strError);
    UniValue GetVotesArray() const;
    void SetSynced(bool synced);    // sets fSynced on votes (true only if valid)

    // sync proposal votes with a node
    void SyncVotes(CNode* pfrom, bool fPartial, int& nInvCount) const;

    // sets fValid and strInvalid, returns fValid
    bool UpdateValid(int nHeight);
    // Static checks that should be done only once - sets strInvalid
    bool IsWellFormed(const CAmount& nTotalBudget);
    bool IsValid() const  { return fValid; }
    std::string IsInvalidReason() const { return strprintf("%s (%s)", strProposalName, strInvalid); }

    bool IsEstablished() const;
    bool IsPassing(int nBlockStartBudget, int nBlockEndBudget, int mnCount) const;

    std::string GetName() const { return strProposalName; }
    std::string GetURL() const { return strURL; }
    int GetBlockStart() const { return nBlockStart; }
    int GetBlockEnd() const { return nBlockEnd; }
    CScript GetPayee() const { return address; }
    int GetTotalPaymentCount() const;
    int GetRemainingPaymentCount(int nCurrentHeight) const;
    int GetBlockStartCycle() const;
    static int GetBlockCycle(int nCurrentHeight);
    int GetBlockEndCycle() const;
    const uint256& GetFeeTXHash() const { return nFeeTXHash;  }
    double GetRatio() const;
    int GetVoteCount(CBudgetVote::VoteDirection vd) const;
    int GetYeas() const { return GetVoteCount(CBudgetVote::VOTE_YES); }
    int GetNays() const { return GetVoteCount(CBudgetVote::VOTE_NO); }
    int GetAbstains() const { return GetVoteCount(CBudgetVote::VOTE_ABSTAIN); };
    CAmount GetAmount() const { return nAmount; }
    void SetAllotted(CAmount nAllotedIn) { nAlloted = nAllotedIn; }
    CAmount GetAllotted() const { return nAlloted; }

    void CleanAndRemove();

    uint256 GetHash() const
    {
        CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
        ss << strProposalName;
        ss << strURL;
        ss << nBlockStart;
        ss << nBlockEnd;
        ss << nAmount;
        ss << std::vector<unsigned char>(address.begin(), address.end());
        return ss.GetHash();
    }

    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        //for syncing with other clients
        READWRITE(LIMITED_STRING(strProposalName, 20));
        READWRITE(LIMITED_STRING(strURL, 64));
        READWRITE(nTime);
        READWRITE(nBlockStart);
        READWRITE(nBlockEnd);
        READWRITE(nAmount);
        READWRITE(*(CScriptBase*)(&address));
        READWRITE(nTime);
        READWRITE(nFeeTXHash);
        READWRITE(nBlockFeeTx);

        //for saving to the serialized db
        READWRITE(mapVotes);
    }

    // compare proposals by proposal hash
    inline bool operator>(const CBudgetProposal& other) const { return GetHash() > other.GetHash(); }
    // compare proposals pointers by hash
    static inline bool PtrGreater(CBudgetProposal* a, CBudgetProposal* b) { return *a > *b; }
    // compare proposals pointers by net yes count (solve tie with feeHash)
    static bool PtrHigherYes(CBudgetProposal* a, CBudgetProposal* b);

};

// Proposals are cast then sent to peers with this object, which leaves the votes out
class CBudgetProposalBroadcast : public CBudgetProposal
{
public:
    CBudgetProposalBroadcast() : CBudgetProposal() {}
    CBudgetProposalBroadcast(const CBudgetProposal& other) : CBudgetProposal(other) {}
    CBudgetProposalBroadcast(const CBudgetProposalBroadcast& other) : CBudgetProposal(other) {}
    CBudgetProposalBroadcast(std::string strProposalNameIn, std::string strURLIn, int nPaymentCount, CScript addressIn, CAmount nAmountIn, int nBlockStartIn, uint256 nFeeTXHashIn);

    void swap(CBudgetProposalBroadcast& first, CBudgetProposalBroadcast& second) // nothrow
    {
        // enable ADL (not necessary in our case, but good practice)
        using std::swap;
        // by swapping the members of two classes,
        // the two classes are effectively swapped
        swap(first.strProposalName, second.strProposalName);
        swap(first.nBlockStart, second.nBlockStart);
        swap(first.strURL, second.strURL);
        swap(first.nBlockEnd, second.nBlockEnd);
        swap(first.nAmount, second.nAmount);
        swap(first.address, second.address);
        swap(first.nTime, second.nTime);
        swap(first.nFeeTXHash, second.nFeeTXHash);
        first.mapVotes.swap(second.mapVotes);
    }

    CBudgetProposalBroadcast& operator=(CBudgetProposalBroadcast from)
    {
        swap(*this, from);
        return *this;
    }

    void Relay();

    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        //for syncing with other clients
        READWRITE(LIMITED_STRING(strProposalName, 20));
        READWRITE(LIMITED_STRING(strURL, 64));
        READWRITE(nTime);
        READWRITE(nBlockStart);
        READWRITE(nBlockEnd);
        READWRITE(nAmount);
        READWRITE(*(CScriptBase*)(&address));
        READWRITE(nFeeTXHash);
    }
};


#endif
