#include "settingsmanager.h"

#include <QStandardPaths>
#include <QDir>
#include <QFileInfo>

SettingsManager::SettingsManager(QObject *parent)
    : QObject(parent)
    , m_settings()
{
    QString defaultDir = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    if (defaultDir.isEmpty())
        defaultDir = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);

    m_downloadDirectory = m_settings.value(QStringLiteral("downloadDirectory"), defaultDir).toString();
    m_maxResults = qBound(1, m_settings.value(QStringLiteral("maxResults"), 10).toInt(), 25);
    m_volume = qBound(0.0, m_settings.value(QStringLiteral("volume"), 1.0).toDouble(), 1.0);
    const QString savedCap = m_settings.value(QStringLiteral("qualityCap"), QStringLiteral("Auto")).toString();
    m_qualityCap = qualityCapOptions().contains(savedCap) ? savedCap : QStringLiteral("Auto");
    const QString savedTheme = m_settings.value(QStringLiteral("theme"), QStringLiteral("dark")).toString();
    m_theme = themeOptions().contains(savedTheme) ? savedTheme : QStringLiteral("dark");
    m_forceFallbackPlayer = m_settings.value(QStringLiteral("forceFallbackPlayer"), false).toBool();
}

QString SettingsManager::downloadDirectory() const
{
    return m_downloadDirectory;
}

bool SettingsManager::trySetDownloadDirectory(const QString &dir)
{
    const QString trimmed = dir.trimmed();
    if (trimmed.isEmpty())
        return false;
    if (m_downloadDirectory == trimmed)
        return true;
    // Validate: must be creatable and writable.
    QDir d(trimmed);
    if (!d.mkpath(QStringLiteral(".")) || !QFileInfo(trimmed).isWritable())
        return false;
    m_downloadDirectory = trimmed;
    m_settings.setValue(QStringLiteral("downloadDirectory"), trimmed);
    emit downloadDirectoryChanged();
    return true;
}

void SettingsManager::setDownloadDirectory(const QString &dir)
{
    // WRITE accessor delegates to the validating setter; silently ignores
    // invalid values so a bad QML binding can't break downloads.
    trySetDownloadDirectory(dir);
}

int SettingsManager::maxResults() const
{
    return m_maxResults;
}

void SettingsManager::setMaxResults(int results)
{
    int bounded = qBound(1, results, 25);
    if (m_maxResults == bounded)
        return;
    m_maxResults = bounded;
    m_settings.setValue(QStringLiteral("maxResults"), bounded);
    emit maxResultsChanged();
}

double SettingsManager::volume() const
{
    return m_volume;
}

void SettingsManager::setVolume(double vol)
{
    double bounded = qBound(0.0, vol, 1.0);
    if (qFuzzyCompare(m_volume + 1.0, bounded + 1.0))
        return;
    m_volume = bounded;
    m_settings.setValue(QStringLiteral("volume"), bounded);
    emit volumeChanged();
}

QStringList SettingsManager::qualityCapOptions()
{
    return {QStringLiteral("Auto"), QStringLiteral("720p"), QStringLiteral("1080p"), QStringLiteral("4K")};
}

QString SettingsManager::qualityCap() const
{
    return m_qualityCap;
}

void SettingsManager::setQualityCap(const QString &cap)
{
    if (!qualityCapOptions().contains(cap) || m_qualityCap == cap)
        return;
    m_qualityCap = cap;
    m_settings.setValue(QStringLiteral("qualityCap"), cap);
    emit qualityCapChanged();
}

QStringList SettingsManager::themeOptions()
{
    return {QStringLiteral("dark"), QStringLiteral("light")};
}

QString SettingsManager::urlToLocalFile(const QUrl &url)
{
    if (url.isLocalFile())
        return url.toLocalFile();
    return {};
}

QUrl SettingsManager::localFileToUrl(const QString &path)
{
    return QUrl::fromLocalFile(path);
}

QString SettingsManager::theme() const
{
    return m_theme;
}

void SettingsManager::setTheme(const QString &t)
{
    if (!themeOptions().contains(t) || m_theme == t)
        return;
    m_theme = t;
    m_settings.setValue(QStringLiteral("theme"), t);
    emit themeChanged();
}

bool SettingsManager::forceFallbackPlayer() const
{
    return m_forceFallbackPlayer;
}

void SettingsManager::setForceFallbackPlayer(bool force)
{
    if (m_forceFallbackPlayer == force)
        return;
    m_forceFallbackPlayer = force;
    m_settings.setValue(QStringLiteral("forceFallbackPlayer"), force);
    emit forceFallbackPlayerChanged();
}
