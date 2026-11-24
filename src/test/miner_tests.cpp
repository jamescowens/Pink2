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

BOOST_AUTO_TEST_SUITE_END()
