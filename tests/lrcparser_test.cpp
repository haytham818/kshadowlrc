// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 KShadowLrc Contributors
//
// Offline checks for the LRC parser, timestamp search and translation pairing.

#include "lyrics/lrcparser.h"

#include <QTest>

using namespace kshadowlrc;

class LrcParserTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void multiTimestamp();
    void metadataDropped();
    void extendedTimestampsStripped();
    void timestampShapes();
    void stableSortEqualTimestamps();
    void emptyGapPreserved();
    void kindSelection();
    void nextLineIndex();
    void translationPairing();
};

void LrcParserTest::multiTimestamp()
{
    const Lyric lyric = parseLrc(QStringLiteral("[00:01.014][00:02.062] Hi friend..."));
    QCOMPARE(lyric.kind, LyricKind::Timed);
    QCOMPARE(lyric.lines.size(), 2);
    QCOMPARE(lyric.lines.at(0).text, QStringLiteral("Hi friend..."));
    QCOMPARE(lyric.lines.at(0).startMs, 1014);
    QCOMPARE(lyric.lines.at(1).startMs, 2062);
}

void LrcParserTest::metadataDropped()
{
    const Lyric lyric =
        parseLrc(QStringLiteral("[ar:Someone]\n[ti:A Song]\n[al:An Album]\n[by:author]\n[offset:500]\n"
                                "A stray plain line\n"
                                "[00:01.05] Real lyric\n"));
    QCOMPARE(lyric.kind, LyricKind::Timed);
    QCOMPARE(lyric.lines.size(), 1);
    QCOMPARE(lyric.lines.at(0).text, QStringLiteral("Real lyric"));
    QCOMPARE(lyric.lines.at(0).startMs, 1050);
}

void LrcParserTest::extendedTimestampsStripped()
{
    QCOMPARE(stripExtendedTimestamps(QStringLiteral("[00:12.34]I <01:23.456>love <01:24.123>Rust!")), QStringLiteral("[00:12.34]I love Rust!"));
    // Ill-formed / non-timestamp angle brackets stay put.
    QCOMPARE(stripExtendedTimestamps(QStringLiteral("[00:12.34]Hello <01:23:456>world!")), QStringLiteral("[00:12.34]Hello <01:23:456>world!"));
    QCOMPARE(stripExtendedTimestamps(QStringLiteral("[00:12.34]<FM> 纯音乐")), QStringLiteral("[00:12.34]<FM> 纯音乐"));
    QCOMPARE(stripExtendedTimestamps(QStringLiteral("no brackets")), QStringLiteral("no brackets"));

    const Lyric lyric = parseLrc(QStringLiteral("[00:00.00]你<0:12.34>好<00:13.5>吗"));
    QCOMPARE(lyric.lines.size(), 1);
    QCOMPARE(lyric.lines.at(0).text, QStringLiteral("你好吗"));
}

void LrcParserTest::timestampShapes()
{
    // [mm:ss], [mm:ss.xx], [mm:ss.xxx] and the lenient [mm:ss:xx] all parse.
    QCOMPARE(parseLrc(QStringLiteral("[00:03] a")).lines.at(0).startMs, 3000);
    QCOMPARE(parseLrc(QStringLiteral("[00:03.25] a")).lines.at(0).startMs, 3250);
    QCOMPARE(parseLrc(QStringLiteral("[00:03.256] a")).lines.at(0).startMs, 3256);
    QCOMPARE(parseLrc(QStringLiteral("[00:01:05] a")).lines.at(0).startMs, 1050);
    QCOMPARE(parseLrc(QStringLiteral("[45:05.64] a")).lines.at(0).startMs, (45 * 60 + 5) * 1000 + 640);
}

void LrcParserTest::stableSortEqualTimestamps()
{
    // Same timestamp: input order must survive so translation pairing works.
    const Lyric lyric = parseLrc(QStringLiteral("[00:01.77]Original line\n[00:01.77]Translated line\n[00:04.38]Later"));
    QCOMPARE(lyric.lines.size(), 3);
    QCOMPARE(lyric.lines.at(0).text, QStringLiteral("Original line"));
    QCOMPARE(lyric.lines.at(1).text, QStringLiteral("Translated line"));
    QCOMPARE(lyric.lines.at(2).text, QStringLiteral("Later"));

    // Unsorted input is sorted by time but equal times keep relative order.
    const Lyric unsorted = parseLrc(QStringLiteral("[00:10.00]b\n[00:02.00]a1\n[00:02.00]a2"));
    QCOMPARE(unsorted.lines.at(0).text, QStringLiteral("a1"));
    QCOMPARE(unsorted.lines.at(1).text, QStringLiteral("a2"));
    QCOMPARE(unsorted.lines.at(2).text, QStringLiteral("b"));
}

void LrcParserTest::emptyGapPreserved()
{
    const Lyric lyric = parseLrc(QStringLiteral("[00:01.014] Hi friend...\n[00:02.062]"));
    QCOMPARE(lyric.lines.size(), 2);
    QCOMPARE(lyric.lines.at(0).text, QStringLiteral("Hi friend..."));
    QVERIFY(lyric.lines.at(1).text.isEmpty());
    QCOMPARE(lyric.lines.at(1).startMs, 2062);
}

void LrcParserTest::kindSelection()
{
    QCOMPARE(parseLrc(QString()).kind, LyricKind::None);
    QCOMPARE(parseLrc(QStringLiteral("   \n  \n")).kind, LyricKind::None);

    const Lyric plain = parseLrc(QStringLiteral("Just a plain lyric\nsecond line"));
    QCOMPARE(plain.kind, LyricKind::NoTimestamp);
    QCOMPARE(plain.lines.size(), 2);
    QCOMPARE(plain.lines.at(0).startMs, 0);

    QCOMPARE(parseLrc(QStringLiteral("[00:01.00] timed")).kind, LyricKind::Timed);
}

void LrcParserTest::nextLineIndex()
{
    const Lyric lyric = parseLrc(QStringLiteral("[00:01.00] a\n[00:02.00] b\n[00:03.00] c"));
    QCOMPARE(findNextLyric(lyric, 0), -1); // before the first line
    QCOMPARE(findNextLyric(lyric, 999), -1);
    QCOMPARE(findNextLyric(lyric, 1000), 0); // exact boundary
    QCOMPARE(findNextLyric(lyric, 1500), 0); // between lines
    QCOMPARE(findNextLyric(lyric, 2000), 1); // exact boundary
    QCOMPARE(findNextLyric(lyric, 999999), 2); // after the last line
    QCOMPARE(findNextLyric(Lyric{}, 5000), -1);
}

void LrcParserTest::translationPairing()
{
    const Lyric origin = parseLrc(QStringLiteral("[00:01.77]Please don't say\n[00:04.38]crazy"));
    const Lyric translated = parseLrc(QStringLiteral("[00:01.77]请不要说\n[00:04.38]疯狂"));
    const Lyric paired = pairTranslation(origin, translated);
    QCOMPARE(paired.kind, LyricKind::Timed);
    QCOMPARE(paired.lines.size(), origin.lines.size());
    QCOMPARE(paired.lines.at(0).text, QStringLiteral("请不要说"));
    QCOMPARE(paired.lines.at(1).text, QStringLiteral("疯狂"));

    // Empty translation lines are dropped, but the alignment keeps the slot.
    const Lyric withEmpty = parseLrc(QStringLiteral("[00:01.77]请不要说\n[00:04.38]"));
    const Lyric paired2 = pairTranslation(origin, withEmpty);
    QCOMPARE(paired2.lines.size(), 2);
    QCOMPARE(paired2.lines.at(0).text, QStringLiteral("请不要说"));
    QVERIFY(paired2.lines.at(1).text.isEmpty());

    // A translation that matches nothing yields no translation at all.
    const Lyric noMatch = parseLrc(QStringLiteral("[00:09.99]あ"));
    QCOMPARE(pairTranslation(origin, noMatch).kind, LyricKind::None);

    // Pairing a plain (untimed) original is not applicable.
    QCOMPARE(pairTranslation(Lyric{}, translated).kind, LyricKind::None);
}

QTEST_GUILESS_MAIN(LrcParserTest)

#include "lrcparser_test.moc"
