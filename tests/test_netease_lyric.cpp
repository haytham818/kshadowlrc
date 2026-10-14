// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 KShadowLrc Contributors

// Offline tests for the NetEase lyric path: request building, placeholder
// filtering, "no lyrics" flags and defensive JSON parsing. No network.

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

static LyricStore parse(const QByteArray &body, QString *error)
{
    error->clear();
    return NeteaseTestAccess::parseLyric(body, error);
}

static void testLyricRequest()
{
    const auto request = NeteaseTestAccess::lyricRequest(QStringLiteral("347230"));
    CHECK_EQ(request.url, QUrl(QStringLiteral("https://music.163.com/api/song/lyric")));
    // Regression for trap #1: all three version parameters are mandatory.
    CHECK_EQ(request.body, QByteArray("id=347230&lv=-1&kv=-1&tv=-1"));
}

static void testLyricParsing()
{
    QString error;

    // Both fields are returned verbatim.
    LyricStore store = parse(R"json({"code":200,"lrc":{"version":1,"lyric":"[00:00.00]line one\n[00:03.00]line two"},
        "tlyric":{"version":1,"lyric":"[00:00.00]译一\n[00:03.00]译二"}})json",
                             &error);
    CHECK(error.isEmpty());
    CHECK(store.lyric.contains(QStringLiteral("line one")));
    CHECK(store.lyric.contains(QStringLiteral("line two")));
    CHECK(store.tlyric.contains(QStringLiteral("译一")));
    CHECK(store.tlyric.contains(QStringLiteral("译二")));
    CHECK(store.hasAny());

    // The server placeholder is not a lyric (the Rust reference shows it; we do not).
    store = parse(R"json({"code":200,"lrc":{"version":1,"lyric":"[00:00.00]暂无歌词"}})json", &error);
    CHECK(error.isEmpty());
    CHECK(!store.hasAny());
    CHECK(store.lyric.isEmpty());
    CHECK(store.tlyric.isEmpty());

    // Equivalents without a timestamp, and the pure-music notice, are filtered too.
    store = parse(R"json({"code":200,"lrc":{"lyric":"暂无歌词。"}})json", &error);
    CHECK(error.isEmpty());
    CHECK(!store.hasAny());
    store = parse(R"json({"code":200,"lrc":{"lyric":"[00:00.00]纯音乐，请欣赏"}})json", &error);
    CHECK(error.isEmpty());
    CHECK(!store.hasAny());

    // A real line next to a placeholder keeps the lyric.
    store = parse(R"json({"code":200,"lrc":{"lyric":"[00:00.00]hello\n[00:01.00]暂无歌词"}})json", &error);
    CHECK(error.isEmpty());
    CHECK(!store.lyric.isEmpty());

    // Metadata-only text carries no lyric.
    store = parse(R"json({"code":200,"lrc":{"lyric":"[ar:Beyond]\n[ti:海阔天空]"}})json", &error);
    CHECK(error.isEmpty());
    CHECK(!store.hasAny());

    // `uncollected` / `nolyric` / `pureMusic` mean "no lyrics", not an error, and
    // win even if a body is present.
    for (const QByteArray &flags : {QByteArray(R"json({"code":200,"uncollected":true,"lrc":{"lyric":"[00:00.00]surprise"}})json"),
                                    QByteArray(R"json({"code":200,"nolyric":true})json"),
                                    QByteArray(R"json({"code":200,"pureMusic":true})json")}) {
        store = parse(flags, &error);
        CHECK(error.isEmpty());
        CHECK(!store.hasAny());
    }

    // A translation with timestamps but no text is not a translation.
    store = parse(R"json({"code":200,"lrc":{"lyric":"[00:00.00]hello"},"tlyric":{"lyric":"[00:00.00]\n[00:03.00]"}})json", &error);
    CHECK(error.isEmpty());
    CHECK(store.lyric.contains(QStringLiteral("hello")));
    CHECK(store.tlyric.isEmpty());

    // tlyric empty or null: lyric kept, translation dropped.
    store = parse(R"json({"code":200,"lrc":{"lyric":"[00:00.00]hello"},"tlyric":{"lyric":""}})json", &error);
    CHECK(error.isEmpty());
    CHECK(store.lyric.contains(QStringLiteral("hello")));
    CHECK(store.tlyric.isEmpty());
    store = parse(R"json({"code":200,"lrc":{"lyric":"[00:00.00]hello"},"tlyric":null})json", &error);
    CHECK(error.isEmpty());
    CHECK(store.lyric.contains(QStringLiteral("hello")));
    CHECK(store.tlyric.isEmpty());

    // lrc null: only the translation survives.
    store = parse(R"json({"code":200,"lrc":null,"tlyric":{"lyric":"[00:00.00]t"}})json", &error);
    CHECK(error.isEmpty());
    CHECK(store.lyric.isEmpty());
    CHECK(store.tlyric.contains(QStringLiteral("t")));

    // Neither field present: an empty store, still a success.
    store = parse(R"json({"code":200})json", &error);
    CHECK(error.isEmpty());
    CHECK(!store.hasAny());
}

static void testLyricErrors()
{
    QString error;

    // An empty body with HTTP 200 is the encrypted-failure mode; it must be an
    // error, never "no lyrics".
    for (const QByteArray &bad : {QByteArray(), QByteArray("   "), QByteArray(R"json({"lrc":{"lyric":)json"), QByteArray("\"nope\""), QByteArray("[1,2,3]")}) {
        error.clear();
        const LyricStore store = NeteaseTestAccess::parseLyric(bad, &error);
        CHECK(!error.isEmpty());
        CHECK(!store.hasAny());
    }
    (void)NeteaseTestAccess::parseLyric(QByteArray("nope"), nullptr);
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("test_netease_lyric"));
    KLocalizedString::setApplicationDomain("kshadowlrc");

    testLyricRequest();
    testLyricParsing();
    testLyricErrors();

    if (g_failures > 0) {
        std::fprintf(stderr, "test_netease_lyric: %d check(s) failed\n", g_failures);
        return 1;
    }
    std::fprintf(stdout, "test_netease_lyric: request, placeholder filtering and defensive lyric parsing all passed\n");
    return 0;
}
