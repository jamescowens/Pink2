// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2012 The Bitcoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef BITCOIN_INIT_H
#define BITCOIN_INIT_H

#include "wallet.h"
namespace boost {
    class thread_group;
} // namespace boost

extern CWallet* pwalletMain;
extern CWallet* pstakeDB;
/** How long shutdown waits for every core thread to stop before the backstop. */
static const int64_t SHUTDOWN_JOIN_TIMEOUT_MS = 30000;
/** How long the backstop's final flush may take. */
static const int64_t SHUTDOWN_FORCED_FLUSH_TIMEOUT_MS = 10000;

void StartShutdown();
/** Arms the shutdown watchdog to end the process with status 1 nMillis from now,
 *  unless shutdown disarms it first. An armed watchdog's deadline only moves later. */
void ArmShutdownWatchdog(int64_t nMillis);
/** Shutdown, phase 1: set fShutdown, wake every core thread, and join them all
 *  within nTimeoutMillis. Returns true if every thread stopped. Idempotent.
 *  fCallerStopped says whether what the caller stops itself (the GUI's console
 *  executor) has stopped; the watchdog is disarmed only if it has and every core
 *  thread was joined. */
bool ShutdownStopThreads(int64_t nTimeoutMillis = SHUTDOWN_JOIN_TIMEOUT_MS, bool fCallerStopped = true);
/** Shutdown, phase 2: flush and close the databases and free the wallets. With
 *  fClean false (a thread did not stop) it flushes, then ends the process with
 *  _Exit(0) and does not return. Idempotent. */
void ShutdownFinish(bool fClean);
/** Both phases. */
void Shutdown(void* parg);
bool AppInit2(boost::thread_group& threadGroup);
std::string HelpMessage();

#endif
