// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 KShadowLrc Contributors
//
// Offline checks for the lyric engine pipeline using a fake provider.

#include "lyrics/lyriccache.h"
#include "lyrics/lyricengine.h"

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

class FakeProvider : public LyricProvider
{
public:
    explicit FakeProvider(QString name)
        : m_name(std::move(name))
    {
    }

    QString name() const override
    {
        return m_name;
    }
    QString displayName() const override
    {
        return m_name;
    }

    void searchSong(quint64 requestId, const TrackMeta &track, int limit) override
    {
        Q_UNUSED(track)
        Q_UNUSED(limit)
        m_searchIds.append(requestId);
        if (autoDeliver) {
            Q_EMIT searchFinished(requestId, searchResults);
        }
    }

    void queryLyric(quint64 requestId, const QString &songId) override
    {
        m_queryIds.append(requestId);
        if (autoDeliver) {
            Q_EMIT lyricFinished(requestId, songId, lyricStore);
        }
    }

    bool isLikelySongId(const QString &id) const override
    {
        Q_UNUSED(id)
        return false;
    }

    void deliverSearch(quint64 requestId, const QList<SongInfo> &results)
    {
        Q_EMIT searchFinished(requestId, results);
    }
    void deliverLyric(quint64 requestId, const QString &songId, const LyricStore &store)
    {
        Q_EMIT lyricFinished(requestId, songId, store);
    }

    bool autoDeliver = true;
    QList<SongInfo> searchResults;
    LyricStore lyricStore;
    QList<quint64> m_searchIds;
    QList<quint64> m_queryIds;

private:
    QString m_name;
};
static TrackMeta makeTrack(const QString &title)
{
    TrackMeta t;
    t.title = title;
    t.album = QStringLiteral("Engine Test Album");
    t.artists = {QStringLiteral("Engine Test Artist")};
    t.lengthMs = 180000;
    return t;
}

static SongInfo makeCandidate(const QString &id, const QString &title)
{
    SongInfo s;
    s.id = id;
    s.title = title;
    s.singer = QStringLiteral("Engine Test Artist");
    s.album = QStringLiteral("Engine Test Album");
    s.lengthMs = 180000;
    return s;
}

/*! Remove any cached entry for \p track so the run is deterministic. */
static void clearCache(const TrackMeta &track)
{
    QFile::remove(LyricCache::cachePath(track));
}

static void testInvalidTrack()
{
    FakeProvider provider(QStringLiteral("fake"));
    LyricEngine engine;
    engine.setProviders({&provider});
    engine.setTrack(TrackMeta{});

    CHECK(engine.state() == LyricEngine::State::NotFound);
    CHECK(provider.m_searchIds.isEmpty());
}

static void testFoundWithTranslation()
{
    const TrackMeta track = makeTrack(QStringLiteral("Engine Found Song"));
    clearCache(track);

    FakeProvider provider(QStringLiteral("fake"));
    provider.searchResults = {makeCandidate(QStringLiteral("42"), track.title)};
    provider.lyricStore.lyric = QStringLiteral("[00:01.00]Hello\n[00:02.00]World");
    provider.lyricStore.tlyric = QStringLiteral("[00:01.00]你好\n[00:02.00]世界");

    LyricEngine engine;
    engine.setProviders({&provider});

    int stateChanges = 0;
    QObject::connect(&engine, &LyricEngine::stateChanged, [&stateChanges]() {
        ++stateChanges;
    });

    engine.setTrack(track);
    CHECK(engine.state() == LyricEngine::State::Found);
    CHECK(stateChanges >= 2); // Loading -> Found
    CHECK(engine.lineCount() == 2);
    CHECK(engine.lineText(0) == QStringLiteral("Hello"));
    CHECK(engine.translationText(0) == QStringLiteral("你好"));

    // Before the first line nothing is shown.
    CHECK(engine.currentIndex() == -1);
    CHECK(engine.currentText().isEmpty());

    engine.setElapsed(1000);
    CHECK(engine.currentIndex() == 0);
    CHECK(engine.currentText() == QStringLiteral("Hello"));

    engine.setDisplayMode(LyricDisplayMode::Both);
    CHECK(engine.currentText() == QStringLiteral("Hello"));
    CHECK(engine.currentTranslation() == QStringLiteral("你好"));

    engine.setDisplayMode(LyricDisplayMode::Translation);
    CHECK(engine.currentText() == QStringLiteral("你好"));

    engine.setElapsed(2000);
    CHECK(engine.currentIndex() == 1);
    engine.setDisplayMode(LyricDisplayMode::Both);
    CHECK(engine.currentText() == QStringLiteral("World"));
    CHECK(engine.currentTranslation() == QStringLiteral("世界"));

    // Window accessors for a multi-line UI.
    CHECK(engine.lineWindow(0, 2) == QStringList({QStringLiteral("Hello"), QStringLiteral("World")}));

    // The fresh fetch was cached.
    CHECK(LyricCache::lookup(track).has_value());
    clearCache(track);
}

static void testPlaceholderFiltered()
{
    const TrackMeta track = makeTrack(QStringLiteral("Engine Placeholder Song"));
    clearCache(track);

    FakeProvider provider(QStringLiteral("fake"));
    provider.searchResults = {makeCandidate(QStringLiteral("7"), track.title)};
    provider.lyricStore.lyric = QStringLiteral("[00:00.00]暂无歌词");

    LyricEngine engine;
    engine.setProviders({&provider});
    engine.setTrack(track);

    CHECK(engine.state() == LyricEngine::State::NotFound);
    CHECK(!LyricCache::lookup(track).has_value()); // nothing cached
}

static void testStaleResultIgnored()
{
    const TrackMeta first = makeTrack(QStringLiteral("Engine Stale One"));
    const TrackMeta second = makeTrack(QStringLiteral("Engine Stale Two"));
    clearCache(first);
    clearCache(second);

    FakeProvider provider(QStringLiteral("fake"));
    provider.autoDeliver = false;
    provider.searchResults = {makeCandidate(QStringLiteral("1"), first.title)};
    provider.lyricStore.lyric = QStringLiteral("[00:01.00]stale");

    LyricEngine engine;
    engine.setProviders({&provider});

    engine.setTrack(first);
    CHECK(provider.m_searchIds.size() == 1);
    const quint64 firstSearchId = provider.m_searchIds.at(0);

    engine.setTrack(second);
    CHECK(provider.m_searchIds.size() == 2);
    const quint64 secondSearchId = provider.m_searchIds.at(1);

    // The first track's reply lands late and must be ignored.
    provider.deliverSearch(firstSearchId, {makeCandidate(QStringLiteral("1"), first.title)});
    CHECK(engine.state() == LyricEngine::State::Loading);

    // The current track's reply drives the pipeline.
    provider.searchResults = {makeCandidate(QStringLiteral("2"), second.title)};
    provider.deliverSearch(secondSearchId, provider.searchResults);
    CHECK(provider.m_queryIds.size() == 1);

    provider.deliverLyric(provider.m_queryIds.at(0), QStringLiteral("2"), provider.lyricStore);
    CHECK(engine.state() == LyricEngine::State::Found);
    CHECK(engine.lineText(0) == QStringLiteral("stale"));

    clearCache(first);
    clearCache(second);
}

static void testNoResults()
{
    const TrackMeta track = makeTrack(QStringLiteral("Engine Empty Song"));
    clearCache(track);

    FakeProvider provider(QStringLiteral("fake"));
    provider.searchResults.clear();

    LyricEngine engine;
    engine.setProviders({&provider});
    engine.setTrack(track);
    CHECK(engine.state() == LyricEngine::State::NotFound);
}

static void testClearForgetsTheTrack()
{
    const TrackMeta track = makeTrack(QStringLiteral("Engine Clear Song"));
    clearCache(track);

    FakeProvider provider(QStringLiteral("fake"));
    provider.searchResults = {makeCandidate(QStringLiteral("77"), track.title)};
    provider.lyricStore.lyric = QStringLiteral("[00:01.00]Only line");

    LyricEngine engine;
    engine.setProviders({&provider});
    engine.setTrack(track);
    CHECK(engine.state() == LyricEngine::State::Found);
    CHECK(provider.m_searchIds.size() == 1);

    engine.clear();
    CHECK(engine.state() == LyricEngine::State::Idle);
    CHECK(engine.currentText().isEmpty());
    CHECK(!engine.track().isValid()); // the track is forgotten, not just the lyric

    // The same song must load again after a clear. A "processed track" guard left
    // set across clear() would swallow this and leave the overlay blank forever
    // for a song that had already played once. Drop the cache first so the check
    // sees the search path rather than a cache hit.
    clearCache(track);
    engine.setTrack(track);
    CHECK(engine.state() == LyricEngine::State::Found);
    CHECK(provider.m_searchIds.size() == 2);
    engine.setElapsed(1000);
    CHECK(engine.currentText() == QStringLiteral("Only line"));

    clearCache(track);
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("kshadowlrc"));
    QStandardPaths::setTestModeEnabled(true);

    testInvalidTrack();
    testFoundWithTranslation();
    testPlaceholderFiltered();
    testStaleResultIgnored();
    testNoResults();
    testClearForgetsTheTrack();

    if (g_failures == 0) {
        std::printf("test_lyricengine: all checks passed\n");
        return 0;
    }
    std::fprintf(stderr, "test_lyricengine: %d check(s) failed\n", g_failures);
    return 1;
}
