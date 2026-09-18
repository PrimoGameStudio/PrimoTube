#include "videomodel.h"

VideoModel::VideoModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int VideoModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return m_items.size();
}

QVariant VideoModel::data(const QModelIndex &index, int role) const
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
    case TimestampRole: return item.timestamp;
    case UploadDateRole: return item.uploadDate;
    default: return {};
    }
}

QHash<int, QByteArray> VideoModel::roleNames() const
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
        {TimestampRole, "timestamp"},
        {UploadDateRole, "uploadDate"},
    };
}

void VideoModel::clear()
{
    if (m_items.isEmpty())
        return;
    beginResetModel();
    m_items.clear();
    endResetModel();
}

void VideoModel::addVideo(const VideoItem &item)
{
    beginInsertRows(QModelIndex(), m_items.size(), m_items.size());
    m_items.append(item);
    endInsertRows();
}

void VideoModel::setItems(const QVector<VideoItem> &items)
{
    beginResetModel();
    m_items = items;
    endResetModel();
}
