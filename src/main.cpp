#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QStandardPaths>
#include <QWindow>
#include <QFile>
#include <QFileSystemWatcher>
#include <QTextStream>
#include <QDir>
#include <QColor>
#include <QtLogging>
#include <QtQuickControls2/QQuickStyle>
#include <clocale>

// Reads the Omarchy accent color from colors.toml and watches it for changes,
// so PrimoTube follows the active Omarchy theme dynamically.
class OmarchyTheme : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString accentColor READ accentColor NOTIFY accentColorChanged)
    Q_PROPERTY(bool accentColorValid READ accentColorValid NOTIFY accentColorChanged)

public:
    explicit OmarchyTheme(QObject *parent = nullptr)
        : QObject(parent)
        , m_colorsPath(QDir::homePath() + QLatin1String("/.local/state/omarchy/current/theme/colors.toml"))
    {
        m_watcher.addPath(m_colorsPath);
        connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, &OmarchyTheme::reload);
        reload();
    }

    QString accentColor() const { return m_accentColor; }
    bool accentColorValid() const { return m_accentColorValid; }

public slots:
    void reload() {
        QString color = readAccentColor();
        bool valid = !color.isEmpty();
        if (valid && (color != m_accentColor || m_accentColorValid != valid)) {
            m_accentColor = color;
            m_accentColorValid = true;
            emit accentColorChanged();
        } else if (!valid && m_accentColorValid) {
            m_accentColor = {};
            m_accentColorValid = false;
            emit accentColorChanged();
        }
    }

signals:
    void accentColorChanged();

private:
    QString readAccentColor() {
        QFile file(m_colorsPath);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
            return {};
        QTextStream in(&file);
        while (!in.atEnd()) {
            QString line = in.readLine().trimmed();
            if (line.startsWith(QLatin1String("accent"))) {
                int eq = line.indexOf(QLatin1Char('='));
                if (eq > 0) {
                    QString val = line.mid(eq + 1).trimmed();
                    if (val.startsWith(QLatin1String("\"")) && val.endsWith(QLatin1String("\"")))
                        val = val.mid(1, val.length() - 2);
                    return val;
                }
            }
        }
        return {};
    }

    QFileSystemWatcher m_watcher;
    QString m_accentColor;
    bool m_accentColorValid = false;
    QString m_colorsPath;
};

#include "main.moc"

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

    // Use the Material style so QQuick Controls render consistently across
    // platforms; the per-app Theme.qml palette colors are applied on top.
    QQuickStyle::setStyle(QStringLiteral("Material"));

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
    OmarchyTheme omarchyTheme;

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
    engine.rootContext()->setContextProperty(QStringLiteral("omarchyTheme"), &omarchyTheme);

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
