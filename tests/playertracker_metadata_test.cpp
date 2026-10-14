// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 KShadowLrc Contributors

// Deterministic, offline checks for the MPRIS metadata -> TrackMeta mapping.
// No D-Bus connection and no live player are involved.

#include "mprismeta.h"

#include <QDBusObjectPath>
#include <QTest>
#include <QVariant>

using kshadowlrc::PlaybackStatus;
using kshadowlrc::TrackMeta;
namespace mpris = kshadowlrc::mpris;

class PlayerTrackerMetadataTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void completeMetadata();
    void missingFields();
    void emptyMetadata();
    void malformedArtists();
    void uniqueSongIdFallback();
    void lengthEdgeCases();
    void playbackStatusParsing();
    void plainMapPassthrough();
};

void PlayerTrackerMetadataTest::completeMetadata()
{
    // 1. A complete, well-formed metadata dictionary.
    QVariantMap metadata;
    metadata[QStringLiteral("xesam:title")] = QStringLiteral("Blue Monday");
    metadata[QStringLiteral("xesam:artist")] = QStringList{QStringLiteral("New Order")};
    metadata[QStringLiteral("xesam:album")] = QStringLiteral("Power, Corruption & Lies");
    metadata[QStringLiteral("mpris:length")] = QVariant::fromValue<qlonglong>(215000000); // 215 s
    metadata[QStringLiteral("xesam:url")] = QStringLiteral("file:///home/user/music/blue.mp3");
    metadata[QStringLiteral("mpris:trackid")] = QStringLiteral("/org/mpris/MediaPlayer2/Track/1");

    const TrackMeta track = mpris::trackMetaFromMpris(metadata);
    QVERIFY2(track.title == QStringLiteral("Blue Monday"), "title maps from xesam:title");
    QVERIFY2(track.artists == QStringList{QStringLiteral("New Order")}, "artists maps from xesam:artist");
    QVERIFY2(track.album == QStringLiteral("Power, Corruption & Lies"), "album maps from xesam:album");
    QVERIFY2(track.lengthMs == 215000, "mpris:length microseconds -> milliseconds");
    QVERIFY2(track.uniqueSongId == QStringLiteral("file:///home/user/music/blue.mp3"), "file:// url preferred for uniqueSongId");
    QVERIFY2(track.isValid(), "track with title is valid");
}

void PlayerTrackerMetadataTest::missingFields()
{
    // 2. Missing fields must not drop the track.
    QVariantMap metadata;
    metadata[QStringLiteral("xesam:title")] = QStringLiteral("Title Only");

    const TrackMeta track = mpris::trackMetaFromMpris(metadata);
    QVERIFY2(track.title == QStringLiteral("Title Only"), "title survives missing siblings");
    QVERIFY2(track.artists.isEmpty(), "missing artist -> empty list");
    QVERIFY2(track.album.isEmpty(), "missing album -> empty");
    QVERIFY2(track.lengthMs == 0, "missing length -> 0");
    QVERIFY2(track.uniqueSongId.isEmpty(), "missing url and trackid -> empty uniqueSongId");
}

void PlayerTrackerMetadataTest::emptyMetadata()
{
    // 3. A completely empty dictionary still yields an invalid but safe track.
    const TrackMeta track = mpris::trackMetaFromMpris(QVariantMap{});
    QVERIFY2(!track.isValid(), "empty metadata -> invalid track, no crash");
    QVERIFY2(track.lengthMs == 0, "empty metadata -> length 0");
}

void PlayerTrackerMetadataTest::malformedArtists()
{
    // 4. Malformed xesam:artist variants.
    QVariantMap metadata;
    metadata[QStringLiteral("xesam:artist")] = QStringLiteral("Solo Artist");
    QVERIFY2(mpris::artistsFromMetadata(metadata) == QStringList{QStringLiteral("Solo Artist")}, "artist as a bare string -> single-element list");

    metadata[QStringLiteral("xesam:artist")] = QVariant(42);
    QVERIFY2(mpris::artistsFromMetadata(metadata).isEmpty(), "artist as an int -> empty list, no crash");

    metadata[QStringLiteral("xesam:artist")] = QVariantList{QStringLiteral("A"), QVariant(7), QStringLiteral("B")};
    QVERIFY2((mpris::artistsFromMetadata(metadata) == QStringList{QStringLiteral("A"), QStringLiteral("B")}),
             "artist as a variant list -> strings kept, non-strings skipped");

    metadata[QStringLiteral("xesam:artist")] = QStringList{QStringLiteral("X"), QStringLiteral("Y")};
    QVERIFY2((mpris::artistsFromMetadata(metadata) == QStringList{QStringLiteral("X"), QStringLiteral("Y")}), "artist as a QStringList -> preserved in order");

    // Chromium reports an empty artist name; it is not an artist.
    metadata[QStringLiteral("xesam:artist")] = QStringList{QStringLiteral(""), QStringLiteral("Real")};
    QVERIFY2(mpris::artistsFromMetadata(metadata) == QStringList{QStringLiteral("Real")}, "empty artist strings are dropped");

    metadata[QStringLiteral("xesam:artist")] = QStringList{QStringLiteral("")};
    QVERIFY2(mpris::artistsFromMetadata(metadata).isEmpty(), "only-empty artist list -> empty");
}

void PlayerTrackerMetadataTest::uniqueSongIdFallback()
{
    // 5. uniqueSongId fallback rules.
    QVariantMap metadata;
    metadata[QStringLiteral("xesam:url")] = QStringLiteral("https://example.com/song");
    metadata[QStringLiteral("mpris:trackid")] = QStringLiteral("/org/mpris/MediaPlayer2/Track/42");
    QVERIFY2(mpris::uniqueSongIdFromMetadata(metadata) == QStringLiteral("/org/mpris/MediaPlayer2/Track/42"), "non-file url -> trackid wins");

    metadata[QStringLiteral("mpris:trackid")] = QVariant::fromValue(QDBusObjectPath(QStringLiteral("/org/mpris/MediaPlayer2/Track/7")));
    QVERIFY2(mpris::uniqueSongIdFromMetadata(metadata) == QStringLiteral("/org/mpris/MediaPlayer2/Track/7"),
             "QDBusObjectPath trackid is stringified to its path");

    QVariantMap onlyUrl;
    onlyUrl[QStringLiteral("xesam:url")] = QStringLiteral("https://example.com/stream");
    QVERIFY2(mpris::uniqueSongIdFromMetadata(onlyUrl).isEmpty(), "non-file url without trackid -> empty");
}

void PlayerTrackerMetadataTest::lengthEdgeCases()
{
    // 6. Length edge cases.
    QVERIFY2(mpris::lengthUsToMs(QVariant::fromValue<qlonglong>(12345678)) == 12345, "12345678 us -> 12345 ms");
    QVERIFY2(mpris::lengthUsToMs(QVariant::fromValue<qlonglong>(0)) == 0, "0 us -> 0 ms");
    QVERIFY2(mpris::lengthUsToMs(QVariant::fromValue<qlonglong>(-1)) == 0, "negative length -> 0 ms");
    QVERIFY2(mpris::lengthUsToMs(QVariant()) == 0, "invalid length -> 0 ms");
    QVERIFY2(mpris::lengthUsToMs(QVariant(QStringLiteral("1500000"))) == 1500, "length as a numeric string still parses");
}

void PlayerTrackerMetadataTest::playbackStatusParsing()
{
    // 7. PlaybackStatus parsing.
    QVERIFY2(mpris::playbackStatusFromString(QStringLiteral("Playing")) == PlaybackStatus::Playing, "Playing status");
    QVERIFY2(mpris::playbackStatusFromString(QStringLiteral("Paused")) == PlaybackStatus::Paused, "Paused status");
    QVERIFY2(mpris::playbackStatusFromString(QStringLiteral("Stopped")) == PlaybackStatus::Stopped, "Stopped status");
    QVERIFY2(mpris::playbackStatusFromString(QStringLiteral("garbage")) == PlaybackStatus::Unknown, "unknown status");
}

void PlayerTrackerMetadataTest::plainMapPassthrough()
{
    // 8. A plain (already-demarshalled) map passes through unchanged.
    QVariantMap metadata;
    metadata[QStringLiteral("xesam:title")] = QStringLiteral("Plain");
    QVERIFY2(mpris::variantMapFromDbus(QVariant::fromValue(metadata)) == metadata, "variantMapFromDbus passes a plain map through");
}

QTEST_GUILESS_MAIN(PlayerTrackerMetadataTest)

#include "playertracker_metadata_test.moc"
