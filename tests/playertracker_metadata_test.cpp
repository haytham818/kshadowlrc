// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 KShadowLrc Contributors

// Deterministic, offline checks for the MPRIS metadata -> TrackMeta mapping.
// No D-Bus connection and no live player are involved.

#include "mprismeta.h"

#include <QCoreApplication>
#include <QDBusObjectPath>
#include <QTextStream>
#include <QVariant>

using kshadowlrc::PlaybackStatus;
using kshadowlrc::TrackMeta;
namespace mpris = kshadowlrc::mpris;

namespace
{

int g_failures = 0;

void check(bool condition, const char *what)
{
    QTextStream out(stdout);
    out << (condition ? QStringLiteral("ok:   ") : QStringLiteral("FAIL: ")) << QString::fromUtf8(what) << Qt::endl;
    if (!condition) {
        ++g_failures;
    }
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QTextStream out(stdout);

    // 1. A complete, well-formed metadata dictionary.
    {
        QVariantMap metadata;
        metadata[QStringLiteral("xesam:title")] = QStringLiteral("Blue Monday");
        metadata[QStringLiteral("xesam:artist")] = QStringList{QStringLiteral("New Order")};
        metadata[QStringLiteral("xesam:album")] = QStringLiteral("Power, Corruption & Lies");
        metadata[QStringLiteral("mpris:length")] = QVariant::fromValue<qlonglong>(215000000); // 215 s
        metadata[QStringLiteral("xesam:url")] = QStringLiteral("file:///home/user/music/blue.mp3");
        metadata[QStringLiteral("mpris:trackid")] = QStringLiteral("/org/mpris/MediaPlayer2/Track/1");

        const TrackMeta track = mpris::trackMetaFromMpris(metadata);
        check(track.title == QStringLiteral("Blue Monday"), "title maps from xesam:title");
        check(track.artists == QStringList{QStringLiteral("New Order")}, "artists maps from xesam:artist");
        check(track.album == QStringLiteral("Power, Corruption & Lies"), "album maps from xesam:album");
        check(track.lengthMs == 215000, "mpris:length microseconds -> milliseconds");
        check(track.uniqueSongId == QStringLiteral("file:///home/user/music/blue.mp3"), "file:// url preferred for uniqueSongId");
        check(track.isValid(), "track with title is valid");
    }

    // 2. Missing fields must not drop the track.
    {
        QVariantMap metadata;
        metadata[QStringLiteral("xesam:title")] = QStringLiteral("Title Only");

        const TrackMeta track = mpris::trackMetaFromMpris(metadata);
        check(track.title == QStringLiteral("Title Only"), "title survives missing siblings");
        check(track.artists.isEmpty(), "missing artist -> empty list");
        check(track.album.isEmpty(), "missing album -> empty");
        check(track.lengthMs == 0, "missing length -> 0");
        check(track.uniqueSongId.isEmpty(), "missing url and trackid -> empty uniqueSongId");
    }

    // 3. A completely empty dictionary still yields an invalid but safe track.
    {
        const TrackMeta track = mpris::trackMetaFromMpris(QVariantMap{});
        check(!track.isValid(), "empty metadata -> invalid track, no crash");
        check(track.lengthMs == 0, "empty metadata -> length 0");
    }

    // 4. Malformed xesam:artist variants.
    {
        QVariantMap metadata;
        metadata[QStringLiteral("xesam:artist")] = QStringLiteral("Solo Artist");
        check(mpris::artistsFromMetadata(metadata) == QStringList{QStringLiteral("Solo Artist")}, "artist as a bare string -> single-element list");

        metadata[QStringLiteral("xesam:artist")] = QVariant(42);
        check(mpris::artistsFromMetadata(metadata).isEmpty(), "artist as an int -> empty list, no crash");

        metadata[QStringLiteral("xesam:artist")] = QVariantList{QStringLiteral("A"), QVariant(7), QStringLiteral("B")};
        check(mpris::artistsFromMetadata(metadata) == QStringList{QStringLiteral("A"), QStringLiteral("B")},
              "artist as a variant list -> strings kept, non-strings skipped");

        metadata[QStringLiteral("xesam:artist")] = QStringList{QStringLiteral("X"), QStringLiteral("Y")};
        check(mpris::artistsFromMetadata(metadata) == QStringList{QStringLiteral("X"), QStringLiteral("Y")}, "artist as a QStringList -> preserved in order");

        // Chromium reports an empty artist name; it is not an artist.
        metadata[QStringLiteral("xesam:artist")] = QStringList{QStringLiteral(""), QStringLiteral("Real")};
        check(mpris::artistsFromMetadata(metadata) == QStringList{QStringLiteral("Real")}, "empty artist strings are dropped");

        metadata[QStringLiteral("xesam:artist")] = QStringList{QStringLiteral("")};
        check(mpris::artistsFromMetadata(metadata).isEmpty(), "only-empty artist list -> empty");
    }

    // 5. uniqueSongId fallback rules.
    {
        QVariantMap metadata;
        metadata[QStringLiteral("xesam:url")] = QStringLiteral("https://example.com/song");
        metadata[QStringLiteral("mpris:trackid")] = QStringLiteral("/org/mpris/MediaPlayer2/Track/42");
        check(mpris::uniqueSongIdFromMetadata(metadata) == QStringLiteral("/org/mpris/MediaPlayer2/Track/42"), "non-file url -> trackid wins");

        metadata[QStringLiteral("mpris:trackid")] = QVariant::fromValue(QDBusObjectPath(QStringLiteral("/org/mpris/MediaPlayer2/Track/7")));
        check(mpris::uniqueSongIdFromMetadata(metadata) == QStringLiteral("/org/mpris/MediaPlayer2/Track/7"),
              "QDBusObjectPath trackid is stringified to its path");

        QVariantMap onlyUrl;
        onlyUrl[QStringLiteral("xesam:url")] = QStringLiteral("https://example.com/stream");
        check(mpris::uniqueSongIdFromMetadata(onlyUrl).isEmpty(), "non-file url without trackid -> empty");
    }

    // 6. Length edge cases.
    {
        check(mpris::lengthUsToMs(QVariant::fromValue<qlonglong>(12345678)) == 12345, "12345678 us -> 12345 ms");
        check(mpris::lengthUsToMs(QVariant::fromValue<qlonglong>(0)) == 0, "0 us -> 0 ms");
        check(mpris::lengthUsToMs(QVariant::fromValue<qlonglong>(-1)) == 0, "negative length -> 0 ms");
        check(mpris::lengthUsToMs(QVariant()) == 0, "invalid length -> 0 ms");
        check(mpris::lengthUsToMs(QVariant(QStringLiteral("1500000"))) == 1500, "length as a numeric string still parses");
    }

    // 7. PlaybackStatus parsing.
    {
        check(mpris::playbackStatusFromString(QStringLiteral("Playing")) == PlaybackStatus::Playing, "Playing status");
        check(mpris::playbackStatusFromString(QStringLiteral("Paused")) == PlaybackStatus::Paused, "Paused status");
        check(mpris::playbackStatusFromString(QStringLiteral("Stopped")) == PlaybackStatus::Stopped, "Stopped status");
        check(mpris::playbackStatusFromString(QStringLiteral("garbage")) == PlaybackStatus::Unknown, "unknown status");
    }

    // 8. A plain (already-demarshalled) map passes through unchanged.
    {
        QVariantMap metadata;
        metadata[QStringLiteral("xesam:title")] = QStringLiteral("Plain");
        check(mpris::variantMapFromDbus(QVariant::fromValue(metadata)) == metadata, "variantMapFromDbus passes a plain map through");
    }

    out << (g_failures == 0 ? QStringLiteral("PASS") : QStringLiteral("FAIL")) << QStringLiteral(": mpris metadata mapping (") << g_failures
        << QStringLiteral(" failures)") << Qt::endl;
    return g_failures == 0 ? 0 : 1;
}
