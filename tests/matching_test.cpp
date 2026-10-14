// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 KShadowLrc Contributors
//
// Offline checks for lyric candidate normalisation, similarity and ranking.

#include "lyrics/matching.h"

#include <QTest>

using namespace kshadowlrc;

static TrackMeta track()
{
    TrackMeta t;
    t.title = QStringLiteral("Yesterday");
    t.album = QStringLiteral("Help!");
    t.artists = {QStringLiteral("The Beatles")};
    t.lengthMs = 125000;
    return t;
}

class MatchingTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void normalise();
    void similarity();
    void artistOrdering();
    void durationTolerance();
    void candidateRanking();
};

void MatchingTest::normalise()
{
    QVERIFY(normalizeForMatch(QStringLiteral("Hello, World! 123")) == QStringLiteral("helloworld123"));
    QVERIFY(normalizeForMatch(QStringLiteral("  Страница  ")) == QStringLiteral("страница"));
}

void MatchingTest::similarity()
{
    const double identical = bigramSimilarity(normalizeForMatch(QStringLiteral("你好，世界")), normalizeForMatch(QStringLiteral("你好，世界")));
    QVERIFY(identical == 1.0);

    const double close = bigramSimilarity(normalizeForMatch(QStringLiteral("你好世界")), normalizeForMatch(QStringLiteral("你好，世界")));
    QVERIFY(close > 0.7);

    const double far = bigramSimilarity(normalizeForMatch(QStringLiteral("你好世界")), normalizeForMatch(QStringLiteral("我的世界")));
    QVERIFY(close > far);
}

void MatchingTest::artistOrdering()
{
    const TrackMeta t = track();

    SongInfo correct;
    correct.title = QStringLiteral("Yesterday");
    correct.album = QStringLiteral("Help!");
    correct.singer = QStringLiteral("The Beatles");
    correct.lengthMs = 125000;

    SongInfo wrongArtist;
    wrongArtist.title = QStringLiteral("Yesterday");
    wrongArtist.album = QStringLiteral("Help!");
    wrongArtist.singer = QStringLiteral("Complete Stranger");
    wrongArtist.lengthMs = 125000;

    SongInfo missingArtist;
    missingArtist.title = QStringLiteral("Yesterday");
    missingArtist.album = QStringLiteral("Help!");
    missingArtist.singer = QString();
    missingArtist.lengthMs = 125000;

    const SongMatch mCorrect = matchLikelyLyric(t, correct);
    const SongMatch mWrong = matchLikelyLyric(t, wrongArtist);
    const SongMatch mMissing = matchLikelyLyric(t, missingArtist);

    // A correct artist must score strictly better than a wrong or missing one.
    QVERIFY(mCorrect.score > mWrong.score);
    QVERIFY(mCorrect.score > mMissing.score);
    QVERIFY(mCorrect.rank == 0);
    QVERIFY(mCorrect.rank <= mWrong.rank);
    QVERIFY(mCorrect.rank <= mMissing.rank);
}

void MatchingTest::durationTolerance()
{
    const TrackMeta t = track(); // 125000 ms

    SongInfo within;
    within.title = QStringLiteral("Completely Different Words");
    within.lengthMs = 125000 + kDurationToleranceMs; // exactly at the edge

    SongInfo beyond;
    beyond.title = QStringLiteral("Completely Different Words");
    beyond.lengthMs = 125000 + kDurationToleranceMs + 1; // one ms over

    QVERIFY(withinDurationTolerance(t, within));
    QVERIFY(!withinDurationTolerance(t, beyond));

    // Weak title similarity, so the rank is decided by the duration fast path.
    QVERIFY(matchLikelyLyric(t, within).rank == 1);
    QVERIFY(matchLikelyLyric(t, beyond).rank == 2);

    // An unknown length never counts as a duration match.
    SongInfo unknownLength;
    unknownLength.title = QStringLiteral("Completely Different Words");
    unknownLength.lengthMs = 0;
    QVERIFY(!withinDurationTolerance(t, unknownLength));
    QVERIFY(matchLikelyLyric(t, unknownLength).rank == 2);
}

void MatchingTest::candidateRanking()
{
    const TrackMeta t = track();

    SongInfo best;
    best.id = QStringLiteral("1");
    best.title = QStringLiteral("Yesterday");
    best.album = QStringLiteral("Help!");
    best.singer = QStringLiteral("The Beatles");
    best.lengthMs = 125000;

    SongInfo weak;
    weak.id = QStringLiteral("2");
    weak.title = QStringLiteral("Something Else Entirely");
    weak.lengthMs = 0;

    QList<SongInfo> candidates{weak, best};
    const QList<RankedSong> ranked = rankCandidates(t, candidates, 0);
    QVERIFY(ranked.size() == 2);
    QVERIFY(ranked.at(0).song.id == QStringLiteral("1"));
    QVERIFY(ranked.at(0).match.rank == 0);
    QVERIFY(ranked.at(1).song.id == QStringLiteral("2"));
}

QTEST_GUILESS_MAIN(MatchingTest)

#include "matching_test.moc"
