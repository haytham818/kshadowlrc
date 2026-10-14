// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 KShadowLrc Contributors
//
// Offline checks for the LRCLib JSON -> SongInfo / LyricStore mapping, driven by
// saved fixtures captured from the live API (no network in this test).

#include "lyrics/providers/lrclib.h"

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

static void testSearchMapping()
{
    bool ok = false;
    const QList<SongInfo> results = LRCLibProvider::parseSearchResponse(QByteArray(kSearchFixture), &ok);
    CHECK(ok);
    CHECK(results.size() == 2);
    if (results.size() == 2) {
        CHECK(results.at(0).id == QStringLiteral("23920346"));
        CHECK(results.at(0).title == QStringLiteral("Yesterday"));
        CHECK(results.at(0).singer == QStringLiteral("The Beatles - The Beatles"));
        CHECK(results.at(0).album == QStringLiteral("The Beatles - 1962-1966"));
        CHECK(results.at(0).lengthMs == 125028); // 125.02845 s
        CHECK(results.at(1).lengthMs == 126000);
    }
}

static void testGetMapping()
{
    bool ok = false;
    const auto response = LRCLibProvider::parseGetResponse(QByteArray(kGetFixture), &ok);
    CHECK(ok);
    CHECK(response.found);
    CHECK(!response.instrumental);
    CHECK(response.store.lyric.startsWith(QStringLiteral("[00:05.34] Yesterday")));
    CHECK(response.store.tlyric.isEmpty());
}

static void testInstrumental()
{
    bool ok = false;
    const auto response = LRCLibProvider::parseGetResponse(QByteArray(kInstrumentalFixture), &ok);
    CHECK(ok);
    CHECK(response.found); // a real record was returned...
    CHECK(response.instrumental); // ...marked instrumental...
    CHECK(!response.store.hasAny()); // ...so it carries no lyric text
}

static void testPlainFallback()
{
    bool ok = false;
    const auto response = LRCLibProvider::parseGetResponse(QByteArray(kPlainFixture), &ok);
    CHECK(ok);
    CHECK(response.found);
    CHECK(response.store.lyric == QStringLiteral("line one\nline two"));
}

static void testNotFound()
{
    bool ok = false;
    // 404 body for a get: parsed fine, but nothing was found.
    const auto response = LRCLibProvider::parseGetResponse(QByteArray(kNotFoundFixture), &ok);
    CHECK(ok);
    CHECK(!response.found);

    // The same error body handed to the search parser yields an empty list.
    ok = false;
    const QList<SongInfo> results = LRCLibProvider::parseSearchResponse(QByteArray(kNotFoundFixture), &ok);
    CHECK(ok);
    CHECK(results.isEmpty());
}

static void testMalformed()
{
    bool ok = true;
    LRCLibProvider::parseSearchResponse("this is not json", &ok);
    CHECK(!ok);
    ok = true;
    LRCLibProvider::parseGetResponse("{broken", &ok);
    CHECK(!ok);
}

int main()
{
    testSearchMapping();
    testGetMapping();
    testInstrumental();
    testPlainFallback();
    testNotFound();
    testMalformed();

    if (g_failures == 0) {
        std::printf("test_lrclib_parse: all checks passed\n");
        return 0;
    }
    std::fprintf(stderr, "test_lrclib_parse: %d check(s) failed\n", g_failures);
    return 1;
}
