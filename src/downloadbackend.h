#pragma once

#include <QObject>
#include <QProcess>
#include <QString>

// Downloads a YouTube video to the user's Downloads folder at best
// quality (video+audio merged to MP4 via ffmpeg). One download at a
// time; a new request cancels the previous one.
class SettingsManager;

class DownloadBackend : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool downloading READ downloading NOTIFY downloadingChanged)
    Q_PROPERTY(double progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY downloadError)
    Q_PROPERTY(QString savedPath READ savedPath NOTIFY downloadFinished)
public:
    explicit DownloadBackend(SettingsManager *settings = nullptr, QObject *parent = nullptr);

    bool downloading() const { return m_downloading; }
    double progress() const { return m_progress; }
    QString lastError() const { return m_lastError; }
    QString savedPath() const { return m_savedPath; }

    Q_INVOKABLE void download(const QString &videoIdOrUrl);
    Q_INVOKABLE void cancel();

signals:
    void downloadingChanged();
    void progressChanged();
    void downloadError(const QString &message);
    void downloadFinished(const QString &filePath);

private slots:
    void onReadyRead();
    void onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void onProcessError(QProcess::ProcessError error);

private:
    void setDownloading(bool value);
    void setProgress(double value);
    void startAttempt();
    static bool isFatalError(const QString &output);
    // Verifies the parsed filename exists and lives under the download dir.
    // Returns the canonical path or empty when unusable.
    QString verifiedSavedPath() const;

    QProcess *m_process = nullptr;
    bool m_downloading = false;
    bool m_cancelled = false;
    double m_progress = 0.0;
    QString m_lastError;
    QString m_savedPath;
    QString m_output; // buffered yt-dlp output for progress/filename parsing (tail only)
    QString m_lineBuf; // partial line carry-over so split lines never parse half-clipped
    QString m_watchUrl;
    QString m_destTemplate;
    QString m_pendingUrl;
    bool m_hasPending = false;
    int m_phase = 0; // 0 = default clients (full quality), 1 = android fallback (reliable SD)
    SettingsManager *m_settings = nullptr;
};
