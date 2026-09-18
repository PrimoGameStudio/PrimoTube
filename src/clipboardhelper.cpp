#include "clipboardhelper.h"

#include <QClipboard>
#include <QGuiApplication>

ClipboardHelper::ClipboardHelper(QObject *parent)
    : QObject(parent)
{
}

bool ClipboardHelper::copyText(const QString &text)
{
    if (text.isEmpty())
        return false;
    QClipboard *clipboard = QGuiApplication::clipboard();
    if (!clipboard)
        return false;
    clipboard->setText(text);
    return true;
}
