#pragma once

#include <QObject>
#include <QString>

class DependencyChecker : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool ytDlpAvailable READ ytDlpAvailable CONSTANT)
    Q_PROPERTY(bool ffmpegAvailable READ ffmpegAvailable CONSTANT)
    Q_PROPERTY(QString missingMessage READ missingMessage CONSTANT)
public:
    explicit DependencyChecker(QObject *parent = nullptr);

    bool ytDlpAvailable() const { return m_ytDlp; }
    bool ffmpegAvailable() const { return m_ffmpeg; }
    QString missingMessage() const { return m_missingMessage; }

private:
    bool m_ytDlp = false;
    bool m_ffmpeg = false;
    QString m_missingMessage;
};
