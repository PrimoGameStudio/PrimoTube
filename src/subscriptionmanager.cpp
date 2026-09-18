#include "subscriptionmanager.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

SubscriptionManager::SubscriptionManager(QObject *parent)
    : QAbstractListModel(parent)
    , m_settings()
{
    load();
}

int SubscriptionManager::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return m_items.size();
}

QVariant SubscriptionManager::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_items.size())
        return {};
    const ChannelItem &item = m_items.at(index.row());
    switch (role) {
    case ChannelIdRole: return item.channelId;
    case TitleRole: return item.title;
    case HandleRole: return item.handle;
    case UrlRole: return item.url;
    case AvatarUrlRole: return item.avatarUrl;
    default: return {};
    }
}

QHash<int, QByteArray> SubscriptionManager::roleNames() const
{
    return {
        {ChannelIdRole, "channelId"},
        {TitleRole, "title"},
        {HandleRole, "handle"},
        {UrlRole, "url"},
        {AvatarUrlRole, "avatar"},
    };
}

void SubscriptionManager::subscribe(const QString &channelId, const QString &title,
                                    const QString &handle, const QString &url,
                                    const QString &avatarUrl)
{
    ChannelItem item;
    item.channelId = channelId.trimmed().left(128);
    item.title = title.left(500);
    item.handle = handle.trimmed().left(128);
    item.url = url.trimmed().left(2048);
    item.avatarUrl = avatarUrl.trimmed().left(2048);
    if (item.title.isEmpty())
        item.title = !item.handle.isEmpty() ? item.handle : item.url;
    // Key on id when known, else URL. Title-only entries are rejected to
    // avoid collisions between unrelated channels with the same name.
    if (keyOf(item).isEmpty())
        return;
    // Replace existing entry (dedupe by id, case-insensitive handle/url —
    // YouTube handles are case-insensitive, e.g. @Name vs @name).
    for (int i = 0; i < m_items.size(); ++i) {
        const ChannelItem &cur = m_items.at(i);
        const bool sameId = !item.channelId.isEmpty() && cur.channelId == item.channelId;
        const bool sameHandle = !item.handle.isEmpty() && !cur.handle.isEmpty()
            && cur.handle.compare(item.handle, Qt::CaseInsensitive) == 0;
        const bool sameUrl = !item.url.isEmpty() && !cur.url.isEmpty()
            && cur.url.compare(item.url, Qt::CaseInsensitive) == 0;
        if (sameId || sameHandle || sameUrl) {
            m_items[i] = item;
            const QModelIndex idx = index(i, 0);
            emit dataChanged(idx, idx);
            save();
            return;
        }
    }
    beginInsertRows(QModelIndex(), m_items.size(), m_items.size());
    m_items.append(item);
    endInsertRows();
    // Bound the list so QSettings stays small; drop oldest beyond cap.
    if (m_items.size() > kMaxSubscriptions) {
        beginRemoveRows(QModelIndex(), 0, m_items.size() - kMaxSubscriptions - 1);
        m_items.erase(m_items.begin(), m_items.begin() + (m_items.size() - kMaxSubscriptions));
        endRemoveRows();
    }
    save();
}

void SubscriptionManager::unsubscribe(const QString &channelIdOrUrl)
{
    const QString key = channelIdOrUrl.trimmed();
    if (key.isEmpty())
        return;
    for (int i = 0; i < m_items.size(); ++i) {
        const ChannelItem &cur = m_items.at(i);
        if (cur.channelId == key || (!cur.handle.isEmpty() && cur.handle.compare(key, Qt::CaseInsensitive) == 0)
            || (!cur.url.isEmpty() && cur.url.compare(key, Qt::CaseInsensitive) == 0)) {
            beginRemoveRows(QModelIndex(), i, i);
            m_items.removeAt(i);
            endRemoveRows();
            save();
            return;
        }
    }
}

void SubscriptionManager::clear()
{
    if (m_items.isEmpty())
        return;
    beginResetModel();
    m_items.clear();
    endResetModel();
    save();
}

bool SubscriptionManager::contains(const QString &channelIdOrUrl) const
{
    const QString key = channelIdOrUrl.trimmed();
    if (key.isEmpty())
        return false;
    for (const auto &item : m_items) {
        if ((!item.channelId.isEmpty() && item.channelId == key)
            || (!item.url.isEmpty() && item.url.compare(key, Qt::CaseInsensitive) == 0)
            || (!item.handle.isEmpty() && item.handle.compare(key, Qt::CaseInsensitive) == 0))
            return true;
    }
    return false;
}

QString SubscriptionManager::keyOf(const ChannelItem &item)
{
    if (!item.channelId.isEmpty())
        return item.channelId;
    if (!item.url.isEmpty())
        return item.url;
    return {};
}

void SubscriptionManager::updateAvatar(const QString &channelIdOrUrl, const QString &avatarUrl)
{
    const QString key = channelIdOrUrl.trimmed();
    const QString cleanAvatar = avatarUrl.trimmed().left(2048);
    if (key.isEmpty() || cleanAvatar.isEmpty())
        return;
    for (int i = 0; i < m_items.size(); ++i) {
        ChannelItem &cur = m_items[i];
        const bool match = (!cur.channelId.isEmpty() && cur.channelId == key)
            || (!cur.handle.isEmpty() && cur.handle.compare(key, Qt::CaseInsensitive) == 0)
            || (!cur.url.isEmpty() && cur.url.compare(key, Qt::CaseInsensitive) == 0);
        if (!match)
            continue;
        if (cur.avatarUrl == cleanAvatar)
            return;
        cur.avatarUrl = cleanAvatar;
        const QModelIndex idx = index(i, 0);
        emit dataChanged(idx, idx, {AvatarUrlRole});
        save();
        return;
    }
}

void SubscriptionManager::load()
{
    const QString jsonStr = m_settings.value(QStringLiteral("subscriptionsList")).toString();
    if (jsonStr.isEmpty())
        return;
    const QJsonDocument doc = QJsonDocument::fromJson(jsonStr.toUtf8());
    if (!doc.isArray())
        return;
    m_items.clear();
    for (const auto &v : doc.array()) {
        if (m_items.size() >= kMaxSubscriptions)
            break;
        const QJsonObject obj = v.toObject();
        ChannelItem item;
        item.channelId = obj.value(QStringLiteral("channelId")).toString().trimmed().left(128);
        item.title = obj.value(QStringLiteral("title")).toString().left(500);
        item.handle = obj.value(QStringLiteral("handle")).toString().left(128);
        item.url = obj.value(QStringLiteral("url")).toString().trimmed().left(2048);
        item.avatarUrl = obj.value(QStringLiteral("avatar")).toString().left(2048);
        if (keyOf(item).isEmpty())
            continue;
        // Dedupe defensively (persisted data may predate validation).
        bool dup = false;
        for (const auto &cur : m_items) {
            const bool sameId = !item.channelId.isEmpty() && cur.channelId == item.channelId;
            const bool sameHandle = !item.handle.isEmpty() && !cur.handle.isEmpty()
                && cur.handle.compare(item.handle, Qt::CaseInsensitive) == 0;
            const bool sameUrl = !item.url.isEmpty() && !cur.url.isEmpty()
                && cur.url.compare(item.url, Qt::CaseInsensitive) == 0;
            if (sameId || sameHandle || sameUrl) {
                dup = true;
                break;
            }
        }
        if (!dup)
            m_items.append(item);
    }
}

void SubscriptionManager::save() const
{
    QJsonArray arr;
    for (const auto &item : m_items) {
        QJsonObject obj;
        obj[QStringLiteral("channelId")] = item.channelId;
        obj[QStringLiteral("title")] = item.title;
        obj[QStringLiteral("handle")] = item.handle;
        obj[QStringLiteral("url")] = item.url;
        obj[QStringLiteral("avatar")] = item.avatarUrl;
        arr.append(obj);
    }
    m_settings.setValue(QStringLiteral("subscriptionsList"),
                  QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact)));
}

QString SubscriptionManager::exportJson() const
{
    QJsonArray arr;
    for (const auto &item : m_items) {
        QJsonObject obj;
        obj[QStringLiteral("channelId")] = item.channelId;
        obj[QStringLiteral("title")] = item.title;
        obj[QStringLiteral("handle")] = item.handle;
        obj[QStringLiteral("url")] = item.url;
        obj[QStringLiteral("avatar")] = item.avatarUrl;
        arr.append(obj);
    }
    return QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Indented));
}

bool SubscriptionManager::importJson(const QString &jsonStr)
{
    if (jsonStr.size() > kMaxImportBytes)
        return false;
    const QJsonDocument doc = QJsonDocument::fromJson(jsonStr.toUtf8());
    if (!doc.isArray())
        return false;
    beginResetModel();
    m_items.clear();
    for (const auto &v : doc.array()) {
        if (m_items.size() >= kMaxSubscriptions)
            break;
        const QJsonObject obj = v.toObject();
        ChannelItem item;
        item.channelId = obj.value(QStringLiteral("channelId")).toString().trimmed().left(128);
        item.title = obj.value(QStringLiteral("title")).toString().left(500);
        item.handle = obj.value(QStringLiteral("handle")).toString().left(128);
        item.url = obj.value(QStringLiteral("url")).toString().trimmed().left(2048);
        item.avatarUrl = obj.value(QStringLiteral("avatar")).toString().left(2048);
        if (keyOf(item).isEmpty())
            continue;
        bool dup = false;
        for (const auto &cur : m_items) {
            const bool sameId = !item.channelId.isEmpty() && cur.channelId == item.channelId;
            const bool sameHandle = !item.handle.isEmpty() && !cur.handle.isEmpty()
                && cur.handle.compare(item.handle, Qt::CaseInsensitive) == 0;
            const bool sameUrl = !item.url.isEmpty() && !cur.url.isEmpty()
                && cur.url.compare(item.url, Qt::CaseInsensitive) == 0;
            if (sameId || sameHandle || sameUrl) {
                dup = true;
                break;
            }
        }
        if (!dup)
            m_items.append(item);
    }
    endResetModel();
    save();
    return true;
}

QVariantList SubscriptionManager::snapshot() const
{
    QVariantList out;
    out.reserve(m_items.size());
    for (const auto &item : m_items) {
        QVariantMap m;
        m[QStringLiteral("channelId")] = item.channelId;
        m[QStringLiteral("title")] = item.title;
        m[QStringLiteral("handle")] = item.handle;
        m[QStringLiteral("url")] = item.url;
        out.append(m);
    }
    return out;
}
