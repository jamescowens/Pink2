// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2012 The Bitcoin developers
// Copyright (c) 2013 The NovaCoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef NOVACOIN_MINER_H
#define NOVACOIN_MINER_H

#include "main.h"
#include "wallet.h"

static const int64_t STAKE_SEARCH_SECONDS = 60;          // search back window, in seconds
static const int64_t STAKE_MINER_SLEEP_MS = 1000;        // sleep between passes
static const int64_t STAKE_EXCEPTION_SLEEP_MS = 60000;   // sleep after a caught exception
static const int64_t STAKE_SETTLE_SECONDS = 60;          // time the start conditions must hold before staking starts
static const unsigned int STAKE_SEARCH_CHUNK = 256;      // coins per hold of cs_main and cs_wallet

/* Pick the seconds [nFirstRet, nLastRet] a stake pass searches. nLastRet is nNow;
   nFirstRet is the latest of the second after the last search, the search window
   start, the second after the past limit and the parent-time bound. If that is
   after nNow the window is empty; otherwise, if it is in an earlier hour in the
   other Flash class from nNow, it moves up to the start of nNow's hour.
   Returns false when the window is empty. */
bool GetStakeSearchWindow(int64_t nPastLimit, int64_t nPrevBlockTime, int64_t nLastSearched, int64_t nNow, int64_t& nFirstRet, int64_t& nLastRet);

/* The stake miner's start latch, kept by the stake thread between passes. */
struct CStakeGateState
{
    bool fStarted;      // staking has started
    int64_t nOpenAt;    // before the start: when the settle time ends, or 0 if it has not begun

    CStakeGateState() : fStarted(false), nOpenAt(0) {}
};

/* The number of connected peers needed to start staking: -minstakepeers, default
   MIN_PEERS. A value below 1 gives 1. */
unsigned int GetMinStakePeers();

/* Whether a stake pass may run at nNow. fOpenedRet is true only on the call that starts
   staking.
   - Wallet locked: false; the latch is kept.
   - No peers, or initial download: false; the latch is reset.
   - Not started: the settle time begins on a call with at least nMinPeers peers (any
     number on testnet), and ends STAKE_SETTLE_SECONDS later; staking starts on the
     first call at or after its end. A call with fewer peers ends the settle time, and
     it begins again.
   - Started: true. */
bool CheckStakeGate(CStakeGateState& state, bool fLocked, size_t nPeers, unsigned int nMinPeers, bool fIBD, bool fTestNet, int64_t nNow, bool& fOpenedRet);

/* Where a staking coin's transaction sits: the hash and time of the block that holds
   it and the transaction's offset in that block. */
struct CStakeTxSource
{
    uint256 hashBlock;
    unsigned int nTimeBlock;
    unsigned int nTxOffset;
};

/* Cache of CStakeTxSource by txid. The block hash is a scrypt hash, so it is
   computed once per transaction and kept while the block is on the main chain.
   Owned by the stake thread; not thread-safe. */
class CStakeTxSourceCache
{
public:
    /* Keep the entries in setWanted whose block is in the main chain; drop the rest.
       Requires cs_main. */
    void Prune(const std::set<uint256>& setWanted);

    /* Add an entry for each txid that has none. Call without cs_main: it takes
       cs_main for each disk read and hashes with no lock held. A txid whose
       reads fail is skipped. */
    void Fill(const std::vector<uint256>& vTxids);

    /* The entry for hashTx, or NULL. */
    const CStakeTxSource* Find(const uint256& hashTx) const;

private:
    std::map<uint256, CStakeTxSource> mapSource;
};

/* Search one coin, output prevout of txPrev (prevout.hash is txPrev's id), for a stake
   kernel at the seconds nLast down to nFirst; the first hit sets nTimeRet and returns
   true. A coin whose block time plus nStakeMinAge is after nNow - STAKE_SEARCH_SECONDS
   is not searched. Requires cs_main. */
bool SearchStakeKernel(unsigned int nBits, const CStakeTxSource& source, const CTransaction& txPrev, const COutPoint& prevout, int64_t nFirst, int64_t nLast, int64_t nNow, unsigned int& nTimeRet);

/* Search the coins in order for the first stake kernel in [nFirst, nLast] that a key
   in wallet can sign for, as a pay-to-public-key or pay-to-address output. Takes
   cs_main and wallet.cs_wallet for each STAKE_SEARCH_CHUNK coins and returns false at
   the start of a chunk if the best block is no longer pindexPrev or the node is
   shutting down. A coin is searched only while it is usable (IsStakeCoinUsable);
   coins with no entry in cache are skipped.
   Call with no lock held. nFirst >= nNow - 59 (from GetStakeSearchWindow). */
bool FindStakeKernel(const StakeCoins& vCoins, const CStakeTxSourceCache& cache, const CWallet& wallet, const CBlockIndex* pindexPrev, unsigned int nBits, int64_t nFirst, int64_t nLast, int64_t nNow, CStakeKernel& kernelRet);

/* Build the proof-of-stake block for a kernel found on pindexPrev, at the kernel's
   time: the header, the coinbase, the coinstake inputs, the memory pool
   transactions chosen for that time and height (none spending a coinstake input),
   the reward with the fees of those transactions, the coinstake signatures and the
   block signature by the kernel key. vCoins and nBalance come from the same
   SelectStakeCandidates call as the kernel. Requires cs_main and wallet.cs_wallet.
   Returns false if any step fails, with strFailRet naming it: "tip moved"
   (pindexBest is not pindexPrev), "coin" (the kernel's coin is not usable,
   IsStakeCoinUsable), "credit check" (BeginCoinStake), "coinstake"
   (FinishCoinStake) or "sign" (the block signature). */
bool BuildStakeBlock(CWallet& wallet, const CStakeKernel& kernel, const StakeCoins& vCoins, int64_t nBalance, CBlockIndex* pindexPrev, unsigned int nBits, CBlock& blockRet, std::string& strFailRet);

/* Append memory pool transactions to a block template; returns the fees they pay.
   A transaction is skipped if it is timed after nTxTimeLimit, is not final at
   (nFinalHeight, nFinalTime) (0, 0 keeps IsFinal's defaults) or spends an output in setExcluded. */
int64_t AddPoolTransactions(CBlock* pblock, CBlockIndex* pindexPrev, int64_t nTxTimeLimit, int nFinalHeight, int64_t nFinalTime, const std::set<COutPoint>& setExcluded);

/* Generate a new block, without valid proof-of-work */
CBlock* CreateNewBlock(CWallet* pwallet, bool fProofOfStake=false, int64_t* pFees = 0);

/** Modify the extranonce in a block */
void IncrementExtraNonce(CBlock* pblock, CBlockIndex* pindexPrev, unsigned int& nExtraNonce);

/** Do mining precalculation */
void FormatHashBuffers(CBlock* pblock, char* pmidstate, char* pdata, char* phash1);

/** Check mined proof-of-work block */
bool CheckWork(CBlock* pblock, CWallet& wallet, CReserveKey& reservekey);

/** Check mined proof-of-stake block */
bool CheckStake(CBlock* pblock, CWallet& wallet);

/** Base sha256 mining transform */
void SHA256Transform(void* pstate, void* pinput, const void* pinit);

#endif // NOVACOIN_MINER_H
