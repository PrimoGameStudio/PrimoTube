#pragma once

#include <QObject>
#include <QProcess>
#include <QString>
#include <QStringList>

// Fetches full metadata (description, views, likes, subtitles, …) for one
// video via `yt-dlp --dump-json`. Used by the player to show details that
// flat search results don't carry.
class VideoDetails : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(QString videoId READ videoId NOTIFY loaded)
    Q_PROPERTY(QString title READ title NOTIFY loaded)
    Q_PROPERTY(QString channel READ channel NOTIFY loaded)
    Q_PROPERTY(QString channelId READ channelId NOTIFY loaded)
    Q_PROPERTY(QString channelUrl READ channelUrl NOTIFY loaded)
    Q_PROPERTY(QString description READ description NOTIFY loaded)
    Q_PROPERTY(qint64 viewCount READ viewCount NOTIFY loaded)
    Q_PROPERTY(qint64 likeCount READ likeCount NOTIFY loaded)
    Q_PROPERTY(QString uploadDate READ uploadDate NOTIFY loaded)
    Q_PROPERTY(QStringList subtitleLangs READ subtitleLangs NOTIFY loaded)
    Q_PROPERTY(QString lastError READ lastError NOTIFY loadError)
public:
    explicit VideoDetails(QObject *parent = nullptr);

    bool loading() const { return m_loading; }
    QString videoId() const { return m_videoId; }
    QString title() const { return m_title; }
    QString channel() const { return m_channel; }
    QString channelId() const { return m_channelId; }
    QString channelUrl() const { return m_channelUrl; }
    QString description() const { return m_description; }
    qint64 viewCount() const { return m_viewCount; }
    qint64 likeCount() const { return m_likeCount; }
    QString uploadDate() const { return m_uploadDate; }
    QStringList subtitleLangs() const { return m_subtitleLangs; }
    QString lastError() const { return m_lastError; }

    Q_INVOKABLE void fetch(const QString &videoIdOrUrl);
    Q_INVOKABLE void cancel();

signals:
    void loadingChanged();
    void loaded();
    void loadError(const QString &message);

private slots:
    void onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void onProcessError(QProcess::ProcessError error);

private:
    void setLoading(bool v);
    void clearData();

    QProcess *m_process = nullptr;
    bool m_loading = false;
    bool m_cancelled = false;
    QString m_videoId;
    QString m_title;
    QString m_channel;
    QString m_channelId;
    QString m_channelUrl;
    QString m_description;
    qint64 m_viewCount = -1;
    qint64 m_likeCount = -1;
    QString m_uploadDate;
    QStringList m_subtitleLangs;
    QString m_lastError;
    QString m_activeUrl;
    QString m_pendingUrl;
    bool m_hasPending = false;
};
