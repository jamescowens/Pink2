// Copyright (c) 2026 The Pinkcoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef PINKCOIN_STAKEPROOFS_H
#define PINKCOIN_STAKEPROOFS_H

#include "uint256.h"

// Returns the fixed proof hash for a listed block, or NULL.
const uint256* GetFixedStakeProof(const uint256& hashBlock);

#endif // PINKCOIN_STAKEPROOFS_H
