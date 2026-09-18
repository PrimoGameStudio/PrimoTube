#include "downloadbackend.h"
#include "channelutils.h"
#include "settingsmanager.h"

#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStandardPaths>

DownloadBackend::DownloadBackend(SettingsManager *settings, QObject *parent)
    : QObject(parent)
    , m_process(new QProcess(this))
    , m_settings(settings)
{
    connect(m_process, &QProcess::readyReadStandardOutput, this, &DownloadBackend::onReadyRead);
    connect(m_process, &QProcess::readyReadStandardError, this, &DownloadBackend::onReadyRead);
    connect(m_process, &QProcess::finished, this, &DownloadBackend::onProcessFinished);
    connect(m_process, &QProcess::errorOccurred, this, &DownloadBackend::onProcessError);
}

void DownloadBackend::download(const QString &videoIdOrUrl)
{
    const QString id = videoIdOrUrl.trimmed();
    if (id.isEmpty())
        return;
    if (!ChannelUtils::isAllowedVideoUrl(id)) {
        m_lastError = tr("Only YouTube video links are supported.");
        emit downloadError(m_lastError);
        return;
    }
    const QString watchUrl = id.contains(QStringLiteral("://"))
        ? id
        : QStringLiteral("https://www.youtube.com/watch?v=%1").arg(id);
    // Async: queue if a download is running (previous one is cancelled).
    if (m_process->state() != QProcess::NotRunning) {
        m_pendingUrl = watchUrl;
        m_hasPending = true;
        m_process->kill();
        return;
    }

    QString dir = m_settings ? m_settings->downloadDirectory() : QString();
    if (dir.isEmpty()) {
        dir = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
        if (dir.isEmpty())
            dir = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
    }
    QDir outDir(dir);
    if (dir.isEmpty() || !outDir.mkpath(QStringLiteral(".")) || !QFileInfo(dir).isWritable()) {
        m_lastError = tr("Download directory is not writable: %1").arg(dir);
        emit downloadError(m_lastError);
        return;
    }
    const QString outputTemplate = outDir.filePath(QStringLiteral("%(title)s [%(id)s].%(ext)s"));

    m_cancelled = false;
    m_output.clear();
    m_lineBuf.clear();
    m_savedPath.clear();
    m_lastError.clear();
    m_watchUrl = watchUrl;
    m_destTemplate = outputTemplate;
    m_phase = 0;
    setProgress(0.0);
    setDownloading(true);
    startAttempt();
}

void DownloadBackend::startAttempt()
{
    QStringList args{
        QStringLiteral("-f"), QStringLiteral("bv*+ba/b"),
        QStringLiteral("--merge-output-format"), QStringLiteral("mp4"),
        QStringLiteral("--no-overwrites"),
        QStringLiteral("--no-playlist"),
        QStringLiteral("--newline"),
        QStringLiteral("--no-warnings"),
        QStringLiteral("--restrict-filenames"),
        QStringLiteral("--socket-timeout"), QStringLiteral("20"),
        QStringLiteral("-o"), m_destTemplate,
        m_watchUrl,
    };
    if (m_phase == 1) {
        // Fallback: the android client serves stable URLs but only basic
        // formats (often just 360p progressive). Partial .part files from
        // phase 0 are simply left behind; the new formats resume/restart
        // under their own .part names.
        args.insert(args.size() - 2, QStringLiteral("--extractor-args"));
        args.insert(args.size() - 2, QStringLiteral("youtube:player_client=android"));
        qWarning() << "DownloadBackend: retrying with android player client (reduced quality)";
    }
    m_process->start(ChannelUtils::ytdlpExecutable(), args);
}

bool DownloadBackend::isFatalError(const QString &output)
{
    const QString s = output.toLower();
    return s.contains(QStringLiteral("private video")) || s.contains(QStringLiteral("video is unavailable"))
        || s.contains(QStringLiteral("video unavailable")) || s.contains(QStringLiteral("has been deleted"))
        || s.contains(QStringLiteral("has been removed")) || s.contains(QStringLiteral("account associated"));
}

void DownloadBackend::cancel()
{
    m_cancelled = true;
    m_hasPending = false;
    m_pendingUrl.clear();
    setDownloading(false);
    if (m_process->state() != QProcess::NotRunning)
        m_process->kill();
}

void DownloadBackend::onReadyRead()
{
    const QString chunk = QString::fromUtf8(m_process->readAllStandardOutput())
        + QString::fromUtf8(m_process->readAllStandardError());
    m_output += chunk;
    // Bound memory: keep only the last 32KB for error reporting; filename
    // parsing below uses complete lines only (see m_lineBuf).
    static constexpr int kMaxOutput = 32 * 1024;
    if (m_output.size() > kMaxOutput)
        m_output = m_output.right(kMaxOutput);

    // Parse complete lines only: a 32KB truncation or a chunk boundary can
    // otherwise split a "[download] Destination:" line mid-token and clip
    // the filename. The partial tail is carried over to the next call.
    m_lineBuf += chunk;
    QStringList lines = m_lineBuf.split(QLatin1Char('\n'));
    m_lineBuf = lines.takeLast();

    static const QRegularExpression percentRe(QStringLiteral("\\[download\\]\\s+(\\d+(?:\\.\\d+)?)%"));
    static const QRegularExpression destRe(QStringLiteral("\\[download\\] Destination: (.+)"));
    static const QRegularExpression mergerRe(QStringLiteral("\\[Merger\\] Merging formats into \"(.+)\""));
    static const QRegularExpression doneRe(QStringLiteral("\\[download\\] (.+) has already been downloaded"));

    double lastPercent = -1.0;
    QString mergerLast;
    QString doneLast;
    QString destLast;
    for (const QString &line : lines) {
        auto pm = percentRe.match(line);
        if (pm.hasMatch())
            lastPercent = pm.captured(1).toDouble();
        auto mm = mergerRe.match(line);
        if (mm.hasMatch())
            mergerLast = mm.captured(1);
        auto dm = doneRe.match(line);
        if (dm.hasMatch())
            doneLast = dm.captured(1);
        auto dd = destRe.match(line);
        if (dd.hasMatch())
            destLast = dd.captured(1);
        if (line.contains(QStringLiteral("[Merger]")))
            setProgress(0.99);
    }
    if (lastPercent >= 0.0)
        setProgress(qBound(0.0, lastPercent / 100.0, 1.0));

    // Final filename: merger output wins, then already-downloaded, then last destination.
    // Multiple "[download] Destination:" lines occur for split video+audio;
    // the merged file (or the last destination) is the relevant one.
    QString candidate;
    if (!mergerLast.isEmpty())
        candidate = mergerLast;
    else if (!doneLast.isEmpty())
        candidate = doneLast;
    else if (!destLast.isEmpty())
        candidate = destLast;
    if (!candidate.isEmpty() && m_savedPath != candidate.trimmed()) {
        m_savedPath = candidate.trimmed();
    }
}

void DownloadBackend::onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    if (m_hasPending) {
        m_hasPending = false;
        m_cancelled = false;
        const QString next = m_pendingUrl;
        m_pendingUrl.clear();
        // Restart asynchronously without recursion depth: reuse download().
        // m_process is now NotRunning so this takes the fast path.
        download(next);
        return;
    }
    if (m_cancelled) {
        m_cancelled = false;
        return;
    }
    setDownloading(false);
    if (exitStatus != QProcess::NormalExit || exitCode != 0) {
        // Drain anything not yet parsed, then decide: unplayable videos
        // fail fast, transient/extraction failures fall back to the
        // reliable (but lower-quality) android client once.
        onReadyRead();
        if (!isFatalError(m_output) && m_phase == 0) {
            qWarning() << "DownloadBackend: default clients failed, falling back to android client";
            m_phase = 1;
            m_output.clear();
            m_lineBuf.clear();
            m_savedPath.clear();
            setProgress(0.0);
            setDownloading(true);
            startAttempt();
            return;
        }
        const QString stderrTail = m_output.trimmed().split(QLatin1Char('\n')).last().trimmed();
        m_lastError = stderrTail.isEmpty()
            ? tr("Download failed (yt-dlp exit code %1).").arg(exitCode)
            : stderrTail;
        qWarning() << "DownloadBackend: download failed:" << m_lastError;
        emit downloadError(m_lastError);
        return;
    }
    onReadyRead();
    // Flush a final line without trailing newline so the filename is not
    // lost when yt-dlp exits mid-line.
    if (!m_lineBuf.trimmed().isEmpty()) {
        m_output += m_lineBuf + QLatin1Char('\n');
        m_lineBuf.clear();
        onReadyRead();
    }
    setProgress(1.0);
    const QString verified = verifiedSavedPath();
    if (verified.isEmpty()) {
        m_savedPath.clear();
        m_lastError = tr("Download finished but the file could not be found.");
        qWarning() << "DownloadBackend:" << m_lastError << "parsed as:" << m_savedPath;
        emit downloadError(m_lastError);
        return;
    }
    m_savedPath = verified;
    emit downloadFinished(m_savedPath);
}

QString DownloadBackend::verifiedSavedPath() const
{
    if (m_savedPath.isEmpty())
        return {};
    const QFileInfo fi(m_savedPath);
    if (!fi.exists() || !fi.isFile())
        return {};
    // The parsed path must live under the download directory so a spoofed
    // yt-dlp output line cannot point QML at an arbitrary file.
    QString expectedDir;
    if (!m_destTemplate.isEmpty())
        expectedDir = QFileInfo(m_destTemplate).absoluteDir().canonicalPath();
    else if (m_settings)
        expectedDir = QDir(m_settings->downloadDirectory()).canonicalPath();
    if (expectedDir.isEmpty())
        return fi.canonicalFilePath();
    const QString canonical = fi.canonicalFilePath();
    if (canonical.isEmpty())
        return {};
    if (canonical == expectedDir || canonical.startsWith(expectedDir + QLatin1Char('/')))
        return canonical;
    return {};
}

void DownloadBackend::onProcessError(QProcess::ProcessError error)
{
    Q_UNUSED(error)
    if (m_process->error() == QProcess::FailedToStart) {
        m_cancelled = false;
        setDownloading(false);
        m_lastError = tr("Could not start yt-dlp. Is it installed and in PATH?");
        emit downloadError(m_lastError);
    }
}

void DownloadBackend::setDownloading(bool value)
{
    if (m_downloading == value)
        return;
    m_downloading = value;
    emit downloadingChanged();
}

void DownloadBackend::setProgress(double value)
{
    if (qFuzzyCompare(m_progress + 1.0, value + 1.0))
        return;
    m_progress = value;
    emit progressChanged();
}
