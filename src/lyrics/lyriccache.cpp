// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 KShadowLrc Contributors

#include "lyrics/lyriccache.h"

#include "lyrics/matching.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QStandardPaths>
#include <QStringList>

namespace kshadowlrc::LyricCache
{

QString cacheDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
}

QString cacheKey(const TrackMeta &track)
{
    const QString title = normalizeForMatch(track.title);
    if (title.isEmpty()) {
        // No usable identity: do not cache a song we cannot recognise again.
        return QString();
    }

    QStringList artists;
    artists.reserve(track.artists.size());
    for (const QString &artist : track.artists) {
        artists.append(normalizeForMatch(artist));
    }

    const QString key = title + QLatin1Char('-') + artists.join(QLatin1Char(',')) + QLatin1Char('-') + normalizeForMatch(track.album) + QLatin1Char('-')
        + QString::number(track.lengthMs);

    return QString::fromLatin1(QCryptographicHash::hash(key.toUtf8(), QCryptographicHash::Md5).toHex());
}

QString cachePath(const TrackMeta &track)
{
    const QString digest = cacheKey(track);
    if (digest.isEmpty()) {
        return QString();
    }

    const QString dir = cacheDir();
    if (dir.isEmpty()) {
        return QString();
    }

    // Three-level split of the digest's first bytes, then <hash>.json.
    return dir + QLatin1Char('/') + digest.mid(0, 2) + QLatin1Char('/') + digest.mid(2, 2) + QLatin1Char('/') + digest.mid(4, 2) + QLatin1Char('/') + digest
        + QStringLiteral(".json");
}

std::optional<CacheEntry> lookup(const TrackMeta &track)
{
    const QString path = cachePath(track);
    if (path.isEmpty()) {
        return std::nullopt;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return std::nullopt;
    }
    const QByteArray raw = file.readAll();
    file.close();

    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(raw, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return std::nullopt;
    }

    const QJsonObject object = document.object();
    CacheEntry entry;
    entry.store.lyric = object.value(QStringLiteral("lyric")).toString();
    entry.store.tlyric = object.value(QStringLiteral("tlyric")).toString();
    entry.offsetMs = static_cast<qint64>(object.value(QStringLiteral("offset")).toDouble(0));
    return entry;
}

bool store(const TrackMeta &track, const LyricStore &store, qint64 offsetMs)
{
    if (!store.hasAny()) {
        // Never cache an empty lyric.
        return false;
    }

    const QString path = cachePath(track);
    if (path.isEmpty()) {
        return false;
    }

    QDir dir;
    if (!dir.mkpath(QFileInfo(path).absolutePath())) {
        return false;
    }

    QJsonObject object;
    object.insert(QStringLiteral("lyric"), store.lyric);
    object.insert(QStringLiteral("tlyric"), store.tlyric);
    object.insert(QStringLiteral("offset"), static_cast<double>(offsetMs));

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    const QByteArray payload = QJsonDocument(object).toJson(QJsonDocument::Compact);
    const bool ok = file.write(payload) == payload.size();
    file.close();
    return ok;
}

} // namespace kshadowlrc::LyricCache
