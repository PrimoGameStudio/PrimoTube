#pragma once

#include <QAbstractListModel>
#include <QSettings>
#include <QString>
#include <QVector>
#include "videomodel.h"

class HistoryManager : public QAbstractListModel {
    Q_OBJECT
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

    explicit HistoryManager(bool isFavorites = false, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE void addItem(const QString &videoId, const QString &title, const QString &channel,
                             const QString &thumbnail, const QString &watchUrl,
                             const QString &durationString, int duration,
                             const QString &description = QString(),
                             const QString &channelId = QString(),
                             const QString &channelUrl = QString());
    Q_INVOKABLE void removeItem(const QString &videoId);
    Q_INVOKABLE void clear();
    Q_INVOKABLE bool contains(const QString &videoId) const;
    Q_INVOKABLE QString exportJson() const;
    Q_INVOKABLE bool importJson(const QString &jsonStr);

    static constexpr int kMaxHistory = 100;
    static constexpr int kMaxFavorites = 1000;
    static constexpr int kMaxImportBytes = 5 * 1024 * 1024;

private:
    void load();
    void save() const;

    bool m_isFavorites;
    QVector<VideoItem> m_items;
    mutable QSettings m_settings;
};
