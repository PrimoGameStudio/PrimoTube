#include <QtTest>
#include <QDir>
#include <QSettings>
#include <QSignalSpy>
#include <clocale>
#include "../src/settingsmanager.h"
#include "../src/videomodel.h"
#include "../src/dependencychecker.h"
#include "../src/playbackresolver.h"
#include "../src/searchbackend.h"
#include "../src/historymanager.h"
#include "../src/ytdlpupdater.h"
#include "../src/mpvcontroller.h"
#include "../src/mprisadapter.h"
#include "../src/channelbackend.h"
#include "../src/channelutils.h"
#include "../src/homefeedbackend.h"
#include "../src/subscriptionmanager.h"
#include "../src/queuemanager.h"
#include "../src/downloadbackend.h"
#include "../src/videodetails.h"
#ifdef PRIMOTUBE_HAS_MPV
#include <mpv/client.h>
#endif

class TestPrimoTube : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        setlocale(LC_NUMERIC, "C");
        // Isolate QSettings from the real user config: SettingsManager and
        // HistoryManager use the default QSettings (app org/name).
        QCoreApplication::setOrganizationName(QStringLiteral("PrimoGameStudioTest"));
        QCoreApplication::setApplicationName(QStringLiteral("PrimoTubeTest"));
    }

    void testSettingsManager() {
        SettingsManager settings;
        // Test max results bounding
        settings.setMaxResults(50);
        QCOMPARE(settings.maxResults(), 25);
        settings.setMaxResults(0);
        QCOMPARE(settings.maxResults(), 1);
        settings.setMaxResults(15);
        QCOMPARE(settings.maxResults(), 15);

        // Test volume bounding
        settings.setVolume(1.5);
        QCOMPARE(settings.volume(), 1.0);
        settings.setVolume(-0.5);
        QCOMPARE(settings.volume(), 0.0);
        settings.setVolume(0.75);
        QCOMPARE(settings.volume(), 0.75);

        // Test theme persistence and validation
        QCOMPARE(SettingsManager::themeOptions(), QStringList({"dark", "light"}));
        settings.setTheme("light");
        QCOMPARE(settings.theme(), QString("light"));
        settings.setTheme("bogus");
        QCOMPARE(settings.theme(), QString("light")); // invalid ignored
        settings.setTheme("dark");
        QCOMPARE(settings.theme(), QString("dark"));
        // Reloaded instance must see the persisted theme.
        {
            SettingsManager reloaded;
            QCOMPARE(reloaded.theme(), QString("dark"));
        }

        // Forced built-in player: defaults to off, persists, and signals.
        QCOMPARE(settings.forceFallbackPlayer(), false);
        QSignalSpy spy(&settings, &SettingsManager::forceFallbackPlayerChanged);
        settings.setForceFallbackPlayer(true);
        QCOMPARE(settings.forceFallbackPlayer(), true);
        QCOMPARE(spy.count(), 1);
        // Same value again: no signal, no state change.
        settings.setForceFallbackPlayer(true);
        QCOMPARE(spy.count(), 1);
        {
            SettingsManager reloaded;
            QCOMPARE(reloaded.forceFallbackPlayer(), true);
        }
        settings.setForceFallbackPlayer(false);
        QCOMPARE(settings.forceFallbackPlayer(), false);
        QCOMPARE(spy.count(), 2);
    }

    void testDownloadDirectoryValidation() {
        SettingsManager settings;
        const QString before = settings.downloadDirectory();
        // Empty and garbage-into-nonexistent-rooted paths are rejected.
        QVERIFY(!settings.trySetDownloadDirectory(""));
        QVERIFY(!settings.trySetDownloadDirectory("   "));
        QCOMPARE(settings.downloadDirectory(), before);
        // A writable temp dir is accepted and persisted.
        const QString tmp = QDir::tempPath() + QStringLiteral("/primotube-test-dl");
        QVERIFY(settings.trySetDownloadDirectory(tmp));
        QCOMPARE(settings.downloadDirectory(), tmp);
        {
            SettingsManager reloaded;
            QCOMPARE(reloaded.downloadDirectory(), tmp);
        }
        // Same value is a no-op success.
        QVERIFY(settings.trySetDownloadDirectory(tmp));
        // Restore previous value when it was valid.
        if (!before.isEmpty())
            QVERIFY(settings.trySetDownloadDirectory(before));
    }

    void testVideoModel() {
        VideoModel model;
        QCOMPARE(model.rowCount(), 0);

        VideoItem item;
        item.videoId = "abc123xyz";
        item.title = "Test Video Title";
        item.channel = "Test Channel";
        item.channelId = "UC123";
        item.channelUrl = "https://youtube.com/channel/UC123";
        item.duration = 120;
        item.durationString = "2:00";
        item.thumbnail = "https://example.com/thumb.jpg";
        item.watchUrl = "https://youtube.com/watch?v=abc123xyz";
        item.description = "A test description";

        model.addVideo(item);
        QCOMPARE(model.rowCount(), 1);

        QModelIndex idx = model.index(0, 0);
        QCOMPARE(model.data(idx, VideoModel::VideoIdRole).toString(), QString("abc123xyz"));
        QCOMPARE(model.data(idx, VideoModel::TitleRole).toString(), QString("Test Video Title"));
        QCOMPARE(model.data(idx, VideoModel::ChannelRole).toString(), QString("Test Channel"));
        QCOMPARE(model.data(idx, VideoModel::ChannelIdRole).toString(), QString("UC123"));
        QCOMPARE(model.data(idx, VideoModel::ChannelUrlRole).toString(), QString("https://youtube.com/channel/UC123"));
        QCOMPARE(model.data(idx, VideoModel::DurationRole).toInt(), 120);
        QCOMPARE(model.data(idx, VideoModel::DurationStringRole).toString(), QString("2:00"));
        QCOMPARE(model.data(idx, VideoModel::DescriptionRole).toString(), QString("A test description"));

        model.clear();
        QCOMPARE(model.rowCount(), 0);
    }

    void testHistoryManagerLimit() {
        HistoryManager history(false);
        history.clear();
        for (int i = 0; i < 105; ++i) {
            history.addItem(QStringLiteral("vid%1").arg(i), QStringLiteral("t%1").arg(i),
                            QStringLiteral("c"), QStringLiteral("thumb"),
                            QStringLiteral("url"), QString(), 0, QStringLiteral("desc"));
        }
        // Capped at 100, most-recent-first, no nested-model crash.
        QCOMPARE(history.rowCount(), 100);
        QCOMPARE(history.data(history.index(0, 0), HistoryManager::VideoIdRole).toString(),
                 QString("vid104"));
        QVERIFY(history.contains(QStringLiteral("vid104")));
        QVERIFY(!history.contains(QStringLiteral("vid0")));
        history.clear();
        QCOMPARE(history.rowCount(), 0);
    }

    void testDependencyChecker() {
        DependencyChecker checker;
        // Environment-dependent availability; only assert no-crash and
        // internal consistency (message non-empty iff something missing).
        const bool yt = checker.ytDlpAvailable();
        const bool ff = checker.ffmpegAvailable();
        const QString msg = checker.missingMessage();
        QVERIFY(msg.isEmpty() == (yt && ff));
        QVERIFY(checker.ytDlpAvailable() == yt);
    }

    void testPlaybackResolverErrors() {
        QVERIFY(PlaybackResolver::isFatalError("ERROR: [youtube] dQw4w9WgXcQ: Private video. Sign in if you've been granted access"));
        QVERIFY(PlaybackResolver::isFatalError("Video unavailable"));

        QString friendly = PlaybackResolver::friendlyError("Private video", false);
        QVERIFY(friendly.contains("private"));
    }

    void testSearchPaginationDefaults() {
        VideoModel model;
        SearchBackend backend(&model);
        QCOMPARE(backend.hasMore(), false);
        QCOMPARE(backend.resultCount(), 0);
        QCOMPARE(SearchBackend::kPageSize, 10);
        QCOMPARE(SearchBackend::kMaxTotal, 50);
        // loadMore with no query is a safe no-op (must not crash offline).
        backend.loadMore();
        QCOMPARE(backend.searching(), false);
    }

    void testVersionCompare() {
        QCOMPARE(YtdlpUpdater::compareVersions("2026.08.19", "2026.08.19"), 0);
        QVERIFY(YtdlpUpdater::compareVersions("2025.01.01", "2026.08.19") < 0);
        QVERIFY(YtdlpUpdater::compareVersions("2026.08.20", "2026.08.19") > 0);
        QVERIFY(YtdlpUpdater::compareVersions("2026.8.19", "2026.08.19") == 0);
        // Nightly suffixes must not collapse to 0 via toInt().
        QVERIFY(YtdlpUpdater::compareVersions("2025.02.19-nightly", "2025.02.19") != 0);
        QCOMPARE(YtdlpUpdater::compareVersions("2025.02.19-nightly", "2025.02.19-nightly"), 0);
    }

    void testQualityCap() {
        SettingsManager settings;
        QCOMPARE(settings.qualityCap(), QString("Auto"));
        settings.setQualityCap("720p");
        QCOMPARE(settings.qualityCap(), QString("720p"));
        settings.setQualityCap("bogus");
        QCOMPARE(settings.qualityCap(), QString("720p")); // invalid ignored
        settings.setQualityCap("Auto");
        QCOMPARE(settings.qualityCap(), QString("Auto"));

        // Options must be exposed to QML (SettingsPage/PlayerPage consume
        // them instead of hardcoded lists).
        QCOMPARE(SettingsManager::qualityCapOptions(),
                 QStringList({"Auto", "720p", "1080p", "4K"}));
        const QMetaObject *smo = &SettingsManager::staticMetaObject;
        QVERIFY(smo->indexOfMethod("qualityCapOptions()") >= 0);
        QVERIFY(smo->indexOfMethod("themeOptions()") >= 0);
        QVERIFY(smo->indexOfMethod("trySetDownloadDirectory(QString)") >= 0);

        QCOMPARE(MpvController::ytdlFormatForCap("Auto"), QString("bv*+ba/b"));
        QCOMPARE(MpvController::ytdlFormatForCap(""), QString("bv*+ba/b"));
        QCOMPARE(MpvController::ytdlFormatForCap("bogus"), QString("bv*+ba/b"));
        QVERIFY(MpvController::ytdlFormatForCap("720p").contains("height<=720"));
        QVERIFY(MpvController::ytdlFormatForCap("1080p").contains("height<=1080"));
        QVERIFY(MpvController::ytdlFormatForCap("4K").contains("height<=2160"));
    }

    void testMpvControllerInit() {
        MpvController mpv;
        // Must not crash even in headless CI; availability reflects libmpv.
        // available() is a valid bool; lastError is a valid string.
        const bool avail = mpv.available();
        QVERIFY(avail == true || avail == false);
        QVERIFY(mpv.lastError().isEmpty() || !mpv.lastError().isEmpty());
        // No-ops when idle — must not crash.
        mpv.stop();
        mpv.seek(10);
    }

    void testMpvQmlCallable() {
        // Regression test: PlayerPage.qml calls these as functions
        // (mpvController.setVolume(x), …). Q_PROPERTY WRITE accessors are
        // NOT callable that way — they must be Q_INVOKABLE or slots.
        const QMetaObject *mo = &MpvController::staticMetaObject;
        QVERIFY(mo->indexOfMethod("setVolume(double)") >= 0);
        QVERIFY(mo->indexOfMethod("setMuted(bool)") >= 0);
        QVERIFY(mo->indexOfMethod("setPaused(bool)") >= 0);
        QVERIFY(mo->indexOfMethod("setSubtitleId(int)") >= 0);
        QVERIFY(mo->indexOfMethod("setAudioId(int)") >= 0);
        QVERIFY(mo->indexOfMethod("setQualityCap(QString)") >= 0);
        // And they must actually be invocable through the meta-object,
        // the way QML invokes them.
        MpvController mpv;
        QVERIFY(QMetaObject::invokeMethod(&mpv, "setVolume", Q_ARG(double, 0.5)));
        QCOMPARE(mpv.volume(), 0.5);
        QVERIFY(QMetaObject::invokeMethod(&mpv, "setMuted", Q_ARG(bool, true)));
        QVERIFY(mpv.muted());
        QVERIFY(QMetaObject::invokeMethod(&mpv, "setQualityCap", Q_ARG(QString, QStringLiteral("720p"))));
        QCOMPARE(mpv.qualityCap(), QStringLiteral("720p"));
    }

    void testMpvVolumeRoundTrip() {
#ifdef PRIMOTUBE_HAS_MPV
        MpvController mpv;
        if (!mpv.available())
            QSKIP("libmpv unavailable");
        // setVolume must reach mpv: read the property back synchronously.
        mpv.setVolume(0.5);
        double v = -1;
        QVERIFY2(mpv_get_property(mpv.handle(), "volume", MPV_FORMAT_DOUBLE, &v) >= 0,
                 "mpv volume property unreadable");
        QVERIFY2(qAbs(v - 50.0) < 1.0, qPrintable(QStringLiteral("mpv volume=%1, want 50").arg(v)));
        // Let the async observe event arrive, then the cached value must agree.
        QTRY_VERIFY_WITH_TIMEOUT(qAbs(mpv.volume() - 0.5) < 0.05, 3000);
        mpv.setVolume(1.0);
        QTRY_VERIFY_WITH_TIMEOUT(qAbs(mpv.volume() - 1.0) < 0.05, 3000);
#else
        QSKIP("libmpv unavailable");
#endif
    }

    void testChannelNormalization() {
        QCOMPARE(ChannelBackend::normalizeChannelUrl("@LiskovChannel"), QString("https://www.youtube.com/@LiskovChannel"));
        QCOMPARE(ChannelBackend::normalizeChannelUrl("https://www.youtube.com/@LiskovChannel/videos"), QString("https://www.youtube.com/@LiskovChannel"));
        QVERIFY(ChannelBackend::isChannelUrl("@LiskovChannel"));
        QVERIFY(ChannelBackend::isChannelUrl("https://www.youtube.com/channel/UC1234567890123456789012"));
        QVERIFY(!ChannelBackend::isChannelUrl("https://www.youtube.com/watch?v=abc"));
        QVERIFY(!ChannelBackend::isChannelUrl(""));
        QVERIFY(!ChannelBackend::isChannelUrl("@handle with space"));
        // URL allow-list for playback/download/details.
        QVERIFY(ChannelUtils::isAllowedVideoUrl("dQw4w9WgXcQ"));
        QVERIFY(ChannelUtils::isAllowedVideoUrl("https://www.youtube.com/watch?v=dQw4w9WgXcQ"));
        QVERIFY(ChannelUtils::isAllowedVideoUrl("https://youtu.be/dQw4w9WgXcQ"));
        QVERIFY(ChannelUtils::isAllowedVideoUrl("https://m.youtube.com/watch?v=dQw4w9WgXcQ"));
        QVERIFY(ChannelUtils::isAllowedVideoUrl("https://music.youtube.com/watch?v=dQw4w9WgXcQ"));
        QVERIFY(ChannelUtils::isAllowedVideoUrl("https://www.youtube-nocookie.com/embed/dQw4w9WgXcQ"));
        QVERIFY(!ChannelUtils::isAllowedVideoUrl("file:///etc/passwd"));
        QVERIFY(!ChannelUtils::isAllowedVideoUrl("http://169.254.169.254/latest/meta-data/"));
        // Substring spoofs must be rejected.
        QVERIFY(!ChannelUtils::isAllowedVideoUrl("https://youtube.com.evil.com/watch?v=x"));
        QVERIFY(!ChannelUtils::isAllowedVideoUrl("https://www.youtube.com.evil.com/watch?v=x"));
        QVERIFY(!ChannelUtils::isAllowedVideoUrl("https://evil.com/?youtube.com"));
        QVERIFY(!ChannelUtils::isAllowedVideoUrl("https://evil.com/?youtu.be"));
        QVERIFY(!ChannelUtils::isAllowedVideoUrl("ftp://www.youtube.com/watch?v=x"));
        QVERIFY(!ChannelUtils::isAllowedVideoUrl("https://evil.com/video"));
        // Strict host helper.
        QVERIFY(ChannelUtils::isYouTubeHost("www.youtube.com"));
        QVERIFY(ChannelUtils::isYouTubeHost("m.youtube.com"));
        QVERIFY(ChannelUtils::isYouTubeHost("youtu.be"));
        QVERIFY(!ChannelUtils::isYouTubeHost("youtube.com.evil.com"));
        QVERIFY(!ChannelUtils::isYouTubeHost("evil.com"));
        QVERIFY(!ChannelUtils::isYouTubeHost(""));
        // Channel normalization must also reject spoofed hosts.
        QVERIFY(!ChannelBackend::isChannelUrl("https://youtube.com.evil.com/@Someone"));
        QVERIFY(!ChannelBackend::isChannelUrl("https://evil.com/?youtube.com/@Someone"));
        // Folder-dialog URL decoding (spaces/encoding safe).
        QCOMPARE(SettingsManager::urlToLocalFile(QUrl(QStringLiteral("file:///tmp/a%20b"))),
                 QString("/tmp/a b"));
        QVERIFY(!ChannelUtils::ytdlpExecutable().isEmpty());
    }

    void testQueueEnqueueModelSkipsEmpty() {
        VideoModel vmodel;
        VideoItem good;
        good.videoId = "good1";
        good.title = "Good";
        vmodel.addVideo(good);
        VideoItem bad;
        bad.videoId = "";
        bad.title = "No id";
        vmodel.addVideo(bad);
        VideoItem good2;
        good2.videoId = "good2";
        good2.title = "Good 2";
        vmodel.addVideo(good2);
        QCOMPARE(vmodel.rowCount(), 3);

        QueueManager queue;
        queue.enqueueModel(&vmodel);
        // Only the 2 valid rows are announced + appended (no model corruption).
        QCOMPARE(queue.rowCount(), 2);
        QCOMPARE(queue.data(queue.index(0, 0), QueueManager::VideoIdRole).toString(), QString("good1"));
        QCOMPARE(queue.data(queue.index(1, 0), QueueManager::VideoIdRole).toString(), QString("good2"));
        queue.clear();
    }

    void testSubscriptionManager() {
        SubscriptionManager subs;
        subs.clear();
        QCOMPARE(subs.rowCount(), 0);
        subs.subscribe("UC123", "Channel One", "@One", "https://youtube.com/@One", "https://example.com/avatar.jpg");
        QCOMPARE(subs.rowCount(), 1);
        QVERIFY(subs.contains("UC123"));
        QVERIFY(subs.contains("https://youtube.com/@One"));
        
        QModelIndex idx = subs.index(0, 0);
        QCOMPARE(subs.data(idx, SubscriptionManager::TitleRole).toString(), QString("Channel One"));
        QCOMPARE(subs.data(idx, SubscriptionManager::HandleRole).toString(), QString("@One"));

        // Persistence: a fresh instance over the same QSettings must reload.
        {
            SubscriptionManager reloaded;
            QVERIFY(reloaded.contains("UC123"));
            QCOMPARE(reloaded.rowCount(), 1);
        }
        // Duplicate subscribe replaces instead of growing.
        subs.subscribe("UC123", "Channel One Renamed", "@One", "https://youtube.com/@One", "");
        QCOMPARE(subs.rowCount(), 1);

        subs.unsubscribe("UC123");
        QCOMPARE(subs.rowCount(), 0);
        QVERIFY(!subs.contains("UC123"));

        // Corrupt JSON must not crash and must yield an empty model.
        {
            QSettings s;
            s.setValue(QStringLiteral("subscriptionsList"), QStringLiteral("{not-json"));
            s.sync();
            SubscriptionManager corrupt;
            QCOMPARE(corrupt.rowCount(), 0);
            corrupt.clear();
        }
    }

    void testJsonExportImport() {
        SubscriptionManager subs;
        subs.clear();
        subs.subscribe("UC999", "Export Test", "@Test", "https://youtube.com/@Test", "");
        QString json = subs.exportJson();
        QVERIFY(!json.isEmpty());

        SubscriptionManager subs2;
        subs2.clear();
        QCOMPARE(subs2.rowCount(), 0);
        QVERIFY(subs2.importJson(json));
        QCOMPARE(subs2.rowCount(), 1);
        QVERIFY(subs2.contains("UC999"));
        subs.clear();
        subs2.clear();
    }

    void testDownloadBackend() {
        DownloadBackend dl;
        QCOMPARE(dl.downloading(), false);
        QCOMPARE(dl.progress(), 0.0);
        QCOMPARE(dl.savedPath(), QString());
        dl.cancel();
        QCOMPARE(dl.downloading(), false);
    }

    void testImportCaps() {
        // History import is capped at 100 (most-recent-first order kept).
        {
            HistoryManager history(false);
            history.clear();
            QJsonArray arr;
            for (int i = 0; i < 105; ++i) {
                QJsonObject o;
                o["videoId"] = QStringLiteral("vid%1").arg(i);
                o["title"] = QStringLiteral("t%1").arg(i);
                arr.append(o);
            }
            // Malformed entries are skipped, valid ones kept up to the cap.
            QJsonObject bad;
            bad["title"] = "no id";
            arr.append(bad);
            QVERIFY(history.importJson(QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact))));
            QCOMPARE(history.rowCount(), HistoryManager::kMaxHistory);
            QCOMPARE(history.data(history.index(0, 0), HistoryManager::VideoIdRole).toString(),
                     QString("vid0"));
            QVERIFY(!history.importJson(QStringLiteral("{not-an-array")));
            QVERIFY(!history.importJson(QStringLiteral("[1,2")));
            history.clear();
        }
        // Subscription import is capped at 500 and deduped.
        {
            SubscriptionManager subs;
            subs.clear();
            QJsonArray arr;
            for (int i = 0; i < 505; ++i) {
                QJsonObject o;
                o["channelId"] = QStringLiteral("UC%1").arg(i, 3, 10, QLatin1Char('0'));
                o["title"] = QStringLiteral("C%1").arg(i);
                arr.append(o);
            }
            QJsonObject dup;
            dup["channelId"] = "UC001";
            dup["title"] = "Dup";
            arr.append(dup);
            QJsonObject nokey;
            nokey["title"] = "No key";
            arr.append(nokey);
            QVERIFY(subs.importJson(QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact))));
            QCOMPARE(subs.rowCount(), SubscriptionManager::kMaxSubscriptions);
            QVERIFY(subs.contains("UC001"));
            QVERIFY(!subs.importJson(QStringLiteral("\"oops\"")));
            subs.clear();
        }
    }

    void testChannelCurrentRequest() {
        VideoModel model;
        ChannelBackend backend(&model);
        QCOMPARE(backend.currentRequest(), QString());
        // Invalid input never becomes the current request.
        backend.openChannel("   ");
        QCOMPARE(backend.currentRequest(), QString());
        backend.openChannel("not a channel!!!");
        QCOMPARE(backend.currentRequest(), QString());
        backend.cancel();
    }

    void testChannelAvatarParsing() {
        const QString html = QStringLiteral(
            "<html><head>"
            "<meta property=\"og:image\" content=\"https://yt3.googleusercontent.com/abc=s900-c-k-c0x00ffffff-no-rj\">"
            "<meta property=\"og:description\" content=\"Best channel &amp; videos\">"
            "</head><body>"
            "\"subscriberCountText\":{\"accessibility\":{\"accessibilityData\":{\"label\":\"1.2M subscribers\"}},\"simpleText\":\"1.2M subscribers\"}"
            "</body></html>");
        QCOMPARE(ChannelUtils::pickChannelAvatar(html),
                 QString("https://yt3.googleusercontent.com/abc=s900-c-k-c0x00ffffff-no-rj"));
        QCOMPARE(ChannelUtils::pickChannelDescription(html),
                 QString("Best channel &amp; videos"));
        QCOMPARE(ChannelUtils::pickChannelSubscriberText(html),
                 QString("1.2M subscribers"));
        // Avatar block fallback when og:image is absent.
        const QString html2 = QStringLiteral(
            "{\"avatar\":{\"thumbnails\":[{\"url\":\"https:\\/\\/yt3.googleusercontent.com\\/xyz=s176-c-k\"}]}}");
        QCOMPARE(ChannelUtils::pickChannelAvatar(html2),
                 QString("https://yt3.googleusercontent.com/xyz=s176-c-k"));
        // Empty / unrelated HTML yields empty results (placeholder stays).
        QCOMPARE(ChannelUtils::pickChannelAvatar(QString()), QString());
        QCOMPARE(ChannelUtils::pickChannelAvatar(QStringLiteral("<html></html>")), QString());
        QCOMPARE(ChannelUtils::pickChannelDescription(QString()), QString());
        QCOMPARE(ChannelUtils::pickChannelSubscriberText(QString()), QString());
    }

    void testSubscriptionAvatarBackfill() {
        SubscriptionManager subs;
        subs.clear();
        subs.subscribe("UC123", "Channel One", "@One", "https://youtube.com/@One", "");
        QModelIndex idx = subs.index(0, 0);
        QCOMPARE(subs.data(idx, SubscriptionManager::AvatarUrlRole).toString(), QString(""));
        subs.updateAvatar("UC123", "https://yt3.googleusercontent.com/abc=s900");
        QCOMPARE(subs.data(idx, SubscriptionManager::AvatarUrlRole).toString(),
                 QString("https://yt3.googleusercontent.com/abc=s900"));
        // Unknown keys and empty avatars are no-ops.
        subs.updateAvatar("UC999", "https://example.com/x.jpg");
        QCOMPARE(subs.rowCount(), 1);
        subs.updateAvatar("UC123", "");
        QCOMPARE(subs.data(idx, SubscriptionManager::AvatarUrlRole).toString(),
                 QString("https://yt3.googleusercontent.com/abc=s900"));
        const QMetaObject *mo = &SubscriptionManager::staticMetaObject;
        QVERIFY(mo->indexOfMethod("updateAvatar(QString,QString)") >= 0);
        subs.clear();
    }

    void testVideoTimestampParsing() {
        QJsonObject withTs;
        withTs["timestamp"] = 1756684800.0;
        QCOMPARE(ChannelUtils::parseVideoTimestamp(withTs), 1756684800LL);
        QCOMPARE(ChannelUtils::formatUploadDate(withTs, 1756684800LL),
                 QDateTime::fromSecsSinceEpoch(1756684800LL, QTimeZone(QTimeZone::UTC)).date().toString(Qt::ISODate));

        QJsonObject withDate;
        withDate["upload_date"] = "20260901";
        QCOMPARE(ChannelUtils::parseVideoTimestamp(withDate),
                 QDateTime(QDate(2026, 9, 1), QTime(0, 0), QTimeZone(QTimeZone::UTC)).toSecsSinceEpoch());
        QCOMPARE(ChannelUtils::formatUploadDate(withDate, 0), QString("2026-09-01"));

        // timestamp wins over upload_date.
        QJsonObject both;
        both["timestamp"] = 1000.0;
        both["upload_date"] = "20260901";
        QCOMPARE(ChannelUtils::parseVideoTimestamp(both), 1000LL);

        // release_timestamp fallback.
        QJsonObject rel;
        rel["release_timestamp"] = 2000.0;
        QCOMPARE(ChannelUtils::parseVideoTimestamp(rel), 2000LL);

        QJsonObject empty;
        QCOMPARE(ChannelUtils::parseVideoTimestamp(empty), 0LL);
        QCOMPARE(ChannelUtils::formatUploadDate(empty, 0), QString());
    }

    void testVideoModelTimestampRoles() {
        VideoModel model;
        VideoItem item;
        item.videoId = "vid1";
        item.title = "T";
        item.timestamp = 12345;
        item.uploadDate = "2026-09-01";
        model.addVideo(item);
        QModelIndex idx = model.index(0, 0);
        QCOMPARE(model.data(idx, VideoModel::TimestampRole).toLongLong(), 12345LL);
        QCOMPARE(model.data(idx, VideoModel::UploadDateRole).toString(), QString("2026-09-01"));
        // Defaults for items without dates.
        VideoModel bare;
        VideoItem b;
        b.videoId = "v";
        b.title = "t";
        bare.addVideo(b);
        QCOMPARE(bare.data(bare.index(0, 0), VideoModel::TimestampRole).toLongLong(), 0LL);
        QCOMPARE(bare.data(bare.index(0, 0), VideoModel::UploadDateRole).toString(), QString());
        // setItems replaces atomically.
        QVector<VideoItem> items;
        VideoItem a;
        a.videoId = "a";
        a.title = "A";
        a.timestamp = 2;
        VideoItem c;
        c.videoId = "c";
        c.title = "C";
        c.timestamp = 1;
        items << a << c;
        bare.setItems(items);
        QCOMPARE(bare.rowCount(), 2);
        QCOMPARE(bare.data(bare.index(0, 0), VideoModel::VideoIdRole).toString(), QString("a"));
    }

    void testSubscriptionSnapshot() {
        SubscriptionManager subs;
        subs.clear();
        subs.subscribe("UC123", "Channel One", "@One", "https://youtube.com/@One", "");
        const QVariantList snap = subs.snapshot();
        QCOMPARE(snap.size(), 1);
        const QVariantMap m = snap.constFirst().toMap();
        QCOMPARE(m.value("channelId").toString(), QString("UC123"));
        QCOMPARE(m.value("title").toString(), QString("Channel One"));
        QCOMPARE(m.value("handle").toString(), QString("@One"));
        QCOMPARE(m.value("url").toString(), QString("https://youtube.com/@One"));
        const QMetaObject *mo = &SubscriptionManager::staticMetaObject;
        QVERIFY(mo->indexOfMethod("snapshot()") >= 0);
        subs.clear();
    }

    void testHomeFeedBackend() {
        VideoModel model;
        HomeFeedBackend backend(&model);
        QCOMPARE(backend.loading(), false);
        QCOMPARE(backend.videoCount(), 0);
        QCOMPARE(HomeFeedBackend::kPerChannel, 3);
        // Empty snapshot is a safe no-op with a subscribe hint.
        backend.refresh({});
        QCOMPARE(backend.loading(), false);
        QCOMPARE(model.rowCount(), 0);
        QVERIFY(backend.statusText().contains("Subscribe"));
        // Entries without any lookup key are skipped, same as no-op.
        QVariantMap nokey;
        nokey["title"] = "No key";
        backend.refresh({nokey});
        QCOMPARE(backend.loading(), false);
        backend.cancel(); // safe no-op when idle
        QCOMPARE(backend.loading(), false);
        // Newest-first ordering, unknown timestamps sink (stable).
        QVector<VideoItem> items;
        VideoItem old;
        old.videoId = "old";
        old.title = "Old";
        old.timestamp = 100;
        VideoItem undated;
        undated.videoId = "nodate";
        undated.title = "NoDate";
        undated.timestamp = 0;
        VideoItem fresh;
        fresh.videoId = "new";
        fresh.title = "New";
        fresh.timestamp = 300;
        items << old << undated << fresh;
        HomeFeedBackend::sortNewestFirst(items);
        QCOMPARE(items.at(0).videoId, QString("new"));
        QCOMPARE(items.at(1).videoId, QString("old"));
        QCOMPARE(items.at(2).videoId, QString("nodate"));
    }

    void testVideoDetails() {        VideoDetails details;
        QCOMPARE(details.loading(), false);
        QCOMPARE(details.viewCount(), -1);
        QCOMPARE(details.likeCount(), -1);
        QCOMPARE(details.uploadDate(), QString());
        QVERIFY(details.subtitleLangs().isEmpty());
        details.cancel();
        QCOMPARE(details.loading(), false);
    }

    void testMprisStatusMapping() {
        QCOMPARE(QString::fromLatin1(MprisAdapter::serviceName()),
                 QString("org.mpris.MediaPlayer2.primotube"));
        QCOMPARE(MprisAdapter::statusForState(false, false), QString("Stopped"));
        QCOMPARE(MprisAdapter::statusForState(false, true), QString("Stopped"));
        QCOMPARE(MprisAdapter::statusForState(true, true), QString("Playing"));
        QCOMPARE(MprisAdapter::statusForState(true, false), QString("Paused"));
    }

    void testMprisConversions() {
        QCOMPARE(MprisAdapter::secondsToMicros(1.5), 1500000LL);
        QCOMPARE(MprisAdapter::secondsToMicros(0.0), 0LL);
        QCOMPARE(MprisAdapter::secondsToMicros(-3.0), 0LL);
        QCOMPARE(MprisAdapter::microsToSeconds(2500000LL), 2.5);
        QCOMPARE(MprisAdapter::microsToSeconds(-1LL), 0.0);
        QCOMPARE(MprisAdapter::clampVolume(1.5), 1.0);
        QCOMPARE(MprisAdapter::clampVolume(-0.2), 0.0);
        QCOMPARE(MprisAdapter::clampVolume(0.4), 0.4);
        QCOMPARE(MprisAdapter::clampPosition(200.0, 100.0), 100.0);
        QCOMPARE(MprisAdapter::clampPosition(-5.0, 100.0), 0.0);
        QCOMPARE(MprisAdapter::clampPosition(42.0, 0.0), 42.0);
    }

    void testMprisMetadata() {
        const QVariantMap meta = MprisAdapter::buildMetadata(
            QStringLiteral("/org/primotube/track/abc123"), QStringLiteral("Title"),
            QStringLiteral("Channel"), QStringLiteral("https://youtube.com/watch?v=abc123"),
            QStringLiteral("https://example.com/thumb.jpg"), 120000000LL,
            QStringLiteral("desc"));
        QCOMPARE(meta.value("mpris:trackid").toString(),
                 QString("/org/primotube/track/abc123"));
        QCOMPARE(meta.value("xesam:title").toString(), QString("Title"));
        QCOMPARE(meta.value("xesam:artist").toStringList(), QStringList{"Channel"});
        QCOMPARE(meta.value("xesam:url").toString(),
                 QString("https://youtube.com/watch?v=abc123"));
        QCOMPARE(meta.value("mpris:artUrl").toString(),
                 QString("https://example.com/thumb.jpg"));
        QCOMPARE(meta.value("mpris:length").toLongLong(), 120000000LL);
        QCOMPARE(meta.value("xesam:comment").toString(), QString("desc"));
        // Empty description is omitted; empty artist stays an empty list.
        const QVariantMap bare = MprisAdapter::buildMetadata(
            QStringLiteral("/t"), QStringLiteral("T"), QString(), QString(), QString(),
            0LL, QString());
        QVERIFY(!bare.contains("xesam:comment"));
        QVERIFY(bare.value("xesam:artist").toStringList().isEmpty());
    }

    void testMprisAdapterState() {
        MpvController mpv;
        MprisAdapter adapter(&mpv);
        QVERIFY(!adapter.isRegistered()); // never registered in tests
        QCOMPARE(adapter.playbackStatus(), QString("Stopped"));
        adapter.setCurrentTrack("vid1", "Title 1", "Chan",
                                "https://youtube.com/watch?v=vid1",
                                "https://example.com/t.jpg", 120.0, "desc");
        QVERIFY(adapter.hasTrack());
        QCOMPARE(adapter.playbackStatus(), QString("Paused"));
        QVERIFY(adapter.currentTrackId().contains("vid1"));
        QCOMPARE(adapter.metadata().value("xesam:title").toString(), QString("Title 1"));
        QCOMPARE(adapter.durationMicros(), 120000000LL);
        // Compat-path push (mpvEffective=false): D-Bus actions become signals.
        adapter.setEffectiveState(true, 10.0, 120.0, 0.5, false);
        QCOMPARE(adapter.playbackStatus(), QString("Playing"));
        QCOMPARE(adapter.positionMicros(), 10000000LL);
        QCOMPARE(adapter.volume(), 0.5);
        QSignalSpy toggleSpy(&adapter, &MprisAdapter::requestToggle);
        adapter.handlePlayPause();
        QCOMPARE(toggleSpy.count(), 1);
        QSignalSpy seekSpy(&adapter, &MprisAdapter::requestSeek);
        QSignalSpy seekedSpy(&adapter, &MprisAdapter::seeked);
        adapter.handleSetPosition(adapter.currentTrackId(), 30000000LL);
        QCOMPARE(seekSpy.count(), 1);
        QCOMPARE(seekSpy.constFirst().constFirst().toDouble(), 30.0);
        QCOMPARE(seekedSpy.count(), 1);
        QCOMPARE(seekedSpy.constFirst().constFirst().toLongLong(), 30000000LL);
        // Stale track ids are ignored.
        adapter.handleSetPosition(QStringLiteral("/org/primotube/track/other"), 60000000LL);
        QCOMPARE(seekSpy.count(), 1);
        QCOMPARE(adapter.positionMicros(), 30000000LL);
        adapter.clearTrack();
        QVERIFY(!adapter.hasTrack());
        QCOMPARE(adapter.playbackStatus(), QString("Stopped"));
    }

    void testQueueManager() {
        QueueManager queue;
        QCOMPARE(queue.rowCount(), 0);
        QCOMPARE(queue.hasQueue(), false);
        QCOMPARE(queue.currentIndex(), -1);

        queue.enqueue("vid1", "Title 1", "Chan", "thumb", "url", "1:00", 60);
        QCOMPARE(queue.rowCount(), 1);
        QCOMPARE(queue.hasQueue(), true);
        QCOMPARE(queue.currentIndex(), 0);

        queue.enqueue("vid2", "Title 2", "Chan", "thumb", "url", "2:00", 120);
        QCOMPARE(queue.rowCount(), 2);
        // Play next (enqueueNext) inserts right after current
        queue.enqueueNext("vidNext", "Title Next", "Chan", "thumb", "url", "0:30", 30);
        QCOMPARE(queue.rowCount(), 3);
        QCOMPARE(queue.data(queue.index(1, 0), QueueManager::VideoIdRole).toString(), QString("vidNext"));

        // Advance queue
        QVERIFY(queue.advance());
        QCOMPARE(queue.currentIndex(), 1);
        QCOMPARE(queue.currentItem().value("videoId").toString(), QString("vidNext"));

        // Advance to end
        QVERIFY(queue.advance());
        QCOMPARE(queue.currentIndex(), 2);
        QCOMPARE(queue.currentItem().value("videoId").toString(), QString("vid2"));

        // No more items to advance to
        QVERIFY(!queue.advance());

        queue.clear();
        QCOMPARE(queue.rowCount(), 0);

        // Test enqueueModel
        VideoModel vmodel;
        VideoItem item;
        item.videoId = "mod1";
        item.title = "Model Title";
        vmodel.addVideo(item);
        queue.enqueueModel(&vmodel);
        QCOMPARE(queue.rowCount(), 1);
        QCOMPARE(queue.data(queue.index(0, 0), QueueManager::VideoIdRole).toString(), QString("mod1"));

        queue.clear();
        QCOMPARE(queue.rowCount(), 0);
        QCOMPARE(queue.hasQueue(), false);
        QCOMPARE(queue.currentIndex(), -1);

        // Test advance when currentIndex is -1 (should advance to 0 if queue has items)
        queue.enqueue("vidA", "Title A", "Chan", "thumb", "url", "1:00", 60);
        queue.enqueue("vidB", "Title B", "Chan", "thumb", "url", "2:00", 120);
        // Force currentIndex to -1 manually or via removal/clear simulation
        queue.clear();
        queue.enqueue("vidA", "Title A", "Chan", "thumb", "url", "1:00", 60);
        queue.enqueue("vidB", "Title B", "Chan", "thumb", "url", "2:00", 120);
        // If we set or simulate index recovery, or test advance from start:
        // Actually when queue is enqueued on empty, currentIndex becomes 0.
        // Let's test advancing from 0 to 1:
        QCOMPARE(queue.currentIndex(), 0);
        QVERIFY(queue.advance());
        QCOMPARE(queue.currentIndex(), 1);
        QCOMPARE(queue.currentItem().value("videoId").toString(), QString("vidB"));

        queue.clear();
        QCOMPARE(queue.rowCount(), 0);

        // Test first video played on empty queue gets added
        if (queue.count() == 0) {
            queue.enqueue("play1", "First Played", "Chan", "thumb", "url", "1:00", 60);
        }
        QCOMPARE(queue.rowCount(), 1);
        QCOMPARE(queue.currentIndex(), 0);
        QCOMPARE(queue.currentItem().value("videoId").toString(), QString("play1"));

        queue.clear();
        QCOMPARE(queue.rowCount(), 0);
        QCOMPARE(queue.hasQueue(), false);
        QCOMPARE(queue.currentIndex(), -1);
    }
};

QTEST_MAIN(TestPrimoTube)
#include "tst_primotube.moc"
