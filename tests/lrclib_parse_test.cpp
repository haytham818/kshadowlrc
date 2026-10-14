// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 KShadowLrc Contributors
//
// Offline checks for the LRCLib JSON -> SongInfo / LyricStore mapping, driven by
// saved fixtures captured from the live API (no network in this test).

#include "lyrics/providers/lrclib.h"

#include <QTest>

using namespace kshadowlrc;

// The class has to precede the fixtures: one of them contains an apostrophe
// ("they're") and moc lexes a lone apostrophe as the start of a character
// literal, which would swallow the Q_OBJECT macro if it came first.
class LrclibParseTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void searchMapping();
    void getMapping();
    void instrumental();
    void plainFallback();
    void notFound();
    void malformed();
};

// Shape of GET /api/search?track_name=Yesterday&artist_name=The%20Beatles
static const auto kSearchFixture = R"JSON([
{"id":23920346,"name":"Yesterday","trackName":"Yesterday","artistName":"The Beatles - The Beatles","albumName":"The Beatles - 1962-1966","duration":125.02845,"instrumental":false,"hasWordSync":false,"plainLyrics":"Yesterday, all my troubles seemed so far away","syncedLyrics":"[00:05.34] Yesterday, all my troubles seemed so far away\n[00:13.20] Now it looks as though they're here to stay"},
{"id":3058074,"name":"Yesterday","trackName":"Yesterday","artistName":"The Beatles","albumName":"The Beatles 1962 - 1966","duration":126.0,"instrumental":false,"hasWordSync":false,"plainLyrics":"Yesterday, all my troubles","syncedLyrics":"[00:05.00] Yesterday, all my troubles seemed so far away"}
])JSON";

// Shape of GET /api/get/{id}
static const auto kGetFixture = R"JSON(
{"id":23920346,"name":"Yesterday","trackName":"Yesterday","artistName":"The Beatles - The Beatles","albumName":"The Beatles - 1962-1966","duration":125.02845,"instrumental":false,"hasWordSync":false,"plainLyrics":"plain body","syncedLyrics":"[00:05.34] Yesterday, all my troubles seemed so far away"}
)JSON";

// /api/get for an instrumental track: no lyric text at all.
static const auto kInstrumentalFixture = R"JSON(
{"id":1,"trackName":"Intro","artistName":"Someone","albumName":"An Album","duration":30.0,"instrumental":true,"hasWordSync":false,"plainLyrics":null,"syncedLyrics":null}
)JSON";

// Plain-only track: syncedLyrics absent, plainLyrics present.
static const auto kPlainFixture = R"JSON(
{"id":2,"trackName":"Plain","artistName":"X","albumName":"Y","duration":10.0,"instrumental":false,"hasWordSync":false,"plainLyrics":"line one\nline two"}
)JSON";

// The real 404 body.
static const auto kNotFoundFixture = R"JSON(
{"message":"Failed to find specified track","name":"TrackNotFound","statusCode":404}
)JSON";

void LrclibParseTest::searchMapping()
{
    bool ok = false;
    const QList<SongInfo> results = LRCLibProvider::parseSearchResponse(QByteArray(kSearchFixture), &ok);
    QVERIFY(ok);
    QVERIFY(results.size() == 2);
    if (results.size() == 2) {
        QVERIFY(results.at(0).id == QStringLiteral("23920346"));
        QVERIFY(results.at(0).title == QStringLiteral("Yesterday"));
        QVERIFY(results.at(0).singer == QStringLiteral("The Beatles - The Beatles"));
        QVERIFY(results.at(0).album == QStringLiteral("The Beatles - 1962-1966"));
        QVERIFY(results.at(0).lengthMs == 125028); // 125.02845 s
        QVERIFY(results.at(1).lengthMs == 126000);
    }
}

void LrclibParseTest::getMapping()
{
    bool ok = false;
    const auto response = LRCLibProvider::parseGetResponse(QByteArray(kGetFixture), &ok);
    QVERIFY(ok);
    QVERIFY(response.found);
    QVERIFY(!response.instrumental);
    QVERIFY(response.store.lyric.startsWith(QStringLiteral("[00:05.34] Yesterday")));
    QVERIFY(response.store.tlyric.isEmpty());
}

void LrclibParseTest::instrumental()
{
    bool ok = false;
    const auto response = LRCLibProvider::parseGetResponse(QByteArray(kInstrumentalFixture), &ok);
    QVERIFY(ok);
    QVERIFY(response.found); // a real record was returned...
    QVERIFY(response.instrumental); // ...marked instrumental...
    QVERIFY(!response.store.hasAny()); // ...so it carries no lyric text
}

void LrclibParseTest::plainFallback()
{
    bool ok = false;
    const auto response = LRCLibProvider::parseGetResponse(QByteArray(kPlainFixture), &ok);
    QVERIFY(ok);
    QVERIFY(response.found);
    QVERIFY(response.store.lyric == QStringLiteral("line one\nline two"));
}

void LrclibParseTest::notFound()
{
    bool ok = false;
    // 404 body for a get: parsed fine, but nothing was found.
    const auto response = LRCLibProvider::parseGetResponse(QByteArray(kNotFoundFixture), &ok);
    QVERIFY(ok);
    QVERIFY(!response.found);

    // The same error body handed to the search parser yields an empty list.
    ok = false;
    const QList<SongInfo> results = LRCLibProvider::parseSearchResponse(QByteArray(kNotFoundFixture), &ok);
    QVERIFY(ok);
    QVERIFY(results.isEmpty());
}

void LrclibParseTest::malformed()
{
    bool ok = true;
    LRCLibProvider::parseSearchResponse("this is not json", &ok);
    QVERIFY(!ok);
    ok = true;
    LRCLibProvider::parseGetResponse("{broken", &ok);
    QVERIFY(!ok);
}

QTEST_GUILESS_MAIN(LrclibParseTest)

#include "lrclib_parse_test.moc"
