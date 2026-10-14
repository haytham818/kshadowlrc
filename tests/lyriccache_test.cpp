// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 KShadowLrc Contributors
//
// Offline checks for cache key stability and the write/read round trip.

#include "lyrics/lyriccache.h"

#include <QFile>
#include <QStandardPaths>
#include <QTest>

using namespace kshadowlrc;

static TrackMeta track(qint64 lengthMs = 125000)
{
    TrackMeta t;
    t.title = QStringLiteral("Cache Test Song");
    t.album = QStringLiteral("Unit Test Album");
    t.artists = {QStringLiteral("Artist One"), QStringLiteral("Artist Two")};
    t.lengthMs = lengthMs;
    return t;
}

class LyricCacheTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void roundTrip();
};

void LyricCacheTest::initTestCase()
{
    QCoreApplication::setApplicationName(QStringLiteral("kshadowlrc"));
    // Redirect QStandardPaths to ~/.qttest so the test never touches real user
    // data and runs deterministically.
    QStandardPaths::setTestModeEnabled(true);
}

void LyricCacheTest::roundTrip()
{
    const TrackMeta t = track();

    // Key stability and shape.
    const QString key1 = LyricCache::cacheKey(t);
    const QString key2 = LyricCache::cacheKey(track());
    QVERIFY(!key1.isEmpty());
    QVERIFY(key1.size() == 32);
    QVERIFY(key1 == key2);
    QVERIFY(LyricCache::cacheKey(track(999999)) != key1); // length is part of the key

    // A track without a title cannot be keyed.
    TrackMeta untitled = t;
    untitled.title.clear();
    QVERIFY(LyricCache::cacheKey(untitled).isEmpty());
    QVERIFY(LyricCache::cachePath(untitled).isEmpty());

    const QString path = LyricCache::cachePath(t);
    QVERIFY(!path.isEmpty());
    QVERIFY(path.contains(key1));
    QVERIFY(path.contains(QStringLiteral(".json")));
    // Three-level directory split of the digest.
    QVERIFY(
        path.contains(QStringLiteral("/") + key1.left(2) + QStringLiteral("/") + key1.mid(2, 2) + QStringLiteral("/") + key1.mid(4, 2) + QStringLiteral("/")));

    // A miss before anything was written.
    QVERIFY(!LyricCache::lookup(t).has_value());

    // An empty store is not cached.
    QVERIFY(!LyricCache::store(t, LyricStore()));
    QVERIFY(!LyricCache::lookup(t).has_value());

    // Fresh fetch round trip: offset defaults to 0.
    LyricStore store;
    store.lyric = QStringLiteral("[00:01.00]hello\n[00:02.00]world");
    store.tlyric = QStringLiteral("[00:01.00]你好\n[00:02.00]世界");
    QVERIFY(LyricCache::store(t, store));
    {
        const auto entry = LyricCache::lookup(t);
        QVERIFY(entry.has_value());
        if (entry) {
            QVERIFY(entry->store.lyric == store.lyric);
            QVERIFY(entry->store.tlyric == store.tlyric);
            QVERIFY(entry->offsetMs == 0);
        }
    }

    // A stored offset is restored on read.
    QVERIFY(LyricCache::store(t, store, 4200));
    {
        const auto entry = LyricCache::lookup(t);
        QVERIFY(entry.has_value());
        if (entry) {
            QVERIFY(entry->offsetMs == 4200);
            QVERIFY(entry->store.lyric == store.lyric);
        }
    }

    // Clean up the file this test wrote.
    QFile::remove(path);
}

QTEST_GUILESS_MAIN(LyricCacheTest)

#include "lyriccache_test.moc"
