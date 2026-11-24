#pragma once

#include <QAbstractListModel>
#include <QString>
#include <QVector>

struct VideoItem {
    QString videoId;
    QString title;
    QString channel;
    QString channelId;
    QString channelUrl;
    QString thumbnail;
    QString watchUrl;
    QString durationString;
    QString description;
    QString uploadDate; // display string, e.g. "2026-09-01"
    int duration = 0; // seconds
    qint64 timestamp = 0; // unix seconds, 0 when unknown
};

class VideoModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
public:
    enum Roles {
        VideoIdRole = Qt::UserRole + 1,
        TitleRole,
        ChannelRole,
        ChannelIdRole,
        ChannelUrlRole,
        ThumbnailRole,
        WatchUrlRole,
        DurationRole,
        DurationStringRole,
        DescriptionRole,
        TimestampRole,
        UploadDateRole,
    };
    Q_ENUM(Roles)

    explicit VideoModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int count() const { return m_items.size(); }
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE void clear();
    void addVideo(const VideoItem &item);
    // Atomically replaces all items (used for the sorted Home feed).
    void setItems(const QVector<VideoItem> &items);

signals:
    void countChanged();

private:
    QVector<VideoItem> m_items;
};
