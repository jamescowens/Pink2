// Copyright (c) 2026 The Pinkcoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file license.txt or http://www.opensource.org/licenses/mit-license.php.

// Tests for the joinable core-thread registry and the shutdown-aware sleep
// (StartCoreThread, JoinCoreThreads, CoreThreadCount, ShutdownSleep).

#include <atomic>
#include <string>
#include <vector>

#include <boost/test/unit_test.hpp>

#include "util.h"

namespace {

std::atomic<bool> fRelease(false);
std::atomic<bool> fOneShotDone(false);

void LoopUntilShutdown(void*)
{
    while (!fShutdown)
        ShutdownSleep(50);
}

void IgnoreShutdownUntilReleased(void*)
{
    while (!fRelease)
        MilliSleep(10);
}

void OneShot(void*)
{
    fOneShotDone = true;
}

void SetShutdownAfter100ms(void*)
{
    MilliSleep(100);
    fShutdown = true;
}

struct ShutdownFlagFixture
{
    ShutdownFlagFixture()
    {
        fShutdown = false;
        fRelease = false;
        fOneShotDone = false;
    }
    ~ShutdownFlagFixture()
    {
        // Leave nothing running for the next test case.
        fShutdown = true;
        fRelease = true;
        std::vector<std::string> v;
        JoinCoreThreads(5000, v);
        fShutdown = false;
        fRelease = false;
    }
};

}

BOOST_FIXTURE_TEST_SUITE(shutdown_tests, ShutdownFlagFixture)

BOOST_AUTO_TEST_CASE(registry_joins_all)
{
    BOOST_REQUIRE(StartCoreThread("test-loop-1", LoopUntilShutdown, NULL));
    BOOST_REQUIRE(StartCoreThread("test-loop-2", LoopUntilShutdown, NULL));
    BOOST_REQUIRE(StartCoreThread("test-loop-3", LoopUntilShutdown, NULL));
    BOOST_CHECK_EQUAL(CoreThreadCount(), 3U);

    fShutdown = true;
    std::vector<std::string> v;
    BOOST_CHECK(JoinCoreThreads(2000, v));
    BOOST_CHECK(v.empty());
    BOOST_CHECK_EQUAL(CoreThreadCount(), 0U);
}

BOOST_AUTO_TEST_CASE(registry_reports_straggler)
{
    BOOST_REQUIRE(StartCoreThread("test-straggler", IgnoreShutdownUntilReleased, NULL));

    fShutdown = true;
    std::vector<std::string> v;
    BOOST_CHECK(!JoinCoreThreads(200, v));
    BOOST_REQUIRE_EQUAL(v.size(), 1U);
    BOOST_CHECK_EQUAL(v[0], "test-straggler");
    BOOST_CHECK_EQUAL(CoreThreadCount(), 1U);

    fRelease = true;
    v.clear();
    BOOST_CHECK(JoinCoreThreads(2000, v));
    BOOST_CHECK(v.empty());
    BOOST_CHECK_EQUAL(CoreThreadCount(), 0U);
}

BOOST_AUTO_TEST_CASE(registry_prunes_finished)
{
    BOOST_REQUIRE(StartCoreThread("test-oneshot", OneShot, NULL));
    for (int i = 0; i < 100 && !fOneShotDone; i++)
        MilliSleep(10);
    BOOST_REQUIRE(fOneShotDone);
    MilliSleep(50); // let the one-shot thread finish returning

    BOOST_REQUIRE(StartCoreThread("test-loop", LoopUntilShutdown, NULL));
    BOOST_CHECK_EQUAL(CoreThreadCount(), 1U);
}

namespace {
std::atomic<bool> fChildStarted(false);
void StartChild(void*)
{
    // A registered thread starting another one (as StartNode does) must not try
    // to join itself while pruning.
    fChildStarted = StartCoreThread("test-child", LoopUntilShutdown, NULL);
    while (!fShutdown)
        ShutdownSleep(50);
}
}

BOOST_AUTO_TEST_CASE(registry_start_from_registered_thread)
{
    fChildStarted = false;
    BOOST_REQUIRE(StartCoreThread("test-parent", StartChild, NULL));
    for (int i = 0; i < 100 && !fChildStarted; i++)
        MilliSleep(10);
    BOOST_CHECK(fChildStarted);
    BOOST_CHECK_EQUAL(CoreThreadCount(), 2U);

    fShutdown = true;
    std::vector<std::string> v;
    BOOST_CHECK(JoinCoreThreads(2000, v));
    BOOST_CHECK(v.empty());
}

BOOST_AUTO_TEST_CASE(registry_refuses_after_shutdown)
{
    fShutdown = true;
    BOOST_CHECK(!StartCoreThread("late", LoopUntilShutdown, NULL));
    BOOST_CHECK_EQUAL(CoreThreadCount(), 0U);
}

BOOST_AUTO_TEST_CASE(shutdown_sleep_returns_early)
{
    BOOST_REQUIRE(StartCoreThread("test-setter", SetShutdownAfter100ms, NULL));
    int64_t t0 = GetTimeMillis();
    BOOST_CHECK(!ShutdownSleep(10000));
    BOOST_CHECK(GetTimeMillis() - t0 < 500);
}

BOOST_AUTO_TEST_CASE(shutdown_sleep_full)
{
    int64_t t0 = GetTimeMillis();
    BOOST_CHECK(ShutdownSleep(150));
    BOOST_CHECK(GetTimeMillis() - t0 >= 150);
}

BOOST_AUTO_TEST_SUITE_END()
