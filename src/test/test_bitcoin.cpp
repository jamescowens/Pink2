#define BOOST_TEST_MODULE Pinkcoin Test Suite
#include <boost/test/unit_test.hpp>
#include <boost/filesystem.hpp>

#include <cstdio>
#include <cstdlib>

#include "db.h"
#include "main.h"
#include "wallet.h"
#include "checkpoints.h"

CWallet* pwalletMain;
CClientUIInterface uiInterface;

// Globals that init.cpp defines; the test binary links everything except
// init.o. The fixture gives them the values AppInit2 sets by default.
CWallet* pstakeDB;
bool fConfChange;
bool fNTPSuccess;
unsigned int nNodeLifespan;
unsigned int nDerivationMethodIndex;
bool fUseFastIndex;
enum Checkpoints::CPMode CheckpointsMode;

extern bool fPrintToConsole;
extern void noui_connect();

static void FixtureFailure(const char* pszWhat)
{
    fprintf(stderr, "test_pink2 setup failed: %s\n", pszWhat);
    abort();
}

struct TestingSetup {
    boost::filesystem::path pathTemp;

    TestingSetup() {
        // First of all, a fresh data directory, so that nothing can use the
        // default one. GetDataDir() caches its first answer.
        pathTemp = boost::filesystem::temp_directory_path() / boost::filesystem::unique_path("test_pink2_%%%%-%%%%-%%%%");
        boost::filesystem::create_directories(pathTemp);
#ifndef WIN32
        setenv("HOME", pathTemp.string().c_str(), 1);
#endif
        mapArgs["-datadir"] = pathTemp.string();
        if (GetDataDir().string() != pathTemp.string())
            FixtureFailure("the data directory is not the temporary one");

        fPrintToDebugger = true; // don't want to write to debug.log file
        mapArgs["-flushwallet"] = "0"; // no wallet flush thread
        nNodeLifespan = 7;
        fUseFastIndex = true;
        CheckpointsMode = Checkpoints::STRICT;
        nDerivationMethodIndex = 0;

        noui_connect();
        bitdb.MakeMock();
        if (!LoadBlockIndex(true))
            FixtureFailure("LoadBlockIndex");

        // With -flushwallet=0 the flush functions return at once. Running them
        // here first means the flush threads that LoadWallet and LoadStakeDB
        // start return before they read any argument.
        std::string strNoFile;
        ThreadFlushWalletDB(&strNoFile);
        ThreadFlushStakeDB(&strNoFile);
        RenameThread("test_pink2");

        bool fFirstRun;
        pwalletMain = new CWallet("wallet.dat");
        pwalletMain->LoadWallet(fFirstRun);
        bool fFirstStakeOut;
        pstakeDB = new CWallet("stake.dat");
        if (pstakeDB->LoadStakeDB(fFirstStakeOut) != SDB_LOAD_OK)
            FixtureFailure("LoadStakeDB");
        RegisterWallet(pwalletMain);
        RegisterWallet(pstakeDB);
    }
    ~TestingSetup()
    {
        fShutdown = true;
        UnregisterWallet(pstakeDB);
        UnregisterWallet(pwalletMain);
        delete pstakeDB;
        pstakeDB = NULL;
        delete pwalletMain;
        pwalletMain = NULL;
        bitdb.Flush(true);
        boost::system::error_code ec;
        boost::filesystem::remove_all(pathTemp, ec);
    }
};

BOOST_GLOBAL_FIXTURE(TestingSetup);

// No test may reach a shutdown: fail loudly instead of exiting with status 0
void Shutdown(void* parg)
{
    fprintf(stderr, "test_pink2: Shutdown() was called\n");
    abort();
}

void StartShutdown()
{
    fprintf(stderr, "test_pink2: StartShutdown() was called\n");
    abort();
}
