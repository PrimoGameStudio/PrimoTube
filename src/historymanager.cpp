#include "historymanager.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

HistoryManager::HistoryManager(bool isFavorites, QObject *parent)
    : QAbstractListModel(parent)
    , m_isFavorites(isFavorites)
    , m_settings()
{
    load();
}

int HistoryManager::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return m_items.size();
}

QVariant HistoryManager::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_items.size())
        return {};
    const VideoItem &item = m_items.at(index.row());
    switch (role) {
    case VideoIdRole: return item.videoId;
    case TitleRole: return item.title;
    case ChannelRole: return item.channel;
    case ChannelIdRole: return item.channelId;
    case ChannelUrlRole: return item.channelUrl;
    case ThumbnailRole: return item.thumbnail;
    case WatchUrlRole: return item.watchUrl;
    case DurationRole: return item.duration;
    case DurationStringRole: return item.durationString;
    case DescriptionRole: return item.description;
    default: return {};
    }
}

QHash<int, QByteArray> HistoryManager::roleNames() const
{
    return {
        {VideoIdRole, "videoId"},
        {TitleRole, "title"},
        {ChannelRole, "channel"},
        {ChannelIdRole, "channelId"},
        {ChannelUrlRole, "channelUrl"},
        {ThumbnailRole, "thumbnail"},
        {WatchUrlRole, "watchUrl"},
        {DurationRole, "duration"},
        {DurationStringRole, "durationString"},
        {DescriptionRole, "description"},
    };
}

void HistoryManager::addItem(const QString &videoId, const QString &title, const QString &channel,
                             const QString &thumbnail, const QString &watchUrl,
                             const QString &durationString, int duration,
                             const QString &description, const QString &channelId,
                             const QString &channelUrl)
{
    const QString cleanId = videoId.trimmed();
    if (cleanId.isEmpty() || cleanId.size() > 128)
        return;

    // Remove existing if present (to move to top for history, or update)
    for (int i = 0; i < m_items.size(); ++i) {
        if (m_items[i].videoId == cleanId) {
            beginRemoveRows(QModelIndex(), i, i);
            m_items.removeAt(i);
            endRemoveRows();
            break;
        }
    }

    beginInsertRows(QModelIndex(), 0, 0);
    VideoItem item;
    item.videoId = cleanId;
    item.title = title.left(500);
    item.channel = channel.left(500);
    item.thumbnail = thumbnail.trimmed().left(2048);
    item.watchUrl = watchUrl.trimmed().left(2048);
    item.durationString = durationString.left(32);
    item.duration = qMax(0, duration);
    item.description = description.left(5000);
    item.channelId = channelId.trimmed().left(128);
    item.channelUrl = channelUrl.trimmed().left(2048);
    m_items.prepend(item);
    endInsertRows();

    // Limit list size (separate notification — Qt forbids nested
    // beginInsertRows/beginRemoveRows without an end in between).
    const int cap = m_isFavorites ? kMaxFavorites : kMaxHistory;
    if (m_items.size() > cap) {
        beginRemoveRows(QModelIndex(), cap, m_items.size() - 1);
        m_items.resize(cap);
        endRemoveRows();
    }
    save();
}

void HistoryManager::removeItem(const QString &videoId)
{
    for (int i = 0; i < m_items.size(); ++i) {
        if (m_items[i].videoId == videoId) {
            beginRemoveRows(QModelIndex(), i, i);
            m_items.removeAt(i);
            endRemoveRows();
            save();
            break;
        }
    }
}

void HistoryManager::clear()
{
    if (m_items.isEmpty())
        return;
    beginResetModel();
    m_items.clear();
    endResetModel();
    save();
}

bool HistoryManager::contains(const QString &videoId) const
{
    for (const auto &item : m_items) {
        if (item.videoId == videoId)
            return true;
    }
    return false;
}

void HistoryManager::load()
{
    const QString key = m_isFavorites ? QStringLiteral("favoritesList") : QStringLiteral("historyList");
    const QString jsonStr = m_settings.value(key).toString();
    if (jsonStr.isEmpty())
        return;

    QJsonDocument doc = QJsonDocument::fromJson(jsonStr.toUtf8());
    if (!doc.isArray())
        return;

    const int cap = m_isFavorites ? kMaxFavorites : kMaxHistory;
    m_items.clear();
    const QJsonArray arr = doc.array();
    for (const auto &v : arr) {
        if (m_items.size() >= cap)
            break;
        const QJsonObject obj = v.toObject();
        VideoItem item;
        item.videoId = obj.value(QStringLiteral("videoId")).toString().trimmed();
        if (item.videoId.isEmpty() || item.videoId.size() > 128)
            continue;
        item.title = obj.value(QStringLiteral("title")).toString().left(500);
        item.channel = obj.value(QStringLiteral("channel")).toString().left(500);
        item.thumbnail = obj.value(QStringLiteral("thumbnail")).toString().left(2048);
        item.watchUrl = obj.value(QStringLiteral("watchUrl")).toString().left(2048);
        item.durationString = obj.value(QStringLiteral("durationString")).toString().left(32);
        item.duration = obj.value(QStringLiteral("duration")).toInt(0);
        item.description = obj.value(QStringLiteral("description")).toString().left(5000);
        item.channelId = obj.value(QStringLiteral("channelId")).toString().left(128);
        item.channelUrl = obj.value(QStringLiteral("channelUrl")).toString().left(2048);
        m_items.append(item);
    }
}

void HistoryManager::save() const
{
    QJsonArray arr;
    for (const auto &item : m_items) {
        QJsonObject obj;
        obj[QStringLiteral("videoId")] = item.videoId;
        obj[QStringLiteral("title")] = item.title;
        obj[QStringLiteral("channel")] = item.channel;
        obj[QStringLiteral("thumbnail")] = item.thumbnail;
        obj[QStringLiteral("watchUrl")] = item.watchUrl;
        obj[QStringLiteral("durationString")] = item.durationString;
        obj[QStringLiteral("duration")] = item.duration;
        obj[QStringLiteral("description")] = item.description;
        obj[QStringLiteral("channelId")] = item.channelId;
        obj[QStringLiteral("channelUrl")] = item.channelUrl;
        arr.append(obj);
    }
    QJsonDocument doc(arr);
    const QString key = m_isFavorites ? QStringLiteral("favoritesList") : QStringLiteral("historyList");
    m_settings.setValue(key, QString::fromUtf8(doc.toJson(QJsonDocument::Compact)));
}

QString HistoryManager::exportJson() const
{
    QJsonArray arr;
    for (const auto &item : m_items) {
        QJsonObject obj;
        obj[QStringLiteral("videoId")] = item.videoId;
        obj[QStringLiteral("title")] = item.title;
        obj[QStringLiteral("channel")] = item.channel;
        obj[QStringLiteral("thumbnail")] = item.thumbnail;
        obj[QStringLiteral("watchUrl")] = item.watchUrl;
        obj[QStringLiteral("durationString")] = item.durationString;
        obj[QStringLiteral("duration")] = item.duration;
        obj[QStringLiteral("description")] = item.description;
        obj[QStringLiteral("channelId")] = item.channelId;
        obj[QStringLiteral("channelUrl")] = item.channelUrl;
        arr.append(obj);
    }
    return QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Indented));
}

bool HistoryManager::importJson(const QString &jsonStr)
{
    if (jsonStr.size() > kMaxImportBytes)
        return false;
    const QJsonDocument doc = QJsonDocument::fromJson(jsonStr.toUtf8());
    if (!doc.isArray())
        return false;
    const int cap = m_isFavorites ? kMaxFavorites : kMaxHistory;
    beginResetModel();
    m_items.clear();
    for (const auto &v : doc.array()) {
        if (m_items.size() >= cap)
            break;
        const QJsonObject obj = v.toObject();
        VideoItem item;
        item.videoId = obj.value(QStringLiteral("videoId")).toString().trimmed();
        if (item.videoId.isEmpty() || item.videoId.size() > 128)
            continue;
        item.title = obj.value(QStringLiteral("title")).toString().left(500);
        item.channel = obj.value(QStringLiteral("channel")).toString().left(500);
        item.thumbnail = obj.value(QStringLiteral("thumbnail")).toString().left(2048);
        item.watchUrl = obj.value(QStringLiteral("watchUrl")).toString().left(2048);
        item.durationString = obj.value(QStringLiteral("durationString")).toString().left(32);
        item.duration = obj.value(QStringLiteral("duration")).toInt(0);
        item.description = obj.value(QStringLiteral("description")).toString().left(5000);
        item.channelId = obj.value(QStringLiteral("channelId")).toString().left(128);
        item.channelUrl = obj.value(QStringLiteral("channelUrl")).toString().left(2048);
        m_items.append(item);
    }
    endResetModel();
    save();
    return true;
}
