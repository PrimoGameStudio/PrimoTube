#include <QtQuickTest>
#include <QCoreApplication>
#include <QQmlContext>
#include <QQmlEngine>
#include <QSettings>
#include <clocale>

#include "clipboardhelper.h"
#include "historymanager.h"
#include "queuemanager.h"
#include "subscriptionmanager.h"
#include "videomodel.h"

// Supplies the same context properties as main.cpp to each QML test file.
// QuickTest creates a fresh engine per test file, so the models here are
// isolated from each other and from the user's real config.
class Setup : public QObject
{
    Q_OBJECT
public:
    Setup()
    {
        setlocale(LC_NUMERIC, "C");
        QCoreApplication::setOrganizationName(QStringLiteral("PrimoGameStudioTest"));
        QCoreApplication::setApplicationName(QStringLiteral("PrimoTubeQmlTest"));
    }

public slots:
    void applicationAvailable()
    {
        QSettings().clear();
    }

    void qmlEngineAvailable(QQmlEngine *engine)
    {
        QQmlContext *ctx = engine->rootContext();
        ctx->setContextProperty(QStringLiteral("videoModel"), new VideoModel(engine));
        ctx->setContextProperty(QStringLiteral("historyModel"), new HistoryManager(false, engine));
        ctx->setContextProperty(QStringLiteral("favoritesModel"), new HistoryManager(true, engine));
        ctx->setContextProperty(QStringLiteral("subscriptionsModel"), new SubscriptionManager(engine));
        ctx->setContextProperty(QStringLiteral("queueModel"), new QueueManager(engine));
        ctx->setContextProperty(QStringLiteral("clipboardHelper"), new ClipboardHelper(engine));
    }

    void cleanupTestCase()
    {
        QSettings().clear();
    }
};

QUICK_TEST_MAIN_WITH_SETUP(tst_qml, Setup)

#include "tst_qml.moc"
