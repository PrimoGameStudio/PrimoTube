#pragma once

#include <QAbstractListModel>
#include <QSettings>
#include <QString>
#include <QVariantList>
#include <QVector>

struct ChannelItem {
    QString channelId;
    QString title;
    QString handle;
    QString url;
    QString avatarUrl;
};

// Local channel subscriptions, persisted via QSettings (JSON array under
// "subscriptionsList"). Mirrors the HistoryManager pattern.
class SubscriptionManager : public QAbstractListModel {
    Q_OBJECT
public:
    enum Roles {
        ChannelIdRole = Qt::UserRole + 1,
        TitleRole,
        HandleRole,
        UrlRole,
        AvatarUrlRole,
    };
    Q_ENUM(Roles)

    explicit SubscriptionManager(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE void subscribe(const QString &channelId, const QString &title,
                               const QString &handle = QString(),
                               const QString &url = QString(),
                               const QString &avatarUrl = QString());
    Q_INVOKABLE void unsubscribe(const QString &channelIdOrUrl);
    Q_INVOKABLE void clear();
    Q_INVOKABLE bool contains(const QString &channelIdOrUrl) const;
    // Backfills the stored avatar once the channel profile HTML arrives
    // (subscribe() runs before the avatar is known). No-op when the entry
    // is unknown or the avatar is unchanged.
    Q_INVOKABLE void updateAvatar(const QString &channelIdOrUrl, const QString &avatarUrl);
    Q_INVOKABLE QString exportJson() const;
    Q_INVOKABLE bool importJson(const QString &jsonStr);
    // Snapshot for HomeFeedBackend.refresh(): [{channelId,title,handle,url}].
    Q_INVOKABLE QVariantList snapshot() const;

    static constexpr int kMaxSubscriptions = 500;
    static constexpr int kMaxImportBytes = 5 * 1024 * 1024;

private:
    static QString keyOf(const ChannelItem &item);
    void load();
    void save() const;

    QVector<ChannelItem> m_items;
    mutable QSettings m_settings;
};
