#pragma once

#include <QObject>
#include <QString>

// Exposes the system clipboard to QML (Qt provides no QML clipboard API).
class ClipboardHelper : public QObject {
    Q_OBJECT
public:
    explicit ClipboardHelper(QObject *parent = nullptr);

    Q_INVOKABLE bool copyText(const QString &text);
};
