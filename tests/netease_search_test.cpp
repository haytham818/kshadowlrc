// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 KShadowLrc Contributors

// Offline tests for the NetEase search path: request building, keyword
// construction, defensive JSON parsing and isLikelySongId. No network.

#include "providers/netease.h"

#include <KLocalizedString>

#include <QTest>
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

static TrackMeta makeTrack(const QString &title, const QString &album, const QStringList &artists)
{
    TrackMeta track;
    track.title = title;
    track.album = album;
    track.artists = artists;
    return track;
}

class NeteaseSearchTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void keywordConstruction();
    void searchRequest();
    void searchParsing();
    void isLikelySongId();
};

void NeteaseSearchTest::initTestCase()
{
    QCoreApplication::setApplicationName(QStringLiteral("test_netease_search"));
    KLocalizedString::setApplicationDomain("kshadowlrc");
}

void NeteaseSearchTest::keywordConstruction()
{
    QCOMPARE(NeteaseTestAccess::keyword(makeTrack(QStringLiteral("Song"), {}, {})), QStringLiteral("Song"));
    QCOMPARE(NeteaseTestAccess::keyword(makeTrack(QStringLiteral("Song"), QStringLiteral("Album"), {})), QStringLiteral("Song Album"));
    QCOMPARE(NeteaseTestAccess::keyword(makeTrack(QStringLiteral("Song"), {}, {QStringLiteral("A"), QStringLiteral("B")})), QStringLiteral("Song A/B"));
    QCOMPARE(NeteaseTestAccess::keyword(makeTrack(QStringLiteral("T"), QStringLiteral("Al"), {QStringLiteral("A"), QStringLiteral("B")})),
             QStringLiteral("T Al A/B"));

    // Missing title: album/artists still search.
    QCOMPARE(NeteaseTestAccess::keyword(makeTrack({}, QStringLiteral("Al"), {QStringLiteral("A")})), QStringLiteral("Al A"));
    // Missing album.
    QCOMPARE(NeteaseTestAccess::keyword(makeTrack(QStringLiteral("T"), {}, {QStringLiteral("A")})), QStringLiteral("T A"));
    // No artists.
    QCOMPARE(NeteaseTestAccess::keyword(makeTrack(QStringLiteral("T"), QStringLiteral("Al"), {})), QStringLiteral("T Al"));
    // Nothing usable at all.
    QCOMPARE(NeteaseTestAccess::keyword(makeTrack({}, {}, {})), QString());
    QCOMPARE(NeteaseTestAccess::keyword(makeTrack(QStringLiteral("   "), QStringLiteral(" "), {QStringLiteral(" ")})), QString());
    // Whitespace collapsed, artist / join.
    QCOMPARE(NeteaseTestAccess::keyword(makeTrack(QStringLiteral("  T  "), QStringLiteral(" A "), {QStringLiteral(" X "), QStringLiteral("Y")})),
             QStringLiteral("T A X/Y"));
}

void NeteaseSearchTest::searchRequest()
{
    const auto request = NeteaseTestAccess::searchRequest(QStringLiteral("海阔天空 Beyond 乐与怒"), 30);
    QCOMPARE(request.url, QUrl(QStringLiteral("https://music.163.com/api/cloudsearch/pc")));

    const QByteArray body = request.body;
    QVERIFY(body.startsWith("s="));
    const int sEnd = body.indexOf("&type=1");
    QVERIFY(sEnd > 2);
    QCOMPARE(QUrl::fromPercentEncoding(body.mid(2, sEnd - 2)), QStringLiteral("海阔天空 Beyond 乐与怒"));
    QVERIFY(body.endsWith("&type=1&limit=30&offset=0&total=true"));

    // limit <= 0 falls back to the default.
    QVERIFY(NeteaseTestAccess::searchRequest(QStringLiteral("x"), 0).body.endsWith("&type=1&limit=30&offset=0&total=true"));
    QVERIFY(NeteaseTestAccess::searchRequest(QStringLiteral("x"), 7).body.endsWith("&type=1&limit=7&offset=0&total=true"));
}

void NeteaseSearchTest::searchParsing()
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
    QVERIFY(error.isEmpty());
    QCOMPARE(songs.size(), 3);

    QCOMPARE(songs[0].id, QStringLiteral("347230"));
    QCOMPARE(songs[0].title, QStringLiteral("海阔天空"));
    QCOMPARE(songs[0].singer, QStringLiteral("Beyond"));
    QCOMPARE(songs[0].album, QStringLiteral("乐与怒"));
    QCOMPARE(songs[0].lengthMs, static_cast<qint64>(326000));

    // Stringified id, artist entry without a name skipped, `al: null` -> empty.
    QCOMPARE(songs[1].id, QStringLiteral("5257138"));
    QCOMPARE(songs[1].singer, QStringLiteral("Beyond"));
    QCOMPARE(songs[1].album, QString());
    QCOMPARE(songs[1].lengthMs, static_cast<qint64>(400000));

    // Missing ar/al -> empty.
    QCOMPARE(songs[2].id, QStringLiteral("111"));
    QCOMPARE(songs[2].singer, QString());
    QCOMPARE(songs[2].album, QString());
    QCOMPARE(songs[2].lengthMs, static_cast<qint64>(1000));

    // Malformed fields are tolerated, not fatal.
    const QByteArray partial = R"json({"code":200,"result":{"songs":[{"id":222,"name":"partial","ar":null,"al":"not an object","dt":"42"}]}})json";
    error = QStringLiteral("dirty");
    const QList<SongInfo> partialSongs = NeteaseTestAccess::parseSearch(partial, &error);
    QVERIFY(error.isEmpty());
    QCOMPARE(partialSongs.size(), 1);
    QCOMPARE(partialSongs[0].id, QStringLiteral("222"));
    QCOMPARE(partialSongs[0].title, QStringLiteral("partial"));
    QCOMPARE(partialSongs[0].singer, QString());
    QCOMPARE(partialSongs[0].album, QString());
    QCOMPARE(partialSongs[0].lengthMs, static_cast<qint64>(42));

    // Empty result set vs missing `songs` vs missing `result`: all succeed empty.
    error = QStringLiteral("dirty");
    QVERIFY(NeteaseTestAccess::parseSearch(R"json({"code":200,"result":{"songCount":0,"songs":[]}})json", &error).isEmpty());
    QVERIFY(error.isEmpty());
    error = QStringLiteral("dirty");
    QVERIFY(NeteaseTestAccess::parseSearch(R"json({"code":200,"result":{"songCount":0}})json", &error).isEmpty());
    QVERIFY(error.isEmpty());
    error = QStringLiteral("dirty");
    QVERIFY(NeteaseTestAccess::parseSearch(R"json({"code":200})json", &error).isEmpty());
    QVERIFY(error.isEmpty());

    // Invalid input is an error, never an empty success.
    for (const QByteArray &bad : {QByteArray(), QByteArray("   "), QByteArray(R"json({"result":{"songs":[)json"), QByteArray("[1,2,3]")}) {
        error.clear();
        QVERIFY(NeteaseTestAccess::parseSearch(bad, &error).isEmpty());
        QVERIFY(!error.isEmpty());
    }
    // A null error pointer must be tolerated.
    (void)NeteaseTestAccess::parseSearch(QByteArray("nope"), nullptr);
}

void NeteaseSearchTest::isLikelySongId()
{
    NeteaseProvider provider;
    QCOMPARE(provider.name(), QStringLiteral("netease"));

    for (const QString &id : {QStringLiteral("1145141919"), QStringLiteral("114"), QStringLiteral("0")}) {
        QVERIFY(provider.isLikelySongId(id));
    }
    for (const QString &id : {QStringLiteral(""),
                              QStringLiteral("11451.41919"),
                              QStringLiteral("abc"),
                              QStringLiteral("12a"),
                              QStringLiteral(" 12"),
                              QStringLiteral("-12"),
                              QStringLiteral("١٢")}) {
        QVERIFY(!provider.isLikelySongId(id));
    }
}

QTEST_GUILESS_MAIN(NeteaseSearchTest)

#include "netease_search_test.moc"
