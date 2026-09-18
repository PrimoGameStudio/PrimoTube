#pragma once

#include <QObject>
#include <QProcess>
#include <QString>
#include <QStringList>

// Resolves a YouTube watch URL / video id into a single progressive stream
// URL playable by Qt Multimedia. Tries a ladder of format selectors so
// videos without a progressive MP4 still get a chance (e.g. progressive
// WebM, then best single-file). Split DASH track pairs are never returned,
// as the built-in player cannot mux them.
class PlaybackResolver : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool resolving READ resolving NOTIFY resolvingChanged)
    Q_PROPERTY(QString streamUrl READ streamUrl NOTIFY resolved)
    Q_PROPERTY(QString lastError READ lastError NOTIFY resolveError)
    Q_PROPERTY(QString rawError READ rawError NOTIFY rawErrorChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusTextChanged)
public:
    explicit PlaybackResolver(QObject *parent = nullptr);

    bool resolving() const { return m_resolving; }
    QString streamUrl() const { return m_streamUrl; }
    QString lastError() const { return m_lastError; }
    QString rawError() const { return m_rawError; }
    QString statusText() const { return m_statusText; }

    Q_INVOKABLE void resolve(const QString &videoIdOrUrl);
    Q_INVOKABLE void cancel();

    static bool isFatalError(const QString &stderrText);
    static QString friendlyError(const QString &stderrText, bool exhausted);

signals:
    void resolvingChanged();
    void resolved(const QString &streamUrl);
    void resolveError(const QString &message);
    void rawErrorChanged();
    void statusTextChanged();

private slots:
    void onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void onProcessError(QProcess::ProcessError error);

private:
    void setResolving(bool value);
    void setStatusText(const QString &text);
    void startStage();
    void fail(const QString &message, const QString &rawStderr);

    QProcess *m_process = nullptr;
    bool m_resolving = false;
    bool m_cancelled = false;
    QString m_streamUrl;
    QString m_lastError;
    QString m_rawError;
    QString m_statusText;
    QString m_watchUrl;
    QString m_pendingUrl;
    bool m_hasPending = false;
    int m_stage = 0;
    int m_attempt = 0;
    static constexpr int kMaxAttempts = 2;
    // Ordered format ladder: first match yielding exactly one URL wins.
    const QStringList m_stages{
        QStringLiteral("b[ext=mp4]/b"),
        QStringLiteral("b[ext=webm]/b"),
        QStringLiteral("best"),
    };
};
