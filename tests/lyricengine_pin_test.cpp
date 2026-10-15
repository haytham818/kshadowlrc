// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 KShadowLrc Contributors
//
// Offline checks for the engine's pinned-lyric override: a pin matching the
// track wins before the cache and before any search, an untimed pin is used
// as-is, a non-matching pin is ignored, and a pin is never written to the cache.
// Isolated the same way as pinstore_test.cpp: HOME and the XDG locations point
// at a private QTemporaryDir before the application object exists.

#include "enginestubs.h"

#include "lyrics/lyriccache.h"
#include "lyrics/lyricengine.h"
#include "lyrics/pinstore.h"

#include <QCoreApplication>
#include <QFile>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

#include <cstdio>

using namespace kshadowlrc;

namespace
{

/*! Remove any cached entry for \p track so the run is deterministic. */
void clearCache(const TrackMeta &track)
{
    QFile::remove(LyricCache::cachePath(track));
}

LyricStore pinnedStore(const QString &line)
{
    LyricStore store;
    store.lyric = line;
    return store;
}

} // namespace

class LyricEnginePinTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();

    void matchingPinPreventsAnySearch();
    void matchingPinIsReadBeforeTheCache();
    void nonMatchingPinIsIgnored();
    void untimedPinnedLyricIsUsedAsIs();
    void clearingThePinRestoresTheWalk();
    void pinnedLyricIsNotCached();
};

void LyricEnginePinTest::initTestCase()
{
    const QString home = QString::fromUtf8(qgetenv("HOME"));
    QVERIFY(!home.isEmpty());
    QVERIFY(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation).startsWith(home));
    QVERIFY(QStandardPaths::writableLocation(QStandardPaths::CacheLocation).startsWith(home));
}

/*! A matching pin answers the track outright: no search, no query, no local read. */
void LyricEnginePinTest::matchingPinPreventsAnySearch()
{
    const TrackMeta track = stubTrack(QStringLiteral("Pin Prevents Search"));
    clearCache(track);

    StubProvider netease(QStringLiteral("netease"));
    netease.searchResults = {stubCandidate(QStringLiteral("n1"), track.title)};
    netease.lyricStore = timedStore(QStringLiteral("[00:01.00]Provider line"));

    StubLocalSource local;
    local.store = timedStore(QStringLiteral("[00:01.00]Local line"));

    LyricEngine engine;
    engine.setProviders({&netease});
    engine.setLocalSource(&local);
    engine.setSourceOrder({QStringLiteral("netease"), QStringLiteral("local")});
    engine.setPinnedLyric(pinstore::keyFor(track), pinnedStore(QStringLiteral("[00:01.00]Pinned line")));
    engine.setTrack(track);

    QVERIFY(engine.state() == LyricEngine::State::Found);
    QCOMPARE(engine.lineText(0), QStringLiteral("Pinned line"));
    QCOMPARE(netease.searchCalls, 0);
    QCOMPARE(netease.queryCalls, 0);
    QCOMPARE(local.loadCalls, 0);
}

/*! The pin is applied before the cache, so a cached lyric and its offset lose. */
void LyricEnginePinTest::matchingPinIsReadBeforeTheCache()
{
    const TrackMeta track = stubTrack(QStringLiteral("Pin Before Cache"));
    clearCache(track);

    // A usable cache entry with a saved offset: had the cache been consulted it
    // would have supplied this text and this offset instead of the pin.
    QVERIFY(LyricCache::store(track, timedStore(QStringLiteral("[00:01.00]Cached line")), 5000));

    StubProvider netease(QStringLiteral("netease"));

    LyricEngine engine;
    engine.setProviders({&netease});
    engine.setSourceOrder({QStringLiteral("netease")});
    engine.setPinnedLyric(pinstore::keyFor(track), pinnedStore(QStringLiteral("[00:01.00]Pinned line")));
    engine.setTrack(track);

    QVERIFY(engine.state() == LyricEngine::State::Found);
    QCOMPARE(engine.lineText(0), QStringLiteral("Pinned line"));
    QCOMPARE(engine.lyricOffsetMs(), 0); // the cached 5000 never reached the engine
    QCOMPARE(netease.searchCalls, 0);

    QFile::remove(LyricCache::cachePath(track));
}

/*! A pin for another song does not touch this one: the normal walk still runs. */
void LyricEnginePinTest::nonMatchingPinIsIgnored()
{
    const TrackMeta track = stubTrack(QStringLiteral("Pin Non Matching"));
    clearCache(track);

    StubProvider netease(QStringLiteral("netease"));
    netease.searchResults = {stubCandidate(QStringLiteral("n1"), track.title)};
    netease.lyricStore = timedStore(QStringLiteral("[00:01.00]Provider line"));

    LyricEngine engine;
    engine.setProviders({&netease});
    engine.setSourceOrder({QStringLiteral("netease")});
    engine.setPinnedLyric(QStringLiteral("file:///tmp/some-other-song.mp3"), pinnedStore(QStringLiteral("[00:01.00]Stale pin")));
    engine.setTrack(track);

    QVERIFY(engine.state() == LyricEngine::State::Found);
    QCOMPARE(engine.lineText(0), QStringLiteral("Provider line"));
    QCOMPARE(netease.searchCalls, 1);
    QCOMPARE(netease.queryCalls, 1);
}

/*! An untimed pin is the user's choice, so it is kept, not deferred like a local file. */
void LyricEnginePinTest::untimedPinnedLyricIsUsedAsIs()
{
    const TrackMeta track = stubTrack(QStringLiteral("Pin Untimed"));
    clearCache(track);

    StubProvider netease(QStringLiteral("netease"));
    netease.searchResults = {stubCandidate(QStringLiteral("n1"), track.title)};
    netease.lyricStore = timedStore(QStringLiteral("[00:01.00]Provider line"));

    LyricEngine engine;
    engine.setProviders({&netease});
    engine.setSourceOrder({QStringLiteral("netease")});
    engine.setPinnedLyric(pinstore::keyFor(track), pinnedStore(QStringLiteral("plain pinned body\nsecond line")));
    engine.setTrack(track);

    QVERIFY(engine.state() == LyricEngine::State::Found);
    QCOMPARE(engine.originLyric().kind, LyricKind::NoTimestamp); // used as-is
    QCOMPARE(engine.currentText(), QStringLiteral("plain pinned body\nsecond line"));
    QCOMPARE(netease.searchCalls, 0);
}

/*! Clearing the pin (an empty key or store) hands the track back to the walk. */
void LyricEnginePinTest::clearingThePinRestoresTheWalk()
{
    const TrackMeta track = stubTrack(QStringLiteral("Pin Clearing"));
    clearCache(track);

    StubProvider netease(QStringLiteral("netease"));
    netease.searchResults = {stubCandidate(QStringLiteral("n1"), track.title)};
    netease.lyricStore = timedStore(QStringLiteral("[00:01.00]Provider line"));

    LyricEngine engine;
    engine.setProviders({&netease});
    engine.setSourceOrder({QStringLiteral("netease")});
    engine.setPinnedLyric(pinstore::keyFor(track), pinnedStore(QStringLiteral("[00:01.00]Pinned line")));
    engine.setTrack(track);
    QCOMPARE(engine.lineText(0), QStringLiteral("Pinned line"));
    QCOMPARE(netease.searchCalls, 0);
    QVERIFY(!engine.pinnedLyricKey().isEmpty());

    engine.setPinnedLyric(QString(), LyricStore{});
    QVERIFY(engine.pinnedLyricKey().isEmpty());
    QVERIFY(!engine.pinnedLyricStore().hasAny());

    engine.clear();
    engine.setTrack(track);
    QCOMPARE(engine.lineText(0), QStringLiteral("Provider line"));
    QCOMPARE(netease.searchCalls, 1);

    QFile::remove(LyricCache::cachePath(track));
}

/*! A pin is already the first priority, so it never grows a cache copy. */
void LyricEnginePinTest::pinnedLyricIsNotCached()
{
    const TrackMeta track = stubTrack(QStringLiteral("Pin Not Cached"));
    clearCache(track);

    LyricEngine engine;
    engine.setPinnedLyric(pinstore::keyFor(track), pinnedStore(QStringLiteral("[00:01.00]Pinned line")));
    engine.setTrack(track);

    QVERIFY(engine.state() == LyricEngine::State::Found);
    QVERIFY(!LyricCache::lookup(track).has_value());
}

int main(int argc, char **argv)
{
    QTemporaryDir tempDir;
    if (!tempDir.isValid()) {
        std::fprintf(stderr, "FAIL: could not create a temporary home directory\n");
        return 1;
    }
    // Before QCoreApplication: HOME relocates the test-mode paths QStandardPaths
    // writes, and the XDG_* variables cover the non-test-mode locations too.
    qputenv("HOME", tempDir.path().toUtf8());
    qputenv("XDG_DATA_HOME", (tempDir.path() + QStringLiteral("/data")).toUtf8());
    qputenv("XDG_CONFIG_HOME", (tempDir.path() + QStringLiteral("/config")).toUtf8());
    qputenv("XDG_CACHE_HOME", (tempDir.path() + QStringLiteral("/cache")).toUtf8());

    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("kshadowlrc"));
    QStandardPaths::setTestModeEnabled(true);

    LyricEnginePinTest tc;
    return QTest::qExec(&tc, argc, argv);
}

#include "lyricengine_pin_test.moc"
