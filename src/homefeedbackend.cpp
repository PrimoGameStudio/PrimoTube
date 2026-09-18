#include "homefeedbackend.h"
#include "channelutils.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <algorithm>

HomeFeedBackend::HomeFeedBackend(VideoModel *model, QObject *parent)
    : QObject(parent)
    , m_model(model)
    , m_process(new QProcess(this))
{
    connect(m_process, &QProcess::finished, this, &HomeFeedBackend::onProcessFinished);
    connect(m_process, &QProcess::errorOccurred, this, &HomeFeedBackend::onProcessError);
}

void HomeFeedBackend::sortNewestFirst(QVector<VideoItem> &items)
{
    std::stable_sort(items.begin(), items.end(), [](const VideoItem &a, const VideoItem &b) {
        return a.timestamp > b.timestamp;
    });
}

void HomeFeedBackend::refresh(const QVariantList &channels)
{
    if (m_loading)
        return;
    QVector<PendingChannel> pending;
    pending.reserve(channels.size());
    for (const QVariant &v : channels) {
        const QVariantMap m = v.toMap();
        const QString id = m.value(QStringLiteral("channelId")).toString().trimmed();
        const QString url = m.value(QStringLiteral("url")).toString().trimmed();
        const QString handle = m.value(QStringLiteral("handle")).toString().trimmed();
        const QString title = m.value(QStringLiteral("title")).toString().trimmed();
        QString key = !id.isEmpty() ? id : (!url.isEmpty() ? url : handle);
        if (key.isEmpty())
            continue;
        pending.append({key, title});
    }
    if (pending.isEmpty()) {
        m_lastError.clear();
        setStatusText(tr("Subscribe to channels to build your Home feed."));
        return;
    }
    m_cancelled = false;
    ++m_generation;
    m_pending = pending;
    m_done = 0;
    m_failed = 0;
    m_accum.clear();
    m_accum.reserve(pending.size() * kPerChannel);
    m_firstError.clear();
    m_lastError.clear();
    m_videoCount = 0;
    emit videosChanged();
    setLoading(true);
    emit progressChanged();
    startNext();
}

void HomeFeedBackend::cancel()
{
    if (!m_loading)
        return;
    m_cancelled = true;
    ++m_generation; // invalidate in-flight finished() payload
    if (m_process->state() != QProcess::NotRunning)
        m_process->kill();
    finish(true);
}

void HomeFeedBackend::startNext()
{
    if (m_pending.isEmpty()) {
        finish(false);
        return;
    }
    const PendingChannel &next = m_pending.first();
    setStatusText(tr("Loading %1 (%2/%3)…").arg(next.title.isEmpty() ? next.key : next.title).arg(m_done + 1).arg(m_done + m_pending.size()));
    QString videosUrl = ChannelUtils::channelVideosUrl(next.key);
    if (videosUrl.isEmpty()) {
        // Bare UC… channel id (same rule as ChannelBackend).
        videosUrl = QStringLiteral("https://www.youtube.com/channel/%1/videos").arg(next.key);
    }
    m_procGen = m_generation;
    m_process->start(ChannelUtils::ytdlpExecutable(),
                     QStringList{
                         videosUrl,
                         QStringLiteral("--dump-json"),
                         QStringLiteral("--flat-playlist"),
                         QStringLiteral("--no-warnings"),
                         QStringLiteral("--socket-timeout"), QStringLiteral("20"),
                         QStringLiteral("--playlist-end"), QString::number(kPerChannel),
                     });
}

void HomeFeedBackend::onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    if (m_procGen != m_generation)
        return; // stale payload from a cancelled run
    if (!m_loading || m_pending.isEmpty())
        return;
    const PendingChannel cur = m_pending.takeFirst();
    ++m_done;
    if (m_cancelled) {
        emit progressChanged();
        return;
    }
    if (exitStatus != QProcess::NormalExit || exitCode != 0) {
        ++m_failed;
        if (m_firstError.isEmpty()) {
            const QString stderrText = QString::fromUtf8(m_process->readAllStandardError()).trimmed();
            m_firstError = stderrText.isEmpty()
                ? tr("Could not load %1 (yt-dlp exit code %2).").arg(cur.title.isEmpty() ? cur.key : cur.title).arg(exitCode)
                : stderrText.split(QLatin1Char('\n')).last();
        }
        // Drain stdout so the next channel starts clean.
        m_process->readAllStandardOutput();
    } else {
        const QByteArray out = m_process->readAllStandardOutput();
        for (const QByteArray &line : out.split('\n')) {
            const QByteArray trimmed = line.trimmed();
            if (trimmed.isEmpty())
                continue;
            QJsonParseError err{};
            const QJsonDocument doc = QJsonDocument::fromJson(trimmed, &err);
            if (err.error != QJsonParseError::NoError || !doc.isObject())
                continue;
            const QJsonObject obj = doc.object();
            VideoItem item;
            item.videoId = obj.value(QStringLiteral("id")).toString();
            item.title = obj.value(QStringLiteral("title")).toString();
            item.channel = obj.value(QStringLiteral("playlist_channel")).toString();
            if (item.channel.isEmpty())
                item.channel = obj.value(QStringLiteral("channel")).toString();
            if (item.channel.isEmpty())
                item.channel = obj.value(QStringLiteral("uploader")).toString();
            if (item.channel.isEmpty())
                item.channel = cur.title;
            item.channelId = obj.value(QStringLiteral("playlist_channel_id")).toString();
            if (item.channelId.isEmpty())
                item.channelId = obj.value(QStringLiteral("channel_id")).toString();
            if (item.channelId.isEmpty())
                item.channelId = obj.value(QStringLiteral("uploader_id")).toString();
            item.channelUrl = obj.value(QStringLiteral("channel_url")).toString();
            if (item.channelUrl.isEmpty())
                item.channelUrl = obj.value(QStringLiteral("uploader_url")).toString();
            item.duration = static_cast<int>(obj.value(QStringLiteral("duration")).toDouble(0));
            item.durationString = obj.value(QStringLiteral("duration_string")).toString();
            if (item.durationString.isEmpty())
                item.durationString = ChannelUtils::formatDuration(item.duration);
            item.thumbnail = ChannelUtils::pickVideoThumbnail(obj, item.videoId);
            item.description = obj.value(QStringLiteral("description")).toString();
            item.watchUrl = obj.value(QStringLiteral("webpage_url")).toString();
            if (item.watchUrl.isEmpty() && !item.videoId.isEmpty())
                item.watchUrl = QStringLiteral("https://www.youtube.com/watch?v=%1").arg(item.videoId);
            item.timestamp = ChannelUtils::parseVideoTimestamp(obj);
            item.uploadDate = ChannelUtils::formatUploadDate(obj, item.timestamp);
            if (item.videoId.isEmpty() || item.title.isEmpty())
                continue;
            m_accum.append(item);
        }
    }
    emit progressChanged();
    if (m_pending.isEmpty())
        finish(false);
    else
        startNext();
}

void HomeFeedBackend::onProcessError(QProcess::ProcessError error)
{
    Q_UNUSED(error)
    if (m_process->error() != QProcess::FailedToStart)
        return;
    if (m_procGen != m_generation)
        return;
    if (!m_loading)
        return;
    if (!m_pending.isEmpty()) {
        const PendingChannel cur = m_pending.takeFirst();
        ++m_done;
        ++m_failed;
        if (m_firstError.isEmpty())
            m_firstError = tr("Could not start yt-dlp. Is it installed and in PATH?");
        emit progressChanged();
    }
    if (m_pending.isEmpty())
        finish(false);
    else
        startNext();
}

void HomeFeedBackend::finish(bool cancelled)
{
    if (cancelled) {
        m_pending.clear();
        setLoading(false);
        setStatusText(tr("Home feed refresh cancelled."));
        emit progressChanged();
        return;
    }
    sortNewestFirst(m_accum);
    m_model->setItems(m_accum);
    m_videoCount = m_accum.size();
    setLoading(false);
    if (m_videoCount == 0) {
        m_lastError = m_firstError.isEmpty() ? tr("No recent videos found.") : m_firstError;
        setStatusText(m_lastError);
        emit loadError(m_lastError);
    } else if (m_failed > 0) {
        m_lastError = tr("%1 of %2 channel(s) failed. Showing %3 video(s).").arg(m_failed).arg(m_done).arg(m_videoCount);
        setStatusText(m_lastError);
        emit loadError(m_lastError);
    } else {
        m_lastError.clear();
        setStatusText(tr("%n video(s) from %1 channel(s).", nullptr, m_videoCount).arg(m_done));
    }
    m_pending.clear();
    emit videosChanged();
    emit progressChanged();
}

void HomeFeedBackend::setLoading(bool value)
{
    if (m_loading == value)
        return;
    m_loading = value;
    emit loadingChanged();
}

void HomeFeedBackend::setStatusText(const QString &text)
{
    if (m_statusText == text)
        return;
    m_statusText = text;
    emit statusTextChanged();
}
