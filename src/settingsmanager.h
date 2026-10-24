#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QSettings>
#include <QUrl>

class SettingsManager : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString downloadDirectory READ downloadDirectory WRITE setDownloadDirectory NOTIFY downloadDirectoryChanged)
    Q_PROPERTY(int maxResults READ maxResults WRITE setMaxResults NOTIFY maxResultsChanged)
    Q_PROPERTY(double volume READ volume WRITE setVolume NOTIFY volumeChanged)
    Q_PROPERTY(QString qualityCap READ qualityCap WRITE setQualityCap NOTIFY qualityCapChanged)
    Q_PROPERTY(QString theme READ theme WRITE setTheme NOTIFY themeChanged)
    // When true, playback always uses the lightweight QtMultimedia player
    // instead of libmpv (low CPU/GPU, but progressive streams only).
    Q_PROPERTY(bool forceFallbackPlayer READ forceFallbackPlayer WRITE setForceFallbackPlayer NOTIFY forceFallbackPlayerChanged)
public:
    explicit SettingsManager(QObject *parent = nullptr);

    Q_INVOKABLE static QStringList qualityCapOptions(); // "Auto", "720p", "1080p", "4K"
    Q_INVOKABLE static QStringList themeOptions(); // "dark", "light"
    // Converts a FolderDialog url to a local path (handles spaces/encoding).
    Q_INVOKABLE static QString urlToLocalFile(const QUrl &url);
    Q_INVOKABLE static QUrl localFileToUrl(const QString &path);

    QString downloadDirectory() const;
    void setDownloadDirectory(const QString &dir);
    // Validating setter with feedback for settings forms: returns true when
    // the directory was accepted. WRITE accessors can't be called as QML
    // functions, hence the separate invokable.
    Q_INVOKABLE bool trySetDownloadDirectory(const QString &dir);

    int maxResults() const;
    void setMaxResults(int results);

    double volume() const;
    void setVolume(double vol);

    QString qualityCap() const;
    void setQualityCap(const QString &cap);

    QString theme() const;
    void setTheme(const QString &t);

    bool forceFallbackPlayer() const;
    void setForceFallbackPlayer(bool force);

signals:
    void downloadDirectoryChanged();
    void maxResultsChanged();
    void volumeChanged();
    void qualityCapChanged();
    void themeChanged();
    void forceFallbackPlayerChanged();

private:
    QSettings m_settings;
    QString m_downloadDirectory;
    int m_maxResults;
    double m_volume;
    QString m_qualityCap;
    QString m_theme;
    bool m_forceFallbackPlayer = false;
};
