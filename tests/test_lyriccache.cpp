// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 KShadowLrc Contributors
//
// Offline checks for cache key stability and the write/read round trip.

#include "lyrics/lyriccache.h"

#include <QCoreApplication>
#include <QFile>
#include <QStandardPaths>

#include <cstdio>

using namespace kshadowlrc;

static int g_failures = 0;

#define CHECK(cond)                                                                                                                                            \
    do {                                                                                                                                                       \
        if (!(cond)) {                                                                                                                                         \
            ++g_failures;                                                                                                                                      \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                                                               \
        }                                                                                                                                                      \
    } while (0)

static TrackMeta track(qint64 lengthMs = 125000)
{
    TrackMeta t;
    t.title = QStringLiteral("Cache Test Song");
    t.album = QStringLiteral("Unit Test Album");
    t.artists = {QStringLiteral("Artist One"), QStringLiteral("Artist Two")};
    t.lengthMs = lengthMs;
    return t;
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("kshadowlrc"));
    // Redirect QStandardPaths to ~/.qttest so the test never touches real user
    // data and runs deterministically.
    QStandardPaths::setTestModeEnabled(true);

    const TrackMeta t = track();

    // Key stability and shape.
    const QString key1 = LyricCache::cacheKey(t);
    const QString key2 = LyricCache::cacheKey(track());
    CHECK(!key1.isEmpty());
    CHECK(key1.size() == 32);
    CHECK(key1 == key2);
    CHECK(LyricCache::cacheKey(track(999999)) != key1); // length is part of the key

    // A track without a title cannot be keyed.
    TrackMeta untitled = t;
    untitled.title.clear();
    CHECK(LyricCache::cacheKey(untitled).isEmpty());
    CHECK(LyricCache::cachePath(untitled).isEmpty());

    const QString path = LyricCache::cachePath(t);
    CHECK(!path.isEmpty());
    CHECK(path.contains(key1));
    CHECK(path.contains(QStringLiteral(".json")));
    // Three-level directory split of the digest.
    CHECK(
        path.contains(QStringLiteral("/") + key1.left(2) + QStringLiteral("/") + key1.mid(2, 2) + QStringLiteral("/") + key1.mid(4, 2) + QStringLiteral("/")));

    // A miss before anything was written.
    CHECK(!LyricCache::lookup(t).has_value());

    // An empty store is not cached.
    CHECK(!LyricCache::store(t, LyricStore()));
    CHECK(!LyricCache::lookup(t).has_value());

    // Fresh fetch round trip: offset defaults to 0.
    LyricStore store;
    store.lyric = QStringLiteral("[00:01.00]hello\n[00:02.00]world");
    store.tlyric = QStringLiteral("[00:01.00]你好\n[00:02.00]世界");
    CHECK(LyricCache::store(t, store));
    {
        const auto entry = LyricCache::lookup(t);
        CHECK(entry.has_value());
        if (entry) {
            CHECK(entry->store.lyric == store.lyric);
            CHECK(entry->store.tlyric == store.tlyric);
            CHECK(entry->offsetMs == 0);
        }
    }

    // A stored offset is restored on read.
    CHECK(LyricCache::store(t, store, 4200));
    {
        const auto entry = LyricCache::lookup(t);
        CHECK(entry.has_value());
        if (entry) {
            CHECK(entry->offsetMs == 4200);
            CHECK(entry->store.lyric == store.lyric);
        }
    }

    // Clean up the file this test wrote.
    QFile::remove(path);

    if (g_failures == 0) {
        std::printf("test_lyriccache: all checks passed\n");
        return 0;
    }
    std::fprintf(stderr, "test_lyriccache: %d check(s) failed\n", g_failures);
    return 1;
}
