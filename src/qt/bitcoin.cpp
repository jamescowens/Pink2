/*
 * W.J. van der Laan 2011-2012
 */
#include "bitcoingui.h"
#include "clientmodel.h"
#include "walletmodel.h"
#include "optionsmodel.h"
#include "messagemodel.h"
#include "guiutil.h"
#include "guiconstants.h"

#include "init.h"
#include "util.h"
#include "ui_interface.h"
#include "qtipcserver.h"

#include <QApplication>

#include <algorithm>
#include <QStyleFactory>
#include <QMessageBox>
#include <QTextCodec>
#include <QLocale>
#include <QTranslator>
#include <QSplashScreen>
#include <QTimer>
#include <QLibraryInfo>
#include <QDesktopWidget>
#include <QFontDatabase>
#include <QScreen>
#include <QSemaphore>
#include <QThread>

#include <memory>

#ifdef Q_OS_MAC
#include "macdockiconhandler.h"
#endif

#if defined(BITCOIN_NEED_QT_PLUGINS) && !defined(_BITCOIN_QT_PLUGINS_INCLUDED)
#define _BITCOIN_QT_PLUGINS_INCLUDED
#define __INSURE__
#include <QtPlugin>
Q_IMPORT_PLUGIN(qcncodecs)
Q_IMPORT_PLUGIN(qjpcodecs)
Q_IMPORT_PLUGIN(qtwcodecs)
Q_IMPORT_PLUGIN(qkrcodecs)
Q_IMPORT_PLUGIN(qtaccessiblewidgets)
#endif

// Need a global reference for the notifications to find the GUI
static BitcoinGUI *guiref;
static QSplashScreen *splashref;

// Runs fn on the GUI thread and waits for it to finish. A core thread stops waiting
// once shutdown has started (fShutdown): the shutdown joins it, and the GUI thread
// may never run a queued call again because its event loop has ended. A call that
// has not started by then is dropped. Returns false if the wait ended before fn
// finished.
template <typename Fn>
static bool RunOnGUIThread(BitcoinGUI* gui, Fn fn)
{
    if (QThread::currentThread() == gui->thread())
    {
        fn();
        return true;
    }
    std::shared_ptr<QSemaphore> done = std::make_shared<QSemaphore>(0);
    QMetaObject::invokeMethod(gui, [fn, done]() {
        if (!fShutdown)
            fn();
        done->release();
    }, Qt::QueuedConnection);
    while (!done->tryAcquire(1, 100))
    {
        if (fShutdown)
            return done->tryAcquire();
    }
    return true;
}

static void ThreadSafeMessageBox(const std::string& message, const std::string& caption, int style)
{
    // Message from network thread
    BitcoinGUI* gui = guiref;
    bool fShown = false;
    if (gui)
    {
        bool modal = (style & CClientUIInterface::MODAL);
        QString qCaption = QString::fromStdString(caption);
        QString qMessage = QString::fromStdString(message);
        if (modal)
        {
            // Wait for the user to click OK.
            fShown = RunOnGUIThread(gui, [gui, qCaption, qMessage]() { gui->error(qCaption, qMessage, true); });
        }
        else
        {
            QMetaObject::invokeMethod(gui, "error", Qt::QueuedConnection,
                                      Q_ARG(QString, qCaption), Q_ARG(QString, qMessage), Q_ARG(bool, false));
            fShown = true;
        }
    }
    if (!fShown)
    {
        printf("%s: %s\n", caption.c_str(), message.c_str());
        fprintf(stderr, "%s: %s\n", caption.c_str(), message.c_str());
    }
}

static bool ThreadSafeAskFee(int64_t nFeeRequired, const std::string& strCaption)
{
    BitcoinGUI* gui = guiref;
    if(!gui)
        return false;
    if(nFeeRequired < MIN_TX_FEE || nFeeRequired <= nTransactionFee || fDaemon)
        return true;

    // Shared with the GUI call, which may still finish after a wait that shutdown ended.
    std::shared_ptr<bool> payFee = std::make_shared<bool>(false);
    if (!RunOnGUIThread(gui, [gui, nFeeRequired, payFee]() { gui->askFee(nFeeRequired, payFee.get()); }))
        return false;
    return *payFee;
}

static void ThreadSafeHandleURI(const std::string& strURI)
{
    if(!guiref)
        return;

    // Queued, not blocking: nothing needs the result, and a blocking call into the
    // GUI thread while it joins the core threads at shutdown would deadlock.
    QMetaObject::invokeMethod(guiref, "handleURI", Qt::QueuedConnection,
                               Q_ARG(QString, QString::fromStdString(strURI)));
}

static void InitMessage(const std::string &message)
{
    if(splashref)
    {
        splashref->showMessage(QString::fromStdString(message), Qt::AlignBottom|Qt::AlignHCenter, QColor(149, 131, 216));
        QApplication::instance()->processEvents();
    }
}

static void QueueShutdown()
{
    QMetaObject::invokeMethod(QCoreApplication::instance(), "quit", Qt::QueuedConnection);
}

/*
   Translate string to current locale using Qt.
 */
static std::string Translate(const char* psz)
{
    return QCoreApplication::translate("bitcoin-core", psz).toStdString();
}

/* Handle runaway exceptions. Shows a message box with the problem and quits the program.
 */
static void handleRunawayException(std::exception *e)
{
    PrintExceptionContinue(e, "Runaway exception");
    QMessageBox::critical(0, "Runaway exception", BitcoinGUI::tr("A fatal error occurred. Pinkcoin can no longer continue safely and will quit.") + QString("\n\n") + QString::fromStdString(strMiscWarning));
    exit(1);
}

// Stops the RPC console executor, which runs RPC commands against the wallet, and then
// the core threads; they share the shutdown deadline. Used after init, and when init
// fails or stops: the executor starts with the window. Returns false if anything did
// not stop in time.
static bool StopConsoleAndCoreThreads(BitcoinGUI& window)
{
    // Before anything that can block, the log line below included. The watchdog
    // stays armed until the console executor and the core threads have stopped.
    ArmShutdownWatchdog(SHUTDOWN_JOIN_TIMEOUT_MS + SHUTDOWN_FORCED_FLUSH_TIMEOUT_MS);
    int64_t nStart = GetTimeMillis();
    bool fConsoleStopped = window.shutdownRPCConsole(SHUTDOWN_JOIN_TIMEOUT_MS);
#ifdef PINK2_SHUTDOWN_TEST_HOOKS
    // Test builds only: behave as if a console command had used up the whole
    // deadline, to exercise the GUI's forced shutdown path.
    if (GetBoolArg("-shutdownconsolestucktest"))
    {
        fConsoleStopped = false;
        nStart -= SHUTDOWN_JOIN_TIMEOUT_MS;
    }
#endif
    if (!fConsoleStopped)
        printf("Shutdown: rpcconsole-executor did not stop within %d s\n", (int)(SHUTDOWN_JOIN_TIMEOUT_MS / 1000));
    // Give the core threads at least a few seconds even if the console used
    // up the deadline: most stop within a second, and each one that stops is
    // one fewer running through the forced path.
    static const int64_t SHUTDOWN_MIN_JOIN_MS = 5000;
    int64_t nLeft = std::max<int64_t>(SHUTDOWN_MIN_JOIN_MS, SHUTDOWN_JOIN_TIMEOUT_MS - (GetTimeMillis() - nStart));
    return ShutdownStopThreads(nLeft, fConsoleStopped) && fConsoleStopped;
}

#ifdef PINK2_SHUTDOWN_TEST_HOOKS
// Test builds only (-shutdownguicalltest): a registered thread makes a modal GUI call
// after the event loop has ended and before the GUI stops taking such calls, so the
// call is queued and never run.
static std::atomic<bool> fGuiCallTestGo(false);
static std::atomic<bool> fGuiCallTestCalling(false);

static void ThreadGuiCallTest(void* parg)
{
    while (!fGuiCallTestGo && !fShutdown)
        MilliSleep(10);
    if (fShutdown)
        return;
    fGuiCallTestCalling = true;
    uiInterface.ThreadSafeMessageBox("Shutdown GUI call test", "Pinkcoin", CClientUIInterface::OK | CClientUIInterface::MODAL);
    printf("test-guicall: the GUI call returned\n");
}
#endif

#ifndef BITCOIN_QT_TEST
int main(int argc, char *argv[])
{
    // Do this early as we don't want to bother initializing if we are just calling IPC
    ipcScanRelay(argc, argv);

#if QT_VERSION < 0x050000
    // Internal string conversion is all UTF-8
    QTextCodec::setCodecForTr(QTextCodec::codecForName("UTF-8"));
    QTextCodec::setCodecForCStrings(QTextCodec::codecForTr());
#endif

    Q_INIT_RESOURCE(bitcoin);
    QApplication app(argc, argv);

    // Install global event filter that makes sure that long tooltips can be word-wrapped
    app.installEventFilter(new GUIUtil::ToolTipToRichTextFilter(TOOLTIP_WRAP_THRESHOLD, &app));

    // Command-line options take precedence:
    ParseParameters(argc, argv);

    // ... then bitcoin.conf:
    if (!boost::filesystem::is_directory(GetDataDir(false)))
    {
        // This message can not be translated, as translation is not initialized yet
        // (which not yet possible because lang=XX can be overridden in bitcoin.conf in the data directory)
        QMessageBox::critical(0, "Pinkcoin",
                              QString("Error: Specified data directory \"%1\" does not exist.").arg(QString::fromStdString(mapArgs["-datadir"])));
        return 1;
    }
    ReadConfigFile(mapArgs, mapMultiArgs);

    // Set common app style for all platforms.
    app.setStyle(QStyleFactory::create("Fusion"));

    // Application identification (must be set before OptionsModel is initialized,
    // as it is used to locate QSettings)
    app.setOrganizationName("Pinkcoin");
    //XXX app.setOrganizationDomain("");
    if(GetBoolArg("-testnet")) // Separate UI settings for testnet
        app.setApplicationName("Pinkcoin-Qt-testnet");
    else
        app.setApplicationName("Pinkcoin-Qt");

    // ... then GUI settings:
    OptionsModel optionsModel;

    // Get desired locale (e.g. "de_DE") from command line or use system locale
    QString lang_territory = QString::fromStdString(GetArg("-lang", QLocale::system().name().toStdString()));
    QString lang = lang_territory;
    // Convert to "de" only by truncating "_DE"
    lang.truncate(lang_territory.lastIndexOf('_'));

    QTranslator qtTranslatorBase, qtTranslator, translatorBase, translator;
    // Load language files for configured locale:
    // - First load the translator for the base language, without territory
    // - Then load the more specific locale translator

    // Load e.g. qt_de.qm
    if (qtTranslatorBase.load("qt_" + lang, QLibraryInfo::location(QLibraryInfo::TranslationsPath)))
        app.installTranslator(&qtTranslatorBase);

    // Load e.g. qt_de_DE.qm
    if (qtTranslator.load("qt_" + lang_territory, QLibraryInfo::location(QLibraryInfo::TranslationsPath)))
        app.installTranslator(&qtTranslator);

    // Load e.g. bitcoin_de.qm (shortcut "de" needs to be defined in bitcoin.qrc)
    if (translatorBase.load(lang, ":/translations/"))
        app.installTranslator(&translatorBase);

    // Load e.g. bitcoin_de_DE.qm (shortcut "de_DE" needs to be defined in bitcoin.qrc)
    if (translator.load(lang_territory, ":/translations/"))
        app.installTranslator(&translator);

    // Subscribe to global signals from core
    uiInterface.ThreadSafeMessageBox.connect(ThreadSafeMessageBox);
    uiInterface.ThreadSafeAskFee.connect(ThreadSafeAskFee);
    uiInterface.ThreadSafeHandleURI.connect(ThreadSafeHandleURI);
    uiInterface.InitMessage.connect(InitMessage);
    uiInterface.QueueShutdown.connect(QueueShutdown);
    uiInterface.Translate.connect(Translate);

    // Show help message immediately after parsing command-line options (for "-lang") and setting locale,
    // but before showing splash screen.
    if (mapArgs.count("-?") || mapArgs.count("--help"))
    {
        GUIUtil::HelpMessageBox help;
        help.showOrPrint();
        return 1;
    }

#ifdef Q_OS_MAC
    // on mac, also change the icon now because it would look strange to have a testnet splash (green) and a std app icon (orange)
    if(GetBoolArg("-testnet")) {
        MacDockIconHandler::instance()->setIcon(QIcon(":icons/bitcoin_testnet"));
    }
#endif

    QRect screenGeometry = app.desktop()->screenGeometry();
    int splashWidth = (int)(0.55*screenGeometry.width());
    QPixmap splashPixmap = QPixmap(":/images/splash").scaledToWidth(splashWidth, Qt::SmoothTransformation);

    QSplashScreen splash(splashPixmap, 0);

    if (GetBoolArg("-splash", true) && !GetBoolArg("-min"))
    {
        splash.show();
        splashref = &splash;
    }

    app.processEvents();

    app.setQuitOnLastWindowClosed(false);

    try
    {
        // Regenerate startup link, to fix links to old versions
        if (GUIUtil::GetStartOnSystemStartup())
            GUIUtil::SetStartOnSystemStartup(true);

        boost::thread_group threadGroup;
        BitcoinGUI window;
        guiref = &window;
        if(AppInit2(threadGroup))
        {
            bool fClean = true;
            {
                // The models live in this block. The core threads are stopped and
                // joined (ShutdownStopThreads) before the block ends, so none can call
                // into a model while it is destroyed; the wallets are freed only after
                // (ShutdownFinish), so the models' destructors still find them.

                if (splashref)
                    splash.finish(&window);

                ClientModel clientModel(&optionsModel);
                WalletModel walletModel(pwalletMain, &optionsModel);
                WalletModel stakeModel(pstakeDB, &optionsModel);
                MessageModel messageModel(pwalletMain, &walletModel);

                window.setClientModel(&clientModel);
                window.setWalletModel(&walletModel, &stakeModel);
                window.setMessageModel(&messageModel);

                window.updateMainToolbar();

                // If -min option passed, start window minimized.
                if(GetBoolArg("-min"))
                {
                    window.showMinimized();
                }
                else
                {
                    window.show();
                }

                // Place this here as guiref has to be defined if we don't want to lose URIs
                ipcInit(argc, argv);

                // A shutdown can be requested before the event loop runs (an RPC stop or
                // a signal during startup), and a queued quit() handled outside exec() is
                // lost. So poll the request: skip exec() if it is already set, and quit as
                // soon as it is set while exec() or a nested event loop runs.
                QTimer pollShutdownTimer;
                QObject::connect(&pollShutdownTimer, &QTimer::timeout, [&app]() {
                    if (fRequestShutdown)
                        app.quit();
                });
#ifdef PINK2_SHUTDOWN_TEST_HOOKS
                if (GetBoolArg("-shutdownguicalltest"))
                    StartCoreThread("test-guicall", ThreadGuiCallTest, NULL);
#endif
                pollShutdownTimer.start(200);
                if (!fRequestShutdown)
                    app.exec();
                pollShutdownTimer.stop();
                // Shutdown starts here: bound it before the window is taken down.
                ArmShutdownWatchdog(SHUTDOWN_JOIN_TIMEOUT_MS + SHUTDOWN_FORCED_FLUSH_TIMEOUT_MS);
#ifdef PINK2_SHUTDOWN_TEST_HOOKS
                if (GetBoolArg("-shutdownguicalltest"))
                {
                    fGuiCallTestGo = true;
                    for (int i = 0; i < 200 && !fGuiCallTestCalling; i++)
                        MilliSleep(10);
                    MilliSleep(500); // the call is queued by now
                }
#endif

                // From here core threads' GUI calls (message boxes, fee prompts, URIs)
                // return at once, and a call already waiting for this thread stops
                // waiting when the shutdown below starts.
                guiref = 0;
                window.hide();
                window.setClientModel(0);
                window.setWalletModel(0, 0);
                window.setMessageModel(0);

                fClean = StopConsoleAndCoreThreads(window);

                // Forced path: a thread may still call into the models, so leave from
                // here, before they are destroyed. ShutdownFinish(false) does not return.
                if (!fClean)
                    ShutdownFinish(false);
            }
            // Shutdown the core and free the wallets, but don't exit Bitcoin-Qt here
            ShutdownFinish(fClean);
            threadGroup.interrupt_all();
            threadGroup.join_all();
        }
        else
        {
            // Init may have started the node and RPC threads before it failed, or
            // stopped because a shutdown was requested, which is not a failure.
            bool fShutdownRequested = fRequestShutdown;
            guiref = 0;
            ShutdownFinish(StopConsoleAndCoreThreads(window));
            threadGroup.interrupt_all();
            threadGroup.join_all();
            return fShutdownRequested ? 0 : 1;
        }
    } catch (std::exception& e) {
        handleRunawayException(&e);
    } catch (...) {
        handleRunawayException(NULL);
    }
    return 0;
}
#endif // BITCOIN_QT_TEST
