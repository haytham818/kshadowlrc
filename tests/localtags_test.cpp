// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 KShadowLrc Contributors
//
// Offline checks for the TagLib-backed tag reader. Each test writes a lyric
// into a copy of a committed fixture with TagLib's own writer and reads it back
// through readLyricsText(), so both the write and the read path are exercised.
//
// The fixtures under tests/data/ are tiny (0.9-8 KB) silent containers, one per
// tag format, generated once with:
//   ffmpeg -f lavfi -i anullsrc=r=8000:cl=mono -t 0.3 -c:a flac  lyrics.flac
//   ffmpeg -f lavfi -i anullsrc=r=8000:cl=mono -t 0.3 -c:a libmp3lame audio.mp3
//   ffmpeg -f lavfi -i anullsrc=r=8000:cl=mono -t 0.3 -c:a aac audio.m4a
// (lyrics.flac additionally carries a Vorbis LYRICS field). They are committed
// because a fixture cannot be built in memory: TagLib refuses a container with
// no valid audio frames, even through its ByteVectorStream. Tests never shell
// out to ffmpeg or sox.

#include "lyrics/localtags.h"

#include <QFile>
#include <QTemporaryDir>
#include <QTest>

#include <taglib/flacfile.h>
#include <taglib/id3v2tag.h>
#include <taglib/mp4file.h>
#include <taglib/mpegfile.h>
#include <taglib/synchronizedlyricsframe.h>
#include <taglib/tpropertymap.h>
#include <taglib/tstring.h>
#include <taglib/unsynchronizedlyricsframe.h>
#include <taglib/xiphcomment.h>

using namespace kshadowlrc;

class LocalTagsTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void flacVorbisLyrics();
    void flacUnsyncedLyricsField();
    void flacBomIsStripped();
    void mp3Uslt();
    void mp3UsltWithDescription();
    void mp3SyltIsIgnored();
    void mp4Lyrics();
    void fileWithoutLyrics();
    void missingFile();

private:
    QString copyFixture(const QTemporaryDir &dir, const QString &fixture, const QString &name);
};

QString LocalTagsTest::copyFixture(const QTemporaryDir &dir, const QString &fixture, const QString &name)
{
    const QString source = QString::fromUtf8(KSHADOWLRC_TEST_DATA_DIR) + QLatin1Char('/') + fixture;
    const QString target = dir.filePath(name);
    QFile::copy(source, target);
    return target;
}

void LocalTagsTest::flacVorbisLyrics()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = copyFixture(dir, QStringLiteral("lyrics.flac"), QStringLiteral("song.flac"));
    QVERIFY(QFile::exists(path));

    const QString text = QStringLiteral("[00:01.00]written one\n[00:02.00]written two\n");
    const QByteArray encoded = QFile::encodeName(path);
    TagLib::FLAC::File file(encoded.constData());
    QVERIFY(file.isValid());
    TagLib::Ogg::XiphComment *comment = file.xiphComment(true);
    QVERIFY(comment);
    comment->addField("LYRICS", TagLib::String(text.toUtf8().constData(), TagLib::String::UTF8), true);
    QVERIFY(file.save());

    const std::optional<QString> read = readLyricsText(path);
    QVERIFY(read.has_value());
    QCOMPARE(*read, text);
}

void LocalTagsTest::flacUnsyncedLyricsField()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = copyFixture(dir, QStringLiteral("lyrics.flac"), QStringLiteral("song.flac"));

    // APE / Monkey's Audio, and taggers that follow the Ogg spelling, call the
    // same field UNSYNCEDLYRICS. The fixture's own LYRICS field is removed so
    // this is what readLyricsText() has to find.
    const QString text = QStringLiteral("[00:01.00]unsynced\n");
    const QByteArray encoded = QFile::encodeName(path);
    TagLib::FLAC::File file(encoded.constData());
    QVERIFY(file.isValid());
    TagLib::Ogg::XiphComment *comment = file.xiphComment();
    QVERIFY(comment);
    comment->removeFields("LYRICS");
    comment->addField("UNSYNCEDLYRICS", TagLib::String(text.toUtf8().constData(), TagLib::String::UTF8), true);
    QVERIFY(file.save());

    const std::optional<QString> read = readLyricsText(path);
    QVERIFY(read.has_value());
    QCOMPARE(*read, text);
}

void LocalTagsTest::flacBomIsStripped()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = copyFixture(dir, QStringLiteral("lyrics.flac"), QStringLiteral("song.flac"));

    const QString text = QStringLiteral("[00:01.00]bom line\n");
    const QByteArray encoded = QFile::encodeName(path);
    TagLib::FLAC::File file(encoded.constData());
    QVERIFY(file.isValid());
    const QByteArray bomText = (QStringLiteral("\uFEFF") + text).toUtf8();
    file.xiphComment(true)->addField("LYRICS", TagLib::String(bomText.constData(), TagLib::String::UTF8), true);
    QVERIFY(file.save());

    const std::optional<QString> read = readLyricsText(path);
    QVERIFY(read.has_value());
    QCOMPARE(*read, text);
}

void LocalTagsTest::mp3Uslt()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = copyFixture(dir, QStringLiteral("audio.mp3"), QStringLiteral("song.mp3"));

    const QString text = QStringLiteral("[00:01.00]written one\n[00:02.00]written two\n");
    const QByteArray encoded = QFile::encodeName(path);
    TagLib::MPEG::File file(encoded.constData());
    QVERIFY(file.isValid());
    TagLib::ID3v2::Tag *id3 = file.ID3v2Tag(true);
    QVERIFY(id3);
    auto *frame = new TagLib::ID3v2::UnsynchronizedLyricsFrame(TagLib::String::UTF8);
    frame->setText(TagLib::String(text.toUtf8().constData(), TagLib::String::UTF8));
    id3->addFrame(frame);
    QVERIFY(file.save(TagLib::MPEG::File::ID3v2, TagLib::File::StripOthers));

    const std::optional<QString> read = readLyricsText(path);
    QVERIFY(read.has_value());
    QCOMPARE(*read, text);
}

void LocalTagsTest::mp3UsltWithDescription()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = copyFixture(dir, QStringLiteral("audio.mp3"), QStringLiteral("song.mp3"));

    // Several USLT frames may coexist; this one carries a description, which
    // TagLib surfaces under the LYRICS:<description> property key.
    const QString text = QStringLiteral("[00:01.00]described\n");
    const QByteArray encoded = QFile::encodeName(path);
    TagLib::MPEG::File file(encoded.constData());
    QVERIFY(file.isValid());
    TagLib::ID3v2::Tag *id3 = file.ID3v2Tag(true);
    QVERIFY(id3);
    auto *frame = new TagLib::ID3v2::UnsynchronizedLyricsFrame(TagLib::String::UTF8);
    frame->setDescription(TagLib::String("Translation"));
    frame->setText(TagLib::String(text.toUtf8().constData(), TagLib::String::UTF8));
    id3->addFrame(frame);
    QVERIFY(file.save(TagLib::MPEG::File::ID3v2, TagLib::File::StripOthers));

    const std::optional<QString> read = readLyricsText(path);
    QVERIFY(read.has_value());
    QCOMPARE(*read, text);
}

void LocalTagsTest::mp3SyltIsIgnored()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = copyFixture(dir, QStringLiteral("audio.mp3"), QStringLiteral("song.mp3"));

    // Synchronised lyrics carry their own clock and are deliberately not
    // consumed: readLyricsText() has to leave the file lyric-less.
    const QByteArray encoded = QFile::encodeName(path);
    TagLib::MPEG::File file(encoded.constData());
    QVERIFY(file.isValid());
    TagLib::ID3v2::Tag *id3 = file.ID3v2Tag(true);
    QVERIFY(id3);
    auto *frame = new TagLib::ID3v2::SynchronizedLyricsFrame(TagLib::String::UTF8);
    frame->setTimestampFormat(TagLib::ID3v2::SynchronizedLyricsFrame::AbsoluteMilliseconds);
    TagLib::ID3v2::SynchronizedLyricsFrame::SynchedTextList synched;
    synched.append(TagLib::ID3v2::SynchronizedLyricsFrame::SynchedText(1000, TagLib::String("timed", TagLib::String::UTF8)));
    frame->setSynchedText(synched);
    id3->addFrame(frame);
    QVERIFY(file.save(TagLib::MPEG::File::ID3v2, TagLib::File::StripOthers));

    QVERIFY(!readLyricsText(path).has_value());
}

void LocalTagsTest::mp4Lyrics()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = copyFixture(dir, QStringLiteral("audio.m4a"), QStringLiteral("song.m4a"));

    const QString text = QStringLiteral("[00:01.00]written one\n[00:02.00]written two\n");
    const QByteArray encoded = QFile::encodeName(path);
    TagLib::MP4::File file(encoded.constData());
    QVERIFY(file.isValid());
    TagLib::PropertyMap properties;
    properties.replace("LYRICS", TagLib::StringList(TagLib::String(text.toUtf8().constData(), TagLib::String::UTF8)));
    file.setProperties(properties);
    QVERIFY(file.save());

    const std::optional<QString> read = readLyricsText(path);
    QVERIFY(read.has_value());
    QCOMPARE(*read, text);
}

void LocalTagsTest::fileWithoutLyrics()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    // audio.mp3 has no lyric frame of any kind.
    const QString path = copyFixture(dir, QStringLiteral("audio.mp3"), QStringLiteral("song.mp3"));
    QVERIFY(QFile::exists(path));
    QVERIFY(!readLyricsText(path).has_value());
}

void LocalTagsTest::missingFile()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(!readLyricsText(dir.filePath(QStringLiteral("nope.flac"))).has_value());
}

QTEST_GUILESS_MAIN(LocalTagsTest)

#include "localtags_test.moc"
