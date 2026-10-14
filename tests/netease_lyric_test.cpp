// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 KShadowLrc Contributors

// Offline tests for the NetEase lyric path: request building, placeholder
// filtering, "no lyrics" flags and defensive JSON parsing. No network.

#include "providers/netease.h"

#include <KLocalizedString>

#include <QTest>
#include <QUrl>

using namespace kshadowlrc;

namespace kshadowlrc
{

/*! Reaches the provider's private helpers; declared a friend in netease.h. */
struct NeteaseTestAccess {
    static NeteaseProvider::ApiRequest lyricRequest(const QString &songId)
    {
        return NeteaseProvider::buildLyricRequest(songId);
    }
    static LyricStore parseLyric(const QByteArray &body, QString *error)
    {
        return NeteaseProvider::parseLyricBody(body, error);
    }
};

} // namespace kshadowlrc

static LyricStore parse(const QByteArray &body, QString *error)
{
    error->clear();
    return NeteaseTestAccess::parseLyric(body, error);
}

class NeteaseLyricTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void lyricRequest();
    void lyricParsing();
    void lyricErrors();
};

void NeteaseLyricTest::initTestCase()
{
    QCoreApplication::setApplicationName(QStringLiteral("test_netease_lyric"));
    KLocalizedString::setApplicationDomain("kshadowlrc");
}

void NeteaseLyricTest::lyricRequest()
{
    const auto request = NeteaseTestAccess::lyricRequest(QStringLiteral("347230"));
    QCOMPARE(request.url, QUrl(QStringLiteral("https://music.163.com/api/song/lyric")));
    // Regression for trap #1: all three version parameters are mandatory.
    QCOMPARE(request.body, QByteArray("id=347230&lv=-1&kv=-1&tv=-1"));
}

void NeteaseLyricTest::lyricParsing()
{
    QString error;

    // Both fields are returned verbatim.
    LyricStore store = parse(R"json({"code":200,"lrc":{"version":1,"lyric":"[00:00.00]line one\n[00:03.00]line two"},
        "tlyric":{"version":1,"lyric":"[00:00.00]译一\n[00:03.00]译二"}})json",
                             &error);
    QVERIFY(error.isEmpty());
    QVERIFY(store.lyric.contains(QStringLiteral("line one")));
    QVERIFY(store.lyric.contains(QStringLiteral("line two")));
    QVERIFY(store.tlyric.contains(QStringLiteral("译一")));
    QVERIFY(store.tlyric.contains(QStringLiteral("译二")));
    QVERIFY(store.hasAny());

    // The server placeholder is not a lyric (the Rust reference shows it; we do not).
    store = parse(R"json({"code":200,"lrc":{"version":1,"lyric":"[00:00.00]暂无歌词"}})json", &error);
    QVERIFY(error.isEmpty());
    QVERIFY(!store.hasAny());
    QVERIFY(store.lyric.isEmpty());
    QVERIFY(store.tlyric.isEmpty());

    // Equivalents without a timestamp, and the pure-music notice, are filtered too.
    store = parse(R"json({"code":200,"lrc":{"lyric":"暂无歌词。"}})json", &error);
    QVERIFY(error.isEmpty());
    QVERIFY(!store.hasAny());
    store = parse(R"json({"code":200,"lrc":{"lyric":"[00:00.00]纯音乐，请欣赏"}})json", &error);
    QVERIFY(error.isEmpty());
    QVERIFY(!store.hasAny());

    // A real line next to a placeholder keeps the lyric.
    store = parse(R"json({"code":200,"lrc":{"lyric":"[00:00.00]hello\n[00:01.00]暂无歌词"}})json", &error);
    QVERIFY(error.isEmpty());
    QVERIFY(!store.lyric.isEmpty());

    // Metadata-only text carries no lyric.
    store = parse(R"json({"code":200,"lrc":{"lyric":"[ar:Beyond]\n[ti:海阔天空]"}})json", &error);
    QVERIFY(error.isEmpty());
    QVERIFY(!store.hasAny());

    // `uncollected` / `nolyric` / `pureMusic` mean "no lyrics", not an error, and
    // win even if a body is present.
    for (const QByteArray &flags : {QByteArray(R"json({"code":200,"uncollected":true,"lrc":{"lyric":"[00:00.00]surprise"}})json"),
                                    QByteArray(R"json({"code":200,"nolyric":true})json"),
                                    QByteArray(R"json({"code":200,"pureMusic":true})json")}) {
        store = parse(flags, &error);
        QVERIFY(error.isEmpty());
        QVERIFY(!store.hasAny());
    }

    // A translation with timestamps but no text is not a translation.
    store = parse(R"json({"code":200,"lrc":{"lyric":"[00:00.00]hello"},"tlyric":{"lyric":"[00:00.00]\n[00:03.00]"}})json", &error);
    QVERIFY(error.isEmpty());
    QVERIFY(store.lyric.contains(QStringLiteral("hello")));
    QVERIFY(store.tlyric.isEmpty());

    // tlyric empty or null: lyric kept, translation dropped.
    store = parse(R"json({"code":200,"lrc":{"lyric":"[00:00.00]hello"},"tlyric":{"lyric":""}})json", &error);
    QVERIFY(error.isEmpty());
    QVERIFY(store.lyric.contains(QStringLiteral("hello")));
    QVERIFY(store.tlyric.isEmpty());
    store = parse(R"json({"code":200,"lrc":{"lyric":"[00:00.00]hello"},"tlyric":null})json", &error);
    QVERIFY(error.isEmpty());
    QVERIFY(store.lyric.contains(QStringLiteral("hello")));
    QVERIFY(store.tlyric.isEmpty());

    // lrc null: only the translation survives.
    store = parse(R"json({"code":200,"lrc":null,"tlyric":{"lyric":"[00:00.00]t"}})json", &error);
    QVERIFY(error.isEmpty());
    QVERIFY(store.lyric.isEmpty());
    QVERIFY(store.tlyric.contains(QStringLiteral("t")));

    // Neither field present: an empty store, still a success.
    store = parse(R"json({"code":200})json", &error);
    QVERIFY(error.isEmpty());
    QVERIFY(!store.hasAny());
}

void NeteaseLyricTest::lyricErrors()
{
    QString error;

    // An empty body with HTTP 200 is the encrypted-failure mode; it must be an
    // error, never "no lyrics".
    for (const QByteArray &bad : {QByteArray(), QByteArray("   "), QByteArray(R"json({"lrc":{"lyric":)json"), QByteArray("\"nope\""), QByteArray("[1,2,3]")}) {
        error.clear();
        const LyricStore store = NeteaseTestAccess::parseLyric(bad, &error);
        QVERIFY(!error.isEmpty());
        QVERIFY(!store.hasAny());
    }
    (void)NeteaseTestAccess::parseLyric(QByteArray("nope"), nullptr);
}

QTEST_GUILESS_MAIN(NeteaseLyricTest)

#include "netease_lyric_test.moc"
