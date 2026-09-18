#include "playbackresolver.h"

#include <QDebug>

#include "channelutils.h"

PlaybackResolver::PlaybackResolver(QObject *parent)
    : QObject(parent)
    , m_process(new QProcess(this))
{
    connect(m_process, &QProcess::finished, this, &PlaybackResolver::onProcessFinished);
    connect(m_process, &QProcess::errorOccurred, this, &PlaybackResolver::onProcessError);
}

void PlaybackResolver::resolve(const QString &videoIdOrUrl)
{
    const QString id = videoIdOrUrl.trimmed();
    if (id.isEmpty())
        return;
    if (!ChannelUtils::isAllowedVideoUrl(id)) {
        fail(tr("Only YouTube video links are supported."), {});
        return;
    }
    const QString watchUrl = id.contains(QStringLiteral("://"))
        ? id
        : QStringLiteral("https://www.youtube.com/watch?v=%1").arg(id);
    // Fully async: queue the request if a resolve is already running.
    if (m_process->state() != QProcess::NotRunning) {
        m_pendingUrl = watchUrl;
        m_hasPending = true;
        m_process->kill();
        return;
    }
    m_watchUrl = watchUrl;
    m_cancelled = false;
    m_stage = 0;
    m_attempt = 0;
    m_streamUrl.clear();
    m_lastError.clear();
    if (!m_rawError.isEmpty()) {
        m_rawError.clear();
        emit rawErrorChanged();
    }
    setResolving(true);
    startStage();
}

void PlaybackResolver::cancel()
{
    m_cancelled = true;
    m_hasPending = false;
    m_pendingUrl.clear();
    setResolving(false);
    setStatusText({});
    if (m_process->state() != QProcess::NotRunning)
        m_process->kill();
}

void PlaybackResolver::onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    if (m_hasPending) {
        m_hasPending = false;
        m_watchUrl = m_pendingUrl;
        m_pendingUrl.clear();
        m_cancelled = false;
        m_stage = 0;
        m_attempt = 0;
        m_streamUrl.clear();
        m_lastError.clear();
        if (!m_rawError.isEmpty()) {
            m_rawError.clear();
            emit rawErrorChanged();
        }
        setResolving(true);
        startStage();
        return;
    }
    if (m_cancelled) {
        m_cancelled = false;
        return;
    }
    if (exitStatus != QProcess::NormalExit) {
        fail(tr("Stream lookup was interrupted."), {});
        return;
    }

    const QString stderrText = QString::fromUtf8(m_process->readAllStandardError()).trimmed();
    QStringList lines;
    for (const QString &line : QString::fromUtf8(m_process->readAllStandardOutput()).split(QLatin1Char('\n'))) {
        if (!line.trimmed().isEmpty())
            lines.append(line.trimmed());
    }

    // Exactly one URL = a playable single-file stream.
    if (exitCode == 0 && lines.size() == 1) {
        setResolving(false);
        setStatusText({});
        m_streamUrl = lines.constFirst();
        emit resolved(m_streamUrl);
        return;
    }

    // A multi-line result is a split DASH pair (video-only + audio-only),
    // which Qt Multimedia cannot mux — treat as "no match" for this stage.
    const bool dashPair = (exitCode == 0 && lines.size() > 1);

    if (!dashPair && isFatalError(stderrText)) {
        fail(friendlyError(stderrText, false), stderrText);
        return;
    }
    // Transient per-request failures are common (YouTube's player API
    // intermittently returns responses with no usable stream URLs), so
    // retry the same stage before falling through to the next format.
    if (!dashPair && m_attempt + 1 < kMaxAttempts) {
        qWarning() << "PlaybackResolver: attempt" << (m_attempt + 1) << "of" << kMaxAttempts
                   << "for format" << m_stages.at(m_stage) << "failed:" << stderrText;
        ++m_attempt;
        startStage();
        return;
    }
    if (m_stage + 1 < m_stages.size()) {
        if (!dashPair)
            qWarning() << "PlaybackResolver: format" << m_stages.at(m_stage) << "exhausted:" << stderrText;
        ++m_stage;
        m_attempt = 0;
        startStage();
        return;
    }
    if (dashPair) {
        fail(tr("YouTube only offers split audio/video tracks for this video, "
                "which the built-in player cannot play."),
             stderrText);
        return;
    }
    fail(friendlyError(stderrText, true), stderrText);
}

void PlaybackResolver::onProcessError(QProcess::ProcessError error)
{
    Q_UNUSED(error)
    if (m_process->error() == QProcess::FailedToStart) {
        m_cancelled = false;
        fail(tr("Could not start yt-dlp. Is it installed and in PATH?"), {});
    }
}

void PlaybackResolver::setResolving(bool value)
{
    if (m_resolving == value)
        return;
    m_resolving = value;
    emit resolvingChanged();
}

void PlaybackResolver::setStatusText(const QString &text)
{
    if (m_statusText == text)
        return;
    m_statusText = text;
    emit statusTextChanged();
}

void PlaybackResolver::startStage()
{
    if (m_stage == 0 && m_attempt == 0)
        setStatusText(tr("Resolving stream URL… (SD progressive)"));
    else
        setStatusText(tr("Resolving… (format %1/%2, attempt %3/%4)")
                           .arg(m_stage + 1)
                           .arg(m_stages.size())
                           .arg(m_attempt + 1)
                           .arg(kMaxAttempts));

    QStringList args{
        QStringLiteral("-f"), m_stages.at(m_stage),
        QStringLiteral("-g"),
        QStringLiteral("--no-warnings"),
        // The android player client serves stable stream URLs; the default
        // SABR/visionos responses intermittently omit them (PO-token gating),
        // causing spurious "format not available" failures.
        QStringLiteral("--extractor-args"), QStringLiteral("youtube:player_client=android"),
        QStringLiteral("--socket-timeout"), QStringLiteral("20"),
        m_watchUrl,
    };
    m_process->start(ChannelUtils::ytdlpExecutable(), args);
}

void PlaybackResolver::fail(const QString &message, const QString &rawStderr)
{
    m_cancelled = false;
    m_streamUrl.clear();
    setResolving(false);
    setStatusText({});
    const QString raw = rawStderr.trimmed().split(QLatin1Char('\n')).last().trimmed();
    if (m_rawError != raw) {
        m_rawError = raw;
        emit rawErrorChanged();
    }
    m_lastError = message;
    emit resolveError(m_lastError);
}

bool PlaybackResolver::isFatalError(const QString &stderrText)
{
    const QString s = stderrText.toLower();
    return s.contains(QStringLiteral("private video")) || s.contains(QStringLiteral("video is unavailable"))
        || s.contains(QStringLiteral("video unavailable")) || s.contains(QStringLiteral("has been deleted"))
        || s.contains(QStringLiteral("has been removed")) || s.contains(QStringLiteral("account associated"))
        || s.contains(QStringLiteral("sign in")) || s.contains(QStringLiteral("log in"))
        || s.contains(QStringLiteral("confirm your age")) || s.contains(QStringLiteral("age gate"))
        || s.contains(QStringLiteral("age_limit")) || s.contains(QStringLiteral("age-verified"))
        || s.contains(QStringLiteral("not available in your country"))
        || s.contains(QStringLiteral("blocked in")) || s.contains(QStringLiteral("copyright"));
}

QString PlaybackResolver::friendlyError(const QString &stderrText, bool exhausted)
{
    const QString s = stderrText.toLower();
    if (s.contains(QStringLiteral("private video")))
        return tr("This video is private and cannot be played.");
    if (s.contains(QStringLiteral("video is unavailable")) || s.contains(QStringLiteral("video unavailable"))
        || s.contains(QStringLiteral("has been deleted")) || s.contains(QStringLiteral("has been removed"))
        || s.contains(QStringLiteral("account associated")))
        return tr("This video is unavailable on YouTube "
                  "(it may have been removed, deleted, or made private).");
    if (s.contains(QStringLiteral("sign in")) || s.contains(QStringLiteral("log in"))
        || s.contains(QStringLiteral("confirm your age")) || s.contains(QStringLiteral("age gate"))
        || s.contains(QStringLiteral("age_limit")) || s.contains(QStringLiteral("age-verified")))
        return tr("This video requires sign-in or age verification "
                  "and cannot be played in PrimoTube.");
    if (s.contains(QStringLiteral("not available in your country")) || s.contains(QStringLiteral("blocked in"))
        || s.contains(QStringLiteral("copyright")))
        return tr("This video is blocked in your region or on copyright grounds.");
    if (exhausted || s.contains(QStringLiteral("requested format is not available"))
        || s.contains(QStringLiteral("no video formats")))
        return tr("No directly playable stream found for this video, "
                  "even after trying alternate formats.");
    const QString lastLine = stderrText.split(QLatin1Char('\n')).last().trimmed();
    if (!lastLine.isEmpty())
        return lastLine;
    return tr("Could not resolve a playable stream for this video.");
}
