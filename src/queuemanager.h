#pragma once

#include <QAbstractListModel>
#include <QVector>
#include <QString>
#include <QVariantMap>
#include "videomodel.h"

class QueueManager : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int currentIndex READ currentIndex NOTIFY currentIndexChanged)
    Q_PROPERTY(bool hasQueue READ hasQueue NOTIFY queueChanged)
    Q_PROPERTY(int count READ count NOTIFY queueChanged)
public:
    enum Roles {
        VideoIdRole = VideoModel::VideoIdRole,
        TitleRole = VideoModel::TitleRole,
        ChannelRole = VideoModel::ChannelRole,
        ChannelIdRole = VideoModel::ChannelIdRole,
        ChannelUrlRole = VideoModel::ChannelUrlRole,
        ThumbnailRole = VideoModel::ThumbnailRole,
        WatchUrlRole = VideoModel::WatchUrlRole,
        DurationRole = VideoModel::DurationRole,
        DurationStringRole = VideoModel::DurationStringRole,
        DescriptionRole = VideoModel::DescriptionRole,
    };

    explicit QueueManager(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    int currentIndex() const { return m_currentIndex; }
    bool hasQueue() const { return !m_items.isEmpty(); }
    int count() const { return m_items.size(); }

    Q_INVOKABLE void enqueue(const QString &videoId, const QString &title, const QString &channel,
                             const QString &thumbnail, const QString &watchUrl,
                             const QString &durationString, int duration,
                             const QString &description = QString(),
                             const QString &channelId = QString(),
                             const QString &channelUrl = QString());

    Q_INVOKABLE void enqueueNext(const QString &videoId, const QString &title, const QString &channel,
                                 const QString &thumbnail, const QString &watchUrl,
                                 const QString &durationString, int duration,
                                 const QString &description = QString(),
                                 const QString &channelId = QString(),
                                 const QString &channelUrl = QString());

    Q_INVOKABLE void removeAt(int index);
    Q_INVOKABLE void clear();
    Q_INVOKABLE void playIndex(int index);
    Q_INVOKABLE bool advance();
    Q_INVOKABLE QVariantMap currentItem() const;
    Q_INVOKABLE void enqueueModel(VideoModel *model);

signals:
    void currentIndexChanged();
    void queueChanged();
    void itemRequested(const QString &videoId, const QString &title, const QString &channel,
                       const QString &channelId, const QString &channelUrl, const QString &watchUrl,
                       const QString &durationString, int duration, const QString &thumbnail, const QString &description);

private:
    QVector<VideoItem> m_items;
    int m_currentIndex = -1;

    static constexpr int kMaxQueue = 200;
    bool isFull() const { return m_items.size() >= kMaxQueue; }
};
