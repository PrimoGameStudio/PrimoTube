#include "channelbackend.h"
#include "channelutils.h"
#include "videomodel.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QUrl>

ChannelBackend::ChannelBackend(VideoModel *model, QObject *parent)
    : QObject(parent)
    , m_model(model)
    , m_headerProcess(new QProcess(this))
    , m_videosProcess(new QProcess(this))
    , m_net(new QNetworkAccessManager(this))
{
    connect(m_headerProcess, &QProcess::finished, this, &ChannelBackend::onHeaderFinished);
    connect(m_headerProcess, &QProcess::errorOccurred, this, &ChannelBackend::onProcessError);
    connect(m_videosProcess, &QProcess::finished, this, &ChannelBackend::onVideosFinished);
    connect(m_videosProcess, &QProcess::errorOccurred, this, &ChannelBackend::onProcessError);
}

bool ChannelBackend::isChannelUrl(const QString &query)
{
    return ChannelUtils::isChannelUrl(query);
}

QString ChannelBackend::normalizeChannelUrl(const QString &query)
{
    return ChannelUtils::normalizeChannelUrl(query);
}

QString ChannelBackend::channelVideosUrl(const QString &query)
{
    return ChannelUtils::channelVideosUrl(query);
}

void ChannelBackend::openChannel(const QString &channelIdOrUrlOrHandle)
{
    const QString input = channelIdOrUrlOrHandle.trimmed();
    if (input.isEmpty())
        return;
    // A bare UC… channel ID (24 chars starting with UC) is also accepted.
    QString canonical = normalizeChannelUrl(input);
    if (canonical.isEmpty()) {
        static const QRegularExpression ucId(QStringLiteral("^UC[\\w-]{22}$"));
        if (ucId.match(input).hasMatch())
            canonical = QStringLiteral("https://www.youtube.com/channel/%1").arg(input);
    }
    if (canonical.isEmpty()) {
        fail(tr("That doesn't look like a channel link or handle."));
        return;
    }
    // Remember the latest request even when queued, so stacked pages can
    // tell the shared backend moved on to another channel.
    m_currentInput = input;
    if (m_headerProcess->state() != QProcess::NotRunning
        || m_videosProcess->state() != QProcess::NotRunning) {
        m_pendingInput = input;
        m_hasPending = true;
        if (m_headerProcess->state() != QProcess::NotRunning)
            m_headerProcess->kill();
        if (m_videosProcess->state() != QProcess::NotRunning)
            m_videosProcess->kill();
        return;
    }
    m_cancelled = false;
    ++m_generation;
    abortProfileFetch();
    m_channelUrl = canonical;
    m_videosUrl = canonical + QStringLiteral("/videos");
    m_channelId.clear();
    m_channelTitle.clear();
    m_channelHandle.clear();
    m_description.clear();
    m_avatarUrl.clear();
    m_subscriberCount = -1;
    m_subscriberText.clear();
    m_fetchedCount = 0;
    m_pendingLoadMore = false;
    m_model->clear();
    m_lastError.clear();
    setHasMore(false);
    setLoading(true);
    setStatusText(tr("Loading channel…"));
    fetchHeader();
}

void ChannelBackend::loadMore()
{
    if (m_videosUrl.isEmpty())
        return;
    if (m_fetchedCount >= kMaxTotal || !m_hasMore)
        return;
    // Header fetch doubles as "loading" until the first videos page arrives;
    // queue the request instead of dropping it during header load.
    if (m_headerProcess->state() != QProcess::NotRunning
        || m_videosProcess->state() != QProcess::NotRunning) {
        // Queue one follow-up page instead of silently dropping the request.
        m_pendingLoadMore = true;
        return;
    }
    if (m_loading)
        return;
    const int start = m_fetchedCount + 1;
    const int end = qMin(m_fetchedCount + kPageSize, kMaxTotal);
    if (end < start)
        return;
    m_cancelled = false;
    setLoading(true);
    setStatusText(tr("Loading more videos… (%1–%2)").arg(start).arg(end));
    fetchVideos(start, end);
}

void ChannelBackend::cancel()
{
    m_cancelled = true;
    m_hasPending = false;
    m_pendingLoadMore = false;
    ++m_generation; // invalidate stale finished() payloads
    abortProfileFetch();
    if (m_headerProcess->state() != QProcess::NotRunning)
        m_headerProcess->kill();
    if (m_videosProcess->state() != QProcess::NotRunning)
        m_videosProcess->kill();
    setLoading(false);
    setStatusText(tr("Channel loading cancelled."));
}

void ChannelBackend::fetchHeader()
{
    // Single-entry flat dump on the videos tab: fast and reliable.
    m_headerGen = m_generation;
    m_headerProcess->start(ChannelUtils::ytdlpExecutable(),
                           QStringList{
                               m_videosUrl,
                               QStringLiteral("--dump-json"),
                               QStringLiteral("--flat-playlist"),
                               QStringLiteral("--no-warnings"),
                               QStringLiteral("--socket-timeout"), QStringLiteral("20"),
                               QStringLiteral("--playlist-end"), QStringLiteral("1"),
                           });
}

void ChannelBackend::fetchVideos(int start, int end)
{
    m_videosGen = m_generation;
    m_videosProcess->start(ChannelUtils::ytdlpExecutable(),
                           QStringList{
                               m_videosUrl,
                               QStringLiteral("--dump-json"),
                               QStringLiteral("--flat-playlist"),
                               QStringLiteral("--no-warnings"),
                               QStringLiteral("--socket-timeout"), QStringLiteral("20"),
                               QStringLiteral("--playlist-start"), QString::number(start),
                               QStringLiteral("--playlist-end"), QString::number(end),
                           });
}

void ChannelBackend::onHeaderFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    if (m_hasPending) {
        m_hasPending = false;
        const QString next = m_pendingInput;
        m_pendingInput.clear();
        // Reset running state so openChannel() proceeds.
        setLoading(false);
        openChannel(next);
        return;
    }
    if (m_cancelled) {
        m_cancelled = false;
        return;
    }
    if (m_headerGen != m_generation) {
        // Stale payload from a killed run superseded by openChannel/cancel.
        return;
    }
    if (exitStatus != QProcess::NormalExit || exitCode != 0) {
        const QString stderrText = QString::fromUtf8(m_headerProcess->readAllStandardError()).trimmed();
        fail(stderrText.isEmpty() ? tr("Could not load channel (yt-dlp exit code %1).").arg(exitCode)
                                  : stderrText.split(QLatin1Char('\n')).last());
        return;
    }
    const QByteArray out = m_headerProcess->readAllStandardOutput();
    QJsonObject first;
    for (const QByteArray &line : out.split('\n')) {
        const QByteArray trimmed = line.trimmed();
        if (trimmed.isEmpty())
            continue;
        QJsonParseError err{};
        const QJsonDocument doc = QJsonDocument::fromJson(trimmed, &err);
        if (err.error == QJsonParseError::NoError && doc.isObject()) {
            first = doc.object();
            break;
        }
    }
    // Header fields from video entry: use channel/uploader (avoid video title).
    m_channelTitle = first.value(QStringLiteral("playlist_channel")).toString();
    if (m_channelTitle.isEmpty())
        m_channelTitle = first.value(QStringLiteral("channel")).toString();
    if (m_channelTitle.isEmpty())
        m_channelTitle = first.value(QStringLiteral("uploader")).toString();
    m_channelId = first.value(QStringLiteral("playlist_channel_id")).toString();
    if (m_channelId.isEmpty())
        m_channelId = first.value(QStringLiteral("channel_id")).toString();
    if (m_channelId.isEmpty())
        m_channelId = first.value(QStringLiteral("uploader_id")).toString();
    // Derive handle from URL when a /@handle form was used.
    const QString lowerUrl = m_channelUrl.toLower();
    const int atIdx = lowerUrl.indexOf(QStringLiteral("/@"));
    if (atIdx >= 0)
        m_channelHandle = m_channelUrl.mid(atIdx + 1);
    QString chanUrl = first.value(QStringLiteral("channel_url")).toString();
    if (chanUrl.isEmpty())
        chanUrl = first.value(QStringLiteral("uploader_url")).toString();
    if (!chanUrl.isEmpty() && chanUrl.startsWith(QStringLiteral("http"))) {
        // Keep the handle form (/…/@name) when the user opened one;
        // otherwise prefer the canonical URL from yt-dlp.
        const bool openedHandle = m_channelUrl.contains(QStringLiteral("/@"));
        if (!openedHandle)
            m_channelUrl = chanUrl;
    }
    if (!m_channelId.isEmpty() && !m_channelUrl.contains(m_channelId)
        && !m_channelUrl.contains(QStringLiteral("/@")) && !m_channelUrl.contains(QStringLiteral("/c/"))
        && !m_channelUrl.contains(QStringLiteral("/user/")))
        m_channelUrl = QStringLiteral("https://www.youtube.com/channel/%1").arg(m_channelId);
    // Note: when the user opened a /@handle URL we keep it as-is;
    // channelId is stored separately (see channelChanged).
    m_videosUrl = m_channelUrl + QStringLiteral("/videos");
    // Avoid setting channel description/avatar from first video
    m_description.clear();
    m_avatarUrl = first.value(QStringLiteral("uploader_avatar")).toString();
    m_subscriberCount = -1;
    m_subscriberText.clear();
    for (const char *key : {"channel_follower_count", "subscriber_count"}) {
        const QJsonValue v = first.value(QString::fromLatin1(key));
        if (!v.isUndefined() && !v.isNull()) {
            m_subscriberCount = static_cast<qint64>(v.toDouble(-1));
            break;
        }
    }
    const QString formatted = ChannelUtils::formatCount(m_subscriberCount);
    if (!formatted.isEmpty())
        m_subscriberText = tr("%1 subscribers").arg(formatted);
    if (m_channelTitle.isEmpty())
        m_channelTitle = ChannelUtils::cleanChannelName(m_channelUrl, m_channelHandle);
    emit channelChanged();
    // Best-effort profile fetch: flat-playlist JSON carries no avatar,
    // description, or subscriber count — the channel page HTML does.
    fetchChannelProfile();
    // Now load the first page of uploads.
    setStatusText(tr("Loading channel videos…"));
    fetchVideos(1, kPageSize);
}

void ChannelBackend::onVideosFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    if (m_hasPending) {
        m_hasPending = false;
        const QString next = m_pendingInput;
        m_pendingInput.clear();
        setLoading(false);
        openChannel(next);
        return;
    }
    setLoading(false);
    if (m_cancelled) {
        m_cancelled = false;
        return;
    }
    if (m_videosGen != m_generation) {
        // Stale payload from a superseded run; ignore.
        return;
    }
    if (exitStatus != QProcess::NormalExit || exitCode != 0) {
        const QString stderrText = QString::fromUtf8(m_videosProcess->readAllStandardError()).trimmed();
        // If we already show a header, keep it and report videos error inline.
        const QString msg = stderrText.isEmpty()
            ? tr("Could not load channel videos (yt-dlp exit code %1).").arg(exitCode)
            : stderrText.split(QLatin1Char('\n')).last();
        if (m_fetchedCount == 0 && m_channelTitle.isEmpty()) {
            fail(msg);
        } else {
            m_lastError = msg;
            setStatusText(msg);
            emit loadError(msg);
        }
        return;
    }
    const QByteArray out = m_videosProcess->readAllStandardOutput();
    // Note: m_model is cleared in openChannel(), so every videos page appends.
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
        if (item.channel.isEmpty())
            item.channel = m_channelTitle;
        item.channelId = obj.value(QStringLiteral("playlist_channel_id")).toString();
        if (item.channelId.isEmpty())
            item.channelId = obj.value(QStringLiteral("channel_id")).toString();
        if (item.channelId.isEmpty())
            item.channelId = obj.value(QStringLiteral("uploader_id")).toString();
        if (item.channelId.isEmpty())
            item.channelId = m_channelId;
        item.channelUrl = obj.value(QStringLiteral("channel_url")).toString();
        if (item.channelUrl.isEmpty())
            item.channelUrl = obj.value(QStringLiteral("uploader_url")).toString();
        if (item.channelUrl.isEmpty() && !item.channelId.isEmpty())
            item.channelUrl = QStringLiteral("https://www.youtube.com/channel/%1").arg(item.channelId);
        if (item.channelUrl.isEmpty())
            item.channelUrl = m_channelUrl;
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
        // Backfill header from the first video when the header dump was empty.
        if (m_channelTitle.isEmpty() && !item.channel.isEmpty()) {
            m_channelTitle = item.channel;
            m_channelId = item.channelId;
            m_channelUrl = item.channelUrl;
            emit channelChanged();
        }
        if (item.videoId.isEmpty() || item.title.isEmpty())
            continue;
        m_model->addVideo(item);
        ++count;
    }
    // The header fetch consumed playlist entry #1 as metadata only; the
    // first videos page re-fetches 1..10 (one redundant entry over the
    // network, but the model itself holds no duplicates since the header
    // entry is never inserted).
    m_fetchedCount += count;
    const bool more = (m_fetchedCount < kMaxTotal) && (count > 0);
    setHasMore(more);
    if (m_fetchedCount == 0) {
        setStatusText(tr("No uploads found for this channel."));
    } else {
        setStatusText(tr("%n video(s) loaded.", nullptr, m_fetchedCount));
    }
    emit videosChanged();
    emit channelLoaded();
    if (m_pendingLoadMore) {
        m_pendingLoadMore = false;
        loadMore();
    }
}

void ChannelBackend::onProcessError(QProcess::ProcessError error)
{
    Q_UNUSED(error)
    const bool headerFailed = m_headerProcess->error() == QProcess::FailedToStart;
    const bool videosFailed = m_videosProcess->error() == QProcess::FailedToStart;
    if (!headerFailed && !videosFailed)
        return;
    if (m_hasPending) {
        m_hasPending = false;
        const QString next = m_pendingInput;
        m_pendingInput.clear();
        setLoading(false);
        openChannel(next);
        return;
    }
    fail(tr("Could not start yt-dlp. Is it installed and in PATH?"));
}

void ChannelBackend::fail(const QString &message)
{
    m_cancelled = false;
    setLoading(false);
    m_lastError = message;
    setStatusText(message);
    emit loadError(message);
}

void ChannelBackend::abortProfileFetch()
{
    ++m_profileGen; // invalidate any in-flight reply
    if (m_profileReply) {
        m_profileReply->abort();
        m_profileReply->deleteLater();
        m_profileReply = nullptr;
    }
}

void ChannelBackend::fetchChannelProfile()
{
    abortProfileFetch();
    if (m_channelUrl.isEmpty())
        return;
    const QUrl url(m_channelUrl);
    if (!url.isValid() || !ChannelUtils::isYouTubeHost(url.host()))
        return;
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader, QByteArrayLiteral("Mozilla/5.0"));
    req.setTransferTimeout(8000);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    m_profileGen = m_generation;
    QNetworkReply *reply = m_net->get(req);
    m_profileReply = reply;
    connect(reply, &QNetworkReply::finished, this, &ChannelBackend::onProfileFinished);
}

void ChannelBackend::onProfileFinished()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply *>(sender());
    if (!reply)
        return;
    const bool isCurrent = (m_profileReply == reply) && (m_profileGen == m_generation);
    m_profileReply = nullptr;
    reply->deleteLater();
    if (!isCurrent)
        return; // superseded by openChannel/cancel
    if (reply->error() != QNetworkReply::NoError)
        return; // best-effort: keep placeholder
    QByteArray html = reply->read(kMaxProfileBytes);
    if (html.isEmpty())
        return;
    const QString text = QString::fromUtf8(html);
    bool changed = false;
    const QString avatar = ChannelUtils::pickChannelAvatar(text);
    if (!avatar.isEmpty() && avatar != m_avatarUrl) {
        m_avatarUrl = avatar;
        changed = true;
    }
    if (m_description.isEmpty()) {
        const QString desc = ChannelUtils::pickChannelDescription(text);
        if (!desc.isEmpty()) {
            m_description = desc;
            changed = true;
        }
    }
    if (m_subscriberText.isEmpty()) {
        const QString subs = ChannelUtils::pickChannelSubscriberText(text);
        if (!subs.isEmpty()) {
            m_subscriberText = subs;
            changed = true;
        }
    }
    if (changed)
        emit channelChanged();
}

void ChannelBackend::setLoading(bool value)
{
    if (m_loading == value)
        return;
    m_loading = value;
    emit loadingChanged();
}

void ChannelBackend::setStatusText(const QString &text)
{
    if (m_statusText == text)
        return;
    m_statusText = text;
    emit statusTextChanged();
}

void ChannelBackend::setHasMore(bool value)
{
    if (m_hasMore == value)
        return;
    m_hasMore = value;
    emit hasMoreChanged();
}
