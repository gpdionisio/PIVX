// Copyright (c) 2014-2016 The Dash developers
// Copyright (c) 2015-2020 The PIVX developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "activemasternode.h"

#include "addrman.h"
#include "evo/providertx.h"
#include "evo/deterministicmns.h"
#include "masternode-sync.h"
#include "masternode.h"
#include "masternodeconfig.h"
#include "masternodeman.h"
#include "messagesigner.h"
#include "netbase.h"
#include "protocol.h"

// Keep track of the active Masternode
CActiveMasternodeInfo activeMasternodeInfo;
CActiveDeterministicMasternodeManager* activeMasternodeManager;

std::string CActiveDeterministicMasternodeManager::GetStateString() const
{
    switch (state) {
        case MASTERNODE_WAITING_FOR_PROTX:    return "WAITING_FOR_PROTX";
        case MASTERNODE_POSE_BANNED:          return "POSE_BANNED";
        case MASTERNODE_REMOVED:              return "REMOVED";
        case MASTERNODE_OPERATOR_KEY_CHANGED: return "OPERATOR_KEY_CHANGED";
        case MASTERNODE_PROTX_IP_CHANGED:     return "PROTX_IP_CHANGED";
        case MASTERNODE_READY:                return "READY";
        case MASTERNODE_ERROR:                return "ERROR";
        default:                              return "UNKNOWN";
    }
}

std::string CActiveDeterministicMasternodeManager::GetStatus() const
{
    switch (state) {
        case MASTERNODE_WAITING_FOR_PROTX:    return "Waiting for ProTx to appear on-chain";
        case MASTERNODE_POSE_BANNED:          return "Masternode was PoSe banned";
        case MASTERNODE_REMOVED:              return "Masternode removed from list";
        case MASTERNODE_OPERATOR_KEY_CHANGED: return "Operator key changed or revoked";
        case MASTERNODE_PROTX_IP_CHANGED:     return "IP address specified in ProTx changed";
        case MASTERNODE_READY:                return "Ready";
        case MASTERNODE_ERROR:                return "Error. " + strError;
        default:                              return "Unknown";
    }
}

void CActiveDeterministicMasternodeManager::Init()
{
    if (!fMasterNode || !deterministicMNManager->IsDIP3Enforced())
        return;

    LOCK(cs_main);

    // Check that our local network configuration is correct
    if (!fListen) {
        // listen option is probably overwritten by smth else, no good
        state = MASTERNODE_ERROR;
        strError = "Masternode must accept connections from outside. Make sure listen configuration option is not overwritten by some another parameter.";
        LogPrintf("%s -- ERROR: %s\n", __func__, strError);
        return;
    }

    if (!GetLocalAddress(activeMasternodeInfo.service)) {
        state = MASTERNODE_ERROR;
        return;
    }

    CDeterministicMNList mnList = deterministicMNManager->GetListAtChainTip();

    CDeterministicMNCPtr dmn = mnList.GetMNByOperatorKey(activeMasternodeInfo.keyIDOperator);
    if (!dmn) {
        // MN not appeared on the chain yet
        return;
    }

    if (!mnList.IsMNValid(dmn->proTxHash)) {
        if (mnList.IsMNPoSeBanned(dmn->proTxHash)) {
            state = MASTERNODE_POSE_BANNED;
        } else {
            state = MASTERNODE_REMOVED;
        }
        return;
    }

    LogPrintf("%s -- proTxHash=%s, proTx=%s\n", __func__, dmn->proTxHash.ToString(), dmn->ToString());

    if (activeMasternodeInfo.service != dmn->pdmnState->addr) {
        state = MASTERNODE_ERROR;
        strError = "Local address does not match the address from ProTx";
        LogPrintf("%s -- ERROR: %s", __func__, strError);
        return;
    }

    if (!Params().IsRegTestNet()) {
        // Check socket connectivity
        const std::string& strService = activeMasternodeInfo.service.ToString();
        LogPrintf("%s -- Checking inbound connection to '%s'\n", __func__, strService);
        SOCKET hSocket;
        bool fConnected = ConnectSocket(activeMasternodeInfo.service, hSocket, nConnectTimeout) && IsSelectableSocket(hSocket);
        CloseSocket(hSocket);

        if (!fConnected) {
            state = MASTERNODE_ERROR;
            LogPrintf("%s -- ERROR: Could not connect to %s\n", __func__, strService);
            return;
        }
    }

    activeMasternodeInfo.proTxHash = dmn->proTxHash;
    activeMasternodeInfo.outpoint = dmn->collateralOutpoint;
    state = MASTERNODE_READY;
}

void CActiveDeterministicMasternodeManager::UpdatedBlockTip(const CBlockIndex* pindexNew, const CBlockIndex* pindexFork, bool fInitialDownload)
{
    if (!fMasterNode || !deterministicMNManager->IsDIP3Enforced())
        return;

    LOCK(cs_main);

    if (state == MASTERNODE_READY) {
        auto oldMNList = deterministicMNManager->GetListForBlock(pindexNew->pprev);
        auto newMNList = deterministicMNManager->GetListForBlock(pindexNew);
        if (!newMNList.IsMNValid(activeMasternodeInfo.proTxHash)) {
            // MN disappeared from MN list
            state = MASTERNODE_REMOVED;
            activeMasternodeInfo.SetNullProTx();
            // MN might have reappeared in same block with a new ProTx
            Init();
            return;
        }

        auto oldDmn = oldMNList.GetMN(activeMasternodeInfo.proTxHash);
        auto newDmn = newMNList.GetMN(activeMasternodeInfo.proTxHash);
        if (newDmn->pdmnState->keyIDOperator != oldDmn->pdmnState->keyIDOperator) {
            // MN operator key changed or revoked
            state = MASTERNODE_OPERATOR_KEY_CHANGED;
            activeMasternodeInfo.SetNullProTx();
            // MN might have reappeared in same block with a new ProTx
            Init();
            return;
        }

        if (newDmn->pdmnState->addr != oldDmn->pdmnState->addr) {
            // MN IP changed
            state = MASTERNODE_PROTX_IP_CHANGED;
            activeMasternodeInfo.SetNullProTx();
            Init();
            return;
        }
    } else {
        // MN might have (re)appeared with a new ProTx or we've found some peers
        // and figured out our local address
        Init();
    }
}

bool CActiveDeterministicMasternodeManager::GetLocalAddress(CService& addrRet)
{
    // First try to find whatever local address is specified by externalip option
    bool fFoundLocal = GetLocal(addrRet) && IsValidNetAddr(addrRet);
    if (!fFoundLocal && Params().IsRegTestNet()) {
        if (Lookup("127.0.0.1", addrRet, GetListenPort(), false)) {
            fFoundLocal = true;
        }
    }
    if(!fFoundLocal) {
        bool empty = true;
        // If we have some peers, let's try to find our local address from one of them
        g_connman->ForEachNodeContinueIf([&fFoundLocal, &empty](CNode* pnode) {
            empty = false;
            if (pnode->addr.IsIPv4())
                fFoundLocal = GetLocal(activeMasternodeInfo.service, &pnode->addr) && IsValidNetAddr(activeMasternodeInfo.service);
            return !fFoundLocal;
        });
        strError = "Can't detect valid external address. Please consider using the externalip configuration option if problem persists. Make sure to use IPv4 address only.";
        LogPrintf("%s -- ERROR: %s\n", __func__, strError);
        return false;
    }
    return true;
}

bool CActiveDeterministicMasternodeManager::IsValidNetAddr(const CService& addrIn)
{
    // TODO: check IPv6 and TOR addresses
    return Params().IsRegTestNet() || (addrIn.IsIPv4() && IsReachable(addrIn) && addrIn.IsRoutable());
}

/********* LEGACY *********/

OperationResult initMasternode(const std::string& _strMasterNodePrivKey, const std::string& _strMasterNodeAddr, bool isFromInit)
{
    if (!isFromInit && fMasterNode) {
        return errorOut( "ERROR: Masternode already initialized.");
    }

    LOCK(cs_main); // Lock cs_main so the node doesn't perform any action while we setup the Masternode
    LogPrintf("Initializing masternode, addr %s..\n", _strMasterNodeAddr.c_str());

    if (_strMasterNodePrivKey.empty()) {
        return errorOut("ERROR: Masternode priv key cannot be empty.");
    }

    if (_strMasterNodeAddr.empty()) {
        return errorOut("ERROR: Empty masternodeaddr");
    }

    // Global params set
    strMasterNodeAddr = _strMasterNodeAddr;

    // Address parsing.
    const CChainParams& params = Params();
    int nPort = 0;
    int nDefaultPort = params.GetDefaultPort();
    std::string strHost;
    SplitHostPort(strMasterNodeAddr, nPort, strHost);

    // Allow for the port number to be omitted here and just double check
    // that if a port is supplied, it matches the required default port.
    if (nPort == 0) nPort = nDefaultPort;
    if (nPort != nDefaultPort && !params.IsRegTestNet()) {
        return errorOut(strprintf(_("Invalid -masternodeaddr port %d, only %d is supported on %s-net."),
                                           nPort, nDefaultPort, Params().NetworkIDString()));
    }
    CService addrTest(LookupNumeric(strHost.c_str(), nPort));
    if (!addrTest.IsValid()) {
        return errorOut(strprintf(_("Invalid -masternodeaddr address: %s"), strMasterNodeAddr));
    }

    // Peer port needs to match the masternode public one for IPv4 and IPv6.
    // Onion can run in other ports because those are behind a hidden service which has the public port fixed to the default port.
    if (nPort != GetListenPort() && !addrTest.IsTor()) {
        return errorOut(strprintf(_("Invalid -masternodeaddr port %d, isn't the same as the peer port %d"),
                                  nPort, GetListenPort()));
    }

    CKey key;
    CPubKey pubkey;
    if (!CMessageSigner::GetKeysFromSecret(_strMasterNodePrivKey, key, pubkey)) {
        return errorOut(_("Invalid masternodeprivkey. Please see the documentation."));
    }

    activeMasternode.pubKeyMasternode = pubkey;
    activeMasternode.privKeyMasternode = key;
    activeMasternode.service = addrTest;
    fMasterNode = true;
    return OperationResult(true);
}

//
// Bootup the Masternode, look for a 10000 PIVX input and register on the network
//
void CActiveMasternode::ManageStatus()
{
    if (!fMasterNode) return;

    LogPrint(BCLog::MASTERNODE, "CActiveMasternode::ManageStatus() - Begin\n");

    //need correct blocks to send ping
    if (!Params().IsRegTestNet() && !masternodeSync.IsBlockchainSynced()) {
        status = ACTIVE_MASTERNODE_SYNC_IN_PROCESS;
        LogPrintf("CActiveMasternode::ManageStatus() - %s\n", GetStatusMessage());
        return;
    }

    if (status == ACTIVE_MASTERNODE_SYNC_IN_PROCESS) status = ACTIVE_MASTERNODE_INITIAL;

    if (status == ACTIVE_MASTERNODE_INITIAL) {
        CMasternode* pmn;
        pmn = mnodeman.Find(pubKeyMasternode);
        if (pmn != nullptr) {
            if (pmn->IsEnabled() && pmn->protocolVersion == PROTOCOL_VERSION)
                EnableHotColdMasterNode(pmn->vin, pmn->addr);
        }
    }

    if (status != ACTIVE_MASTERNODE_STARTED) {
        // Set defaults
        status = ACTIVE_MASTERNODE_NOT_CAPABLE;
        notCapableReason = "";

        LogPrintf("%s - Checking inbound connection for masternode to '%s'\n", __func__ , service.ToString());

        CAddress addr(service, NODE_NETWORK);
        if (!g_connman->IsNodeConnected(addr)) {
            CNode* node = g_connman->ConnectNode(addr);
            if (!node) {
                notCapableReason =
                        "Masternode address:port connection availability test failed, could not open a connection to the public masternode address (" +
                        service.ToString() + ")";
                LogPrintf("%s - not capable: %s\n", __func__, notCapableReason);
            } else {
                // don't leak allocated object in memory
                delete node;
            }
            return;
        }

        notCapableReason = "Waiting for start message from controller.";
        return;
    }

    //send to all peers
    std::string errorMessage;
    if (!SendMasternodePing(errorMessage)) {
        LogPrintf("CActiveMasternode::ManageStatus() - Error on Ping: %s\n", errorMessage);
    }
}

void CActiveMasternode::ResetStatus()
{
    status = ACTIVE_MASTERNODE_INITIAL;
    ManageStatus();
}

std::string CActiveMasternode::GetStatusMessage() const
{
    switch (status) {
    case ACTIVE_MASTERNODE_INITIAL:
        return "Node just started, not yet activated";
    case ACTIVE_MASTERNODE_SYNC_IN_PROCESS:
        return "Sync in progress. Must wait until sync is complete to start Masternode";
    case ACTIVE_MASTERNODE_NOT_CAPABLE:
        return "Not capable masternode: " + notCapableReason;
    case ACTIVE_MASTERNODE_STARTED:
        return "Masternode successfully started";
    default:
        return "unknown";
    }
}

bool CActiveMasternode::SendMasternodePing(std::string& errorMessage)
{
    if (vin == nullopt) {
        errorMessage = "Active Masternode not initialized";
        return false;
    }

    if (status != ACTIVE_MASTERNODE_STARTED) {
        errorMessage = "Masternode is not in a running status";
        return false;
    }

    if (!privKeyMasternode.IsValid() || !pubKeyMasternode.IsValid()) {
        errorMessage = "Error upon masternode key.\n";
        return false;
    }

    LogPrintf("CActiveMasternode::SendMasternodePing() - Relay Masternode Ping vin = %s\n", vin->ToString());

    const uint256& nBlockHash = mnodeman.GetBlockHashToPing();
    CMasternodePing mnp(*vin, nBlockHash, GetAdjustedTime());
    if (!mnp.Sign(privKeyMasternode, pubKeyMasternode.GetID())) {
        errorMessage = "Couldn't sign Masternode Ping";
        return false;
    }

    // Update lastPing for our masternode in Masternode list
    CMasternode* pmn = mnodeman.Find(vin->prevout);
    if (pmn != NULL) {
        if (pmn->IsPingedWithin(MasternodePingSeconds(), mnp.sigTime)) {
            errorMessage = "Too early to send Masternode Ping";
            return false;
        }

        // SetLastPing locks the masternode cs, be careful with the lock order.
        pmn->SetLastPing(mnp);
        mnodeman.mapSeenMasternodePing.emplace(mnp.GetHash(), mnp);

        //mnodeman.mapSeenMasternodeBroadcast.lastPing is probably outdated, so we'll update it
        CMasternodeBroadcast mnb(*pmn);
        uint256 hash = mnb.GetHash();
        if (mnodeman.mapSeenMasternodeBroadcast.count(hash)) {
            // SetLastPing locks the masternode cs, be careful with the lock order.
            // TODO: check why are we double setting the last ping here..
            mnodeman.mapSeenMasternodeBroadcast[hash].SetLastPing(mnp);
        }

        mnp.Relay();
        return true;

    } else {
        // Seems like we are trying to send a ping while the Masternode is not registered in the network
        errorMessage = "Masternode List doesn't include our Masternode, shutting down Masternode pinging service! " + vin->ToString();
        status = ACTIVE_MASTERNODE_NOT_CAPABLE;
        notCapableReason = errorMessage;
        return false;
    }
}

// when starting a Masternode, this can enable to run as a hot wallet with no funds
bool CActiveMasternode::EnableHotColdMasterNode(CTxIn& newVin, CService& newService)
{
    if (!fMasterNode) return false;

    status = ACTIVE_MASTERNODE_STARTED;

    //The values below are needed for signing mnping messages going forward
    vin = newVin;
    service = newService;

    LogPrintf("CActiveMasternode::EnableHotColdMasterNode() - Enabled! You may shut down the cold daemon.\n");

    return true;
}

void CActiveMasternode::GetKeys(CKey& _privKeyMasternode, CPubKey& _pubKeyMasternode)
{
    if (!privKeyMasternode.IsValid() || !pubKeyMasternode.IsValid()) {
        throw std::runtime_error("Error trying to get masternode keys");
    }
    _privKeyMasternode = privKeyMasternode;
    _pubKeyMasternode = pubKeyMasternode;
}
