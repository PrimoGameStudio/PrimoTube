#pragma once

#include <QObject>
#include <QString>

// Exposes the system clipboard to QML (Qt provides no QML clipboard API).
class ClipboardHelper : public QObject {
    Q_OBJECT
public:
    explicit ClipboardHelper(QObject *parent = nullptr);

    Q_INVOKABLE bool copyText(const QString &text);
    // Gate for QML actions that hand a URL to the OS (openUrlExternally,
    // share). Mirrors the backend allow-list so untrusted webpage_url values
    // cannot escape to the browser.
    Q_INVOKABLE bool isAllowedPlaybackTarget(const QString &target) const;
};
