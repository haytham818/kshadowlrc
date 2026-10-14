// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 KShadowLrc Contributors
//
// Offline checks for lyric candidate normalisation, similarity and ranking.

#include "lyrics/matching.h"

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

static TrackMeta track()
{
    TrackMeta t;
    t.title = QStringLiteral("Yesterday");
    t.album = QStringLiteral("Help!");
    t.artists = {QStringLiteral("The Beatles")};
    t.lengthMs = 125000;
    return t;
}

static void testNormalise()
{
    CHECK(normalizeForMatch(QStringLiteral("Hello, World! 123")) == QStringLiteral("helloworld123"));
    CHECK(normalizeForMatch(QStringLiteral("  Страница  ")) == QStringLiteral("страница"));
}

static void testSimilarity()
{
    const double identical = bigramSimilarity(normalizeForMatch(QStringLiteral("你好，世界")), normalizeForMatch(QStringLiteral("你好，世界")));
    CHECK(identical == 1.0);

    const double close = bigramSimilarity(normalizeForMatch(QStringLiteral("你好世界")), normalizeForMatch(QStringLiteral("你好，世界")));
    CHECK(close > 0.7);

    const double far = bigramSimilarity(normalizeForMatch(QStringLiteral("你好世界")), normalizeForMatch(QStringLiteral("我的世界")));
    CHECK(close > far);
}

static void testArtistOrdering()
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
    CHECK(mCorrect.score > mWrong.score);
    CHECK(mCorrect.score > mMissing.score);
    CHECK(mCorrect.rank == 0);
    CHECK(mCorrect.rank <= mWrong.rank);
    CHECK(mCorrect.rank <= mMissing.rank);
}

static void testDurationTolerance()
{
    const TrackMeta t = track(); // 125000 ms

    SongInfo within;
    within.title = QStringLiteral("Completely Different Words");
    within.lengthMs = 125000 + kDurationToleranceMs; // exactly at the edge

    SongInfo beyond;
    beyond.title = QStringLiteral("Completely Different Words");
    beyond.lengthMs = 125000 + kDurationToleranceMs + 1; // one ms over

    CHECK(withinDurationTolerance(t, within));
    CHECK(!withinDurationTolerance(t, beyond));

    // Weak title similarity, so the rank is decided by the duration fast path.
    CHECK(matchLikelyLyric(t, within).rank == 1);
    CHECK(matchLikelyLyric(t, beyond).rank == 2);

    // An unknown length never counts as a duration match.
    SongInfo unknownLength;
    unknownLength.title = QStringLiteral("Completely Different Words");
    unknownLength.lengthMs = 0;
    CHECK(!withinDurationTolerance(t, unknownLength));
    CHECK(matchLikelyLyric(t, unknownLength).rank == 2);
}

static void testRankCandidates()
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
    CHECK(ranked.size() == 2);
    CHECK(ranked.at(0).song.id == QStringLiteral("1"));
    CHECK(ranked.at(0).match.rank == 0);
    CHECK(ranked.at(1).song.id == QStringLiteral("2"));
}

int main()
{
    testNormalise();
    testSimilarity();
    testArtistOrdering();
    testDurationTolerance();
    testRankCandidates();

    if (g_failures == 0) {
        std::printf("test_matching: all checks passed\n");
        return 0;
    }
    std::fprintf(stderr, "test_matching: %d check(s) failed\n", g_failures);
    return 1;
}
