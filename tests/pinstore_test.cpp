// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 KShadowLrc Contributors
//
// Offline checks for the pin store: the key rule, the disk round trip, and the
// tolerance of a missing or corrupt file. The store is isolated by pointing
// HOME, XDG_DATA_HOME and XDG_CONFIG_HOME at a private QTemporaryDir *before*
// the application object exists, so nothing here can reach the real
// ~/.local/share. QStandardPaths test mode alone would not be enough: it ignores
// XDG_DATA_HOME and would put the pin file in the shared, surviving ~/.qttest.

#include "lyrics/lyriccache.h"
#include "lyrics/pinstore.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

#include <cstdio>

using namespace kshadowlrc;

namespace
{

/*! A track with the player-supplied identity a local file gets. */
TrackMeta localTrack()
{
    TrackMeta track;
    track.title = QStringLiteral("Pin Test Song");
    track.album = QStringLiteral("Pin Test Album");
    track.artists = {QStringLiteral("Pin Test Artist")};
    track.lengthMs = 180000;
    track.uniqueSongId = QStringLiteral("file:///tmp/kshadowlrc-pin-test.flac");
    return track;
}

/*! A track the player could not name: no uniqueSongId, only metadata. */
TrackMeta metadataTrack()
{
    TrackMeta track;
    track.title = QStringLiteral("Metadata Only Song");
    track.album = QStringLiteral("Metadata Only Album");
    track.artists = {QStringLiteral("Metadata Only Artist")};
    track.lengthMs = 210000;
    return track;
}

pinstore::Pin samplePin()
{
    pinstore::Pin pin;
    pin.store.lyric = QStringLiteral("[00:01.00]line one\n[00:02.00]line two");
    pin.store.tlyric = QStringLiteral("[00:01.00]第一行\n[00:02.00]第二行");
    pin.source = QStringLiteral("NetEase — 光の中へ / ヨルシカ");
    pin.provider = QStringLiteral("netease");
    pin.songId = QStringLiteral("1234567");
    pin.title = QStringLiteral("光の中へ");
    pin.singer = QStringLiteral("ヨルシカ");
    return pin;
}

} // namespace

class PinStoreTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();

    void keyPrefersUniqueSongId();
    void keyFallsBackToMetadataDigest();
    void keyIsEmptyWithoutIdentityAndSaveRefuses();
    void roundTripsThroughDisk();
    void lookupRereadsTheFile();
    void unknownKeyIsNullopt();
    void missingFileIsTolerated();
    void corruptFileIsTolerated();
    void pinsLiveOutsideTheCache();
};

void PinStoreTest::initTestCase()
{
    // main() pointed HOME at the temporary directory; the pin file must resolve
    // under it, never under the developer's real home.
    const QString home = QString::fromUtf8(qgetenv("HOME"));
    QVERIFY(!home.isEmpty());
    QVERIFY(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation).startsWith(home));
    QVERIFY(pinstore::storePath().startsWith(home));
}

/*! A player-supplied identity is used verbatim, so a local file is pinned by path. */
void PinStoreTest::keyPrefersUniqueSongId()
{
    const TrackMeta track = localTrack();
    QCOMPARE(pinstore::keyFor(track), track.uniqueSongId);
}

/*! Without a uniqueSongId, the key is the same metadata digest the cache uses. */
void PinStoreTest::keyFallsBackToMetadataDigest()
{
    const TrackMeta track = metadataTrack();
    QVERIFY(track.uniqueSongId.isEmpty());

    const QString key = pinstore::keyFor(track);
    QVERIFY(!key.isEmpty());
    QCOMPARE(key, LyricCache::cacheKey(track));
    QCOMPARE(key.size(), 32); // an MD5 hex digest, like a cache key
}

/*! No identity and no title: nothing to save under, so save refuses. */
void PinStoreTest::keyIsEmptyWithoutIdentityAndSaveRefuses()
{
    TrackMeta track;
    track.lengthMs = 90000; // neither uniqueSongId nor title

    QVERIFY(pinstore::keyFor(track).isEmpty());
    QVERIFY(!pinstore::save(track, samplePin()));
    QVERIFY(!pinstore::lookup(track).has_value());
    QVERIFY(!pinstore::forget(track));
}

/*! A pin survives a write, a read and a removal, and shares the file with others. */
void PinStoreTest::roundTripsThroughDisk()
{
    const TrackMeta track = localTrack();
    const pinstore::Pin pin = samplePin();

    QVERIFY(pinstore::save(track, pin));
    QVERIFY(QFile::exists(pinstore::storePath()));

    const auto loaded = pinstore::lookup(track);
    QVERIFY(loaded.has_value());
    if (loaded) {
        QCOMPARE(loaded->store.lyric, pin.store.lyric);
        QCOMPARE(loaded->store.tlyric, pin.store.tlyric);
        QCOMPARE(loaded->source, pin.source);
        QCOMPARE(loaded->provider, pin.provider);
        QCOMPARE(loaded->songId, pin.songId);
        QCOMPARE(loaded->title, pin.title);
        QCOMPARE(loaded->singer, pin.singer);
    }

    // Saving again for the same track replaces the entry.
    pinstore::Pin updated = pin;
    updated.source = QStringLiteral("Replaced");
    QVERIFY(pinstore::save(track, updated));
    const auto reloaded = pinstore::lookup(track);
    QVERIFY(reloaded.has_value());
    if (reloaded) {
        QCOMPARE(reloaded->source, QStringLiteral("Replaced"));
    }

    // A second track's pin coexists: the file is one object with several keys.
    const TrackMeta other = metadataTrack();
    pinstore::Pin otherPin = samplePin();
    otherPin.store.tlyric.clear();
    otherPin.source = QStringLiteral("LRCLIB — Metadata Only Song");
    otherPin.provider = QStringLiteral("lrclib");
    QVERIFY(pinstore::save(other, otherPin));
    QVERIFY(pinstore::lookup(track).has_value());
    QVERIFY(pinstore::lookup(other).has_value());

    // forget removes only its own key.
    QVERIFY(pinstore::forget(track));
    QVERIFY(!pinstore::lookup(track).has_value());
    QVERIFY(pinstore::lookup(other).has_value());

    QVERIFY(pinstore::forget(other));
    QVERIFY(!pinstore::lookup(other).has_value());
}

/*! The file is re-read on every lookup, so a hand edit takes effect at once. */
void PinStoreTest::lookupRereadsTheFile()
{
    const TrackMeta track = localTrack();
    QVERIFY(pinstore::save(track, samplePin()));

    QFile file(pinstore::storePath());
    QVERIFY(file.open(QIODevice::ReadOnly));
    QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    file.close();

    const QString key = pinstore::keyFor(track);
    QJsonObject entry = root.value(key).toObject();
    entry.insert(QStringLiteral("source"), QStringLiteral("Hand edited"));
    entry.insert(QStringLiteral("lyric"), QStringLiteral("plain body"));
    root.insert(key, entry);

    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QVERIFY(file.write(QJsonDocument(root).toJson()) > 0);
    file.close();

    const auto loaded = pinstore::lookup(track);
    QVERIFY(loaded.has_value());
    if (loaded) {
        QCOMPARE(loaded->source, QStringLiteral("Hand edited"));
        QCOMPARE(loaded->store.lyric, QStringLiteral("plain body"));
    }

    QVERIFY(pinstore::forget(track));
}

/*! A key that was never pinned is a miss, whether looked up or forgotten. */
void PinStoreTest::unknownKeyIsNullopt()
{
    const TrackMeta pinned = localTrack();
    QVERIFY(pinstore::save(pinned, samplePin()));

    TrackMeta other = localTrack();
    other.uniqueSongId = QStringLiteral("file:///tmp/kshadowlrc-pin-test-other.flac");
    QVERIFY(!pinstore::lookup(other).has_value());
    QVERIFY(!pinstore::forget(other)); // nothing pinned for it

    QVERIFY(pinstore::forget(pinned));
}

/*! No pin file yet: a miss, not an error. */
void PinStoreTest::missingFileIsTolerated()
{
    QFile::remove(pinstore::storePath());
    QVERIFY(!QFile::exists(pinstore::storePath()));

    QVERIFY(!pinstore::lookup(localTrack()).has_value());
}

/*! A corrupt file loses its pins but never crashes, and a save recovers it. */
void PinStoreTest::corruptFileIsTolerated()
{
    QDir().mkpath(QFileInfo(pinstore::storePath()).absolutePath());

    QFile file(pinstore::storePath());
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QVERIFY(file.write("{ this is not json") > 0);
    file.close();

    QVERIFY(!pinstore::lookup(localTrack()).has_value());

    // Valid JSON that is not an object is corrupt for our purposes too.
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QVERIFY(file.write("[1, 2, 3]") > 0);
    file.close();
    QVERIFY(!pinstore::lookup(localTrack()).has_value());

    // The corrupt content is replaced on the next save rather than blocking it.
    QVERIFY(pinstore::save(localTrack(), samplePin()));
    QVERIFY(pinstore::lookup(localTrack()).has_value());
    QVERIFY(pinstore::forget(localTrack()));
}

/*! The pin file lives under AppDataLocation, never under the disposable cache. */
void PinStoreTest::pinsLiveOutsideTheCache()
{
    const QString path = pinstore::storePath();
    QVERIFY(path.endsWith(QStringLiteral("/pins.json")));
    QVERIFY(path.startsWith(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)));

    const QString cacheDir = LyricCache::cacheDir();
    QVERIFY(!cacheDir.isEmpty());
    QVERIFY2(!path.startsWith(cacheDir), "a pin must not live under the disposable cache");
}

int main(int argc, char **argv)
{
    QTemporaryDir tempDir;
    if (!tempDir.isValid()) {
        std::fprintf(stderr, "FAIL: could not create a temporary home directory\n");
        return 1;
    }
    // Must happen before QCoreApplication reads any standard path. HOME is what
    // relocates the test-mode paths QStandardPaths writes; the XDG_* variables
    // cover the normal (non-test-mode) locations as well, so the file cannot land
    // in the real home whichever path Qt resolves.
    qputenv("HOME", tempDir.path().toUtf8());
    qputenv("XDG_DATA_HOME", (tempDir.path() + QStringLiteral("/data")).toUtf8());
    qputenv("XDG_CONFIG_HOME", (tempDir.path() + QStringLiteral("/config")).toUtf8());
    qputenv("XDG_CACHE_HOME", (tempDir.path() + QStringLiteral("/cache")).toUtf8());

    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("kshadowlrc"));
    QStandardPaths::setTestModeEnabled(true);

    PinStoreTest tc;
    return QTest::qExec(&tc, argc, argv);
}

#include "pinstore_test.moc"
