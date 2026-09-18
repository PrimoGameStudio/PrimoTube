#include "dependencychecker.h"

#include <QStandardPaths>

DependencyChecker::DependencyChecker(QObject *parent)
    : QObject(parent)
{
    m_ytDlp = !QStandardPaths::findExecutable(QStringLiteral("yt-dlp")).isEmpty();
    m_ffmpeg = !QStandardPaths::findExecutable(QStringLiteral("ffmpeg")).isEmpty();

    QStringList missing;
    if (!m_ytDlp)
        missing.append(QStringLiteral("yt-dlp"));
    if (!m_ffmpeg)
        missing.append(QStringLiteral("ffmpeg"));

    if (!missing.isEmpty()) {
        m_missingMessage = tr("Required tool(s) not found in PATH: %1. Search, playback, or downloads may fail until installed.")
                               .arg(missing.join(QStringLiteral(", ")));
    }
}
