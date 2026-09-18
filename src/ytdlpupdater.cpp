#include "ytdlpupdater.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkRequest>
#include <QRegularExpression>

#include "channelutils.h"

YtdlpUpdater::YtdlpUpdater(QObject *parent)
    : QObject(parent)
    , m_versionProc(new QProcess(this))
    , m_updateProc(new QProcess(this))
{
    connect(m_versionProc, &QProcess::finished, this, &YtdlpUpdater::onVersionFinished);
    connect(m_updateProc, &QProcess::finished, this, &YtdlpUpdater::onUpdateFinished);
    connect(m_versionProc, &QProcess::errorOccurred, this, &YtdlpUpdater::onVersionError);
    connect(m_updateProc, &QProcess::errorOccurred, this, &YtdlpUpdater::onUpdateError);
}

void YtdlpUpdater::checkForUpdates()
{
    if (m_checking || m_updating)
        return;
    if (m_versionProc->state() != QProcess::NotRunning) {
        // Non-blocking: kill the stale run; its finished() handler will
        // restart the check via m_pendingCheck.
        m_pendingCheck = true;
        m_versionProc->kill();
        return;
    }
    m_checking = true;
    m_updateAvailable = false;
    m_latest.clear();
    setStatus(tr("Checking yt-dlp version…"));
    m_versionProc->start(ChannelUtils::ytdlpExecutable(), {QStringLiteral("--version")});
}

void YtdlpUpdater::updateYtdlp()
{
    if (m_updating)
        return;
    if (m_updateProc->state() != QProcess::NotRunning) {
        m_pendingUpdate = true;
        m_updateProc->kill();
        return;
    }
    m_updating = true;
    setStatus(tr("Updating yt-dlp…"));
    m_updateProc->start(ChannelUtils::ytdlpExecutable(), {QStringLiteral("-U")});
}

int YtdlpUpdater::compareVersions(const QString &a, const QString &b)
{
    // yt-dlp versions are dates: YYYY.MM.DD[.patch], optionally with a
    // suffix like "-nightly". Compare dot-separated parts numerically on
    // the leading digits, then lexically on any suffix, so pre-releases
    // order sanely instead of collapsing to 0 via toInt().
    static const QRegularExpression numPrefix(QStringLiteral("^(\\d+)(.*)$"));
    const QStringList pa = a.trimmed().split(QLatin1Char('.'));
    const QStringList pb = b.trimmed().split(QLatin1Char('.'));
    const int n = qMax(pa.size(), pb.size());
    for (int i = 0; i < n; ++i) {
        const QString sa = i < pa.size() ? pa[i] : QStringLiteral("0");
        const QString sb = i < pb.size() ? pb[i] : QStringLiteral("0");
        const auto ma = numPrefix.match(sa);
        const auto mb = numPrefix.match(sb);
        const int va = ma.hasMatch() ? ma.captured(1).toInt() : 0;
        const int vb = mb.hasMatch() ? mb.captured(1).toInt() : 0;
        if (va != vb)
            return va < vb ? -1 : 1;
        const QString ra = ma.hasMatch() ? ma.captured(2) : sa;
        const QString rb = mb.hasMatch() ? mb.captured(2) : sb;
        if (ra != rb)
            return ra < rb ? -1 : 1;
    }
    return 0;
}

void YtdlpUpdater::onVersionFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    if (m_pendingCheck) {
        m_pendingCheck = false;
        m_checking = false;
        checkForUpdates();
        return;
    }
    if (exitStatus == QProcess::NormalExit && exitCode == 0)
        m_current = QString::fromUtf8(m_versionProc->readAllStandardOutput()).trimmed().split(QLatin1Char('\n')).first().trimmed();
    else
        m_current.clear();

    // Query PyPI for the latest release (short timeout, offline-safe).
    QNetworkRequest req(QUrl(QStringLiteral("https://pypi.org/pypi/yt-dlp/json")));
    req.setTransferTimeout(8000);
    QNetworkReply *reply = m_nam.get(req);
    connect(reply, &QNetworkReply::finished, this, &YtdlpUpdater::onPypiFinished);
}

void YtdlpUpdater::onPypiFinished()
{
    auto *reply = qobject_cast<QNetworkReply *>(sender());
    if (!reply)
        return;
    reply->deleteLater();
    if (reply->error() == QNetworkReply::NoError) {
        const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        m_latest = doc.object().value(QStringLiteral("info")).toObject().value(QStringLiteral("version")).toString();
    }
    m_checking = false;
    if (!m_current.isEmpty() && !m_latest.isEmpty()) {
        m_updateAvailable = compareVersions(m_current, m_latest) < 0;
        if (m_updateAvailable)
            setStatus(tr("yt-dlp %1 available (installed %2).").arg(m_latest, m_current));
        else
            setStatus(tr("yt-dlp is up to date (%1).").arg(m_current));
    } else if (m_current.isEmpty()) {
        setStatus(tr("yt-dlp not found in PATH."));
    } else {
        // Offline or PyPI unreachable — still report installed version.
        setStatus(tr("yt-dlp %1 installed (could not check for updates).").arg(m_current));
    }
    emit statusChanged();
}

void YtdlpUpdater::onUpdateFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    if (m_pendingUpdate) {
        m_pendingUpdate = false;
        m_updating = false;
        updateYtdlp();
        return;
    }
    m_updating = false;
    if (exitStatus == QProcess::NormalExit && exitCode == 0) {
        setStatus(tr("yt-dlp updated. Checking version…"));
        emit statusChanged();
        checkForUpdates();
        return;
    }
    const QString out = (QString::fromUtf8(m_updateProc->readAllStandardOutput())
                         + QLatin1Char('\n')
                         + QString::fromUtf8(m_updateProc->readAllStandardError()))
                            .trimmed();
    const QString tail = out.split(QLatin1Char('\n')).last().trimmed();
    setStatus(tail.isEmpty() ? tr("Update failed.") : tail);
    emit statusChanged();
}

void YtdlpUpdater::setStatus(const QString &s)
{
    if (m_status == s)
        return;
    m_status = s;
    emit statusChanged();
}

void YtdlpUpdater::onVersionError(QProcess::ProcessError error)
{
    if (error != QProcess::FailedToStart)
        return;
    m_current.clear();
    m_checking = false;
    m_updateAvailable = false;
    setStatus(tr("yt-dlp not found in PATH."));
}

void YtdlpUpdater::onUpdateError(QProcess::ProcessError error)
{
    if (error != QProcess::FailedToStart)
        return;
    m_updating = false;
    setStatus(tr("Could not start yt-dlp. Is it installed and in PATH?"));
}
