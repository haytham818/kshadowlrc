// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 KShadowLrc Contributors
//
// Offline checks for the lyric engine pipeline using a fake provider.

#include "lyrics/lyriccache.h"
#include "lyrics/lyricengine.h"

#include <QFile>
#include <QStandardPaths>
#include <QTest>

using namespace kshadowlrc;

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

class LyricEngineTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void invalidTrack();
    void foundWithTranslation();
    void placeholderFiltered();
    void staleResultIgnored();
    void noResults();
    void clearForgetsTheTrack();
};

void LyricEngineTest::initTestCase()
{
    QCoreApplication::setApplicationName(QStringLiteral("kshadowlrc"));
    QStandardPaths::setTestModeEnabled(true);
}

void LyricEngineTest::invalidTrack()
{
    // "netease" is a source the engine knows by name; an arbitrary id would be
    // dropped by the source order now that it is user-configurable.
    FakeProvider provider(QStringLiteral("netease"));
    LyricEngine engine;
    engine.setProviders({&provider});
    engine.setTrack(TrackMeta{});

    QVERIFY(engine.state() == LyricEngine::State::NotFound);
    QVERIFY(provider.m_searchIds.isEmpty());
}

void LyricEngineTest::foundWithTranslation()
{
    const TrackMeta track = makeTrack(QStringLiteral("Engine Found Song"));
    clearCache(track);

    // "netease" is a source the engine knows by name; an arbitrary id would be
    // dropped by the source order now that it is user-configurable.
    FakeProvider provider(QStringLiteral("netease"));
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
    QVERIFY(engine.state() == LyricEngine::State::Found);
    QVERIFY(stateChanges >= 2); // Loading -> Found
    QVERIFY(engine.lineCount() == 2);
    QVERIFY(engine.lineText(0) == QStringLiteral("Hello"));
    QVERIFY(engine.translationText(0) == QStringLiteral("你好"));

    // Before the first line nothing is shown.
    QVERIFY(engine.currentIndex() == -1);
    QVERIFY(engine.currentText().isEmpty());

    engine.setElapsed(1000);
    QVERIFY(engine.currentIndex() == 0);
    QVERIFY(engine.currentText() == QStringLiteral("Hello"));

    engine.setDisplayMode(LyricDisplayMode::Both);
    QVERIFY(engine.currentText() == QStringLiteral("Hello"));
    QVERIFY(engine.currentTranslation() == QStringLiteral("你好"));

    engine.setDisplayMode(LyricDisplayMode::Translation);
    QVERIFY(engine.currentText() == QStringLiteral("你好"));

    engine.setElapsed(2000);
    QVERIFY(engine.currentIndex() == 1);
    engine.setDisplayMode(LyricDisplayMode::Both);
    QVERIFY(engine.currentText() == QStringLiteral("World"));
    QVERIFY(engine.currentTranslation() == QStringLiteral("世界"));

    // Window accessors for a multi-line UI.
    QVERIFY(engine.lineWindow(0, 2) == QStringList({QStringLiteral("Hello"), QStringLiteral("World")}));

    // The fresh fetch was cached.
    QVERIFY(LyricCache::lookup(track).has_value());
    clearCache(track);
}

void LyricEngineTest::placeholderFiltered()
{
    const TrackMeta track = makeTrack(QStringLiteral("Engine Placeholder Song"));
    clearCache(track);

    // "netease" is a source the engine knows by name; an arbitrary id would be
    // dropped by the source order now that it is user-configurable.
    FakeProvider provider(QStringLiteral("netease"));
    provider.searchResults = {makeCandidate(QStringLiteral("7"), track.title)};
    provider.lyricStore.lyric = QStringLiteral("[00:00.00]暂无歌词");

    LyricEngine engine;
    engine.setProviders({&provider});
    engine.setTrack(track);

    QVERIFY(engine.state() == LyricEngine::State::NotFound);
    QVERIFY(!LyricCache::lookup(track).has_value()); // nothing cached
}

void LyricEngineTest::staleResultIgnored()
{
    const TrackMeta first = makeTrack(QStringLiteral("Engine Stale One"));
    const TrackMeta second = makeTrack(QStringLiteral("Engine Stale Two"));
    clearCache(first);
    clearCache(second);

    // "netease" is a source the engine knows by name; an arbitrary id would be
    // dropped by the source order now that it is user-configurable.
    FakeProvider provider(QStringLiteral("netease"));
    provider.autoDeliver = false;
    provider.searchResults = {makeCandidate(QStringLiteral("1"), first.title)};
    provider.lyricStore.lyric = QStringLiteral("[00:01.00]stale");

    LyricEngine engine;
    engine.setProviders({&provider});

    engine.setTrack(first);
    QVERIFY(provider.m_searchIds.size() == 1);
    const quint64 firstSearchId = provider.m_searchIds.at(0);

    engine.setTrack(second);
    QVERIFY(provider.m_searchIds.size() == 2);
    const quint64 secondSearchId = provider.m_searchIds.at(1);

    // The first track's reply lands late and must be ignored.
    provider.deliverSearch(firstSearchId, {makeCandidate(QStringLiteral("1"), first.title)});
    QVERIFY(engine.state() == LyricEngine::State::Loading);

    // The current track's reply drives the pipeline.
    provider.searchResults = {makeCandidate(QStringLiteral("2"), second.title)};
    provider.deliverSearch(secondSearchId, provider.searchResults);
    QVERIFY(provider.m_queryIds.size() == 1);

    provider.deliverLyric(provider.m_queryIds.at(0), QStringLiteral("2"), provider.lyricStore);
    QVERIFY(engine.state() == LyricEngine::State::Found);
    QVERIFY(engine.lineText(0) == QStringLiteral("stale"));

    clearCache(first);
    clearCache(second);
}

void LyricEngineTest::noResults()
{
    const TrackMeta track = makeTrack(QStringLiteral("Engine Empty Song"));
    clearCache(track);

    // "netease" is a source the engine knows by name; an arbitrary id would be
    // dropped by the source order now that it is user-configurable.
    FakeProvider provider(QStringLiteral("netease"));
    provider.searchResults.clear();

    LyricEngine engine;
    engine.setProviders({&provider});
    engine.setTrack(track);
    QVERIFY(engine.state() == LyricEngine::State::NotFound);
}

void LyricEngineTest::clearForgetsTheTrack()
{
    const TrackMeta track = makeTrack(QStringLiteral("Engine Clear Song"));
    clearCache(track);

    // "netease" is a source the engine knows by name; an arbitrary id would be
    // dropped by the source order now that it is user-configurable.
    FakeProvider provider(QStringLiteral("netease"));
    provider.searchResults = {makeCandidate(QStringLiteral("77"), track.title)};
    provider.lyricStore.lyric = QStringLiteral("[00:01.00]Only line");

    LyricEngine engine;
    engine.setProviders({&provider});
    engine.setTrack(track);
    QVERIFY(engine.state() == LyricEngine::State::Found);
    QVERIFY(provider.m_searchIds.size() == 1);

    engine.clear();
    QVERIFY(engine.state() == LyricEngine::State::Idle);
    QVERIFY(engine.currentText().isEmpty());
    QVERIFY(!engine.track().isValid()); // the track is forgotten, not just the lyric

    // The same song must load again after a clear. A "processed track" guard left
    // set across clear() would swallow this and leave the overlay blank forever
    // for a song that had already played once. Drop the cache first so the check
    // sees the search path rather than a cache hit.
    clearCache(track);
    engine.setTrack(track);
    QVERIFY(engine.state() == LyricEngine::State::Found);
    QVERIFY(provider.m_searchIds.size() == 2);
    engine.setElapsed(1000);
    QVERIFY(engine.currentText() == QStringLiteral("Only line"));

    clearCache(track);
}

QTEST_GUILESS_MAIN(LyricEngineTest)

#include "lyricengine_test.moc"
