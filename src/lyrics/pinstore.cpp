// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 KShadowLrc Contributors

#include "lyrics/pinstore.h"

#include "lyrics/lyriccache.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QLoggingCategory>
#include <QStandardPaths>

#include <cstdio>

Q_LOGGING_CATEGORY(lcPins, "kshadowlrc.pins")

namespace kshadowlrc::pinstore
{

namespace
{

QJsonObject toJson(const Pin &pin)
{
    QJsonObject object;
    object.insert(QStringLiteral("lyric"), pin.store.lyric);
    object.insert(QStringLiteral("tlyric"), pin.store.tlyric);
    object.insert(QStringLiteral("source"), pin.source);
    object.insert(QStringLiteral("provider"), pin.provider);
    object.insert(QStringLiteral("songId"), pin.songId);
    object.insert(QStringLiteral("title"), pin.title);
    object.insert(QStringLiteral("singer"), pin.singer);
    return object;
}

Pin fromJson(const QJsonObject &object)
{
    Pin pin;
    pin.store.lyric = object.value(QStringLiteral("lyric")).toString();
    pin.store.tlyric = object.value(QStringLiteral("tlyric")).toString();
    pin.source = object.value(QStringLiteral("source")).toString();
    pin.provider = object.value(QStringLiteral("provider")).toString();
    pin.songId = object.value(QStringLiteral("songId")).toString();
    pin.title = object.value(QStringLiteral("title")).toString();
    pin.singer = object.value(QStringLiteral("singer")).toString();
    return pin;
}

/*!
 * Read the whole key -> pin object. Missing file is an ordinary "no pins yet"
 * and corrupt content is skipped with a warning; neither is an error, so both
 * return an empty object rather than throwing.
 */
QJsonObject readAll()
{
    const QString path = storePath();
    if (path.isEmpty()) {
        return {};
    }

    QFile file(path);
    if (!file.exists()) {
        return {};
    }
    if (!file.open(QIODevice::ReadOnly)) {
        qCWarning(lcPins) << "cannot read pin file" << path;
        return {};
    }
    const QByteArray raw = file.readAll();
    file.close();

    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(raw, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        qCWarning(lcPins) << "ignoring unreadable pin file" << path << ":" << parseError.errorString();
        return {};
    }
    return document.object();
}

/*! Replace the pin file atomically: write a temporary file, then rename it over. */
bool writeAll(const QJsonObject &root)
{
    const QString path = storePath();
    if (path.isEmpty()) {
        return false;
    }

    QDir dir;
    if (!dir.mkpath(QFileInfo(path).absolutePath())) {
        qCWarning(lcPins) << "cannot create pin directory for" << path;
        return false;
    }

    const QString tempPath = path + QStringLiteral(".tmp");
    QFile file(tempPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qCWarning(lcPins) << "cannot write pin file" << tempPath;
        return false;
    }
    const QByteArray payload = QJsonDocument(root).toJson(QJsonDocument::Indented);
    const bool written = file.write(payload) == payload.size();
    file.close();
    if (!written) {
        QFile::remove(tempPath);
        qCWarning(lcPins) << "short write to pin file" << tempPath;
        return false;
    }

    // std::rename atomically replaces an existing destination; QFile::rename
    // refuses to, which would leave the old pins in place after a save.
    if (std::rename(QFile::encodeName(tempPath).constData(), QFile::encodeName(path).constData()) != 0) {
        QFile::remove(tempPath);
        qCWarning(lcPins) << "cannot replace pin file" << path;
        return false;
    }
    return true;
}

} // namespace

QString keyFor(const TrackMeta &track)
{
    if (!track.uniqueSongId.trimmed().isEmpty()) {
        return track.uniqueSongId;
    }
    // No player identity: fall back to the metadata digest, exactly as the cache
    // does, so a song the player cannot name is still pinnable.
    return LyricCache::cacheKey(track);
}

QString storePath()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (dir.isEmpty()) {
        return {};
    }
    return dir + QStringLiteral("/pins.json");
}

std::optional<Pin> lookup(const TrackMeta &track)
{
    const QString key = keyFor(track);
    if (key.isEmpty()) {
        return std::nullopt;
    }

    const QJsonObject root = readAll();
    const auto it = root.constFind(key);
    if (it == root.constEnd() || !it->isObject()) {
        return std::nullopt;
    }
    return fromJson(it->toObject());
}

bool save(const TrackMeta &track, const Pin &pin)
{
    const QString key = keyFor(track);
    if (key.isEmpty()) {
        qCWarning(lcPins) << "refusing to pin a track with no identity";
        return false;
    }

    QJsonObject root = readAll();
    root.insert(key, toJson(pin));
    return writeAll(root);
}

bool forget(const TrackMeta &track)
{
    const QString key = keyFor(track);
    if (key.isEmpty()) {
        return false;
    }

    QJsonObject root = readAll();
    if (!root.contains(key)) {
        return false;
    }
    root.remove(key);
    return writeAll(root);
}

} // namespace kshadowlrc::pinstore
