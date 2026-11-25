//
// Unit tests for the fixed proof hashes, the block lock-in and the Flash hour
//
#include <boost/test/unit_test.hpp>

#include <limits>
#include <stdlib.h>
#include <time.h>
#include <string>

#include <boost/random/mersenne_twister.hpp>

#include "../stakeproofs.h"
#include "../checkpoints.h"
#include "../main.h"
#include "../util.h"

using namespace std;

namespace {

// Saves fTestNet and restores it on scope exit, including a failed REQUIRE
struct TestNetGuard
{
    bool fSave;
    TestNetGuard() : fSave(fTestNet) {}
    ~TestNetGuard() { fTestNet = fSave; }
};

} // namespace

BOOST_AUTO_TEST_SUITE(stake_proofs_tests)

BOOST_AUTO_TEST_CASE(listed_proofs_mainnet)
{
    TestNetGuard guard;
    fTestNet = false;
    const uint256 a("0x092cf20219a5f559a239715916d6f482a72154ec0faaa0090d57ee06bbeb56d6");
    const uint256 b("0xb10d687919a15c8b818d4175b596cad18cb39641536f08280f90db143560d082");
    const uint256 other("0x56b3a3723705990feae97466b2dd827641357dc1d06f9204cc675e005748c4f9");
    const uint256* p = GetFixedStakeProof(a);
    BOOST_REQUIRE(p != NULL);
    BOOST_CHECK(*p == uint256("0xee35038538a17e0d1a25d385e6a6368416698762e99e9916a35ea504706d69fd"));
    p = GetFixedStakeProof(b);
    BOOST_REQUIRE(p != NULL);
    BOOST_CHECK(*p == uint256("0xaef9604c6b20428d5cbe6ea77d1bde6511db161307c6f63b414097987b1ae143"));
    p = GetFixedStakeProof(uint256("0xb9818366d52751455db68f488b7636a7f804d1ce368b3b5e9c8945a1dc68e51e"));
    BOOST_REQUIRE(p != NULL);
    BOOST_CHECK(*p == uint256("0xe7906487a9c311d62390ead5dfe84f23e570c05eaf8343cbe63ef10da8bc218a"));
    BOOST_CHECK(GetFixedStakeProof(other) == NULL);
}

BOOST_AUTO_TEST_CASE(listed_proofs_testnet_empty)
{
    TestNetGuard guard;
    fTestNet = true;
    BOOST_CHECK(GetFixedStakeProof(uint256("0x092cf20219a5f559a239715916d6f482a72154ec0faaa0090d57ee06bbeb56d6")) == NULL);
    BOOST_CHECK(GetFixedStakeProof(uint256("0xb10d687919a15c8b818d4175b596cad18cb39641536f08280f90db143560d082")) == NULL);
    BOOST_CHECK(GetFixedStakeProof(uint256("0xb9818366d52751455db68f488b7636a7f804d1ce368b3b5e9c8945a1dc68e51e")) == NULL);
}

BOOST_AUTO_TEST_CASE(lockin_main_hash)
{
    TestNetGuard guard;
    fTestNet = false;
    BOOST_CHECK(Checkpoints::CheckHardened(3063000, uint256("0xd86b8cf55ed4ef9bfbebb6edf12dbec3914552d59b32f32dd52d339fd966e341")));
}

BOOST_AUTO_TEST_CASE(lockin_other_hash)
{
    TestNetGuard guard;
    fTestNet = false;
    BOOST_CHECK(!Checkpoints::CheckHardened(3063000, uint256(1)));
}

BOOST_AUTO_TEST_CASE(lockin_estimate_unchanged)
{
    TestNetGuard guard;
    fTestNet = false;
    BOOST_CHECK_EQUAL(Checkpoints::GetTotalBlocksEstimate(), 728000);
}

BOOST_AUTO_TEST_CASE(lockin_testnet_none)
{
    TestNetGuard guard;
    fTestNet = true;
    BOOST_CHECK(Checkpoints::CheckHardened(3063000, uint256(1)));
}

#ifndef WIN32
// The four Flash hours, copied from main.cpp (nHour1..nHour4 are not visible
// here), with the hour taken from a reference calendar conversion
static bool FlashHourReference(unsigned int nTime)
{
    time_t t = nTime;
    struct tm tmv;
    gmtime_r(&t, &tmv);
    int h = tmv.tm_hour;
    return h == 15 || h == 20 || h == 1 || h == 6;
}

// Sets TZ=UTC0 for the scope and restores the previous TZ on exit
struct TzGuard
{
    bool fHad;
    std::string strOld;
    TzGuard()
    {
        const char* p = getenv("TZ");
        fHad = (p != NULL);
        if (fHad)
            strOld = p;
        setenv("TZ", "UTC0", 1);
        tzset();
    }
    ~TzGuard()
    {
        if (fHad)
            setenv("TZ", strOld.c_str(), 1);
        else
            unsetenv("TZ");
        tzset();
    }
};

BOOST_AUTO_TEST_CASE(flash_hour_matches_reference)
{
    TzGuard tzGuard;

    // Largest time compared: the top of the 32-bit unsigned range, or
    // INT32_MAX where time_t is 32 bits
    const long long nMax = sizeof(time_t) == 4 ? (long long)std::numeric_limits<int32_t>::max() : 4294967295LL;

    // every hour boundary from 2014-01-01 to nMax
    long long mismatches = 0;
    long long firstBad = -1;
    for (long long b = 1388534400LL; b <= nMax; b += 3600)
    {
        for (long long t = b - 1; t <= b + 1; t++)
        {
            if (t > nMax)
                continue;
            if (IsFlashStake((unsigned int)t) != FlashHourReference((unsigned int)t))
            {
                if (mismatches == 0)
                    firstBad = t;
                mismatches++;
            }
        }
    }
    if (mismatches)
        BOOST_TEST_MESSAGE("first boundary mismatch at t=" << firstBad);
    BOOST_CHECK_EQUAL(mismatches, 0);

    // random times from 0 to nMax
    boost::random::mt19937 gen(20261124);
    mismatches = 0;
    firstBad = -1;
    for (int i = 0; i < 5000000; i++)
    {
        unsigned int t = (unsigned int)(gen() % ((unsigned long long)nMax + 1));
        if (IsFlashStake(t) != FlashHourReference(t))
        {
            if (mismatches == 0)
                firstBad = t;
            mismatches++;
        }
    }
    if (mismatches)
        BOOST_TEST_MESSAGE("first random mismatch at t=" << firstBad);
    BOOST_CHECK_EQUAL(mismatches, 0);
}
#endif

BOOST_AUTO_TEST_SUITE_END()
