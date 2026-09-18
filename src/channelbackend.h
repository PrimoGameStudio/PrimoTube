#pragma once

#include <QObject>
#include <QProcess>
#include <QPointer>
#include <QString>

class QNetworkAccessManager;
class QNetworkReply;
class VideoModel;

// Fetches a YouTube channel's header (name, handle, subscriber count,
// avatar, description) plus its recent uploads via
// `yt-dlp --flat-playlist --dump-json` on the channel's /videos tab.
// Videos are exposed through a VideoModel owned by the caller (like
// SearchBackend), with the same 10-per-page / 50-total pagination.
class ChannelBackend : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(QString channelId READ channelId NOTIFY channelChanged)
    Q_PROPERTY(QString channelTitle READ channelTitle NOTIFY channelChanged)
    Q_PROPERTY(QString channelHandle READ channelHandle NOTIFY channelChanged)
    Q_PROPERTY(QString channelUrl READ channelUrl NOTIFY channelChanged)
    Q_PROPERTY(QString description READ description NOTIFY channelChanged)
    Q_PROPERTY(QString avatarUrl READ avatarUrl NOTIFY channelChanged)
    Q_PROPERTY(qint64 subscriberCount READ subscriberCount NOTIFY channelChanged)
    Q_PROPERTY(QString subscriberText READ subscriberText NOTIFY channelChanged)
    Q_PROPERTY(bool hasMore READ hasMore NOTIFY hasMoreChanged)
    Q_PROPERTY(int videoCount READ videoCount NOTIFY videosChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusTextChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY loadError)
public:
    explicit ChannelBackend(VideoModel *model, QObject *parent = nullptr);

    bool loading() const { return m_loading; }
    QString channelId() const { return m_channelId; }
    QString channelTitle() const { return m_channelTitle; }
    QString channelHandle() const { return m_channelHandle; }
    QString channelUrl() const { return m_channelUrl; }
    QString description() const { return m_description; }
    QString avatarUrl() const { return m_avatarUrl; }
    qint64 subscriberCount() const { return m_subscriberCount; }
    QString subscriberText() const { return m_subscriberText; }
    bool hasMore() const { return m_hasMore; }
    int videoCount() const { return m_fetchedCount; }
    QString statusText() const { return m_statusText; }
    QString lastError() const { return m_lastError; }

    Q_INVOKABLE void openChannel(const QString &channelIdOrUrlOrHandle);
    Q_INVOKABLE void loadMore();
    Q_INVOKABLE void cancel();
    // Raw input of the most recently requested channel (even if queued).
    // Lets stacked ChannelPages detect that the shared backend now shows a
    // different channel and reload their own query on return.
    Q_INVOKABLE QString currentRequest() const { return m_currentInput; }

    // URL helpers (same normalization rules as SearchBackend).
    Q_INVOKABLE static bool isChannelUrl(const QString &query);
    static QString normalizeChannelUrl(const QString &query);
    static QString channelVideosUrl(const QString &query);

    static constexpr int kPageSize = 10;
    static constexpr int kMaxTotal = 50;

signals:
    void loadingChanged();
    void channelChanged();
    void videosChanged();
    void hasMoreChanged();
    void statusTextChanged();
    void loadError(const QString &message);
    void channelLoaded();

private slots:
    void onHeaderFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void onVideosFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void onProcessError(QProcess::ProcessError error);
    void onProfileFinished();

private:
    void setLoading(bool value);
    void setStatusText(const QString &text);
    void setHasMore(bool value);
    void fetchHeader();
    void fetchVideos(int start, int end);
    void fail(const QString &message);
    // Lightweight channel-page HTML fetch for avatar/description/subscriber
    // text (the flat-playlist JSON carries none of these). Best-effort:
    // failure keeps the placeholder and never fails the channel load.
    void fetchChannelProfile();
    void abortProfileFetch();

    VideoModel *m_model = nullptr;
    QProcess *m_headerProcess = nullptr;
    QProcess *m_videosProcess = nullptr;
    bool m_loading = false;
    bool m_cancelled = false;
    bool m_hasMore = false;
    QString m_lastError;
    QString m_statusText;
    QString m_channelId;
    QString m_channelTitle;
    QString m_channelHandle;
    QString m_channelUrl; // canonical, without /videos suffix
    QString m_videosUrl; // canonical + /videos
    QString m_description;
    QString m_avatarUrl;
    qint64 m_subscriberCount = -1;
    QString m_subscriberText;
    int m_fetchedCount = 0;
    bool m_pendingLoadMore = false;
    QString m_pendingInput;
    bool m_hasPending = false;
    QString m_currentInput;
    int m_generation = 0;
    int m_headerGen = -1;
    int m_videosGen = -1;
    QNetworkAccessManager *m_net = nullptr;
    QPointer<QNetworkReply> m_profileReply;
    int m_profileGen = -1;
    static constexpr qint64 kMaxProfileBytes = 4 * 1024 * 1024;
};
