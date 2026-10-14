// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 KShadowLrc Contributors
//
// Offline checks for the LRC parser, timestamp search and translation pairing.

#include "lyrics/lrcparser.h"

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

static void testMultiTimestamp()
{
    const Lyric lyric = parseLrc(QStringLiteral("[00:01.014][00:02.062] Hi friend..."));
    CHECK(lyric.kind == LyricKind::Timed);
    CHECK(lyric.lines.size() == 2);
    CHECK(lyric.lines.at(0).text == QStringLiteral("Hi friend..."));
    CHECK(lyric.lines.at(0).startMs == 1014);
    CHECK(lyric.lines.at(1).startMs == 2062);
}

static void testMetadataDropped()
{
    const Lyric lyric =
        parseLrc(QStringLiteral("[ar:Someone]\n[ti:A Song]\n[al:An Album]\n[by:author]\n[offset:500]\n"
                                "A stray plain line\n"
                                "[00:01.05] Real lyric\n"));
    CHECK(lyric.kind == LyricKind::Timed);
    CHECK(lyric.lines.size() == 1);
    CHECK(lyric.lines.at(0).text == QStringLiteral("Real lyric"));
    CHECK(lyric.lines.at(0).startMs == 1050);
}

static void testExtendedTimestampsStripped()
{
    CHECK(stripExtendedTimestamps(QStringLiteral("[00:12.34]I <01:23.456>love <01:24.123>Rust!")) == QStringLiteral("[00:12.34]I love Rust!"));
    // Ill-formed / non-timestamp angle brackets stay put.
    CHECK(stripExtendedTimestamps(QStringLiteral("[00:12.34]Hello <01:23:456>world!")) == QStringLiteral("[00:12.34]Hello <01:23:456>world!"));
    CHECK(stripExtendedTimestamps(QStringLiteral("[00:12.34]<FM> 纯音乐")) == QStringLiteral("[00:12.34]<FM> 纯音乐"));
    CHECK(stripExtendedTimestamps(QStringLiteral("no brackets")) == QStringLiteral("no brackets"));

    const Lyric lyric = parseLrc(QStringLiteral("[00:00.00]你<0:12.34>好<00:13.5>吗"));
    CHECK(lyric.lines.size() == 1);
    CHECK(lyric.lines.at(0).text == QStringLiteral("你好吗"));
}

static void testTimestampShapes()
{
    // [mm:ss], [mm:ss.xx], [mm:ss.xxx] and the lenient [mm:ss:xx] all parse.
    CHECK(parseLrc(QStringLiteral("[00:03] a")).lines.at(0).startMs == 3000);
    CHECK(parseLrc(QStringLiteral("[00:03.25] a")).lines.at(0).startMs == 3250);
    CHECK(parseLrc(QStringLiteral("[00:03.256] a")).lines.at(0).startMs == 3256);
    CHECK(parseLrc(QStringLiteral("[00:01:05] a")).lines.at(0).startMs == 1050);
    CHECK(parseLrc(QStringLiteral("[45:05.64] a")).lines.at(0).startMs == (45 * 60 + 5) * 1000 + 640);
}

static void testStableSortEqualTimestamps()
{
    // Same timestamp: input order must survive so translation pairing works.
    const Lyric lyric = parseLrc(QStringLiteral("[00:01.77]Original line\n[00:01.77]Translated line\n[00:04.38]Later"));
    CHECK(lyric.lines.size() == 3);
    CHECK(lyric.lines.at(0).text == QStringLiteral("Original line"));
    CHECK(lyric.lines.at(1).text == QStringLiteral("Translated line"));
    CHECK(lyric.lines.at(2).text == QStringLiteral("Later"));

    // Unsorted input is sorted by time but equal times keep relative order.
    const Lyric unsorted = parseLrc(QStringLiteral("[00:10.00]b\n[00:02.00]a1\n[00:02.00]a2"));
    CHECK(unsorted.lines.at(0).text == QStringLiteral("a1"));
    CHECK(unsorted.lines.at(1).text == QStringLiteral("a2"));
    CHECK(unsorted.lines.at(2).text == QStringLiteral("b"));
}

static void testEmptyGapPreserved()
{
    const Lyric lyric = parseLrc(QStringLiteral("[00:01.014] Hi friend...\n[00:02.062]"));
    CHECK(lyric.lines.size() == 2);
    CHECK(lyric.lines.at(0).text == QStringLiteral("Hi friend..."));
    CHECK(lyric.lines.at(1).text.isEmpty());
    CHECK(lyric.lines.at(1).startMs == 2062);
}

static void testKindSelection()
{
    CHECK(parseLrc(QString()).kind == LyricKind::None);
    CHECK(parseLrc(QStringLiteral("   \n  \n")).kind == LyricKind::None);

    const Lyric plain = parseLrc(QStringLiteral("Just a plain lyric\nsecond line"));
    CHECK(plain.kind == LyricKind::NoTimestamp);
    CHECK(plain.lines.size() == 2);
    CHECK(plain.lines.at(0).startMs == 0);

    CHECK(parseLrc(QStringLiteral("[00:01.00] timed")).kind == LyricKind::Timed);
}

static void testFindNextLyric()
{
    const Lyric lyric = parseLrc(QStringLiteral("[00:01.00] a\n[00:02.00] b\n[00:03.00] c"));
    CHECK(findNextLyric(lyric, 0) == -1); // before the first line
    CHECK(findNextLyric(lyric, 999) == -1);
    CHECK(findNextLyric(lyric, 1000) == 0); // exact boundary
    CHECK(findNextLyric(lyric, 1500) == 0); // between lines
    CHECK(findNextLyric(lyric, 2000) == 1); // exact boundary
    CHECK(findNextLyric(lyric, 999999) == 2); // after the last line
    CHECK(findNextLyric(Lyric{}, 5000) == -1);
}

static void testTranslationPairing()
{
    const Lyric origin = parseLrc(QStringLiteral("[00:01.77]Please don't say\n[00:04.38]crazy"));
    const Lyric translated = parseLrc(QStringLiteral("[00:01.77]请不要说\n[00:04.38]疯狂"));
    const Lyric paired = pairTranslation(origin, translated);
    CHECK(paired.kind == LyricKind::Timed);
    CHECK(paired.lines.size() == origin.lines.size());
    CHECK(paired.lines.at(0).text == QStringLiteral("请不要说"));
    CHECK(paired.lines.at(1).text == QStringLiteral("疯狂"));

    // Empty translation lines are dropped, but the alignment keeps the slot.
    const Lyric withEmpty = parseLrc(QStringLiteral("[00:01.77]请不要说\n[00:04.38]"));
    const Lyric paired2 = pairTranslation(origin, withEmpty);
    CHECK(paired2.lines.size() == 2);
    CHECK(paired2.lines.at(0).text == QStringLiteral("请不要说"));
    CHECK(paired2.lines.at(1).text.isEmpty());

    // A translation that matches nothing yields no translation at all.
    const Lyric noMatch = parseLrc(QStringLiteral("[00:09.99]あ"));
    CHECK(pairTranslation(origin, noMatch).kind == LyricKind::None);

    // Pairing a plain (untimed) original is not applicable.
    CHECK(pairTranslation(Lyric{}, translated).kind == LyricKind::None);
}

int main()
{
    testMultiTimestamp();
    testMetadataDropped();
    testExtendedTimestampsStripped();
    testTimestampShapes();
    testStableSortEqualTimestamps();
    testEmptyGapPreserved();
    testKindSelection();
    testFindNextLyric();
    testTranslationPairing();

    if (g_failures == 0) {
        std::printf("test_lrcparser: all checks passed\n");
        return 0;
    }
    std::fprintf(stderr, "test_lrcparser: %d check(s) failed\n", g_failures);
    return 1;
}
