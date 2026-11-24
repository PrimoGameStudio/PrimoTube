#include "queuemanager.h"

#include <QRandomGenerator>
#include <algorithm>

QueueManager::QueueManager(QObject *parent)
    : QAbstractListModel(parent)
{
}

int QueueManager::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return m_items.size();
}

QVariant QueueManager::data(const QModelIndex &index, int role) const
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

QHash<int, QByteArray> QueueManager::roleNames() const
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

void QueueManager::enqueue(const QString &videoId, const QString &title, const QString &channel,
                           const QString &thumbnail, const QString &watchUrl,
                           const QString &durationString, int duration,
                           const QString &description, const QString &channelId,
                           const QString &channelUrl)
{
    VideoItem item;
    item.videoId = videoId.trimmed();
    item.title = title.trimmed();
    item.channel = channel.trimmed();
    item.thumbnail = thumbnail.trimmed();
    item.watchUrl = watchUrl.trimmed();
    item.durationString = durationString.trimmed();
    item.duration = duration;
    item.description = description;
    item.channelId = channelId.trimmed();
    item.channelUrl = channelUrl.trimmed();

    if (item.videoId.isEmpty())
        return;
    if (isFull())
        return;

    const bool wasEmpty = m_items.isEmpty();
    beginInsertRows(QModelIndex(), m_items.size(), m_items.size());
    m_items.append(item);
    endInsertRows();

    if (wasEmpty) {
        m_currentIndex = 0;
        emit currentIndexChanged();
    }
    emit queueChanged();
}

void QueueManager::enqueueNext(const QString &videoId, const QString &title, const QString &channel,
                               const QString &thumbnail, const QString &watchUrl,
                               const QString &durationString, int duration,
                               const QString &description, const QString &channelId,
                               const QString &channelUrl)
{
    VideoItem item;
    item.videoId = videoId.trimmed();
    item.title = title.trimmed();
    item.channel = channel.trimmed();
    item.thumbnail = thumbnail.trimmed();
    item.watchUrl = watchUrl.trimmed();
    item.durationString = durationString.trimmed();
    item.duration = duration;
    item.description = description;
    item.channelId = channelId.trimmed();
    item.channelUrl = channelUrl.trimmed();

    if (item.videoId.isEmpty())
        return;
    if (isFull())
        return;

    const int insertPos = (m_currentIndex >= 0 && m_currentIndex < m_items.size()) ? m_currentIndex + 1 : m_items.size();
    const bool wasEmpty = m_items.isEmpty();

    beginInsertRows(QModelIndex(), insertPos, insertPos);
    m_items.insert(insertPos, item);
    endInsertRows();

    if (wasEmpty) {
        m_currentIndex = 0;
        emit currentIndexChanged();
    }
    emit queueChanged();
}

void QueueManager::removeAt(int index)
{
    if (index < 0 || index >= m_items.size())
        return;
    beginRemoveRows(QModelIndex(), index, index);
    m_items.removeAt(index);
    endRemoveRows();

    if (m_items.isEmpty()) {
        m_currentIndex = -1;
        emit currentIndexChanged();
    } else if (m_currentIndex == index) {
        // Removed the current item: stay at the same position (now the next
        // item), clamping to the end. Index value may be unchanged but the
        // current item changed, so always notify.
        if (m_currentIndex >= m_items.size())
            m_currentIndex = m_items.size() - 1;
        emit currentIndexChanged();
    } else if (m_currentIndex > index) {
        m_currentIndex = qMax(0, m_currentIndex - 1);
        emit currentIndexChanged();
    }
    emit queueChanged();
}

void QueueManager::clear()
{
    if (m_items.isEmpty())
        return;
    beginResetModel();
    m_items.clear();
    m_currentIndex = -1;
    endResetModel();
    emit currentIndexChanged();
    emit queueChanged();
}

int QueueManager::indexOfVideo(const QString &videoId) const
{
    const QString id = videoId.trimmed();
    if (id.isEmpty())
        return -1;
    for (int i = 0; i < m_items.size(); ++i) {
        if (m_items.at(i).videoId == id)
            return i;
    }
    return -1;
}

void QueueManager::setCurrentIndex(int index)
{
    if (index < -1 || index >= m_items.size() || index == m_currentIndex)
        return;
    m_currentIndex = index;
    emit currentIndexChanged();
}

void QueueManager::playIndex(int index)
{
    if (index < 0 || index >= m_items.size())
        return;
    setCurrentIndex(index);
    const VideoItem &item = m_items.at(index);
    emit itemRequested(item.videoId, item.title, item.channel, item.channelId, item.channelUrl,
                       item.watchUrl, item.durationString, item.duration, item.thumbnail, item.description);
}

bool QueueManager::advance()
{
    if (m_items.isEmpty())
        return false;
    const int nextIdx = (m_currentIndex >= 0 && m_currentIndex < m_items.size()) ? m_currentIndex + 1 : 0;
    if (nextIdx >= m_items.size())
        return false;
    playIndex(nextIdx);
    return true;
}

QVariantMap QueueManager::currentItem() const
{
    if (m_currentIndex < 0 || m_currentIndex >= m_items.size())
        return {};
    const VideoItem &item = m_items.at(m_currentIndex);
    QVariantMap map;
    map[QStringLiteral("videoId")] = item.videoId;
    map[QStringLiteral("title")] = item.title;
    map[QStringLiteral("channel")] = item.channel;
    map[QStringLiteral("channelId")] = item.channelId;
    map[QStringLiteral("channelUrl")] = item.channelUrl;
    map[QStringLiteral("thumbnail")] = item.thumbnail;
    map[QStringLiteral("watchUrl")] = item.watchUrl;
    map[QStringLiteral("durationString")] = item.durationString;
    map[QStringLiteral("duration")] = item.duration;
    map[QStringLiteral("description")] = item.description;
    return map;
}

QList<VideoItem> QueueManager::collectValidItems(VideoModel *model)
{
    QList<VideoItem> valid;
    if (!model)
        return valid;
    // beginInsertRows must announce exactly the number of rows actually
    // appended (skipping empty videoId entries).
    for (int i = 0; i < model->rowCount(); ++i) {
        QModelIndex idx = model->index(i, 0);
        VideoItem item;
        item.videoId = model->data(idx, VideoModel::VideoIdRole).toString();
        item.title = model->data(idx, VideoModel::TitleRole).toString();
        item.channel = model->data(idx, VideoModel::ChannelRole).toString();
        item.channelId = model->data(idx, VideoModel::ChannelIdRole).toString();
        item.channelUrl = model->data(idx, VideoModel::ChannelUrlRole).toString();
        item.thumbnail = model->data(idx, VideoModel::ThumbnailRole).toString();
        item.watchUrl = model->data(idx, VideoModel::WatchUrlRole).toString();
        item.durationString = model->data(idx, VideoModel::DurationStringRole).toString();
        item.duration = model->data(idx, VideoModel::DurationRole).toInt();
        item.description = model->data(idx, VideoModel::DescriptionRole).toString();
        if (!item.videoId.isEmpty())
            valid.append(item);
    }
    return valid;
}

void QueueManager::enqueueModel(VideoModel *model)
{
    if (!model || model->rowCount() == 0)
        return;
    QList<VideoItem> valid = collectValidItems(model);
    if (valid.isEmpty() || isFull())
        return;
    // Respect the queue cap: only take what fits.
    if (m_items.size() + valid.size() > kMaxQueue)
        valid = valid.mid(0, kMaxQueue - m_items.size());
    if (valid.isEmpty())
        return;
    const bool wasEmpty = m_items.isEmpty();
    beginInsertRows(QModelIndex(), m_items.size(), m_items.size() + valid.size() - 1);
    for (const VideoItem &item : valid)
        m_items.append(item);
    endInsertRows();

    if (wasEmpty && !m_items.isEmpty()) {
        m_currentIndex = 0;
        emit currentIndexChanged();
    }
    emit queueChanged();
}

void QueueManager::shuffleModel(VideoModel *model)
{
    QList<VideoItem> items = collectValidItems(model);
    if (items.isEmpty())
        return;
    if (items.size() > kMaxQueue)
        items = items.mid(0, kMaxQueue);

    // "Shuffle All" replaces the queue with the whole feed in random order.
    if (!m_items.isEmpty()) {
        beginResetModel();
        m_items.clear();
        m_currentIndex = -1;
        endResetModel();
        emit currentIndexChanged();
        emit queueChanged();
    }

    // Fisher-Yates with Qt's per-thread generator (no srand/time seed).
    for (int i = items.size() - 1; i > 0; --i) {
        const int j = QRandomGenerator::global()->bounded(i + 1);
        std::swap(items[i], items[j]);
    }

    const int start = m_items.size();
    beginInsertRows(QModelIndex(), start, start + items.size() - 1);
    for (const VideoItem &item : items)
        m_items.append(item);
    endInsertRows();
    emit queueChanged();

    // Main.qml handles itemRequested via openPlayerWindow, so the shuffled
    // queue is preserved and the first item starts playing immediately.
    playIndex(0);
}
