// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 KShadowLrc Contributors
//
// Offline checks for the engine's order-driven resolution walk: which source is
// consulted first, that a local hit never reaches the network, that a lyric
// without timestamps is refused everywhere the walk looks, and where the cache
// sits. No network, no D-Bus, no event loop.

#include "enginestubs.h"

#include "lyrics/lyriccache.h"
#include "lyrics/lyricengine.h"

#include <QFile>
#include <QStandardPaths>
#include <QTest>

using namespace kshadowlrc;

namespace
{

/*! Remove any cached entry for \p track so the run is deterministic. */
void clearCache(const TrackMeta &track)
{
    QFile::remove(LyricCache::cachePath(track));
}

} // namespace

class LyricEngineOrderTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();

    void defaultOrderIsNeteaseLrclibLocal();
    void localFirstWinsWithoutSearching();
    void providerOrderBeatsCandidateScore();
    void providerBeforeLocalWins();
    void localUsedWhenProviderYieldsNothing();
    void untimedLocalIsNotUsable();
    void untimedCandidateFallsThroughToTheNextCandidate();
    void providerWithOnlyUntimedLyricsMovesToTheNextSource();
    void cachedUntimedLyricIsSkipped();
    void onlyUntimedContentEndsNotFound();
    void untimedTrackDoesNotLeakIntoTheNextTrack();
    void unknownIdsAreDropped();
    void pendingQueryIsNotDuplicatedByAnotherSearch();

    void providerFirstUsesCacheWithoutSearching();
    void localFirstBeatsCachedProvider();
    void localResultIsNeverCached();
};

void LyricEngineOrderTest::initTestCase()
{
    QCoreApplication::setApplicationName(QStringLiteral("kshadowlrc"));
    QStandardPaths::setTestModeEnabled(true);
}

/*! With nothing configured, a provider is tried before the local file. */
void LyricEngineOrderTest::defaultOrderIsNeteaseLrclibLocal()
{
    const TrackMeta track = stubTrack(QStringLiteral("Order Default Song"));
    clearCache(track);

    StubProvider netease(QStringLiteral("netease"));
    netease.searchResults = {stubCandidate(QStringLiteral("n1"), track.title)};
    netease.lyricStore = timedStore(QStringLiteral("[00:01.00]Provider line"));

    StubLocalSource local;
    local.store = timedStore(QStringLiteral("[00:01.00]Local line"));

    LyricEngine engine;
    engine.setProviders({&netease});
    engine.setLocalSource(&local);
    // No source order set: the built-in default applies.
    engine.setTrack(track);

    QVERIFY(engine.state() == LyricEngine::State::Found);
    QVERIFY(engine.lineText(0) == QStringLiteral("Provider line"));
    QVERIFY(local.loadCalls == 0); // local is last in the default order
}

/*! The only falsifiable form of "a local hit does not go to the network". */
void LyricEngineOrderTest::localFirstWinsWithoutSearching()
{
    const TrackMeta track = stubTrack(QStringLiteral("Order Local First"));
    clearCache(track);

    StubProvider netease(QStringLiteral("netease"));
    netease.searchResults = {stubCandidate(QStringLiteral("n1"), track.title)};
    netease.lyricStore = timedStore(QStringLiteral("[00:01.00]Provider line"));

    StubLocalSource local;
    local.store = timedStore(QStringLiteral("[00:01.00]Local line\n[00:02.00]Local two"));

    LyricEngine engine;
    engine.setProviders({&netease});
    engine.setLocalSource(&local);
    engine.setSourceOrder({QStringLiteral("local"), QStringLiteral("netease")});
    engine.setTrack(track);

    QVERIFY(engine.state() == LyricEngine::State::Found);
    QVERIFY(engine.lineText(0) == QStringLiteral("Local line"));
    QVERIFY(local.loadCalls == 1);
    QCOMPARE(netease.searchCalls, 0); // the local winner stopped the walk before the network
}

/*!
 * Provider position is the primary ranking key: LRCLib's weak candidate is
 * queried and wins even though the NetEase candidate scores far higher. Both
 * providers were still searched, so the order did not cost search latency.
 */
void LyricEngineOrderTest::providerOrderBeatsCandidateScore()
{
    const TrackMeta track = stubTrack(QStringLiteral("Order Provider Primary"));
    clearCache(track);

    StubProvider lrclib(QStringLiteral("lrclib"));
    SongInfo poor;
    poor.id = QStringLiteral("l1");
    poor.title = QStringLiteral("A Completely Unrelated Track");
    poor.singer = QStringLiteral("Nobody At All");
    poor.album = QStringLiteral("No Album");
    poor.lengthMs = 1000;
    lrclib.searchResults = {poor};
    lrclib.lyricStore = timedStore(QStringLiteral("[00:01.00]Lrclib line"));

    StubProvider netease(QStringLiteral("netease"));
    netease.searchResults = {stubCandidate(QStringLiteral("n1"), track.title)}; // rank 0
    netease.lyricStore = timedStore(QStringLiteral("[00:01.00]Netease line"));

    LyricEngine engine;
    engine.setProviders({&lrclib, &netease});
    engine.setSourceOrder({QStringLiteral("lrclib"), QStringLiteral("netease")});
    engine.setTrack(track);

    QVERIFY(engine.state() == LyricEngine::State::Found);
    QVERIFY(engine.lineText(0) == QStringLiteral("Lrclib line"));
    QCOMPARE(lrclib.queryCalls, 1);
    QCOMPARE(netease.queryCalls, 0); // a later provider is never consulted after a winner
    QCOMPARE(netease.searchCalls, 1); // but it was searched up front, in parallel
}

/*! A provider placed before local wins; the local step is never reached. */
void LyricEngineOrderTest::providerBeforeLocalWins()
{
    const TrackMeta track = stubTrack(QStringLiteral("Order Provider First"));
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
    engine.setTrack(track);

    QVERIFY(engine.state() == LyricEngine::State::Found);
    QVERIFY(engine.lineText(0) == QStringLiteral("Provider line"));
    QVERIFY(local.loadCalls == 0); // the walk stopped before the local step
    QCOMPARE(netease.searchCalls, 1);
}

/*! When the provider yields nothing usable, the walk falls through to local. */
void LyricEngineOrderTest::localUsedWhenProviderYieldsNothing()
{
    const TrackMeta track = stubTrack(QStringLiteral("Order Fallthrough"));
    clearCache(track);

    StubProvider netease(QStringLiteral("netease"));
    netease.searchResults = {stubCandidate(QStringLiteral("n1"), track.title)};
    netease.lyricStore = LyricStore{}; // a candidate that carries no lyric at all

    StubLocalSource local;
    local.store = timedStore(QStringLiteral("[00:01.00]Local line"));

    LyricEngine engine;
    engine.setProviders({&netease});
    engine.setLocalSource(&local);
    engine.setSourceOrder({QStringLiteral("netease"), QStringLiteral("local")});
    engine.setTrack(track);

    QVERIFY(engine.state() == LyricEngine::State::Found);
    QVERIFY(engine.lineText(0) == QStringLiteral("Local line"));
    QCOMPARE(netease.searchCalls, 1);
    QVERIFY(local.loadCalls == 1);
}

/*!
 * A plain (untimed) local lyric cannot be synced, so it is not a lyric at all:
 * it never shadows a provider's timed one, and when nothing else yields a timed
 * lyric the result is NotFound with nothing to show.
 */
void LyricEngineOrderTest::untimedLocalIsNotUsable()
{
    // (a) A provider's timed lyric wins over the unusable plain local one.
    {
        const TrackMeta track = stubTrack(QStringLiteral("Order Untimed Local Provider Wins"));
        clearCache(track);

        StubProvider netease(QStringLiteral("netease"));
        netease.searchResults = {stubCandidate(QStringLiteral("n1"), track.title)};
        netease.lyricStore = timedStore(QStringLiteral("[00:01.00]Provider line"));

        StubLocalSource local;
        local.store = LyricStore{QStringLiteral("plain text\nwith no timestamps at all"), QString()};

        LyricEngine engine;
        engine.setProviders({&netease});
        engine.setLocalSource(&local);
        engine.setSourceOrder({QStringLiteral("local"), QStringLiteral("netease")});
        engine.setTrack(track);

        QVERIFY(engine.state() == LyricEngine::State::Found);
        QVERIFY(engine.lineText(0) == QStringLiteral("Provider line"));
        QVERIFY(local.loadCalls == 1); // it was tried...
        QCOMPARE(netease.searchCalls, 1); // ...and did not stop the walk
    }

    // (b) With nothing else usable, the untimed local body is not shown at all.
    {
        const TrackMeta track = stubTrack(QStringLiteral("Order Untimed Local Only"));
        clearCache(track);

        StubProvider netease(QStringLiteral("netease"));
        netease.searchResults.clear(); // the provider answers with nothing at all

        StubLocalSource local;
        local.store = LyricStore{QStringLiteral("plain text\nwith no timestamps at all\nthird line"), QString()};

        LyricEngine engine;
        engine.setProviders({&netease});
        engine.setLocalSource(&local);
        engine.setSourceOrder({QStringLiteral("local"), QStringLiteral("netease")});
        engine.setTrack(track);

        QVERIFY(engine.state() == LyricEngine::State::NotFound);
        QVERIFY(engine.currentText().isEmpty());
        QVERIFY(engine.lastError().isEmpty());
        QVERIFY(local.loadCalls == 1);
        QCOMPARE(netease.searchCalls, 1);
    }
}

/*!
 * A provider's untimed candidate is not usable, so the walk tries that
 * provider's next ranked candidate; a timed one there wins.
 */
void LyricEngineOrderTest::untimedCandidateFallsThroughToTheNextCandidate()
{
    const TrackMeta track = stubTrack(QStringLiteral("Order Untimed Candidate"));
    clearCache(track);

    StubProvider netease(QStringLiteral("netease"));
    netease.autoDeliver = false; // answers each query by hand, so the two differ
    netease.searchResults = {stubCandidate(QStringLiteral("c1"), track.title), stubCandidate(QStringLiteral("c2"), track.title)};

    LyricEngine engine;
    engine.setProviders({&netease});
    engine.setSourceOrder({QStringLiteral("netease")});
    engine.setTrack(track);

    QCOMPARE(netease.m_searchIds.size(), 1);
    netease.deliverSearch(netease.m_searchIds.at(0), netease.searchResults);

    // Candidate 0 is queried first; its untimed lyric is refused.
    QCOMPARE(netease.m_queryIds.size(), 1);
    netease.deliverLyric(netease.m_queryIds.at(0), QStringLiteral("c1"), LyricStore{QStringLiteral("plain text\nno timestamps"), QString()});
    QVERIFY(engine.state() == LyricEngine::State::Loading);

    // The walk falls through to candidate 1, whose timed lyric wins.
    QCOMPARE(netease.m_queryIds.size(), 2);
    netease.deliverLyric(netease.m_queryIds.at(1), QStringLiteral("c2"), timedStore(QStringLiteral("[00:01.00]Second candidate")));

    QVERIFY(engine.state() == LyricEngine::State::Found);
    QCOMPARE(engine.lineText(0), QStringLiteral("Second candidate"));
    QCOMPARE(netease.queryCalls, 2);

    clearCache(track);
}

/*! A provider that can only answer with untimed lyrics moves the walk to the next source. */
void LyricEngineOrderTest::providerWithOnlyUntimedLyricsMovesToTheNextSource()
{
    const TrackMeta track = stubTrack(QStringLiteral("Order Untimed Provider"));
    clearCache(track);

    StubProvider netease(QStringLiteral("netease"));
    netease.searchResults = {stubCandidate(QStringLiteral("n1"), track.title)};
    netease.lyricStore = LyricStore{QStringLiteral("only a plain body"), QString()};

    StubLocalSource local;
    local.store = timedStore(QStringLiteral("[00:01.00]Local line"));

    LyricEngine engine;
    engine.setProviders({&netease});
    engine.setLocalSource(&local);
    engine.setSourceOrder({QStringLiteral("netease"), QStringLiteral("local")});
    engine.setTrack(track);

    QVERIFY(engine.state() == LyricEngine::State::Found);
    QCOMPARE(engine.lineText(0), QStringLiteral("Local line"));
    QCOMPARE(netease.queryCalls, 1); // answered, with an unusable lyric
    QVERIFY(local.loadCalls == 1); // so the walk moved on to the next source
    QVERIFY(engine.lastError().isEmpty()); // an unusable answer is not a failure
}

/*! A cached untimed lyric is skipped, and its saved offset is not applied either. */
void LyricEngineOrderTest::cachedUntimedLyricIsSkipped()
{
    const TrackMeta track = stubTrack(QStringLiteral("Order Untimed Cache"));
    clearCache(track);
    // A cached plain body with a saved offset: had it been applied, both the text
    // and the offset would show.
    QVERIFY(LyricCache::store(track, LyricStore{QStringLiteral("cached plain body"), QString()}, 5000));

    StubProvider netease(QStringLiteral("netease"));
    netease.searchResults = {stubCandidate(QStringLiteral("n1"), track.title)};
    netease.lyricStore = timedStore(QStringLiteral("[00:01.00]Fresh line"));

    LyricEngine engine;
    engine.setProviders({&netease});
    engine.setSourceOrder({QStringLiteral("netease")});
    engine.setTrack(track);

    QVERIFY(engine.state() == LyricEngine::State::Found);
    QCOMPARE(engine.lineText(0), QStringLiteral("Fresh line"));
    QCOMPARE(engine.lyricOffsetMs(), 0); // the cached 5000 never reached the engine
    QCOMPARE(netease.searchCalls, 1); // the skipped cache sent the walk to the network

    clearCache(track);
}

/*!
 * A song whose only lyrics are untimed ends as "no lyrics", not as Found: local,
 * cache and provider are all consulted and all refused.
 */
void LyricEngineOrderTest::onlyUntimedContentEndsNotFound()
{
    const TrackMeta track = stubTrack(QStringLiteral("Order Only Untimed"));
    clearCache(track);
    QVERIFY(LyricCache::store(track, LyricStore{QStringLiteral("cached plain body"), QString()}, 0));

    StubProvider netease(QStringLiteral("netease"));
    netease.searchResults = {stubCandidate(QStringLiteral("n1"), track.title)};
    netease.lyricStore = LyricStore{QStringLiteral("provider plain body"), QString()};

    StubLocalSource local;
    local.store = LyricStore{QStringLiteral("plain local body\nsecond line"), QString()};

    LyricEngine engine;
    engine.setProviders({&netease});
    engine.setLocalSource(&local);
    engine.setSourceOrder({QStringLiteral("local"), QStringLiteral("netease")});
    engine.setTrack(track);

    QVERIFY(engine.state() == LyricEngine::State::NotFound);
    QVERIFY(engine.currentText().isEmpty()); // nothing to show...
    QVERIFY(engine.lineWindow(0, 10).isEmpty()); // ...and no upcoming lines either
    QVERIFY(engine.lastError().isEmpty()); // untimed answers are not failures
    QVERIFY(local.loadCalls == 1);
    QCOMPARE(netease.searchCalls, 1);
    QCOMPARE(netease.queryCalls, 1);

    clearCache(track);
}

/*!
 * An untimed track leaves nothing behind: the next track's provider lyric shows,
 * never a stale plain body from the previous one.
 */
void LyricEngineOrderTest::untimedTrackDoesNotLeakIntoTheNextTrack()
{
    const TrackMeta first = stubTrack(QStringLiteral("Order Untimed First"));
    const TrackMeta second = stubTrack(QStringLiteral("Order Untimed Second"));
    clearCache(first);
    clearCache(second);

    StubProvider netease(QStringLiteral("netease"));
    netease.searchResults.clear(); // nothing usable for the first track

    StubLocalSource local;
    local.store = LyricStore{QStringLiteral("plain local body\nno timestamps"), QString()};

    LyricEngine engine;
    engine.setProviders({&netease});
    engine.setLocalSource(&local);
    engine.setSourceOrder({QStringLiteral("local"), QStringLiteral("netease")});

    engine.setTrack(first);
    QVERIFY(engine.state() == LyricEngine::State::NotFound);
    QVERIFY(engine.currentText().isEmpty());

    // The second track has a usable provider lyric; it must show, with no trace
    // of the first track's untimed body.
    netease.searchResults = {stubCandidate(QStringLiteral("n2"), second.title)};
    netease.lyricStore = timedStore(QStringLiteral("[00:01.00]Second track line"));
    engine.setTrack(second);

    QVERIFY(engine.state() == LyricEngine::State::Found);
    QCOMPARE(engine.lineText(0), QStringLiteral("Second track line"));
    QVERIFY(local.loadCalls == 2);

    clearCache(second);
}

/*! Unknown ids are dropped; missing known ones are appended in default order. */
void LyricEngineOrderTest::unknownIdsAreDropped()
{
    const TrackMeta track = stubTrack(QStringLiteral("Order Unknown Id"));
    clearCache(track);

    StubProvider netease(QStringLiteral("netease"));
    netease.searchResults = {stubCandidate(QStringLiteral("n1"), track.title)};
    netease.lyricStore = timedStore(QStringLiteral("[00:01.00]Provider line"));

    StubLocalSource local;
    local.store = timedStore(QStringLiteral("[00:01.00]Local line"));

    LyricEngine engine;
    engine.setProviders({&netease});
    engine.setLocalSource(&local);
    // "bogus" is dropped; "local" stays first; "netease" is appended after it.
    engine.setSourceOrder({QStringLiteral("bogus"), QStringLiteral("local")});
    engine.setTrack(track);

    QVERIFY(engine.state() == LyricEngine::State::Found);
    QVERIFY(engine.lineText(0) == QStringLiteral("Local line"));
    QCOMPARE(netease.searchCalls, 0);
}

/*!
 * An unrelated provider's search answer arriving while a lyric query is in
 * flight must not re-issue that same query: one queryLyric() per candidate.
 */
void LyricEngineOrderTest::pendingQueryIsNotDuplicatedByAnotherSearch()
{
    const TrackMeta track = stubTrack(QStringLiteral("Order Pending Query"));
    clearCache(track);

    StubProvider netease(QStringLiteral("netease"));
    netease.autoDeliver = false;
    netease.searchResults = {stubCandidate(QStringLiteral("n1"), track.title)};

    StubProvider lrclib(QStringLiteral("lrclib"));
    lrclib.autoDeliver = false;
    lrclib.searchResults = {stubCandidate(QStringLiteral("l1"), track.title)};

    LyricEngine engine;
    engine.setProviders({&netease, &lrclib});
    engine.setSourceOrder({QStringLiteral("netease"), QStringLiteral("lrclib")});
    engine.setTrack(track);

    QCOMPARE(netease.m_searchIds.size(), 1);
    QCOMPARE(lrclib.m_searchIds.size(), 1);

    // NetEase answers first, so its candidate is queried and left in flight.
    netease.deliverSearch(netease.m_searchIds.at(0), netease.searchResults);
    QCOMPARE(netease.m_queryIds.size(), 1);

    // LRCLib's search now answers; it must not touch the in-flight NetEase query.
    lrclib.deliverSearch(lrclib.m_searchIds.at(0), lrclib.searchResults);
    QCOMPARE(netease.m_queryIds.size(), 1);
    QCOMPARE(lrclib.m_queryIds.size(), 0);

    // Completing the NetEase query resolves the track; lrclib is never queried.
    netease.deliverLyric(netease.m_queryIds.at(0), QStringLiteral("n1"), timedStore(QStringLiteral("[00:01.00]Netease line")));
    QVERIFY(engine.state() == LyricEngine::State::Found);
    QVERIFY(engine.lineText(0) == QStringLiteral("Netease line"));
    QCOMPARE(lrclib.m_queryIds.size(), 0);

    clearCache(track);
}

/*! A provider first consults the cache and, on a hit, never touches the network. */
void LyricEngineOrderTest::providerFirstUsesCacheWithoutSearching()
{
    const TrackMeta track = stubTrack(QStringLiteral("Order Cached Provider"));
    clearCache(track);
    QVERIFY(LyricCache::store(track, timedStore(QStringLiteral("[00:05.00]Cached line")), 0));

    StubProvider netease(QStringLiteral("netease"));
    netease.searchResults = {stubCandidate(QStringLiteral("n1"), track.title)};
    netease.lyricStore = timedStore(QStringLiteral("[00:01.00]Fresh line"));

    StubLocalSource local;
    local.store = timedStore(QStringLiteral("[00:01.00]Local line"));

    LyricEngine engine;
    engine.setProviders({&netease});
    engine.setLocalSource(&local);
    engine.setSourceOrder({QStringLiteral("netease"), QStringLiteral("lrclib")});
    engine.setTrack(track);

    QVERIFY(engine.state() == LyricEngine::State::Found);
    QVERIFY(engine.lineText(0) == QStringLiteral("Cached line"));
    QCOMPARE(netease.searchCalls, 0);
    QVERIFY(local.loadCalls == 0);

    clearCache(track);
}

/*! A local lyric placed first outranks a cached provider lyric. */
void LyricEngineOrderTest::localFirstBeatsCachedProvider()
{
    const TrackMeta track = stubTrack(QStringLiteral("Order Local Beats Cache"));
    clearCache(track);
    QVERIFY(LyricCache::store(track, timedStore(QStringLiteral("[00:05.00]Cached line")), 0));

    StubProvider netease(QStringLiteral("netease"));
    netease.searchResults = {stubCandidate(QStringLiteral("n1"), track.title)};
    netease.lyricStore = timedStore(QStringLiteral("[00:01.00]Fresh line"));

    StubLocalSource local;
    local.store = timedStore(QStringLiteral("[00:01.00]Local line"));

    LyricEngine engine;
    engine.setProviders({&netease});
    engine.setLocalSource(&local);
    engine.setSourceOrder({QStringLiteral("local"), QStringLiteral("netease")});
    engine.setTrack(track);

    QVERIFY(engine.state() == LyricEngine::State::Found);
    QVERIFY(engine.lineText(0) == QStringLiteral("Local line"));
    QCOMPARE(netease.searchCalls, 0);

    // The local win must not have touched the cache; the provider entry survives.
    const std::optional<CacheEntry> cached = LyricCache::lookup(track);
    QVERIFY(cached.has_value());
    QVERIFY(cached->store.lyric.contains(QStringLiteral("Cached line")));

    clearCache(track);
}

/*! A local result is never written to the cache, so an edit takes effect at once. */
void LyricEngineOrderTest::localResultIsNeverCached()
{
    const TrackMeta track = stubTrack(QStringLiteral("Order Local Uncached"));
    clearCache(track);

    StubProvider netease(QStringLiteral("netease"));
    netease.searchResults = {stubCandidate(QStringLiteral("n1"), track.title)};
    netease.lyricStore = timedStore(QStringLiteral("[00:01.00]Fresh line"));

    StubLocalSource local;
    local.store = timedStore(QStringLiteral("[00:01.00]Local line"));

    LyricEngine engine;
    engine.setProviders({&netease});
    engine.setLocalSource(&local);
    engine.setSourceOrder({QStringLiteral("local"), QStringLiteral("netease")});
    engine.setTrack(track);

    QVERIFY(engine.state() == LyricEngine::State::Found);
    QVERIFY(engine.lineText(0) == QStringLiteral("Local line"));
    QVERIFY(!LyricCache::lookup(track).has_value()); // nothing written for the local hit
}

QTEST_GUILESS_MAIN(LyricEngineOrderTest)

#include "lyricengine_order_test.moc"
