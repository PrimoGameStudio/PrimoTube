#pragma once

#include <QByteArray>
#include <QString>

// Shared persistence policy for the local JSON stores (history, favorites,
// subscriptions). Writes are atomic and owner-only; reads are size-bounded.
namespace DurableJson {

// Writable config directory for this app, created on demand.
QString configDir();

// Absolute path of <name>.json inside configDir().
QString filePath(const QString &name);

// Reads a bounded file. Returns an empty array when it is missing,
// unreadable, or larger than maxBytes.
QByteArray read(const QString &name, int maxBytes);

// Atomically writes bytes via QSaveFile with owner-only permissions.
// Returns false and fills `error` on failure.
bool write(const QString &name, const QByteArray &bytes, QString *error = nullptr);

// One-time migration: reads a legacy QSettings value from the old
// PrimoTube.conf store. Returns an empty array when it is absent.
QByteArray legacyValue(const QString &key);

} // namespace DurableJson
