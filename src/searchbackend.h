#pragma once

#include <QObject>
#include <QProcess>
#include <QString>

class VideoModel;

class SearchBackend : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool searching READ searching NOTIFY searchingChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY searchError)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusTextChanged)
    Q_PROPERTY(bool hasMore READ hasMore NOTIFY hasMoreChanged)
    Q_PROPERTY(int resultCount READ resultCount NOTIFY searchFinished)
public:
    explicit SearchBackend(VideoModel *model, QObject *parent = nullptr);

    bool searching() const { return m_searching; }
    QString lastError() const { return m_lastError; }
    QString statusText() const { return m_statusText; }
    bool hasMore() const { return m_hasMore; }
    int resultCount() const { return m_fetchedCount; }

    Q_INVOKABLE void search(const QString &query, int maxResults = 10);
    Q_INVOKABLE void loadMore();
    Q_INVOKABLE void cancel();

    Q_INVOKABLE static bool isChannelUrl(const QString &query);
    static QString channelVideosUrl(const QString &query);

    static constexpr int kPageSize = 10;
    static constexpr int kMaxTotal = 50;

signals:
    void searchingChanged();
    void searchError(const QString &message);
    void searchFinished(int count);
    void statusTextChanged();
    void hasMoreChanged();

private slots:
    void onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void onProcessError(QProcess::ProcessError error);

private:
    void setSearching(bool value);
    void setStatusText(const QString &text);
    void setHasMore(bool value);
    void beginSearch(int playlistStart, int playlistEnd, bool append);
    void startPendingIfAny();

    VideoModel *m_model = nullptr;
    QProcess *m_process = nullptr;
    bool m_searching = false;
    bool m_append = false;
    bool m_hasMore = false;
    bool m_userCancelled = false;
    QString m_lastError;
    QString m_statusText;
    QString m_lastQuery;
    QString m_lastSpec; // ytsearch spec or direct URL
    bool m_lastIsDirect = false;
    int m_fetchedCount = 0;
    int m_lastWindow = 0; // requested playlist window size for hasMore heuristic
    int m_pendingStart = 0;
    int m_pendingEnd = 0;
    bool m_hasPending = false;
    bool m_pendingAppend = false;
};
