// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 KShadowLrc Contributors

#include "mprismeta.h"

#include <QDBusArgument>
#include <QDBusObjectPath>
#include <QDBusVariant>

namespace kshadowlrc
{
namespace mpris
{

QVariantMap variantMapFromDbus(const QVariant &value)
{
    if (value.metaType().id() == qMetaTypeId<QDBusArgument>()) {
        return qdbus_cast<QVariantMap>(value);
    }
    return value.toMap();
}

PlaybackStatus playbackStatusFromString(const QString &status)
{
    if (status == QLatin1String("Playing")) {
        return PlaybackStatus::Playing;
    }
    if (status == QLatin1String("Paused")) {
        return PlaybackStatus::Paused;
    }
    if (status == QLatin1String("Stopped")) {
        return PlaybackStatus::Stopped;
    }
    return PlaybackStatus::Unknown;
}

qint64 lengthUsToMs(const QVariant &value)
{
    if (!value.isValid()) {
        return 0;
    }
    bool ok = false;
    const qint64 microseconds = value.toLongLong(&ok);
    if (!ok || microseconds <= 0) {
        return 0;
    }
    return microseconds / 1000;
}

QStringList artistsFromMetadata(const QVariantMap &metadata)
{
    const QVariant value = metadata.value(QStringLiteral("xesam:artist"));
    if (!value.isValid()) {
        return {};
    }

    const auto withoutEmpty = [](const QStringList &artists) {
        QStringList cleaned;
        for (const QString &artist : artists) {
            if (!artist.isEmpty()) {
                cleaned.append(artist);
            }
        }
        return cleaned;
    };

    const int type = value.metaType().id();
    if (type == qMetaTypeId<QDBusArgument>()) {
        return withoutEmpty(qdbus_cast<QStringList>(value));
    }
    if (type == QMetaType::QStringList) {
        return withoutEmpty(value.toStringList());
    }
    if (type == QMetaType::QString) {
        return withoutEmpty(QStringList{value.toString()});
    }
    // Some players send `as` as a plain list; accept it and ignore anything
    // that is not a string rather than failing the track.
    if (type == QMetaType::QVariantList) {
        QStringList artists;
        const QVariantList list = value.toList();
        for (const QVariant &item : list) {
            if (item.metaType().id() == QMetaType::QString && !item.toString().isEmpty()) {
                artists.append(item.toString());
            }
        }
        return artists;
    }
    return {};
}

QString uniqueSongIdFromMetadata(const QVariantMap &metadata)
{
    const QString url = metadata.value(QStringLiteral("xesam:url")).toString();
    if (url.startsWith(QLatin1String("file://"))) {
        return url;
    }

    const QVariant trackId = metadata.value(QStringLiteral("mpris:trackid"));
    if (trackId.isValid()) {
        const QString id = trackId.metaType().id() == qMetaTypeId<QDBusObjectPath>() ? trackId.value<QDBusObjectPath>().path() : trackId.toString();
        if (!id.isEmpty()) {
            return id;
        }
    }
    return {};
}

TrackMeta trackMetaFromMpris(const QVariantMap &metadata)
{
    TrackMeta track;
    track.title = metadata.value(QStringLiteral("xesam:title")).toString();
    track.album = metadata.value(QStringLiteral("xesam:album")).toString();
    track.artists = artistsFromMetadata(metadata);
    track.lengthMs = lengthUsToMs(metadata.value(QStringLiteral("mpris:length")));
    track.uniqueSongId = uniqueSongIdFromMetadata(metadata);
    return track;
}

QVariant unwrapDbusVariant(const QVariant &value)
{
    if (value.metaType().id() == qMetaTypeId<QDBusVariant>()) {
        return value.value<QDBusVariant>().variant();
    }
    return value;
}

} // namespace mpris
} // namespace kshadowlrc
