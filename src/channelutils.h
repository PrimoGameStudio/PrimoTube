#pragma once

#include <QString>

class QJsonObject;

// Shared channel/playlist helpers used by both SearchBackend and
// ChannelBackend. Kept in one place so normalization rules cannot drift.
namespace ChannelUtils {

QString stripTrailingSlash(QString s);
QString normalizeChannelUrl(const QString &query);
bool isChannelUrl(const QString &query);
QString channelVideosUrl(const QString &query);

// Absolute yt-dlp executable when found in PATH (avoids relying on bare
// `PATH` lookup at every QProcess::start call site).
QString ytdlpExecutable();
// True for bare video ids or https URLs on YouTube hosts. Used to reject
// file:/// and intranet URLs before they reach yt-dlp/mpv.
bool isAllowedVideoUrl(const QString &idOrUrl);
// Strict YouTube host check (exact or subdomain). Rejects
// youtube.com.evil.com and evil.com/?youtube.com which substring
// matching would wrongly allow.
bool isYouTubeHost(const QString &host);

QString cleanChannelName(const QString &url, const QString &handle);
QString pickBestThumbnail(const QJsonObject &obj);
QString pickVideoThumbnail(const QJsonObject &obj, const QString &videoId);
// Channel page HTML parsers (lightweight avatar/description/subscriber
// lookup — the flat-playlist JSON carries none of these). Each returns {}
// when the field is absent.
QString pickChannelAvatar(const QString &html);
QString pickChannelDescription(const QString &html);
QString pickChannelSubscriberText(const QString &html);
QString formatDuration(int secs);
QString formatCount(qint64 n);
// Upload timestamp from a yt-dlp flat-playlist entry: prefers `timestamp`,
// falls back to `release_timestamp`, then `upload_date` ("yyyyMMdd").
// Returns 0 when unknown.
qint64 parseVideoTimestamp(const QJsonObject &obj);
// Display date ("yyyy-MM-dd") derived from timestamp/upload_date fields.
QString formatUploadDate(const QJsonObject &obj, qint64 timestamp);

} // namespace ChannelUtils
