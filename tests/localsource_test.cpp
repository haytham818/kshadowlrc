// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 KShadowLrc Contributors
//
// Offline checks for the local lyric source: the file:// id decoding and the
// sidecar naming rules, including the fall-through to the file's own tag.
//
// tests/data/lyrics.flac is the one committed fixture used here: a 0.3 s silent
// FLAC whose Vorbis LYRICS field is
//   [00:01.00]the first committed line
//   [00:04.24]the second committed line
// It is committed rather than built at run time because the tag has to be
// written by TagLib, and TagLib refuses a container with no valid audio frames.

#include "lyrics/localsource.h"
#include "lyrics/lrcparser.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>

using namespace kshadowlrc;

namespace
{
const QString taggedFixture = QString::fromUtf8(KSHADOWLRC_TEST_DATA_DIR) + QStringLiteral("/lyrics.flac");
} // namespace

class LocalSourceTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void songIdToPathRoundTrip();
    void songIdRejectsNonLocalIds();
    void sidecarPairing();
    void translationWithoutOriginalIsIgnored();
    void sidecarBomIsStripped();
    void missingSidecarFallsThroughToTag();
    void emptySidecarFallsThroughToTag();
    void unreadableSidecarFallsThroughToTag();
    void sidecarWinsOverTag();
    void noLyricAnywhere();

private:
    void write(const QString &path, const QByteArray &bytes);
    QString copyTaggedFixture(const QTemporaryDir &dir, const QString &name);
};

void LocalSourceTest::write(const QString &path, const QByteArray &bytes)
{
    QFile file(path);
    QVERIFY2(file.open(QIODevice::WriteOnly | QIODevice::Truncate), qPrintable(path));
    QCOMPARE(file.write(bytes), qint64(bytes.size()));
    file.close();
}

QString LocalSourceTest::copyTaggedFixture(const QTemporaryDir &dir, const QString &name)
{
    const QString target = dir.filePath(name);
    QFile::copy(taggedFixture, target);
    return target;
}

void LocalSourceTest::songIdToPathRoundTrip()
{
    // A real library path: spaces and CJK, so percent-encoding actually bites.
    const QString path = QStringLiteral("/mnt/windows_c/Users/hayth/Music/Album/Guiano ヰ世界情緒 - 月 feat. ヰ世界情緒.flac");
    const QUrl url = QUrl::fromLocalFile(path);

    // The id an MPRIS player exposes is the percent-encoded UTF-8 URL.
    const QString id = QString::fromUtf8(url.toEncoded());
    QVERIFY(id.contains(QLatin1String("%20")));
    const std::optional<QString> decoded = localPathFromSongId(id);
    QVERIFY(decoded.has_value());
    QCOMPARE(*decoded, path);

    // A "pretty" serialisation (spaces and CJK left literal) must decode too.
    QCOMPARE(localPathFromSongId(url.toString()).value(), path);

    const QString spaced = QStringLiteral("/tmp/a b/c d.flac");
    QCOMPARE(localPathFromSongId(QUrl::fromLocalFile(spaced).toString()).value(), spaced);
}

void LocalSourceTest::songIdRejectsNonLocalIds()
{
    QVERIFY(!localPathFromSongId(QString()).has_value());
    QVERIFY(!localPathFromSongId(QStringLiteral("netease:12345")).has_value());
    QVERIFY(!localPathFromSongId(QStringLiteral("https://example.com/song.flac")).has_value());
    // A bare path is not a URL and is not accepted.
    QVERIFY(!localPathFromSongId(QStringLiteral("/home/hayth/song.flac")).has_value());
}

void LocalSourceTest::sidecarPairing()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString audio = dir.filePath(QStringLiteral("Song.flac"));
    write(audio, QByteArray()); // no tag: the sidecar is the only source
    write(dir.filePath(QStringLiteral("Song.lrc")), QByteArrayLiteral("[00:01.00]original\n"));
    write(dir.filePath(QStringLiteral("Song.zh.lrc")), QByteArrayLiteral("[00:01.00]翻译\n"));

    LocalFiles source;
    const std::optional<LyricStore> store = source.load(audio);
    QVERIFY(store.has_value());
    QCOMPARE(store->lyric, QStringLiteral("[00:01.00]original\n"));
    QCOMPARE(store->tlyric, QStringLiteral("[00:01.00]翻译\n"));
}

void LocalSourceTest::translationWithoutOriginalIsIgnored()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString audio = dir.filePath(QStringLiteral("Song.flac"));
    write(audio, QByteArray());
    // A translation with no original is not a lyric.
    write(dir.filePath(QStringLiteral("Song.zh.lrc")), QByteArrayLiteral("[00:01.00]翻译\n"));

    LocalFiles source;
    QVERIFY(!source.load(audio).has_value());
}

void LocalSourceTest::sidecarBomIsStripped()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString audio = dir.filePath(QStringLiteral("Song.flac"));
    write(audio, QByteArray());
    write(dir.filePath(QStringLiteral("Song.lrc")), QByteArrayLiteral("\xEF\xBB\xBF[00:01.00]lyric\n"));

    LocalFiles source;
    const std::optional<LyricStore> store = source.load(audio);
    QVERIFY(store.has_value());
    QCOMPARE(store->lyric, QStringLiteral("[00:01.00]lyric\n"));
}

void LocalSourceTest::missingSidecarFallsThroughToTag()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString audio = copyTaggedFixture(dir, QStringLiteral("Song.flac"));
    QVERIFY(QFile::exists(audio));

    LocalFiles source;
    const std::optional<LyricStore> store = source.load(audio);
    QVERIFY(store.has_value());
    QVERIFY(store->lyric.startsWith(QStringLiteral("[00:01.00]the first committed line")));
    QVERIFY(store->lyric.contains(QStringLiteral("the second committed line")));
    QVERIFY(store->tlyric.isEmpty());

    const Lyric parsed = parseLrc(store->lyric);
    QCOMPARE(parsed.kind, LyricKind::Timed);
    QCOMPARE(parsed.lines.size(), 2);
}

void LocalSourceTest::emptySidecarFallsThroughToTag()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString audio = copyTaggedFixture(dir, QStringLiteral("Song.flac"));
    // A blank file is a leftover, not a lyric: the tag has to win.
    write(dir.filePath(QStringLiteral("Song.lrc")), QByteArrayLiteral("\n   \n"));

    LocalFiles source;
    const std::optional<LyricStore> store = source.load(audio);
    QVERIFY(store.has_value());
    QVERIFY(store->lyric.contains(QStringLiteral("the first committed line")));
}

void LocalSourceTest::unreadableSidecarFallsThroughToTag()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString audio = copyTaggedFixture(dir, QStringLiteral("Song.flac"));
    // A directory at the sidecar path fails to read whether or not the test runs
    // as root, unlike chmod 000.
    QVERIFY(QDir().mkpath(dir.filePath(QStringLiteral("Song.lrc"))));

    LocalFiles source;
    const std::optional<LyricStore> store = source.load(audio);
    QVERIFY(store.has_value());
    QVERIFY(store->lyric.contains(QStringLiteral("the first committed line")));
}

void LocalSourceTest::sidecarWinsOverTag()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString audio = copyTaggedFixture(dir, QStringLiteral("Song.flac"));
    write(dir.filePath(QStringLiteral("Song.lrc")), QByteArrayLiteral("[00:09.00]edited by hand\n"));

    LocalFiles source;
    const std::optional<LyricStore> store = source.load(audio);
    QVERIFY(store.has_value());
    QCOMPARE(store->lyric, QStringLiteral("[00:09.00]edited by hand\n"));
    QVERIFY(!store->lyric.contains(QStringLiteral("committed line")));
}

void LocalSourceTest::noLyricAnywhere()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString audio = dir.filePath(QStringLiteral("Song.flac"));
    write(audio, QByteArray()); // untagged and no sidecar

    LocalFiles source;
    QVERIFY(!source.load(audio).has_value());
    // A file that does not exist at all is just as lyric-less.
    QVERIFY(!source.load(dir.filePath(QStringLiteral("ghost.flac"))).has_value());
}

QTEST_GUILESS_MAIN(LocalSourceTest)

#include "localsource_test.moc"
