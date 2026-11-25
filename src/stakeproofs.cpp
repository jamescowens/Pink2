// Copyright (c) 2026 The Pinkcoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <map>
#include <boost/assign/list_of.hpp> // for 'map_list_of()'

#include "stakeproofs.h"
#include "util.h"

typedef std::map<uint256, uint256> MapStakeProofs;

// Block hash -> fixed proof hash.
static const MapStakeProofs mapStakeProofs =
    boost::assign::map_list_of
    (uint256("0x092cf20219a5f559a239715916d6f482a72154ec0faaa0090d57ee06bbeb56d6"),
     uint256("0xee35038538a17e0d1a25d385e6a6368416698762e99e9916a35ea504706d69fd"))
    (uint256("0xb10d687919a15c8b818d4175b596cad18cb39641536f08280f90db143560d082"),
     uint256("0xaef9604c6b20428d5cbe6ea77d1bde6511db161307c6f63b414097987b1ae143"))
    (uint256("0xb9818366d52751455db68f488b7636a7f804d1ce368b3b5e9c8945a1dc68e51e"),
     uint256("0xe7906487a9c311d62390ead5dfe84f23e570c05eaf8343cbe63ef10da8bc218a"))
    ;

static const MapStakeProofs mapStakeProofsTestNet;

const uint256* GetFixedStakeProof(const uint256& hashBlock)
{
    const MapStakeProofs& m = fTestNet ? mapStakeProofsTestNet : mapStakeProofs;
    MapStakeProofs::const_iterator it = m.find(hashBlock);
    return it == m.end() ? NULL : &it->second;
}
