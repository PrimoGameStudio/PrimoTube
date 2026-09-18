#pragma once

#include <QObject>
#include <QProcess>
#include <QString>
#include <QVector>

#include "videomodel.h"

// Aggregates the N most recent uploads of each subscribed channel into a
// single newest-first feed. Channels are fetched sequentially with one
// QProcess (yt-dlp flat-playlist, 3 entries each) to avoid process spam and
// rate limiting. Refresh is manual-only: QML passes a snapshot of the
// subscription list so C++ never depends on SubscriptionManager directly.
class HomeFeedBackend : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusTextChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY loadError)
    Q_PROPERTY(int videoCount READ videoCount NOTIFY videosChanged)
    Q_PROPERTY(int channelCount READ channelCount NOTIFY progressChanged)
    Q_PROPERTY(int channelsDone READ channelsDone NOTIFY progressChanged)
public:
    explicit HomeFeedBackend(VideoModel *model, QObject *parent = nullptr);

    bool loading() const { return m_loading; }
    QString statusText() const { return m_statusText; }
    QString lastError() const { return m_lastError; }
    int videoCount() const { return m_videoCount; }
    int channelCount() const { return m_pending.size(); }
    int channelsDone() const { return m_done; }

    Q_INVOKABLE void refresh(const QVariantList &channels);
    Q_INVOKABLE void cancel();

    static constexpr int kPerChannel = 3;
    // Sort helper (newest first; unknown timestamps sink to the bottom,
    // stable by channel order).
    static void sortNewestFirst(QVector<VideoItem> &items);

signals:
    void loadingChanged();
    void statusTextChanged();
    void loadError(const QString &message);
    void videosChanged();
    void progressChanged();

private slots:
    void onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void onProcessError(QProcess::ProcessError error);

private:
    struct PendingChannel {
        QString key; // channelId, url, or handle — for yt-dlp lookup
        QString title; // subscription title, fallback channel label
    };

    void setLoading(bool value);
    void setStatusText(const QString &text);
    void startNext();
    void finish(bool cancelled);

    VideoModel *m_model = nullptr;
    QProcess *m_process = nullptr;
    bool m_loading = false;
    bool m_cancelled = false;
    int m_generation = 0;
    int m_procGen = -1;
    QVector<PendingChannel> m_pending;
    int m_done = 0;
    int m_failed = 0;
    QVector<VideoItem> m_accum;
    QString m_firstError;
    QString m_statusText;
    QString m_lastError;
    int m_videoCount = 0;
};
