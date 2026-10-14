// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 KShadowLrc Contributors

// Offline tests for the NetEase search path: request building, keyword
// construction, defensive JSON parsing and isLikelySongId. No network.

#include "providers/netease.h"

#include <KLocalizedString>

#include <cstdio>

#include <QCoreApplication>
#include <QDebug>
#include <QUrl>

using namespace kshadowlrc;

namespace kshadowlrc
{

/*! Reaches the provider's private helpers; declared a friend in netease.h. */
struct NeteaseTestAccess {
    static QString keyword(const TrackMeta &track)
    {
        return NeteaseProvider::buildSearchKeyword(track);
    }
    static NeteaseProvider::ApiRequest searchRequest(const QString &keyword, int limit)
    {
        return NeteaseProvider::buildSearchRequest(keyword, limit);
    }
    static QList<SongInfo> parseSearch(const QByteArray &body, QString *error)
    {
        return NeteaseProvider::parseSearchBody(body, error);
    }
};

} // namespace kshadowlrc

static int g_failures = 0;

// Diagnostics go through fprintf rather than qWarning/qInfo: Qt's default
// logging rules suppress qInfo, and a test summary that ctest -V does not show
// is useless.
#define CHECK(cond)                                                                                                                                            \
    do {                                                                                                                                                       \
        if (!(cond)) {                                                                                                                                         \
            ++g_failures;                                                                                                                                      \
            std::fprintf(stderr, "FAIL: %s at line %d\n", #cond, __LINE__);                                                                                    \
        }                                                                                                                                                      \
    } while (false)

#define CHECK_EQ(actual, expected)                                                                                                                             \
    do {                                                                                                                                                       \
        const auto actualValue = (actual);                                                                                                                     \
        const auto expectedValue = (expected);                                                                                                                 \
        if (!(actualValue == expectedValue)) {                                                                                                                 \
            ++g_failures;                                                                                                                                      \
            QString message;                                                                                                                                   \
            QDebug(&message).nospace() << "FAIL: " << #actual << " == " << #expected << " got " << actualValue << " expected" << expectedValue << " at line "  \
                                       << __LINE__;                                                                                                            \
            std::fprintf(stderr, "%s\n", message.toLocal8Bit().constData());                                                                                   \
        }                                                                                                                                                      \
    } while (false)

static TrackMeta makeTrack(const QString &title, const QString &album, const QStringList &artists)
{
    TrackMeta track;
    track.title = title;
    track.album = album;
    track.artists = artists;
    return track;
}

static void testKeywordConstruction()
{
    CHECK_EQ(NeteaseTestAccess::keyword(makeTrack(QStringLiteral("Song"), {}, {})), QStringLiteral("Song"));
    CHECK_EQ(NeteaseTestAccess::keyword(makeTrack(QStringLiteral("Song"), QStringLiteral("Album"), {})), QStringLiteral("Song Album"));
    CHECK_EQ(NeteaseTestAccess::keyword(makeTrack(QStringLiteral("Song"), {}, {QStringLiteral("A"), QStringLiteral("B")})), QStringLiteral("Song A/B"));
    CHECK_EQ(NeteaseTestAccess::keyword(makeTrack(QStringLiteral("T"), QStringLiteral("Al"), {QStringLiteral("A"), QStringLiteral("B")})),
             QStringLiteral("T Al A/B"));

    // Missing title: album/artists still search.
    CHECK_EQ(NeteaseTestAccess::keyword(makeTrack({}, QStringLiteral("Al"), {QStringLiteral("A")})), QStringLiteral("Al A"));
    // Missing album.
    CHECK_EQ(NeteaseTestAccess::keyword(makeTrack(QStringLiteral("T"), {}, {QStringLiteral("A")})), QStringLiteral("T A"));
    // No artists.
    CHECK_EQ(NeteaseTestAccess::keyword(makeTrack(QStringLiteral("T"), QStringLiteral("Al"), {})), QStringLiteral("T Al"));
    // Nothing usable at all.
    CHECK_EQ(NeteaseTestAccess::keyword(makeTrack({}, {}, {})), QString());
    CHECK_EQ(NeteaseTestAccess::keyword(makeTrack(QStringLiteral("   "), QStringLiteral(" "), {QStringLiteral(" ")})), QString());
    // Whitespace collapsed, artist / join.
    CHECK_EQ(NeteaseTestAccess::keyword(makeTrack(QStringLiteral("  T  "), QStringLiteral(" A "), {QStringLiteral(" X "), QStringLiteral("Y")})),
             QStringLiteral("T A X/Y"));
}

static void testSearchRequest()
{
    const auto request = NeteaseTestAccess::searchRequest(QStringLiteral("海阔天空 Beyond 乐与怒"), 30);
    CHECK_EQ(request.url, QUrl(QStringLiteral("https://music.163.com/api/cloudsearch/pc")));

    const QByteArray body = request.body;
    CHECK(body.startsWith("s="));
    const int sEnd = body.indexOf("&type=1");
    CHECK(sEnd > 2);
    CHECK_EQ(QUrl::fromPercentEncoding(body.mid(2, sEnd - 2)), QStringLiteral("海阔天空 Beyond 乐与怒"));
    CHECK(body.endsWith("&type=1&limit=30&offset=0&total=true"));

    // limit <= 0 falls back to the default.
    CHECK(NeteaseTestAccess::searchRequest(QStringLiteral("x"), 0).body.endsWith("&type=1&limit=30&offset=0&total=true"));
    CHECK(NeteaseTestAccess::searchRequest(QStringLiteral("x"), 7).body.endsWith("&type=1&limit=7&offset=0&total=true"));
}

static void testSearchParsing()
{
    QString error;

    const QByteArray full = R"json({
        "code": 200,
        "result": {
            "songCount": 4,
            "songs": [
                {"id": 347230, "name": "海阔天空", "dt": 326000, "ar": [{"id": 1, "name": "Beyond"}], "al": {"id": 2, "name": "乐与怒"}},
                {"id": "5257138", "name": "海阔天空 (Live)", "dt": 400000, "ar": [{"id": 1, "name": "Beyond"}, {"id": 9}, "bogus"], "al": null},
                {"id": 111, "name": "no artist", "dt": 1000, "ar": []},
                {"name": "orphan without id", "dt": 1, "ar": [], "al": null}
            ]
        }
    })json";

    error = QStringLiteral("dirty");
    const QList<SongInfo> songs = NeteaseTestAccess::parseSearch(full, &error);
    CHECK(error.isEmpty());
    CHECK_EQ(songs.size(), 3);

    CHECK_EQ(songs[0].id, QStringLiteral("347230"));
    CHECK_EQ(songs[0].title, QStringLiteral("海阔天空"));
    CHECK_EQ(songs[0].singer, QStringLiteral("Beyond"));
    CHECK_EQ(songs[0].album, QStringLiteral("乐与怒"));
    CHECK_EQ(songs[0].lengthMs, static_cast<qint64>(326000));

    // Stringified id, artist entry without a name skipped, `al: null` -> empty.
    CHECK_EQ(songs[1].id, QStringLiteral("5257138"));
    CHECK_EQ(songs[1].singer, QStringLiteral("Beyond"));
    CHECK_EQ(songs[1].album, QString());
    CHECK_EQ(songs[1].lengthMs, static_cast<qint64>(400000));

    // Missing ar/al -> empty.
    CHECK_EQ(songs[2].id, QStringLiteral("111"));
    CHECK_EQ(songs[2].singer, QString());
    CHECK_EQ(songs[2].album, QString());
    CHECK_EQ(songs[2].lengthMs, static_cast<qint64>(1000));

    // Malformed fields are tolerated, not fatal.
    const QByteArray partial = R"json({"code":200,"result":{"songs":[{"id":222,"name":"partial","ar":null,"al":"not an object","dt":"42"}]}})json";
    error = QStringLiteral("dirty");
    const QList<SongInfo> partialSongs = NeteaseTestAccess::parseSearch(partial, &error);
    CHECK(error.isEmpty());
    CHECK_EQ(partialSongs.size(), 1);
    CHECK_EQ(partialSongs[0].id, QStringLiteral("222"));
    CHECK_EQ(partialSongs[0].title, QStringLiteral("partial"));
    CHECK_EQ(partialSongs[0].singer, QString());
    CHECK_EQ(partialSongs[0].album, QString());
    CHECK_EQ(partialSongs[0].lengthMs, static_cast<qint64>(42));

    // Empty result set vs missing `songs` vs missing `result`: all succeed empty.
    error = QStringLiteral("dirty");
    CHECK(NeteaseTestAccess::parseSearch(R"json({"code":200,"result":{"songCount":0,"songs":[]}})json", &error).isEmpty());
    CHECK(error.isEmpty());
    error = QStringLiteral("dirty");
    CHECK(NeteaseTestAccess::parseSearch(R"json({"code":200,"result":{"songCount":0}})json", &error).isEmpty());
    CHECK(error.isEmpty());
    error = QStringLiteral("dirty");
    CHECK(NeteaseTestAccess::parseSearch(R"json({"code":200})json", &error).isEmpty());
    CHECK(error.isEmpty());

    // Invalid input is an error, never an empty success.
    for (const QByteArray &bad : {QByteArray(), QByteArray("   "), QByteArray(R"json({"result":{"songs":[)json"), QByteArray("[1,2,3]")}) {
        error.clear();
        CHECK(NeteaseTestAccess::parseSearch(bad, &error).isEmpty());
        CHECK(!error.isEmpty());
    }
    // A null error pointer must be tolerated.
    (void)NeteaseTestAccess::parseSearch(QByteArray("nope"), nullptr);
}

static void testIsLikelySongId()
{
    NeteaseProvider provider;
    CHECK_EQ(provider.name(), QStringLiteral("netease"));

    for (const QString &id : {QStringLiteral("1145141919"), QStringLiteral("114"), QStringLiteral("0")}) {
        CHECK(provider.isLikelySongId(id));
    }
    for (const QString &id : {QStringLiteral(""),
                              QStringLiteral("11451.41919"),
                              QStringLiteral("abc"),
                              QStringLiteral("12a"),
                              QStringLiteral(" 12"),
                              QStringLiteral("-12"),
                              QStringLiteral("١٢")}) {
        CHECK(!provider.isLikelySongId(id));
    }
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("test_netease_search"));
    KLocalizedString::setApplicationDomain("kshadowlrc");

    testKeywordConstruction();
    testSearchRequest();
    testSearchParsing();
    testIsLikelySongId();

    if (g_failures > 0) {
        std::fprintf(stderr, "test_netease_search: %d check(s) failed\n", g_failures);
        return 1;
    }
    std::fprintf(stdout, "test_netease_search: keyword, request and defensive search parsing all passed\n");
    return 0;
}
