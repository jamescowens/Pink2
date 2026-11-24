//
// Unit tests for the stake miner's search window
//
#include <boost/test/unit_test.hpp>

#include "../main.h"
#include "../miner.h"

using namespace std;

BOOST_AUTO_TEST_SUITE(miner_tests)

BOOST_AUTO_TEST_CASE(stake_search_window)
{
    // H: first hour boundary where the Flash state changes
    int64_t H = 1764000000;
    while (IsFlashStake((unsigned int)H) == IsFlashStake((unsigned int)(H - 1)))
        H += 3600;

    int64_t nFirst, nLast;
    int64_t now = H + 1800;
    int64_t P = now - 1000;
    int64_t B = now - 100;

    // window_basic
    BOOST_CHECK(GetStakeSearchWindow(P, B, now - 5, now, nFirst, nLast));
    BOOST_CHECK_EQUAL(nFirst, now - 4);
    BOOST_CHECK_EQUAL(nLast, now);

    // window_cap
    BOOST_CHECK(GetStakeSearchWindow(P, B, now - 1000, now, nFirst, nLast));
    BOOST_CHECK_EQUAL(nFirst, now - 59);
    BOOST_CHECK_EQUAL(nLast, now);

    // window_last_searched_ahead
    BOOST_CHECK(!GetStakeSearchWindow(P, B, now + 3, now, nFirst, nLast));

    // window_same_second
    BOOST_CHECK(!GetStakeSearchWindow(P, B, now, now, nFirst, nLast));

    // window_flash_edge
    now = H + 20;
    BOOST_CHECK(GetStakeSearchWindow(now - 1000, now - 100, now - 100, now, nFirst, nLast));
    BOOST_CHECK_EQUAL(nFirst, H);
    BOOST_CHECK_EQUAL(nLast, H + 20);

    // window_flash_edge_exact
    now = H;
    BOOST_CHECK(GetStakeSearchWindow(now - 1000, now - 100, now - 100, now, nFirst, nLast));
    BOOST_CHECK_EQUAL(nFirst, H);
    BOOST_CHECK_EQUAL(nLast, H);

    // window_same_hour: inside one hour, not at its start: unchanged
    now = H + 50;
    BOOST_CHECK(GetStakeSearchWindow(now - 1000, now - 100, now - 40, now, nFirst, nLast));
    BOOST_CHECK_EQUAL(nFirst, H + 11);
    BOOST_CHECK_EQUAL(nLast, H + 50);

    // window_past_limit
    now = H + 1800;
    BOOST_CHECK(GetStakeSearchWindow(now - 10, now - 100, now - 100, now, nFirst, nLast));
    BOOST_CHECK_EQUAL(nFirst, now - 9);
    BOOST_CHECK_EQUAL(nLast, now);

    // window_prev_time_bound
    BOOST_CHECK(GetStakeSearchWindow(now - 1000, now + 590, now - 100, now, nFirst, nLast));
    BOOST_CHECK_EQUAL(nFirst, now - 10);
    BOOST_CHECK_EQUAL(nLast, now);

    // window_past_limit_ahead: the window is empty when the past limit is later than now
    now = H - 10;
    P = H + 5;
    B = now - 100;
    BOOST_CHECK(!GetStakeSearchWindow(P, B, now - 100, now, nFirst, nLast));
}

BOOST_AUTO_TEST_CASE(stake_coin_current)
{
    // A wallet with no file: the transaction goes straight into mapWallet
    CWallet wallet;
    CTransaction tx;
    tx.nTime = 1764000000;
    tx.vin.resize(1);
    tx.vout.resize(1);
    tx.vout[0].nValue = COIN;
    const uint256 hashTx = tx.GetHash();

    LOCK(wallet.cs_wallet);
    wallet.mapWallet.insert(make_pair(hashTx, CWalletTx(&wallet, tx)));
    const CWalletTx* pwtx = &wallet.mapWallet.find(hashTx)->second;

    // present: the txid maps to the entry
    BOOST_CHECK(IsStakeCoinCurrent(wallet, hashTx, pwtx));

    // another entry, not pwtx
    const CWalletTx wtxOther(&wallet, tx);
    BOOST_CHECK(wtxOther.GetHash() == hashTx);
    BOOST_CHECK(!IsStakeCoinCurrent(wallet, hashTx, &wtxOther));

    // a txid the wallet does not hold
    BOOST_CHECK(!IsStakeCoinCurrent(wallet, ~hashTx, pwtx));
    BOOST_CHECK(!IsStakeCoinUsable(wallet, ~hashTx, pwtx, 0));

    // present and unspent: usable
    BOOST_CHECK(IsStakeCoinUsable(wallet, hashTx, pwtx, 0));

    // spent: still held, not usable; a staking coin must still be unspent in the wallet
    wallet.mapWallet.find(hashTx)->second.MarkSpent(0);
    BOOST_CHECK(IsStakeCoinCurrent(wallet, hashTx, pwtx));
    BOOST_CHECK(!IsStakeCoinUsable(wallet, hashTx, pwtx, 0));

    // absent: the wallet does not hold the txid
    wallet.mapWallet.erase(hashTx);
    BOOST_CHECK(!IsStakeCoinCurrent(wallet, hashTx, pwtx));
}

BOOST_AUTO_TEST_CASE(stake_gate)
{
    CStakeGateState state;
    bool fOpened = false;
    int64_t t = 1000;

    // 1: too few peers to start
    BOOST_CHECK(!CheckStakeGate(state, false, 3, 7, false, false, t, fOpened));
    // 2: enough peers: the settle time starts
    BOOST_CHECK(!CheckStakeGate(state, false, 7, 7, false, false, t, fOpened));
    // 3: one second before the settle time ends
    BOOST_CHECK(!CheckStakeGate(state, false, 7, 7, false, false, t + 59, fOpened));
    // 4: the settle time has ended: open, on this call only
    BOOST_CHECK(CheckStakeGate(state, false, 7, 7, false, false, t + 60, fOpened) && fOpened);
    // 5: open, not newly opened
    BOOST_CHECK(CheckStakeGate(state, false, 7, 7, false, false, t + 61, fOpened) && !fOpened);

    int64_t now = t + 61;
    // 6: started: fewer peers than the minimum keep it open
    BOOST_CHECK(CheckStakeGate(state, false, 3, 7, false, false, ++now, fOpened) && !fOpened);
    // 7: wallet locked
    BOOST_CHECK(!CheckStakeGate(state, true, 3, 7, false, false, ++now, fOpened));
    // 8: unlocked again: the latch was kept
    BOOST_CHECK(CheckStakeGate(state, false, 3, 7, false, false, ++now, fOpened) && !fOpened);
    // 9: no peers: closed, and the latch is reset
    BOOST_CHECK(!CheckStakeGate(state, false, 0, 7, false, false, ++now, fOpened));
    // 10: starting again needs the minimum again
    BOOST_CHECK(!CheckStakeGate(state, false, 3, 7, false, false, ++now, fOpened));
    // 11: enough peers, then initial download: both closed
    BOOST_CHECK(!CheckStakeGate(state, false, 7, 7, false, false, ++now, fOpened));
    BOOST_CHECK(!CheckStakeGate(state, false, 7, 7, true, false, ++now, fOpened));
    // 12: testnet starts with one peer, after the settle time
    BOOST_CHECK(!CheckStakeGate(state, false, 1, 7, false, true, ++now, fOpened));
    BOOST_CHECK(CheckStakeGate(state, false, 1, 7, false, true, now + 60, fOpened) && fOpened);

    // 13-15: a fresh state with a minimum of one peer
    CStakeGateState state1;
    t = 5000;
    // 13: one peer: the settle time starts
    BOOST_CHECK(!CheckStakeGate(state1, false, 1, 1, false, false, t, fOpened));
    // 14: the settle time has ended: open, on this call only
    BOOST_CHECK(CheckStakeGate(state1, false, 1, 1, false, false, t + 60, fOpened) && fOpened);
    // 15: no peers: closed, and the latch is reset
    BOOST_CHECK(!CheckStakeGate(state1, false, 0, 1, false, false, t + 61, fOpened) && !state1.fStarted);
}

BOOST_AUTO_TEST_CASE(stake_gate_settle_restart)
{
    // Fewer peers than the minimum during the settle time: it begins again
    CStakeGateState state;
    bool fOpened = false;
    const int64_t t = 9000;
    BOOST_CHECK(!CheckStakeGate(state, false, 7, 7, false, false, t, fOpened));
    BOOST_CHECK(!CheckStakeGate(state, false, 3, 7, false, false, t + 30, fOpened));
    BOOST_CHECK(!CheckStakeGate(state, false, 7, 7, false, false, t + 60, fOpened));
    BOOST_CHECK(!CheckStakeGate(state, false, 7, 7, false, false, t + 119, fOpened));
    BOOST_CHECK(CheckStakeGate(state, false, 7, 7, false, false, t + 120, fOpened) && fOpened);
}

BOOST_AUTO_TEST_CASE(stake_gate_ibd)
{
    CStakeGateState state;
    bool fOpened = false;
    const int64_t t = 7000;

    // Open the gate
    BOOST_CHECK(!CheckStakeGate(state, false, 7, 7, false, false, t, fOpened));
    BOOST_CHECK(CheckStakeGate(state, false, 7, 7, false, false, t + 60, fOpened) && fOpened);
    // Initial download after the start: closed, and the latch is reset
    BOOST_CHECK(!CheckStakeGate(state, false, 7, 7, true, false, t + 61, fOpened) && !state.fStarted);
    // Enough peers again: the settle time begins again
    BOOST_CHECK(!CheckStakeGate(state, false, 7, 7, false, false, t + 62, fOpened));

    // Testnet still needs a peer
    CStakeGateState stateTestNet;
    BOOST_CHECK(!CheckStakeGate(stateTestNet, false, 0, 7, false, true, t, fOpened));
}

// Erases -minstakepeers from mapArgs when the test ends, whether or not its checks pass
struct MinStakePeersArgGuard
{
    ~MinStakePeersArgGuard() { mapArgs.erase("-minstakepeers"); }
};

BOOST_AUTO_TEST_CASE(stake_min_peers)
{
    MinStakePeersArgGuard guard;
    mapArgs.erase("-minstakepeers");

    BOOST_CHECK_EQUAL(GetMinStakePeers(), 7U);
    mapArgs["-minstakepeers"] = "1";
    BOOST_CHECK_EQUAL(GetMinStakePeers(), 1U);
    mapArgs["-minstakepeers"] = "0";
    BOOST_CHECK_EQUAL(GetMinStakePeers(), 1U);
    mapArgs["-minstakepeers"] = "-3";
    BOOST_CHECK_EQUAL(GetMinStakePeers(), 1U);
}

BOOST_AUTO_TEST_SUITE_END()
