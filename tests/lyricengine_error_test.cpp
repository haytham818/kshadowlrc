// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 KShadowLrc Contributors
//
// Offline checks that a transport/protocol failure becomes State::Error with a
// visible message, while providers that simply yield nothing stay
// State::NotFound. No network, no D-Bus.

#include "enginestubs.h"

#include "lyrics/lyriccache.h"
#include "lyrics/lyricengine.h"

#include <QFile>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTest>

using namespace kshadowlrc;

namespace
{

void clearCache(const TrackMeta &track)
{
    QFile::remove(LyricCache::cachePath(track));
}

} // namespace

class LyricEngineErrorTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();

    void searchFailureSetsError();
    void lyricFailureSetsError();
    void emptyAnswerIsNotFound();
    void unusableAnswerIsNotFound();
    void errorClearedOnNextSuccessfulTrack();
    void deferredLocalWinsOverTransportError();
};

void LyricEngineErrorTest::initTestCase()
{
    QCoreApplication::setApplicationName(QStringLiteral("kshadowlrc"));
    QStandardPaths::setTestModeEnabled(true);
}

void LyricEngineErrorTest::searchFailureSetsError()
{
    const TrackMeta track = stubTrack(QStringLiteral("Error Search Song"));
    clearCache(track);

    StubProvider provider(QStringLiteral("netease"));
    provider.autoDeliver = false;

    LyricEngine engine;
    engine.setProviders({&provider});
    QSignalSpy spy(&engine, &LyricEngine::lastErrorChanged);

    engine.setTrack(track);
    QVERIFY(engine.state() == LyricEngine::State::Loading);
    QVERIFY(provider.m_searchIds.size() == 1);

    provider.failSearch(provider.m_searchIds.at(0), QStringLiteral("network unreachable"));

    QVERIFY(engine.state() == LyricEngine::State::Error);
    QCOMPARE(engine.lastError(), QStringLiteral("network unreachable"));
    QVERIFY(spy.count() >= 1);
}

void LyricEngineErrorTest::lyricFailureSetsError()
{
    const TrackMeta track = stubTrack(QStringLiteral("Error Lyric Song"));
    clearCache(track);

    StubProvider provider(QStringLiteral("netease"));
    provider.autoDeliver = false;
    provider.searchResults = {stubCandidate(QStringLiteral("n1"), track.title)};

    LyricEngine engine;
    engine.setProviders({&provider});

    engine.setTrack(track);
    QVERIFY(engine.state() == LyricEngine::State::Loading);

    // The search answers with a candidate, so the engine issues a lyric query.
    provider.deliverSearch(provider.m_searchIds.at(0), provider.searchResults);
    QVERIFY(provider.m_queryIds.size() == 1);

    provider.failLyric(provider.m_queryIds.at(0), QStringLiteral("n1"), QStringLiteral("connection reset"));

    QVERIFY(engine.state() == LyricEngine::State::Error);
    QCOMPARE(engine.lastError(), QStringLiteral("connection reset"));
}

void LyricEngineErrorTest::emptyAnswerIsNotFound()
{
    const TrackMeta track = stubTrack(QStringLiteral("Error Empty Song"));
    clearCache(track);

    StubProvider provider(QStringLiteral("netease"));
    provider.searchResults.clear();

    LyricEngine engine;
    engine.setProviders({&provider});
    engine.setTrack(track);

    QVERIFY(engine.state() == LyricEngine::State::NotFound);
    QVERIFY(engine.lastError().isEmpty());
}

void LyricEngineErrorTest::unusableAnswerIsNotFound()
{
    const TrackMeta track = stubTrack(QStringLiteral("Error Placeholder Song"));
    clearCache(track);

    StubProvider provider(QStringLiteral("netease"));
    provider.searchResults = {stubCandidate(QStringLiteral("n1"), track.title)};
    provider.lyricStore = timedStore(QStringLiteral("[00:00.00]暂无歌词"));

    LyricEngine engine;
    engine.setProviders({&provider});
    engine.setTrack(track);

    // The provider answered; it just had nothing usable, which is not an error.
    QVERIFY(engine.state() == LyricEngine::State::NotFound);
    QVERIFY(engine.lastError().isEmpty());
}

void LyricEngineErrorTest::errorClearedOnNextSuccessfulTrack()
{
    const TrackMeta failed = stubTrack(QStringLiteral("Error First Song"));
    const TrackMeta ok = stubTrack(QStringLiteral("Error Second Song"));
    clearCache(failed);
    clearCache(ok);

    StubProvider provider(QStringLiteral("netease"));
    provider.autoDeliver = false;

    LyricEngine engine;
    engine.setProviders({&provider});

    engine.setTrack(failed);
    provider.failSearch(provider.m_searchIds.at(0), QStringLiteral("timed out"));
    QVERIFY(engine.state() == LyricEngine::State::Error);
    QCOMPARE(engine.lastError(), QStringLiteral("timed out"));

    // The next resolution starts clean and succeeds, clearing the failure.
    provider.autoDeliver = true;
    provider.searchResults = {stubCandidate(QStringLiteral("n1"), ok.title)};
    provider.lyricStore = timedStore(QStringLiteral("[00:01.00]Second line"));

    engine.setTrack(ok);
    QVERIFY(engine.state() == LyricEngine::State::Found);
    QVERIFY(engine.lastError().isEmpty());

    clearCache(failed);
    clearCache(ok);
}

/*! A deferred local lyric is content, so it wins over a recorded failure too. */
void LyricEngineErrorTest::deferredLocalWinsOverTransportError()
{
    const TrackMeta track = stubTrack(QStringLiteral("Error Deferred Local Song"));
    clearCache(track);

    StubProvider provider(QStringLiteral("netease"));
    provider.autoDeliver = false;

    StubLocalSource local;
    local.store = LyricStore{QStringLiteral("plain text\nno timestamps"), QString()};

    LyricEngine engine;
    engine.setProviders({&provider});
    engine.setLocalSource(&local);
    engine.setSourceOrder({QStringLiteral("local"), QStringLiteral("netease")});
    engine.setTrack(track);

    QVERIFY(engine.state() == LyricEngine::State::Loading);
    QVERIFY(provider.m_searchIds.size() == 1);

    // The provider fails; the deferred local lyric is the only content left, and
    // content wins over the error, clearing lastError exactly as a provider's
    // plain lyric would.
    provider.failSearch(provider.m_searchIds.at(0), QStringLiteral("network unreachable"));

    QVERIFY(engine.state() == LyricEngine::State::Found);
    QVERIFY(engine.lastError().isEmpty());
    QCOMPARE(engine.currentText(), QStringLiteral("plain text\nno timestamps"));
}

QTEST_GUILESS_MAIN(LyricEngineErrorTest)

#include "lyricengine_error_test.moc"
