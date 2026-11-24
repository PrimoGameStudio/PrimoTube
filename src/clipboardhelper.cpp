#include "clipboardhelper.h"

#include <QClipboard>
#include <QGuiApplication>

#include "channelutils.h"

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

bool ClipboardHelper::isAllowedPlaybackTarget(const QString &target) const
{
    return ChannelUtils::isAllowedPlaybackTarget(target);
}
