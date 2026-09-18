#include "channelutils.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QTimeZone>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QUrl>

namespace ChannelUtils {

QString stripTrailingSlash(QString s)
{
    while (s.endsWith(QLatin1Char('/')) && s.size() > 1)
        s.chop(1);
    return s;
}

QString normalizeChannelUrl(const QString &query)
{
    QString q = query.trimmed();
    if (q.isEmpty())
        return {};
    if (q.startsWith(QLatin1Char('@')) && !q.contains(QLatin1Char(' ')) && !q.contains(QLatin1Char('/')))
        return QStringLiteral("https://www.youtube.com/%1").arg(q);
    // Strict host check: parse the URL and compare the host (exact or
    // subdomain) instead of substring matching, so
    // youtube.com.evil.com / evil.com/?youtube.com are rejected.
    QString forParse = q;
    if (!forParse.contains(QStringLiteral("://")))
        forParse = QStringLiteral("https://") + forParse;
    const QUrl url(forParse);
    if (!url.isValid())
        return {};
    if (!isYouTubeHost(url.host()))
        return {};
    const QString lower = q.toLower();
    const bool channelish = lower.contains(QStringLiteral("/channel/"))
        || lower.contains(QStringLiteral("/c/")) || lower.contains(QStringLiteral("/user/"))
        || lower.contains(QStringLiteral("/@")) || lower.contains(QStringLiteral("youtube.com/@"))
        || lower.contains(QStringLiteral("youtube.com/channel"))
        || lower.contains(QStringLiteral("youtube.com/c/"))
        || lower.contains(QStringLiteral("youtube.com/user/"));
    if (!channelish)
        return {};
    const int hashIdx = q.indexOf(QLatin1Char('#'));
    if (hashIdx >= 0)
        q = q.left(hashIdx);
    const int qIdx = q.indexOf(QLatin1Char('?'));
    if (qIdx >= 0)
        q = q.left(qIdx);
    q = stripTrailingSlash(q);
    for (const char *tab : {"videos", "featured", "shorts", "streams", "playlists", "about", "community", "channels"}) {
        const QString suffix = QStringLiteral("/") + QString::fromLatin1(tab);
        if (q.endsWith(suffix, Qt::CaseInsensitive))
            q = q.left(q.size() - suffix.size());
    }
    q = stripTrailingSlash(q);
    if (!q.startsWith(QStringLiteral("http")))
        q = QStringLiteral("https://") + q;
    return q;
}

bool isChannelUrl(const QString &query)
{
    return !normalizeChannelUrl(query).isEmpty();
}

QString channelVideosUrl(const QString &query)
{
    const QString base = normalizeChannelUrl(query);
    if (base.isEmpty())
        return {};
    return base + QStringLiteral("/videos");
}

QString ytdlpExecutable()
{
    const QString abs = QStandardPaths::findExecutable(QStringLiteral("yt-dlp"));
    if (!abs.isEmpty())
        return abs;
    return QStringLiteral("yt-dlp");
}

bool isYouTubeHost(const QString &host)
{
    QString h = host.trimmed().toLower();
    while (h.endsWith(QLatin1Char('.')))
        h.chop(1);
    if (h.isEmpty())
        return false;
    if (h == QLatin1String("youtube.com") || h.endsWith(QLatin1String(".youtube.com")))
        return true;
    if (h == QLatin1String("youtu.be") || h.endsWith(QLatin1String(".youtu.be")))
        return true;
    if (h == QLatin1String("youtube-nocookie.com")
        || h.endsWith(QLatin1String(".youtube-nocookie.com")))
        return true;
    return false;
}

bool isAllowedVideoUrl(const QString &idOrUrl)
{
    const QString s = idOrUrl.trimmed();
    if (s.isEmpty())
        return false;
    if (!s.contains(QStringLiteral("://")))
        return true; // bare video id
    const QUrl url(s);
    if (!url.isValid())
        return false;
    const QString scheme = url.scheme().toLower();
    if (scheme != QLatin1String("http") && scheme != QLatin1String("https"))
        return false;
    return isYouTubeHost(url.host());
}

QString cleanChannelName(const QString &url, const QString &handle)
{
    if (!handle.isEmpty())
        return handle.startsWith(QLatin1Char('@')) ? handle : (QStringLiteral("@") + handle);
    QString s = stripTrailingSlash(url);
    const int idx = s.lastIndexOf(QLatin1Char('/'));
    if (idx >= 0 && idx < s.size() - 1) {
        QString last = s.mid(idx + 1);
        if (!last.isEmpty() && !last.contains(QStringLiteral("youtube.com")))
            return last;
    }
    return s;
}

QString pickBestThumbnail(const QJsonObject &obj)
{
    const QJsonArray thumbs = obj.value(QStringLiteral("thumbnails")).toArray();
    QString best;
    int bestWidth = -1;
    for (const QJsonValue &v : thumbs) {
        const QJsonObject t = v.toObject();
        const QString url = t.value(QStringLiteral("url")).toString();
        const int w = t.value(QStringLiteral("width")).toInt(0);
        if (!url.isEmpty() && w >= bestWidth) {
            bestWidth = w;
            best = url;
        }
    }
    return best;
}

QString pickVideoThumbnail(const QJsonObject &obj, const QString &videoId)
{
    const QString best = pickBestThumbnail(obj);
    if (!best.isEmpty())
        return best;
    if (!videoId.isEmpty())
        return QStringLiteral("https://i.ytimg.com/vi/%1/hqdefault.jpg").arg(videoId);
    return {};
}

QString formatDuration(int secs)
{
    if (secs <= 0)
        return {};
    const int h = secs / 3600;
    const int m = (secs % 3600) / 60;
    const int s = secs % 60;
    if (h > 0)
        return QStringLiteral("%1:%2:%3")
            .arg(h)
            .arg(m, 2, 10, QLatin1Char('0'))
            .arg(s, 2, 10, QLatin1Char('0'));
    return QStringLiteral("%1:%2").arg(m).arg(s, 2, 10, QLatin1Char('0'));
}

QString formatCount(qint64 n)
{
    if (n < 0)
        return {};
    if (n < 1000)
        return QString::number(n);
    if (n < 1000000) {
        const double k = n / 1000.0;
        return (k >= 100 ? QString::number(qRound(k)) : QString::number(k, 'f', 1)) + QStringLiteral("K");
    }
    if (n < 1000000000) {
        const double m = n / 1000000.0;
        return (m >= 100 ? QString::number(qRound(m)) : QString::number(m, 'f', 1)) + QStringLiteral("M");
    }
    const double b = n / 1000000000.0;
    return (b >= 100 ? QString::number(qRound(b)) : QString::number(b, 'f', 1)) + QStringLiteral("B");
}

namespace {
// Unescape the subset of JSON string escapes YouTube embeds in page HTML
// (\/ \" \\ \n \uXXXX) so extracted URLs/descriptions are usable.
QString unescapeJsonString(QString s)
{
    s.replace(QStringLiteral("\\/"), QStringLiteral("/"));
    s.replace(QStringLiteral("\\\""), QStringLiteral("\""));
    s.replace(QStringLiteral("\\\\"), QStringLiteral("\\"));
    s.replace(QStringLiteral("\\n"), QStringLiteral("\n"));
    static const QRegularExpression unicode(QStringLiteral("\\\\u([0-9a-fA-F]{4})"));
    auto it = unicode.globalMatch(s);
    // Apply back-to-front so earlier offsets stay valid.
    QList<QRegularExpressionMatch> matches;
    while (it.hasNext())
        matches.append(it.next());
    for (int i = matches.size() - 1; i >= 0; --i) {
        const auto &m = matches.at(i);
        const QChar c(m.captured(1).toUShort(nullptr, 16));
        s.replace(m.capturedStart(0), m.capturedLength(0), c);
    }
    return s;
}

bool isUsableAvatarUrl(const QString &url)
{
    if (!url.startsWith(QStringLiteral("https://"), Qt::CaseInsensitive))
        return false;
    if (url.contains(QLatin1Char(' ')) || url.contains(QLatin1Char('"')) || url.contains(QLatin1Char('<')))
        return false;
    return true;
}
} // namespace

QString pickChannelAvatar(const QString &html)
{
    if (html.isEmpty())
        return {};
    // 1. og:image — the channel avatar on channel pages.
    static const QRegularExpression ogImage(
        QStringLiteral("<meta[^>]+property=[\"']og:image[\"'][^>]+content=[\"']([^\"']+)"),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch og = ogImage.match(html);
    if (og.hasMatch()) {
        const QString url = unescapeJsonString(og.captured(1)).trimmed();
        if (isUsableAvatarUrl(url))
            return url;
    }
    // 2. Embedded player-response avatar block.
    static const QRegularExpression avatarBlock(
        QStringLiteral("\"avatar\"\\s*:\\s*\\{\\s*\"thumbnails\"\\s*:\\s*\\[\\s*\\{\\s*\"url\"\\s*:\\s*\"([^\"]+)\""));
    const QRegularExpressionMatch ab = avatarBlock.match(html);
    if (ab.hasMatch()) {
        const QString url = unescapeJsonString(ab.captured(1)).trimmed();
        if (isUsableAvatarUrl(url))
            return url;
    }
    return {};
}

QString pickChannelDescription(const QString &html)
{
    if (html.isEmpty())
        return {};
    static const QRegularExpression ogDesc(
        QStringLiteral("<meta[^>]+(?:property|name)=[\"']og:description[\"'][^>]+content=[\"']([^\"']*)"),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch og = ogDesc.match(html);
    if (og.hasMatch()) {
        const QString desc = unescapeJsonString(og.captured(1)).trimmed();
        if (!desc.isEmpty())
            return desc.left(5000);
    }
    static const QRegularExpression shortDesc(
        QStringLiteral("\"shortDescription\"\\s*:\\s*\"((?:[^\"\\\\]|\\\\.)*)\""));
    const QRegularExpressionMatch sd = shortDesc.match(html);
    if (sd.hasMatch()) {
        const QString desc = unescapeJsonString(sd.captured(1)).trimmed();
        if (!desc.isEmpty())
            return desc.left(5000);
    }
    return {};
}

QString pickChannelSubscriberText(const QString &html)
{
    if (html.isEmpty())
        return {};
    // e.g. "subscriberCountText":{"accessibility":{...},"simpleText":"2.1M subscribers"}
    static const QRegularExpression subText(
        QStringLiteral("\"subscriberCountText\"\\s*:\\s*\\{.*?\"simpleText\"\\s*:\\s*\"((?:[^\"\\\\]|\\\\.)*)\""));
    const QRegularExpressionMatch m = subText.match(html);
    if (m.hasMatch()) {
        const QString text = unescapeJsonString(m.captured(1)).trimmed();
        if (!text.isEmpty())
            return text.left(64);
    }
    return {};
}

qint64 parseVideoTimestamp(const QJsonObject &obj)
{
    for (const char *key : {"timestamp", "release_timestamp"}) {
        const QJsonValue v = obj.value(QString::fromLatin1(key));
        if (v.isDouble()) {
            const qint64 ts = static_cast<qint64>(v.toDouble(0));
            if (ts > 0)
                return ts;
        } else if (v.isString()) {
            const qint64 ts = v.toString().trimmed().toLongLong();
            if (ts > 0)
                return ts;
        }
    }
    const QString uploadDate = obj.value(QStringLiteral("upload_date")).toString().trimmed();
    if (uploadDate.size() == 8) {
        const QDate d = QDate::fromString(uploadDate, QStringLiteral("yyyyMMdd"));
        if (d.isValid()) {
            const QDateTime dt(d, QTime(0, 0), QTimeZone(QTimeZone::UTC));
            if (dt.isValid())
                return dt.toSecsSinceEpoch();
        }
    }
    return 0;
}

QString formatUploadDate(const QJsonObject &obj, qint64 timestamp)
{
    if (timestamp > 0) {
        const QDateTime dt = QDateTime::fromSecsSinceEpoch(timestamp, QTimeZone(QTimeZone::UTC));
        if (dt.isValid())
            return dt.date().toString(Qt::ISODate);
    }
    const QString uploadDate = obj.value(QStringLiteral("upload_date")).toString().trimmed();
    if (uploadDate.size() == 8) {
        const QDate d = QDate::fromString(uploadDate, QStringLiteral("yyyyMMdd"));
        if (d.isValid())
            return d.toString(Qt::ISODate);
    }
    return {};
}

} // namespace ChannelUtils
