#include "durablejson.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>

namespace DurableJson {

QString configDir()
{
    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    if (dir.isEmpty())
        dir = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
    if (!QDir().mkpath(dir))
        qWarning() << "DurableJson: could not create config directory" << dir;
    return dir;
}

QString filePath(const QString &name)
{
    return configDir() + QLatin1Char('/') + name + QStringLiteral(".json");
}

QByteArray read(const QString &name, int maxBytes)
{
    QFile file(filePath(name));
    if (!file.exists() || !file.open(QIODevice::ReadOnly))
        return {};
    if (file.size() > maxBytes)
        return {};
    return file.readAll();
}

bool write(const QString &name, const QByteArray &bytes, QString *error)
{
    const QString path = filePath(name);
    const QFile::Permissions ownerOnly = QFile::ReadOwner | QFile::WriteOwner;
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error)
            *error = file.errorString();
        return false;
    }
    file.setPermissions(ownerOnly);
    if (file.write(bytes) != bytes.size()) {
        if (error)
            *error = file.errorString();
        return false;
    }
    if (!file.commit()) {
        if (error)
            *error = file.errorString();
        return false;
    }
    // commit() renames the temp file; make sure the final file is private
    // even if the directory umask would have given it wider permissions.
    QFile::setPermissions(path, ownerOnly);
    return true;
}

QByteArray legacyValue(const QString &key)
{
    QSettings settings;
    return settings.value(key).toString().toUtf8();
}

} // namespace DurableJson
