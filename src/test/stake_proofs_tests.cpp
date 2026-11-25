//
// Unit tests for the fixed proof hashes
//
#include <boost/test/unit_test.hpp>

#include "../stakeproofs.h"
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

BOOST_AUTO_TEST_SUITE_END()
