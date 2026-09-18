#pragma once

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QObject>
#include <QProcess>
#include <QString>

// Checks whether the installed yt-dlp is outdated (PyPI is the source of
// truth) and can self-update via `yt-dlp -U`.
class YtdlpUpdater : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString currentVersion READ currentVersion NOTIFY statusChanged)
    Q_PROPERTY(QString latestVersion READ latestVersion NOTIFY statusChanged)
    Q_PROPERTY(bool updateAvailable READ updateAvailable NOTIFY statusChanged)
    Q_PROPERTY(bool checking READ checking NOTIFY statusChanged)
    Q_PROPERTY(bool updating READ updating NOTIFY statusChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusChanged)
public:
    explicit YtdlpUpdater(QObject *parent = nullptr);

    QString currentVersion() const { return m_current; }
    QString latestVersion() const { return m_latest; }
    bool updateAvailable() const { return m_updateAvailable; }
    bool checking() const { return m_checking; }
    bool updating() const { return m_updating; }
    QString statusText() const { return m_status; }

    Q_INVOKABLE void checkForUpdates();
    Q_INVOKABLE void updateYtdlp();

    static int compareVersions(const QString &a, const QString &b);

signals:
    void statusChanged();

private slots:
    void onVersionFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void onPypiFinished();
    void onUpdateFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void onVersionError(QProcess::ProcessError error);
    void onUpdateError(QProcess::ProcessError error);

private:
    void setStatus(const QString &s);

    QNetworkAccessManager m_nam;
    QProcess *m_versionProc = nullptr;
    QProcess *m_updateProc = nullptr;
    QString m_current;
    QString m_latest;
    bool m_updateAvailable = false;
    bool m_checking = false;
    bool m_updating = false;
    bool m_pendingCheck = false;
    bool m_pendingUpdate = false;
    QString m_status;
};
