#include "searchbackend.h"
#include "channelutils.h"
#include "videomodel.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>

namespace {
bool isDirectOrPlaylistQuery(const QString &q)
{
    if (ChannelUtils::isChannelUrl(q))
        return true;
    // Only YouTube URLs are treated as direct yt-dlp targets. Any other
    // scheme/host (file://, intranet, generic https://) stays as plain
    // search text so it never reaches yt-dlp as a URL spec.
    const bool looksLikeUrl = q.contains(QStringLiteral("://"))
        || q.startsWith(QStringLiteral("https://")) || q.startsWith(QStringLiteral("http://"))
        || q.startsWith(QStringLiteral("www.")) || q.startsWith(QStringLiteral("youtube.com"))
        || q.startsWith(QStringLiteral("youtu.be"));
    if (looksLikeUrl) {
        if (q.contains(QStringLiteral("://")))
            return ChannelUtils::isAllowedVideoUrl(q) || ChannelUtils::isChannelUrl(q);
        // Schemeless www./youtube.com/youtu.be prefix: verify the host so
        // www.evil.com/video is not treated as direct. isAllowedVideoUrl
        // returns true for any schemeless string (bare id), so parse here.
        const QUrl schemeless(QStringLiteral("https://") + q);
        if (ChannelUtils::isYouTubeHost(schemeless.host()))
            return true;
        return ChannelUtils::isChannelUrl(q);
    }
    if ((q.startsWith(QStringLiteral("PL")) || q.startsWith(QStringLiteral("RD"))
         || q.startsWith(QStringLiteral("UU")) || q.startsWith(QStringLiteral("OL")))
        && q.length() > 10 && !q.contains(QLatin1Char(' '))) {
        bool plausible = true;
        for (const QChar c : q) {
            if (!c.isLetterOrNumber() && c != QLatin1Char('_') && c != QLatin1Char('-')) {
                plausible = false;
                break;
            }
        }
        if (plausible)
            return true;
    }
    // "list=" alone is not enough (ordinary search text can contain it);
    // require URL markers or a spaceless token to treat as a playlist link.
    // A URL with list= is only direct when it is an allowed YouTube URL.
    if (q.contains(QStringLiteral("list="))) {
        if (q.contains(QStringLiteral("://")))
            return ChannelUtils::isAllowedVideoUrl(q) || ChannelUtils::isChannelUrl(q);
        if (!q.contains(QLatin1Char(' '))
            || q.startsWith(QStringLiteral("www.")) || q.startsWith(QStringLiteral("youtube.com"))
            || q.startsWith(QStringLiteral("youtu.be")))
            return true;
    }
    return false;
}
} // namespace

namespace {
QString pickChannelUrl(const QJsonObject &obj, const QString &channelId)
{
    QString url = obj.value(QStringLiteral("channel_url")).toString();
    if (url.isEmpty())
        url = obj.value(QStringLiteral("uploader_url")).toString();
    if (url.isEmpty() && !channelId.isEmpty())
        url = QStringLiteral("https://www.youtube.com/channel/%1").arg(channelId);
    return url;
}
} // namespace

bool SearchBackend::isChannelUrl(const QString &query)
{
    return ChannelUtils::isChannelUrl(query);
}

QString SearchBackend::channelVideosUrl(const QString &query)
{
    return ChannelUtils::channelVideosUrl(query);
}

SearchBackend::SearchBackend(VideoModel *model, QObject *parent)
    : QObject(parent)
    , m_model(model)
    , m_process(new QProcess(this))
{
    connect(m_process, &QProcess::finished, this, &SearchBackend::onProcessFinished);
    connect(m_process, &QProcess::errorOccurred, this, &SearchBackend::onProcessError);
}

void SearchBackend::search(const QString &query, int maxResults)
{
    const QString q = query.trimmed();
    if (q.isEmpty())
        return;
    const int n = qBound(1, maxResults, kMaxTotal);

    // Fully async: never block the UI thread. If a search is running,
    // queue this one and let finished() pick it up.
    if (m_process->state() != QProcess::NotRunning) {
        m_pendingStart = 1;
        m_pendingEnd = n;
        m_pendingAppend = false;
        m_hasPending = true;
        // Remember query so finished() can launch it.
        m_lastQuery = q;
        const bool direct = isDirectOrPlaylistQuery(q);
        m_lastIsDirect = direct;
        m_lastSpec = direct ? q : QStringLiteral("ytsearch%1:%2").arg(kMaxTotal).arg(q);
        m_process->kill();
        return;
    }

    m_userCancelled = false;
    m_lastQuery = q;
    m_lastIsDirect = isDirectOrPlaylistQuery(q);
    m_lastSpec = m_lastIsDirect ? q : QStringLiteral("ytsearch%1:%2").arg(kMaxTotal).arg(q);
    m_fetchedCount = 0;
    m_model->clear();
    m_lastError.clear();
    setHasMore(false);
    setSearching(true);
    setStatusText(tr("Searching for \"%1\"…").arg(q));
    beginSearch(1, n, false);
}

void SearchBackend::loadMore()
{
    if (m_searching || m_lastQuery.isEmpty())
        return;
    if (m_fetchedCount >= kMaxTotal)
        return;
    if (!m_hasMore)
        return;
    const int start = m_fetchedCount + 1;
    const int end = qMin(m_fetchedCount + kPageSize, kMaxTotal);
    if (end < start)
        return;

    if (m_process->state() != QProcess::NotRunning) {
        m_pendingStart = start;
        m_pendingEnd = end;
        m_pendingAppend = true;
        m_hasPending = true;
        m_process->kill();
        return;
    }
    m_userCancelled = false;
    setSearching(true);
    setStatusText(tr("Loading more results… (%1–%2)").arg(start).arg(end));
    beginSearch(start, end, true);
}

void SearchBackend::cancel()
{
    m_userCancelled = true;
    m_hasPending = false;
    if (m_process->state() != QProcess::NotRunning)
        m_process->kill();
    // finished() will clear the searching flag; clear immediately too
    // so the UI responds without waiting for the kill to propagate.
    setSearching(false);
    setStatusText(tr("Search cancelled."));
}

void SearchBackend::beginSearch(int playlistStart, int playlistEnd, bool append)
{
    m_append = append;
    m_lastWindow = qMax(1, playlistEnd - playlistStart + 1);
    QStringList args{
        m_lastSpec,
        QStringLiteral("--dump-json"),
        QStringLiteral("--flat-playlist"),
        QStringLiteral("--no-warnings"),
        QStringLiteral("--socket-timeout"), QStringLiteral("20"),
        QStringLiteral("--playlist-start"), QString::number(playlistStart),
        QStringLiteral("--playlist-end"), QString::number(playlistEnd),
    };
    if (!m_lastIsDirect) {
        args.append(QStringLiteral("--no-playlist"));
    }
    m_process->start(ChannelUtils::ytdlpExecutable(), args);
}

void SearchBackend::startPendingIfAny()
{
    if (!m_hasPending)
        return;
    m_hasPending = false;
    m_userCancelled = false;
    if (!m_pendingAppend) {
        m_fetchedCount = 0;
        m_model->clear();
        m_lastError.clear();
        setHasMore(false);
        setStatusText(tr("Searching for \"%1\"…").arg(m_lastQuery));
    } else {
        setStatusText(tr("Loading more results… (%1–%2)").arg(m_pendingStart).arg(m_pendingEnd));
    }
    setSearching(true);
    beginSearch(m_pendingStart, m_pendingEnd, m_pendingAppend);
}

void SearchBackend::onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    // A new request arrived while we were running — launch it now.
    if (m_hasPending) {
        startPendingIfAny();
        return;
    }
    setSearching(false);
    if (m_userCancelled) {
        m_userCancelled = false;
        return;
    }
    if (exitStatus != QProcess::NormalExit || exitCode != 0) {
        const QString stderrText = QString::fromUtf8(m_process->readAllStandardError()).trimmed();
        m_lastError = stderrText.isEmpty()
            ? tr("Search failed (yt-dlp exit code %1).").arg(exitCode)
            : stderrText.split(QLatin1Char('\n')).last();
        emit searchError(m_lastError);
        setStatusText(m_lastError);
        return;
    }

    const QByteArray out = m_process->readAllStandardOutput();
    int count = 0;
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
        item.channelId = obj.value(QStringLiteral("playlist_channel_id")).toString();
        if (item.channelId.isEmpty())
            item.channelId = obj.value(QStringLiteral("channel_id")).toString();
        if (item.channelId.isEmpty())
            item.channelId = obj.value(QStringLiteral("uploader_id")).toString();
        item.channelUrl = pickChannelUrl(obj, item.channelId);
        item.duration = static_cast<int>(obj.value(QStringLiteral("duration")).toDouble(0));
        item.durationString = obj.value(QStringLiteral("duration_string")).toString();
        if (item.durationString.isEmpty())
            item.durationString = ChannelUtils::formatDuration(item.duration);
        item.thumbnail = ChannelUtils::pickVideoThumbnail(obj, item.videoId);
        item.description = obj.value(QStringLiteral("description")).toString();
        item.timestamp = ChannelUtils::parseVideoTimestamp(obj);
        item.uploadDate = ChannelUtils::formatUploadDate(obj, item.timestamp);
        item.watchUrl = obj.value(QStringLiteral("webpage_url")).toString();
        if (item.watchUrl.isEmpty() && !item.videoId.isEmpty())
            item.watchUrl = QStringLiteral("https://www.youtube.com/watch?v=%1").arg(item.videoId);
        if (item.videoId.isEmpty() || item.title.isEmpty())
            continue;

        m_model->addVideo(item);
        ++count;
    }

    m_fetchedCount += count;
    // More pages exist only if we got a full window and haven't hit the cap.
    // A short window means the backend is exhausted.
    const int window = m_lastWindow > 0 ? m_lastWindow : kPageSize;
    const bool more = (m_fetchedCount < kMaxTotal) && (count >= window) && (count > 0);
    setHasMore(more);

    if (!m_append && count == 0) {
        setStatusText(tr("No results found."));
    } else if (m_append) {
        setStatusText(tr("%1 video(s) loaded.").arg(m_fetchedCount));
    } else {
        setStatusText(tr("%n video(s) found.", nullptr, m_fetchedCount));
    }
    emit searchFinished(m_fetchedCount);
}

void SearchBackend::onProcessError(QProcess::ProcessError error)
{
    Q_UNUSED(error)
    // The finished() handler reports details; handle the "failed to start" case here.
    if (m_process->error() == QProcess::FailedToStart) {
        if (m_hasPending) {
            startPendingIfAny();
            return;
        }
        m_lastError = tr("Could not start yt-dlp. Is it installed and in PATH?");
        setSearching(false);
        emit searchError(m_lastError);
        setStatusText(m_lastError);
    }
}

void SearchBackend::setSearching(bool value)
{
    if (m_searching == value)
        return;
    m_searching = value;
    emit searchingChanged();
}

void SearchBackend::setStatusText(const QString &text)
{
    if (m_statusText == text)
        return;
    m_statusText = text;
    emit statusTextChanged();
}

void SearchBackend::setHasMore(bool value)
{
    if (m_hasMore == value)
        return;
    m_hasMore = value;
    emit hasMoreChanged();
}
