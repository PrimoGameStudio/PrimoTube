#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QStandardPaths>
#include <QWindow>
#include <QtLogging>
#include <clocale>

#include "clipboardhelper.h"
#include "channelbackend.h"
#include "dependencychecker.h"
#include "downloadbackend.h"
#include "historymanager.h"
#include "homefeedbackend.h"
#include "mpvcontroller.h"
#include "mpvitem.h"
#include "mprisadapter.h"
#include "playbackresolver.h"
#include "queuemanager.h"
#include "searchbackend.h"
#include "settingsmanager.h"
#include "subscriptionmanager.h"
#include "videodetails.h"
#include "videomodel.h"
#include "ytdlpupdater.h"

int main(int argc, char *argv[])
{
    // libmpv requires the C numeric locale (it aborts/warns otherwise).
    // Set before AND after QGuiApplication: Qt resets LC_NUMERIC on init.
    setlocale(LC_NUMERIC, "C");
    QGuiApplication app(argc, argv);
    setlocale(LC_NUMERIC, "C");
    app.setOrganizationName(QStringLiteral("PrimoGameStudio"));
    app.setApplicationName(QStringLiteral("PrimoTube"));

    SettingsManager settingsManager;
    DependencyChecker dependencyChecker;

    if (!dependencyChecker.ytDlpAvailable() || !dependencyChecker.ffmpegAvailable()) {
        qWarning() << dependencyChecker.missingMessage();
    }

    VideoModel videoModel;
    VideoModel channelVideoModel;
    VideoModel homeFeedModel;
    HistoryManager historyModel(false);
    HistoryManager favoritesModel(true);
    SubscriptionManager subscriptionsModel;
    QueueManager queueModel;
    SearchBackend searchBackend(&videoModel);
    ChannelBackend channelBackend(&channelVideoModel);
    HomeFeedBackend homeFeedBackend(&homeFeedModel);
    PlaybackResolver playbackResolver;
    DownloadBackend downloadBackend(&settingsManager);
    ClipboardHelper clipboardHelper;
    VideoDetails videoDetails;
    YtdlpUpdater ytdlpUpdater;
    MpvController mpvController;
    mpvController.setQualityCap(settingsManager.qualityCap());
    MprisAdapter mprisAdapter(&mpvController);

    qmlRegisterType<MpvVideoItem>("PrimoTube", 1, 0, "MpvVideo");

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("videoModel"), &videoModel);
    engine.rootContext()->setContextProperty(QStringLiteral("channelVideoModel"), &channelVideoModel);
    engine.rootContext()->setContextProperty(QStringLiteral("homeFeedModel"), &homeFeedModel);
    engine.rootContext()->setContextProperty(QStringLiteral("historyModel"), &historyModel);
    engine.rootContext()->setContextProperty(QStringLiteral("favoritesModel"), &favoritesModel);
    engine.rootContext()->setContextProperty(QStringLiteral("subscriptionsModel"), &subscriptionsModel);
    engine.rootContext()->setContextProperty(QStringLiteral("queueModel"), &queueModel);
    engine.rootContext()->setContextProperty(QStringLiteral("searchBackend"), &searchBackend);
    engine.rootContext()->setContextProperty(QStringLiteral("channelBackend"), &channelBackend);
    engine.rootContext()->setContextProperty(QStringLiteral("homeFeedBackend"), &homeFeedBackend);
    engine.rootContext()->setContextProperty(QStringLiteral("playbackResolver"), &playbackResolver);
    engine.rootContext()->setContextProperty(QStringLiteral("downloadBackend"), &downloadBackend);
    engine.rootContext()->setContextProperty(QStringLiteral("clipboardHelper"), &clipboardHelper);
    engine.rootContext()->setContextProperty(QStringLiteral("settingsManager"), &settingsManager);
    engine.rootContext()->setContextProperty(QStringLiteral("dependencyChecker"), &dependencyChecker);
    engine.rootContext()->setContextProperty(QStringLiteral("videoDetails"), &videoDetails);
    engine.rootContext()->setContextProperty(QStringLiteral("ytdlpUpdater"), &ytdlpUpdater);
    engine.rootContext()->setContextProperty(QStringLiteral("mpvController"), &mpvController);
    engine.rootContext()->setContextProperty(QStringLiteral("mprisAdapter"), &mprisAdapter);

    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed, &app, []() { QCoreApplication::exit(-1); },
        Qt::QueuedConnection);
    engine.loadFromModule(QStringLiteral("PrimoTube"), QStringLiteral("Main"));

    // MPRIS: expose playback on the session bus (no-op without Qt DBus or
    // without a session bus, e.g. headless CI).
    if (!engine.rootObjects().isEmpty()) {
        if (QWindow *mainWindow = qobject_cast<QWindow *>(engine.rootObjects().constFirst()))
            mprisAdapter.setRaiseWindow(mainWindow);
    }
    mprisAdapter.registerService();

    return app.exec();
}
