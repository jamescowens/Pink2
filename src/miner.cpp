// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2012 The Bitcoin developers
// Copyright (c) 2013 The NovaCoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "txdb.h"
#include "miner.h"
#include "kernel.h"

#include <limits>

using namespace std;

bool GetStakeSearchWindow(int64_t nPastLimit, int64_t nPrevBlockTime, int64_t nLastSearched, int64_t nNow, int64_t& nFirstRet, int64_t& nLastRet)
{
    nLastRet = nNow;
    int64_t nFirst = max(max(nLastSearched + 1, nNow - STAKE_SEARCH_SECONDS + 1),
                         max(nPastLimit + 1, PastDrift(nPrevBlockTime)));
    if (nFirst > nNow)
    {
        nFirstRet = nFirst;
        return false; // empty before the Flash class is considered
    }
    // Within one hour the Flash class can't differ. Across an hour, the window
    // starts no earlier than nNow's hour if the classes differ.
    if (nFirst / 3600 != nNow / 3600 && IsFlashStake((unsigned int)nFirst) != IsFlashStake((unsigned int)nNow))
        nFirst = max(nFirst, nNow - nNow % 3600);
    nFirstRet = nFirst;
    return nFirst <= nLastRet;
}

unsigned int GetMinStakePeers()
{
    const int64_t nPeers = GetArg("-minstakepeers", MIN_PEERS);
    if (nPeers < 1)
        return 1;
    return (unsigned int)min(nPeers, (int64_t)std::numeric_limits<unsigned int>::max());
}

bool CheckStakeGate(CStakeGateState& state, bool fLocked, size_t nPeers, unsigned int nMinPeers, bool fIBD, bool fTestNet, int64_t nNow, bool& fOpenedRet)
{
    fOpenedRet = false;
    if (fLocked)
        return false;
    if (nPeers == 0 || fIBD)
    {
        state.fStarted = false;
        state.nOpenAt = 0;
        return false;
    }
    if (state.fStarted)
        return true;
    if (nPeers < nMinPeers && !fTestNet)
    {
        state.nOpenAt = 0;
        return false;
    }
    if (state.nOpenAt == 0)
        state.nOpenAt = nNow + STAKE_SETTLE_SECONDS;
    if (nNow < state.nOpenAt)
        return false;
    state.fStarted = true;
    fOpenedRet = true;
    return true;
}

int static FormatHashBlocks(void* pbuffer, unsigned int len)
{
    unsigned char* pdata = (unsigned char*)pbuffer;
    unsigned int blocks = 1 + ((len + 8) / 64);
    unsigned char* pend = pdata + 64 * blocks;
    memset(pdata + len, 0, 64 * blocks - len);
    pdata[len] = 0x80;
    unsigned int bits = len * 8;
    pend[-1] = (bits >> 0) & 0xff;
    pend[-2] = (bits >> 8) & 0xff;
    pend[-3] = (bits >> 16) & 0xff;
    pend[-4] = (bits >> 24) & 0xff;
    return blocks;
}

static const unsigned int pSHA256InitState[8] =
{0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};

void SHA256Transform(void* pstate, void* pinput, const void* pinit)
{
    SHA256_CTX ctx;
    unsigned char data[64];

    SHA256_Init(&ctx);

    for (int i = 0; i < 16; i++)
        ((uint32_t*)data)[i] = ByteReverse(((uint32_t*)pinput)[i]);

    for (int i = 0; i < 8; i++)
        ctx.h[i] = ((uint32_t*)pinit)[i];

    SHA256_Update(&ctx, data, sizeof(data));
    for (int i = 0; i < 8; i++)
        ((uint32_t*)pstate)[i] = ctx.h[i];
}

// Some explaining would be appreciated
class COrphan
{
public:
    CTransaction* ptx;
    set<uint256> setDependsOn;
    double dPriority;
    double dFeePerKb;

    COrphan(CTransaction* ptxIn)
    {
        ptx = ptxIn;
        dPriority = dFeePerKb = 0;
    }

    void print() const
    {
        printf("COrphan(hash=%s, dPriority=%.1f, dFeePerKb=%.1f)\n",
               ptx->GetHash().ToString().substr(0,10).c_str(), dPriority, dFeePerKb);
        BOOST_FOREACH(uint256 hash, setDependsOn)
            printf("   setDependsOn %s\n", hash.ToString().substr(0,10).c_str());
    }
};


uint64_t nLastBlockTx = 0;
uint64_t nLastBlockSize = 0;
int64_t nLastCoinStakeSearchInterval = 0;
 
// We want to sort transactions by priority and fee, so:
typedef boost::tuple<double, double, CTransaction*> TxPriority;
class TxPriorityCompare
{
    bool byFee;
public:
    TxPriorityCompare(bool _byFee) : byFee(_byFee) { }
    bool operator()(const TxPriority& a, const TxPriority& b)
    {
        if (byFee)
        {
            if (a.get<1>() == b.get<1>())
                return a.get<0>() < b.get<0>();
            return a.get<1>() < b.get<1>();
        }
        else
        {
            if (a.get<0>() == b.get<0>())
                return a.get<1>() < b.get<1>();
            return a.get<0>() < b.get<0>();
        }
    }
};

// AddPoolTransactions: append memory pool transactions to pblock, in priority order
int64_t AddPoolTransactions(CBlock* pblock, CBlockIndex* pindexPrev, int64_t nTxTimeLimit, int nFinalHeight, int64_t nFinalTime, const std::set<COutPoint>& setExcluded)
{
    // Largest block you're willing to create:
    unsigned int nBlockMaxSize = GetArg("-blockmaxsize", MAX_BLOCK_SIZE_GEN/2);
    // Limit to betweeen 1K and MAX_BLOCK_SIZE-1K for sanity:
    nBlockMaxSize = std::max((unsigned int)1000, std::min((unsigned int)(MAX_BLOCK_SIZE-1000), nBlockMaxSize));

    // How much of the block should be dedicated to high-priority transactions,
    // included regardless of the fees they pay
    unsigned int nBlockPrioritySize = GetArg("-blockprioritysize", 27000);
    nBlockPrioritySize = std::min(nBlockMaxSize, nBlockPrioritySize);

    // Minimum block size you want to create; block will be filled with free transactions
    // until there are no more or the block reaches this size:
    unsigned int nBlockMinSize = GetArg("-blockminsize", 0);
    nBlockMinSize = std::min(nBlockMaxSize, nBlockMinSize);

    // Fee-per-kilobyte amount considered the same as "free"
    // Be careful setting this: if you set it to zero then
    // a transaction spammer can cheaply fill blocks using
    // 1-satoshi-fee transactions. It should be set above the real
    // cost to you of processing a transaction.
    int64_t nMinTxFee = MIN_TX_FEE;
    if (mapArgs.count("-mintxfee"))
        ParseMoney(mapArgs["-mintxfee"], nMinTxFee);

    int64_t nFees = 0;
    {
        LOCK2(cs_main, mempool.cs);
        CTxDB txdb("r");

        // Priority order to process transactions
        list<COrphan> vOrphan; // list memory doesn't move
        map<uint256, vector<COrphan*> > mapDependers;

        // This vector will be sorted into a priority queue:
        vector<TxPriority> vecPriority;
        vecPriority.reserve(mempool.mapTx.size());
        for (map<uint256, CTransaction>::iterator mi = mempool.mapTx.begin(); mi != mempool.mapTx.end(); ++mi)
        {
            CTransaction& tx = (*mi).second;
            if (tx.IsCoinBase() || tx.IsCoinStake() || !tx.IsFinal(nFinalHeight, nFinalTime))
                continue;

            // Skip a transaction that spends one of the excluded outputs
            bool fSpendsExcluded = false;
            BOOST_FOREACH(const CTxIn& txinCheck, tx.vin)
            {
                if (setExcluded.count(txinCheck.prevout))
                {
                    fSpendsExcluded = true;
                    break;
                }
            }
            if (fSpendsExcluded)
                continue;

            double dPriority = 0;
            int64_t nTotalIn = 0;
            bool fMissingInputs = false;
            vector<uint256> vPooledParents; // one entry per input spent from the memory pool
            BOOST_FOREACH(const CTxIn& txin, tx.vin)
            {
                // Read prev transaction
                CTransaction txPrev;
                CTxIndex txindex;
                if (!txPrev.ReadFromDisk(txdb, txin.prevout, txindex))
                {
                    // Every input must be in the chain or in the memory pool;
                    // skip the transaction otherwise.
                    if (!mempool.mapTx.count(txin.prevout.hash))
                    {
                        printf("ERROR: mempool transaction missing input\n");
                        fMissingInputs = true;
                        break;
                    }

                    // Has to wait for dependencies
                    vPooledParents.push_back(txin.prevout.hash);
                    nTotalIn += mempool.mapTx[txin.prevout.hash].vout[txin.prevout.n].nValue;
                    continue;
                }
                int64_t nValueIn = txPrev.vout[txin.prevout.n].nValue;
                nTotalIn += nValueIn;

                int nConf = txindex.GetDepthInMainChain();
                dPriority += (double)nValueIn * nConf;
            }
            if (fMissingInputs) continue;

            // Queue entry for a transaction that waits on pooled parents
            COrphan* porphan = NULL;
            if (!vPooledParents.empty())
            {
                // Use list for automatic deletion
                vOrphan.push_back(COrphan(&tx));
                porphan = &vOrphan.back();
                BOOST_FOREACH(const uint256& hashParent, vPooledParents)
                {
                    mapDependers[hashParent].push_back(porphan);
                    porphan->setDependsOn.insert(hashParent);
                }
            }

            // Priority is sum(valuein * age) / txsize
            unsigned int nTxSize = ::GetSerializeSize(tx, SER_NETWORK, PROTOCOL_VERSION);
            dPriority /= nTxSize;

            // This is a more accurate fee-per-kilobyte than is used by the client code, because the
            // client code rounds up the size to the nearest 1K. That's good, because it gives an
            // incentive to create smaller transactions.
            double dFeePerKb =  double(nTotalIn-tx.GetValueOut()) / (double(nTxSize)/1000.0);

            if (porphan)
            {
                porphan->dPriority = dPriority;
                porphan->dFeePerKb = dFeePerKb;
            }
            else
                vecPriority.push_back(TxPriority(dPriority, dFeePerKb, &(*mi).second));
        }

        // Collect transactions into block
        map<uint256, CTxIndex> mapTestPool;
        uint64_t nBlockSize = 1000;
        uint64_t nBlockTx = 0;
        int nBlockSigOps = 100;
        bool fSortedByFee = (nBlockPrioritySize <= 0);

        TxPriorityCompare comparer(fSortedByFee);
        std::make_heap(vecPriority.begin(), vecPriority.end(), comparer);

        while (!vecPriority.empty())
        {
            // Take highest priority transaction off the priority queue:
            double dPriority = vecPriority.front().get<0>();
            double dFeePerKb = vecPriority.front().get<1>();
            CTransaction& tx = *(vecPriority.front().get<2>());

            std::pop_heap(vecPriority.begin(), vecPriority.end(), comparer);
            vecPriority.pop_back();

            // Size limits
            unsigned int nTxSize = ::GetSerializeSize(tx, SER_NETWORK, PROTOCOL_VERSION);
            if (nBlockSize + nTxSize >= nBlockMaxSize)
                continue;

            // Legacy limits on sigOps:
            unsigned int nTxSigOps = tx.GetLegacySigOpCount();
            if (nBlockSigOps + nTxSigOps >= MAX_BLOCK_SIGOPS)
                continue;

            // Timestamp limit
            if ((int64_t)tx.nTime > nTxTimeLimit)
                continue;

            // Transaction fee
            int64_t nMinFee = tx.GetMinFee(nBlockSize, GMF_BLOCK);

            // Skip free transactions if we're past the minimum block size:
            if (fSortedByFee && (dFeePerKb < nMinTxFee) && (nBlockSize + nTxSize >= nBlockMinSize))
                continue;

            // Prioritize by fee once past the priority size or we run out of high-priority
            // transactions:
            if (!fSortedByFee &&
                ((nBlockSize + nTxSize >= nBlockPrioritySize) || (dPriority < COIN * 144 / 250)))
            {
                fSortedByFee = true;
                comparer = TxPriorityCompare(fSortedByFee);
                std::make_heap(vecPriority.begin(), vecPriority.end(), comparer);
            }

            // Connecting shouldn't fail due to dependency on other memory pool transactions
            // because we're already processing them in order of dependency
            map<uint256, CTxIndex> mapTestPoolTmp(mapTestPool);
            MapPrevTx mapInputs;
            bool fInvalid;
            if (!tx.FetchInputs(txdb, mapTestPoolTmp, false, true, mapInputs, fInvalid))
                continue;

            int64_t nTxFees = tx.GetValueIn(mapInputs)-tx.GetValueOut();
            if (nTxFees < nMinFee)
                continue;

            nTxSigOps += tx.GetP2SHSigOpCount(mapInputs);
            if (nBlockSigOps + nTxSigOps >= MAX_BLOCK_SIGOPS)
                continue;

            if (!tx.ConnectInputs(txdb, mapInputs, mapTestPoolTmp, CDiskTxPos(1,1,1), pindexPrev, false, true))
                continue;
            mapTestPoolTmp[tx.GetHash()] = CTxIndex(CDiskTxPos(1,1,1), tx.vout.size());
            swap(mapTestPool, mapTestPoolTmp);

            // Added
            pblock->vtx.push_back(tx);
            nBlockSize += nTxSize;
            ++nBlockTx;
            nBlockSigOps += nTxSigOps;
            nFees += nTxFees;

            if (fDebug && GetBoolArg("-printpriority"))
            {
                printf("priority %.1f feeperkb %.1f txid %s\n",
                       dPriority, dFeePerKb, tx.GetHash().ToString().c_str());
            }

            // Add transactions that depend on this one to the priority queue
            uint256 hash = tx.GetHash();
            if (mapDependers.count(hash))
            {
                BOOST_FOREACH(COrphan* porphan, mapDependers[hash])
                {
                    if (!porphan->setDependsOn.empty())
                    {
                        porphan->setDependsOn.erase(hash);
                        if (porphan->setDependsOn.empty())
                        {
                            vecPriority.push_back(TxPriority(porphan->dPriority, porphan->dFeePerKb, porphan->ptx));
                            std::push_heap(vecPriority.begin(), vecPriority.end(), comparer);
                        }
                    }
                }
            }
        }

        nLastBlockTx = nBlockTx;
        nLastBlockSize = nBlockSize;

        if (fDebug && GetBoolArg("-printpriority"))
            printf("AddPoolTransactions(): total size %" PRIu64"\n", nBlockSize);
    }

    return nFees;
}

// CreateNewBlock: create new block (without proof-of-work/proof-of-stake)
CBlock* CreateNewBlock(CWallet* pwallet, bool fProofOfStake, int64_t* pFees)
{
    // Create new block
    unique_ptr<CBlock> pblock(new CBlock());
    if (!pblock.get())
        return NULL;

    CBlockIndex* pindexPrev = pindexBest;

    // Create coinbase tx
    CTransaction txNew;
    txNew.vin.resize(1);
    txNew.vin[0].prevout.SetNull();
    txNew.vout.resize(1);
    
    
    int nHeight = pindexPrev->nHeight+1; // height of new block

    if (!fProofOfStake)
    {
        CReserveKey reservekey(pwallet);
        CPubKey pubkey;
        if (!reservekey.GetReservedKey(pubkey))
            return NULL;
        txNew.vout[0].scriptPubKey.SetDestination(pubkey.GetID());
    }
    else
    {
        // Height first in coinbase required for block.version=2
        txNew.vin[0].scriptSig = (CScript() << nHeight) + COINBASE_FLAGS;
        assert(txNew.vin[0].scriptSig.size() <= 100);

        txNew.vout[0].SetEmpty();
    }

    // Add our coinbase tx as first transaction
    pblock->vtx.push_back(txNew);

    time_t rawtime;

    time ( &rawtime );

    pblock->nBits = GetNextTargetRequired(pindexPrev, fProofOfStake, rawtime);

    // Collect memory pool transactions into the block
    int64_t nFees = 0;
    {
        LOCK2(cs_main, mempool.cs);

        int64_t nTxTimeLimit = GetAdjustedTime();
        if (fProofOfStake)
            nTxTimeLimit = min(nTxTimeLimit, (int64_t)pblock->vtx[0].nTime);
        nFees = AddPoolTransactions(pblock.get(), pindexPrev, nTxTimeLimit, 0, 0, std::set<COutPoint>());

        if (!fProofOfStake)
            pblock->vtx[0].vout[0].nValue = GetProofOfWorkReward(nHeight, nFees);

        if (pFees)
            *pFees = nFees;

        // Fill in header
        pblock->hashPrevBlock  = pindexPrev->GetBlockHash();
        pblock->nTime          = max(pindexPrev->GetPastTimeLimit()+1, pblock->GetMaxTransactionTime());
        pblock->nTime          = max(pblock->GetBlockTime(), PastDrift(pindexPrev->GetBlockTime()));
        if (!fProofOfStake)
            pblock->UpdateTime(pindexPrev);
        pblock->nNonce         = 0;
    }

    return pblock.release();
}


void IncrementExtraNonce(CBlock* pblock, CBlockIndex* pindexPrev, unsigned int& nExtraNonce)
{
    // Update nExtraNonce
    static uint256 hashPrevBlock;
    if (hashPrevBlock != pblock->hashPrevBlock)
    {
        nExtraNonce = 0;
        hashPrevBlock = pblock->hashPrevBlock;
    }
    ++nExtraNonce;

    unsigned int nHeight = pindexPrev->nHeight+1; // Height first in coinbase required for block.version=2
    pblock->vtx[0].vin[0].scriptSig = (CScript() << nHeight << CBigNum(nExtraNonce)) + COINBASE_FLAGS;
    assert(pblock->vtx[0].vin[0].scriptSig.size() <= 100);

    pblock->hashMerkleRoot = pblock->BuildMerkleTree();
}


void FormatHashBuffers(CBlock* pblock, char* pmidstate, char* pdata, char* phash1)
{
    //
    // Pre-build hash buffers
    //
    struct
    {
        struct unnamed2
        {
            int nVersion;
            uint256 hashPrevBlock;
            uint256 hashMerkleRoot;
            unsigned int nTime;
            unsigned int nBits;
            unsigned int nNonce;
        }
        block;
        unsigned char pchPadding0[64];
        uint256 hash1;
        unsigned char pchPadding1[64];
    }
    tmp;
    memset(&tmp, 0, sizeof(tmp));

    tmp.block.nVersion       = pblock->nVersion;
    tmp.block.hashPrevBlock  = pblock->hashPrevBlock;
    tmp.block.hashMerkleRoot = pblock->hashMerkleRoot;
    tmp.block.nTime          = pblock->nTime;
    tmp.block.nBits          = pblock->nBits;
    tmp.block.nNonce         = pblock->nNonce;

    FormatHashBlocks(&tmp.block, sizeof(tmp.block));
    FormatHashBlocks(&tmp.hash1, sizeof(tmp.hash1));

    // Byte swap all the input buffer
    for (unsigned int i = 0; i < sizeof(tmp)/4; i++)
        ((unsigned int*)&tmp)[i] = ByteReverse(((unsigned int*)&tmp)[i]);

    // Precalc the first half of the first hash, which stays constant
    SHA256Transform(pmidstate, &tmp.block, pSHA256InitState);

    memcpy(pdata, &tmp.block, 128);
    memcpy(phash1, &tmp.hash1, 64);
}


bool CheckWork(CBlock* pblock, CWallet& wallet, CReserveKey& reservekey)
{
    uint256 hashBlock = pblock->GetHash();
    uint256 hashTarget = CBigNum().SetCompact(pblock->nBits).getuint256();

    if(!pblock->IsProofOfWork())
        return error("CheckWork() : %s is not a proof-of-work block", hashBlock.GetHex().c_str());

    if (hashBlock > hashTarget)
        return error("CheckWork() : proof-of-work not meeting target");

    //// debug print
    printf("CheckWork() : new proof-of-work block found  \n  hash: %s  \ntarget: %s\n", hashBlock.GetHex().c_str(), hashTarget.GetHex().c_str());
    pblock->print();
    printf("generated %s\n", FormatMoney(pblock->vtx[0].vout[0].nValue).c_str());

    // Found a solution
    {
        LOCK(cs_main);
        if (pblock->hashPrevBlock != hashBestChain)
            return error("CheckWork() : generated block is stale");

        // Remove key from key pool
        reservekey.KeepKey();

        // Track how many getdata requests this block gets
        {
            LOCK(wallet.cs_wallet);
            wallet.mapRequestCount[hashBlock] = 0;
        }

        // Process this block the same as if we had received it from another node
        if (!ProcessBlock(NULL, pblock))
            return error("CheckWork() : ProcessBlock, block not accepted");
    }

    return true;
}

bool CheckStake(CBlock* pblock, CWallet& wallet)
{
    uint256 proofHash = 0, hashTarget = 0;
    uint256 hashBlock = pblock->GetHash();

    if(!pblock->IsProofOfStake())
        return error("CheckStake() : %s is not a proof-of-stake block", hashBlock.GetHex().c_str());

    // verify hash target and signature of coinstake tx
    if (!CheckProofOfStake(pblock->vtx[1], pblock->nBits, proofHash, hashTarget))
        return error("CheckStake() : proof-of-stake checking failed");

    //// debug print
    printf("CheckStake() : new proof-of-stake block found  \n  hash: %s \nproofhash: %s  \ntarget: %s\n", hashBlock.GetHex().c_str(), proofHash.GetHex().c_str(), hashTarget.GetHex().c_str());
    pblock->print();
    printf("out %s\n", FormatMoney(pblock->vtx[1].GetValueOut()).c_str());

    // Found a solution
    {
        LOCK(cs_main);
        if (pblock->hashPrevBlock != hashBestChain)
            return error("CheckStake() : generated block is stale");

        // Track how many getdata requests this block gets
        {
            LOCK(wallet.cs_wallet);
            wallet.mapRequestCount[hashBlock] = 0;
        }

        // Process this block the same as if we had received it from another node
        if (!ProcessBlock(NULL, pblock))
            return error("CheckStake() : ProcessBlock, block not accepted");
    }

    return true;
}

bool BuildStakeBlock(CWallet& wallet, const CStakeKernel& kernel, const StakeCoins& vCoins, int64_t nBalance, CBlockIndex* pindexPrev, unsigned int nBits, CBlock& blockRet, std::string& strFailRet)
{
    AssertLockHeld(cs_main);
    AssertLockHeld(wallet.cs_wallet);
    strFailRet.clear();
    if (pindexBest != pindexPrev)
    {
        strFailRet = "tip moved";
        return false;
    }

    // A pass uses a staking coin only while the wallet still holds it; a staking
    // coin must still be unspent in the wallet
    if (!IsStakeCoinUsable(wallet, kernel.hashTx, kernel.pwtx, kernel.nOut))
    {
        strFailRet = "coin";
        return false;
    }

    const unsigned int nTime = kernel.nTime;
    const int nHeight = pindexPrev->nHeight + 1;

    // Header, at the kernel's time
    blockRet.SetNull();
    blockRet.hashPrevBlock = pindexPrev->GetBlockHash();
    blockRet.nTime = nTime;
    blockRet.nBits = nBits;
    blockRet.nNonce = 0;

    // Coinbase, as CreateNewBlock makes it for proof-of-stake, at the kernel's time
    CTransaction txCoinBase;
    txCoinBase.vin.resize(1);
    txCoinBase.vin[0].prevout.SetNull();
    txCoinBase.vin[0].scriptSig = (CScript() << nHeight) + COINBASE_FLAGS;
    assert(txCoinBase.vin[0].scriptSig.size() <= 100);
    txCoinBase.vout.resize(1);
    txCoinBase.vout[0].SetEmpty();
    txCoinBase.nTime = nTime;
    blockRet.vtx.push_back(txCoinBase);

    // Coinstake inputs and outputs; the values are set once the fees are known
    CTransaction txCoinStake;
    std::vector<const CWalletTx*> vwtxPrev;
    int64_t nCredit = 0;
    if (!wallet.BeginCoinStake(kernel, vCoins, nBalance, txCoinStake, vwtxPrev, nCredit))
    {
        strFailRet = "credit check";
        return false;
    }

    // Memory pool transactions for the kernel's time and this height, none spending
    // a coinstake input. This also sets nLastBlockTx and nLastBlockSize.
    std::set<COutPoint> setStakeInputs;
    BOOST_FOREACH(const CTxIn& txin, txCoinStake.vin)
        setStakeInputs.insert(txin.prevout);
    const int64_t nFees = AddPoolTransactions(&blockRet, pindexPrev, nTime, nHeight, nTime, setStakeInputs);

    // The reward includes the fees of the transactions in the block
    if (!wallet.FinishCoinStake(nFees, nCredit, vwtxPrev, nHeight, txCoinStake))
    {
        strFailRet = "coinstake";
        return false;
    }

    // Coinbase, coinstake, then the pool transactions; signed by the kernel key
    blockRet.vtx.insert(blockRet.vtx.begin() + 1, txCoinStake);
    blockRet.hashMerkleRoot = blockRet.BuildMerkleTree();
    CKey key(kernel.key);
    if (!key.Sign(blockRet.GetHash(), blockRet.vchBlockSig))
    {
        strFailRet = "sign";
        return false;
    }
    return true;
}

// Sleep nMilliseconds in steps of at most STAKE_MINER_SLEEP_MS; return early at shutdown.
static void StakeMinerSleep(int64_t nMilliseconds)
{
    while (nMilliseconds > 0 && !fShutdown)
    {
        const int64_t nStep = min(nMilliseconds, STAKE_MINER_SLEEP_MS);
        MilliSleep(nStep);
        nMilliseconds -= nStep;
    }
}

// One stake pass. It searches the seconds after nLastSearched up to now, each once,
// against the best block taken at the start of the pass, and builds and submits a
// block only if it finds a kernel. It uses a staking coin only while the wallet
// still holds it.
static void StakeMinerPass(CWallet* pwallet, CStakeTxSourceCache& cache, int64_t& nLastSearched)
{
    const int64_t nNow = GetAdjustedTime();
    if (nNow <= nLastSearched)
        return; // nothing new to search: seconds are searched in increasing order

    CBlockIndex* pindexPrev = NULL;
    unsigned int nBits = 0;
    int64_t nFirst = 0, nLast = 0;
    bool fWindow = false;
    bool fCandidates = false;
    bool fKernel = false;
    bool fBuilt = false;
    std::string strBuildFail;
    int64_t nElapsed = 0;
    StakeCoins vCoins;
    int64_t nBalance = 0;
    std::vector<uint256> vTxids;
    std::set<uint256> setTxids;
    int64_t nWalkMicros = 0, nSearchMicros = 0, nBuildMicros = 0;

    // Hold 1: the best block, the window, the target and the wallet walk
    {
        LOCK2(cs_main, pwallet->cs_wallet);
        const int64_t nStart = GetTimeMicros();
        pindexPrev = pindexBest;
        fWindow = GetStakeSearchWindow(pindexPrev->GetPastTimeLimit(), pindexPrev->GetBlockTime(), nLastSearched, nNow, nFirst, nLast);
        nElapsed = nNow - nLastSearched;
        nLastCoinStakeSearchInterval = nElapsed;
        nLastSearched = nNow; // the window is fixed: its seconds are not searched again
        if (fWindow)
        {
            // Every second in the window has nNow's Flash class, so one target fits all
            nBits = GetNextTargetRequired(pindexPrev, true, (unsigned int)nNow);
            fCandidates = pwallet->SelectStakeCandidates((unsigned int)nNow, vCoins, nBalance);
            BOOST_FOREACH(const CStakeCoin& coin, vCoins)
            {
                if (setTxids.insert(coin.hashTx).second)
                    vTxids.push_back(coin.hashTx);
            }
        }
        nWalkMicros = GetTimeMicros() - nStart;
    }

    if (fWindow && fCandidates)
    {
        // Block data for coins not yet in the cache: a lock per read, hashing unlocked
        cache.Fill(vTxids);

        // Keep only the candidates' entries whose block is still in the main chain
        {
            LOCK(cs_main);
            const int64_t nStart = GetTimeMicros();
            cache.Prune(setTxids);
            nWalkMicros += GetTimeMicros() - nStart;
        }

        // Hold 2: FindStakeKernel takes cs_main and cs_wallet for each chunk of coins
        CStakeKernel kernel;
        const int64_t nSearchStart = GetTimeMicros();
        fKernel = FindStakeKernel(vCoins, cache, *pwallet, pindexPrev, nBits, nFirst, nLast, nNow, kernel);
        nSearchMicros = GetTimeMicros() - nSearchStart;

        // Hold 3: build and submit, on the block the search used
        if (fKernel)
        {
            LOCK2(cs_main, pwallet->cs_wallet);
            const int64_t nStart = GetTimeMicros();
            CBlock block;
            if (BuildStakeBlock(*pwallet, kernel, vCoins, nBalance, pindexPrev, nBits, block, strBuildFail))
            {
                fBuilt = true;
                SetThreadPriority(THREAD_PRIORITY_NORMAL);
                CheckStake(&block, *pwallet);
                SetThreadPriority(THREAD_PRIORITY_LOWEST);
            }
            nBuildMicros = GetTimeMicros() - nStart;
        }
    }

    if (fDebug)
    {
        const int64_t nSearched = fWindow ? nLast - nFirst + 1 : 0;
        printf("StakeMiner : pass candidates=%" PRIszu " searched=%" PRId64 " lost=%" PRId64 " kernel=%d built=%d hold_us=%" PRId64 " (walk %" PRId64 ", search %" PRId64 ", build %" PRId64 ")\n",
               vCoins.size(), nSearched, nElapsed - nSearched, fKernel ? 1 : 0, fBuilt ? 1 : 0,
               nWalkMicros + nSearchMicros + nBuildMicros, nWalkMicros, nSearchMicros, nBuildMicros);
        if (fKernel && !fBuilt)
            printf("StakeMiner : kernel found, block not built: %s\n", strBuildFail.c_str());
    }
}

void StakeMiner(CWallet *pwallet)
{
    SetThreadPriority(THREAD_PRIORITY_LOWEST);

    // Make this thread recognisable as the mining thread
    RenameThread("pinkcoin-miner");

    const unsigned int nMinStakePeers = GetMinStakePeers();
    CStakeGateState gate;           // the start latch
    CStakeTxSourceCache cache;      // owned by this thread
    int64_t nLastSearched = 0;      // the latest second searched; set when staking starts
    int64_t nSleep = STAKE_MINER_SLEEP_MS;

    while (!fShutdown)
    {
        StakeMinerSleep(nSleep);
        nSleep = STAKE_MINER_SLEEP_MS;
        if (fShutdown)
            return;

        try
        {
            size_t nPeers = 0;
            {
                LOCK(cs_vNodes);
                nPeers = vNodes.size();
            }
            const int64_t nNow = GetAdjustedTime();
            bool fOpened = false;
            if (!CheckStakeGate(gate, pwallet->IsLocked(), nPeers, nMinStakePeers, IsInitialBlockDownload(), fTestNet, nNow, fOpened))
            {
                nLastCoinStakeSearchInterval = 0;
                continue;
            }
            if (fOpened)
            {
                // The first pass searches the seconds after this one. Each second is
                // searched at most once.
                nLastSearched = std::max(nLastSearched, nNow);
                if (fDebug)
                    printf("StakeMiner : staking starts with %" PRIszu " peers\n", nPeers);
            }

            StakeMinerPass(pwallet, cache, nLastSearched);
        }
        catch (std::exception& e)
        {
            printf("ERROR: StakeMiner : pass skipped after exception: %s; next pass in %" PRId64 " s\n",
                   e.what(), STAKE_EXCEPTION_SLEEP_MS / 1000);
            nSleep = STAKE_EXCEPTION_SLEEP_MS;
        }
    }
}


void CStakeTxSourceCache::Prune(const std::set<uint256>& setWanted)
{
    AssertLockHeld(cs_main);
    std::map<uint256, CStakeTxSource> mapKept;
    BOOST_FOREACH(const uint256& hashTx, setWanted)
    {
        std::map<uint256, CStakeTxSource>::const_iterator it = mapSource.find(hashTx);
        if (it == mapSource.end())
            continue;
        std::map<uint256, CBlockIndex*>::const_iterator mi = mapBlockIndex.find(it->second.hashBlock);
        if (mi != mapBlockIndex.end() && mi->second->IsInMainChain())
            mapKept.insert(*it);
    }
    mapSource.swap(mapKept);
}

void CStakeTxSourceCache::Fill(const std::vector<uint256>& vTxids)
{
    CTxDB txdb("r");
    BOOST_FOREACH(const uint256& hashTx, vTxids)
    {
        if (mapSource.count(hashTx))
            continue;

        CTxIndex txindex;
        {
            LOCK(cs_main);
            if (!txdb.ReadTxIndex(hashTx, txindex))
                continue;
        }

        CBlock block;
        {
            LOCK(cs_main);
            if (!block.ReadFromDisk(txindex.pos.nFile, txindex.pos.nBlockPos, false))
                continue;
        }

        CStakeTxSource source;
        source.hashBlock = block.GetHash(); // scrypt: no lock held
        source.nTimeBlock = block.GetBlockTime();
        source.nTxOffset = txindex.pos.nTxPos - txindex.pos.nBlockPos;
        mapSource.insert(std::make_pair(hashTx, source));
    }
}

const CStakeTxSource* CStakeTxSourceCache::Find(const uint256& hashTx) const
{
    std::map<uint256, CStakeTxSource>::const_iterator it = mapSource.find(hashTx);
    return it == mapSource.end() ? NULL : &it->second;
}

bool SearchStakeKernel(unsigned int nBits, const CStakeTxSource& source, const CTransaction& txPrev, const COutPoint& prevout, int64_t nFirst, int64_t nLast, int64_t nNow, unsigned int& nTimeRet)
{
    AssertLockHeld(cs_main);

    // Only coins past the min age at the oldest second a pass can search
    if ((int64_t)source.nTimeBlock + nStakeMinAge > nNow - STAKE_SEARCH_SECONDS)
        return false;

    for (int64_t t = nLast; t >= nFirst; t--)
    {
        uint256 hashProofOfStake = 0, targetProofOfStake = 0;
        if (CheckStakeKernelHash(nBits, source.hashBlock, source.nTimeBlock, source.nTxOffset, txPrev, prevout, (unsigned int)t, hashProofOfStake, targetProofOfStake))
        {
            nTimeRet = (unsigned int)t;
            return true;
        }
    }
    return false;
}

bool FindStakeKernel(const StakeCoins& vCoins, const CStakeTxSourceCache& cache, const CWallet& wallet, const CBlockIndex* pindexPrev, unsigned int nBits, int64_t nFirst, int64_t nLast, int64_t nNow, CStakeKernel& kernelRet)
{
    const bool fPrint = fDebug && GetBoolArg("-printcoinstake");

    for (size_t nChunk = 0; nChunk < vCoins.size(); nChunk += STAKE_SEARCH_CHUNK)
    {
        LOCK2(cs_main, wallet.cs_wallet);
        if (pindexBest != pindexPrev || fShutdown)
            return false;

        const size_t nEnd = min(vCoins.size(), nChunk + STAKE_SEARCH_CHUNK);
        for (size_t i = nChunk; i < nEnd; i++)
        {
            if (fShutdown)
                return false;

            // A pass uses a staking coin only while the wallet still holds it; a
            // staking coin must still be unspent in the wallet
            const CStakeCoin& coin = vCoins[i];
            if (!IsStakeCoinUsable(wallet, coin.hashTx, coin.pwtx, coin.nOut))
                continue;
            const CStakeTxSource* pSource = cache.Find(coin.hashTx);
            if (!pSource)
                continue;

            const CWalletTx* pwtx = coin.pwtx;
            const unsigned int nOut = coin.nOut;
            unsigned int nTime = 0;
            if (!SearchStakeKernel(nBits, *pSource, *pwtx, COutPoint(coin.hashTx, nOut), nFirst, nLast, nNow, nTime))
                continue;
            if (fPrint)
                printf("FindStakeKernel : kernel found\n");

            // The coinstake can only sign for pay to public key and pay to address;
            // a pay-to-address kernel pays the coinstake to its public key.
            const CScript& scriptPubKeyKernel = pwtx->vout[nOut].scriptPubKey;
            std::vector<valtype> vSolutions;
            txnouttype whichType;
            if (!Solver(scriptPubKeyKernel, whichType, vSolutions))
            {
                if (fPrint)
                    printf("FindStakeKernel : failed to parse kernel\n");
                continue;
            }
            if (whichType != TX_PUBKEY && whichType != TX_PUBKEYHASH)
            {
                if (fPrint)
                    printf("FindStakeKernel : no support for kernel type=%d\n", whichType);
                continue;
            }

            CKey key;
            CScript scriptPubKeyOut;
            if (whichType == TX_PUBKEYHASH)
            {
                if (!wallet.GetKey(uint160(vSolutions[0]), key))
                {
                    if (fPrint)
                        printf("FindStakeKernel : failed to get key for kernel type=%d\n", whichType);
                    continue;
                }
                scriptPubKeyOut << key.GetPubKey() << OP_CHECKSIG;
            }
            else
            {
                const valtype& vchPubKey = vSolutions[0];
                if (!wallet.GetKey(Hash160(vchPubKey), key))
                {
                    if (fPrint)
                        printf("FindStakeKernel : failed to get key for kernel type=%d\n", whichType);
                    continue;
                }
                if (key.GetPubKey() != vchPubKey)
                {
                    if (fPrint)
                        printf("FindStakeKernel : invalid key for kernel type=%d\n", whichType);
                    continue;
                }
                CScript(scriptPubKeyKernel).swap(scriptPubKeyOut); // copy-construct: CScript's implicit copy assignment is deprecated
            }

            kernelRet.hashTx = coin.hashTx;
            kernelRet.pwtx = pwtx;
            kernelRet.nOut = nOut;
            kernelRet.nTime = nTime;
            kernelRet.key = key;
            CScript(scriptPubKeyKernel).swap(kernelRet.scriptPubKeyKernel);
            kernelRet.scriptPubKeyOut.swap(scriptPubKeyOut);
            if (fPrint)
                printf("FindStakeKernel : found kernel type=%d at %u\n", whichType, nTime);
            return true;
        }
    }
    return false;
}
