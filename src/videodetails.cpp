#include "videodetails.h"

#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>

#include "channelutils.h"

VideoDetails::VideoDetails(QObject *parent)
    : QObject(parent)
    , m_process(new QProcess(this))
{
    connect(m_process, &QProcess::finished, this, &VideoDetails::onProcessFinished);
    connect(m_process, &QProcess::errorOccurred, this, &VideoDetails::onProcessError);
}

void VideoDetails::fetch(const QString &videoIdOrUrl)
{
    const QString id = videoIdOrUrl.trimmed();
    if (id.isEmpty())
        return;
    if (!ChannelUtils::isAllowedVideoUrl(id)) {
        m_lastError = tr("Only YouTube video links are supported.");
        emit loadError(m_lastError);
        return;
    }
    const QString url = id.contains(QStringLiteral("://"))
        ? id
        : QStringLiteral("https://www.youtube.com/watch?v=%1").arg(id);
    if (m_process->state() != QProcess::NotRunning) {
        m_pendingUrl = url;
        m_hasPending = true;
        m_process->kill();
        return;
    }
    m_cancelled = false;
    m_activeUrl = url;
    m_pendingUrl.clear();
    m_hasPending = false;
    // Keep previous data visible until the new payload arrives; failure
    // paths below preserve it as well.
    setLoading(true);
    m_process->start(ChannelUtils::ytdlpExecutable(),
                     QStringList{
                         QStringLiteral("--dump-json"),
                         QStringLiteral("--no-playlist"),
                         QStringLiteral("--no-warnings"),
                         QStringLiteral("--socket-timeout"), QStringLiteral("20"),
                         url,
                     });
}

void VideoDetails::cancel()
{
    m_cancelled = true;
    m_hasPending = false;
    setLoading(false);
    if (m_process->state() != QProcess::NotRunning)
        m_process->kill();
}

void VideoDetails::onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    if (m_hasPending) {
        m_hasPending = false;
        const QString next = m_pendingUrl;
        m_pendingUrl.clear();
        fetch(next);
        return;
    }
    setLoading(false);
    if (m_cancelled) {
        m_cancelled = false;
        return;
    }
    if (exitStatus != QProcess::NormalExit || exitCode != 0) {
        const QString err = QString::fromUtf8(m_process->readAllStandardError()).trimmed();
        m_lastError = err.isEmpty() ? tr("Could not load video details.") : err.split(QLatin1Char('\n')).last();
        // Preserve previous good data; just report the error.
        emit loadError(m_lastError);
        return;
    }
    const QByteArray out = m_process->readAllStandardOutput();
    QJsonParseError perr{};
    const QJsonDocument doc = QJsonDocument::fromJson(out, &perr);
    if (perr.error != QJsonParseError::NoError || !doc.isObject()) {
        m_lastError = tr("Could not parse video details.");
        emit loadError(m_lastError);
        return;
    }
    const QJsonObject obj = doc.object();
    m_videoId = obj.value(QStringLiteral("id")).toString();
    m_title = obj.value(QStringLiteral("title")).toString();
    m_channel = obj.value(QStringLiteral("channel")).toString();
    if (m_channel.isEmpty())
        m_channel = obj.value(QStringLiteral("uploader")).toString();
    m_channelId = obj.value(QStringLiteral("channel_id")).toString();
    if (m_channelId.isEmpty())
        m_channelId = obj.value(QStringLiteral("uploader_id")).toString();
    m_channelUrl = obj.value(QStringLiteral("channel_url")).toString();
    if (m_channelUrl.isEmpty())
        m_channelUrl = obj.value(QStringLiteral("uploader_url")).toString();
    if (m_channelUrl.isEmpty() && !m_channelId.isEmpty())
        m_channelUrl = QStringLiteral("https://www.youtube.com/channel/%1").arg(m_channelId);
    m_description = obj.value(QStringLiteral("description")).toString();
    m_viewCount = static_cast<qint64>(obj.value(QStringLiteral("view_count")).toDouble(-1));
    m_likeCount = static_cast<qint64>(obj.value(QStringLiteral("like_count")).toDouble(-1));
    const QString rawDate = obj.value(QStringLiteral("upload_date")).toString(); // YYYYMMDD
    const bool dateOk = rawDate.size() == 8
        && std::all_of(rawDate.cbegin(), rawDate.cend(),
                       [](QChar c) { return c.isDigit(); });
    if (dateOk)
        m_uploadDate = rawDate.left(4) + QLatin1Char('-') + rawDate.mid(4, 2) + QLatin1Char('-') + rawDate.mid(6, 2);
    else
        m_uploadDate = rawDate;
    m_subtitleLangs.clear();
    const QJsonObject subs = obj.value(QStringLiteral("subtitles")).toObject();
    for (auto it = subs.begin(); it != subs.end(); ++it)
        m_subtitleLangs.append(it.key());
    const QJsonObject autoSubs = obj.value(QStringLiteral("automatic_captions")).toObject();
    for (auto it = autoSubs.begin(); it != autoSubs.end(); ++it) {
        if (!m_subtitleLangs.contains(it.key()))
            m_subtitleLangs.append(it.key() + QStringLiteral(" (auto)"));
    }
    m_subtitleLangs.sort();
    emit loaded();
}

void VideoDetails::onProcessError(QProcess::ProcessError error)
{
    Q_UNUSED(error)
    if (m_process->error() == QProcess::FailedToStart) {
        if (m_hasPending) {
            m_hasPending = false;
            fetch(m_pendingUrl);
            return;
        }
        setLoading(false);
        m_lastError = tr("Could not start yt-dlp. Is it installed and in PATH?");
        emit loadError(m_lastError);
    }
}

void VideoDetails::setLoading(bool v)
{
    if (m_loading == v)
        return;
    m_loading = v;
    emit loadingChanged();
}

void VideoDetails::clearData()
{
    m_videoId.clear();
    m_title.clear();
    m_channel.clear();
    m_channelId.clear();
    m_channelUrl.clear();
    m_description.clear();
    m_viewCount = -1;
    m_likeCount = -1;
    m_uploadDate.clear();
    m_subtitleLangs.clear();
}
