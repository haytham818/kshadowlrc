// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 KShadowLrc Contributors

// Pins down the ignore-list matching rules in playermatch.h: the two entry
// forms (no wildcard: whole name / dot-boundary prefix / one segment; wildcard:
// whole-name anchored glob), their near-misses, case handling, and the empty
// list. Pure logic, no bus, no GUI: a table of cases feeds both
// ignoredPatternMatches() and isIgnoredPlayer() and every row is checked.

#include "playermatch.h"

#include <QTest>

#include <vector>

using kshadowlrc::ignoredPatternMatches;
using kshadowlrc::isIgnoredPlayer;
using kshadowlrc::matchingIgnorePattern;

namespace
{

struct PatternCase {
    const char *pattern;
    const char *busName;
    bool expected;
    const char *why;
};

} // namespace

class PlayerMatchTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void ignoredPatternTable();
    void ignoredPlayerList();
    void matchingEntryIsReported();
};

void PlayerMatchTest::ignoredPatternTable()
{
    // --- One entry at a time: ignoredPatternMatches(). -----------------------
    const std::vector<PatternCase> cases = {
        // No wildcard, rule 1: equals the whole bus name.
        {"org.mpris.MediaPlayer2.playerctld", "org.mpris.MediaPlayer2.playerctld", true, "entry equals the whole bus name"},
        {"org.mpris.MediaPlayer2.firefox", "org.mpris.MediaPlayer2.firefox", true, "entry equals the whole bus name, no instance suffix"},

        // No wildcard, rule 2: a dot-boundary prefix (old startsWith behaviour).
        {"org.mpris.MediaPlayer2.firefox", "org.mpris.MediaPlayer2.firefox.instance1", true, "dot-boundary prefix matches an instance-suffixed name"},
        {"org.mpris.MediaPlayer2", "org.mpris.MediaPlayer2.chromium.instance1", true, "the interface prefix ignores every MPRIS player (chromium)"},
        {"org.mpris.MediaPlayer2", "org.mpris.MediaPlayer2.firefox.instance1", true, "the interface prefix ignores every MPRIS player (firefox)"},

        // No wildcard, rule 3: one dot-separated segment.
        {"firefox", "org.mpris.MediaPlayer2.firefox.instance1", true, "bare segment matches an instance-suffixed name"},
        {"playerctld", "org.mpris.MediaPlayer2.playerctld", true, "bare segment matches the exact name"},
        {"instance1", "org.mpris.MediaPlayer2.firefox.instance1", true, "any segment matches, not only the first"},

        // Near misses: must NOT match. Segment equality, never substring.
        {"firefox", "org.mpris.MediaPlayer2.firefoxes.instance1", false, "near-miss: firefox must not match firefoxes"},
        {"fire", "org.mpris.MediaPlayer2.firefox.instance1", false, "near-miss: fire must not match firefox"},
        {"org.mpris.MediaPlayer2.firefox", "org.mpris.MediaPlayer2.firefoxinstance", false, "the prefix boundary must be a dot"},
        {"org.mpris.MediaPlayer2.fire", "org.mpris.MediaPlayer2.firefox.instance1", false, "a prefix ending mid-segment does not match"},
        {"org.mpris.MediaPlayer2.firefox", "org.mpris.MediaPlayer2.chromium.instance1", false, "unrelated player does not match"},
        {"", "org.mpris.MediaPlayer2.firefox", false, "empty entry matches nothing"},

        // Wildcard: case-insensitive glob against the whole name (unchanged).
        {"org.mpris.MediaPlayer2.firefox*", "org.mpris.MediaPlayer2.firefox.instance1", true, "trailing glob matches the dotted prefix and any instance"},
        {"org.mpris.MediaPlayer2.firefox*", "org.mpris.MediaPlayer2.firefox", true, "* may match the empty tail"},
        {"*firefox*", "org.mpris.MediaPlayer2.firefox.instance1", true, "embedded glob matches"},
        {"org.mpris.MediaPlayer2.*", "org.mpris.MediaPlayer2.anything.at.all", true, "match-every-player glob"},
        {"org.mpris.MediaPlayer2.chromiu?", "org.mpris.MediaPlayer2.chromium", true, "? matches a single character"},

        // Glob anchoring: the pattern covers the whole bus name, not a prefix.
        {"firefox*", "org.mpris.MediaPlayer2.firefox", false, "glob is anchored: firefox* does not match the full bus name"},

        // Case-insensitivity in both forms.
        {"FIREFOX", "org.mpris.MediaPlayer2.firefox.instance1", true, "segment match is case-insensitive"},
        {"firefox", "org.mpris.MediaPlayer2.FIREFOX.instance1", true, "segment match ignores bus-name case"},
        {"ORG.MPRIS.MEDIAPLAYER2.FIREFOX", "org.mpris.MediaPlayer2.firefox.instance1", true, "dot-boundary prefix match is case-insensitive"},
        {"ORG.MPRIS.MEDIAPLAYER2.FIREFOX*", "org.mpris.MediaPlayer2.firefox.instance1", true, "glob match is case-insensitive"},
    };

    for (const PatternCase &c : cases) {
        const bool got = ignoredPatternMatches(QString::fromUtf8(c.pattern), QString::fromUtf8(c.busName));
        const QString why = QStringLiteral("pattern '%1' vs '%2' -> %3 (%4)")
                                .arg(QString::fromUtf8(c.pattern),
                                     QString::fromUtf8(c.busName),
                                     got ? QStringLiteral("match") : QStringLiteral("no match"),
                                     QString::fromUtf8(c.why));
        QVERIFY2(got == c.expected, qPrintable(why));
    }
}

void PlayerMatchTest::ignoredPlayerList()
{
    // --- The list: isIgnoredPlayer(). ---------------------------------------
    // The literal out-of-the-box defaults; with the union rule they work as the
    // old startsWith prefixes always did.
    const QStringList defaults = {
        QStringLiteral("org.mpris.MediaPlayer2.firefox"),
        QStringLiteral("org.mpris.MediaPlayer2.playerctld"),
    };
    QVERIFY2(isIgnoredPlayer(defaults, QStringLiteral("org.mpris.MediaPlayer2.firefox.instance1234")), "default list ignores a firefox instance");
    QVERIFY2(isIgnoredPlayer(defaults, QStringLiteral("org.mpris.MediaPlayer2.firefox")), "default list ignores the bare firefox name");
    QVERIFY2(isIgnoredPlayer(defaults, QStringLiteral("org.mpris.MediaPlayer2.playerctld")), "default list ignores playerctld");
    QVERIFY2(!isIgnoredPlayer(defaults, QStringLiteral("org.mpris.MediaPlayer2.chromium.instance1")), "default list leaves chromium alone");

    QVERIFY2(!isIgnoredPlayer(QStringList{}, QStringLiteral("org.mpris.MediaPlayer2.firefox")), "empty list ignores nothing (firefox)");
    QVERIFY2(!isIgnoredPlayer(QStringList{}, QStringLiteral("org.mpris.MediaPlayer2.chromium.instance1")), "empty list ignores nothing (chromium)");
    QVERIFY2(!isIgnoredPlayer({QStringLiteral("spotify")}, QStringLiteral("org.mpris.MediaPlayer2.chromium.instance1")),
             "a non-matching entry ignores nothing");
    QVERIFY2(isIgnoredPlayer({QStringLiteral("spotify"), QStringLiteral("chromium")}, QStringLiteral("org.mpris.MediaPlayer2.chromium.instance1")),
             "a later matching entry is honoured");
    QVERIFY2(!isIgnoredPlayer({QString(), QString()}, QStringLiteral("org.mpris.MediaPlayer2.chromium.instance1")), "blank entries are inert");
}

void PlayerMatchTest::matchingEntryIsReported()
{
    // --- Which entry matched: matchingIgnorePattern(). ----------------------
    // Exactly isIgnoredPlayer()'s rule, but returning the entry, so the settings
    // window can name the rule that caught a player.
    const QStringList patterns = {
        QStringLiteral("spotify"),
        QStringLiteral("org.mpris.MediaPlayer2.firefox"),
        QStringLiteral("org.mpris.MediaPlayer2.*"),
    };
    QVERIFY2(matchingIgnorePattern(patterns, QStringLiteral("org.mpris.MediaPlayer2.firefox.instance1")) == QStringLiteral("org.mpris.MediaPlayer2.firefox"),
             "returns the matching entry, not the first entry");
    QVERIFY2(matchingIgnorePattern(patterns, QStringLiteral("org.mpris.MediaPlayer2.chromium.instance1")) == QStringLiteral("org.mpris.MediaPlayer2.*"),
             "returns the glob that matched as the fallback");
    QVERIFY2(matchingIgnorePattern(patterns, QStringLiteral("org.example.vlc")).isEmpty(), "no match yields an empty string");
    QVERIFY2(matchingIgnorePattern({QStringLiteral("spotify")}, QStringLiteral("org.mpris.MediaPlayer2.chromium.instance1")).isEmpty(),
             "a non-matching list yields an empty string");
    QVERIFY2(matchingIgnorePattern(QStringList{}, QStringLiteral("org.mpris.MediaPlayer2.chromium.instance1")).isEmpty(),
             "the empty list yields an empty string");

    // The two forms agree: matchingIgnorePattern() is truthful exactly when
    // isIgnoredPlayer() is.
    for (const QString &name : {QStringLiteral("org.mpris.MediaPlayer2.firefox.instance1"), QStringLiteral("org.mpris.MediaPlayer2.chromium.instance1")}) {
        QVERIFY2(matchingIgnorePattern(patterns, name).isEmpty() == !isIgnoredPlayer(patterns, name), "matchingIgnorePattern agrees with isIgnoredPlayer");
    }
}

QTEST_GUILESS_MAIN(PlayerMatchTest)

#include "playermatch_test.moc"
