// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 KShadowLrc Contributors
//
// The one translation unit that includes TagLib. Keeping it here stops TagLib
// types from leaking into headers the rest of the tree includes.

#include "lyrics/localtags.h"

#include "kshadowlrc.h"

#include <QFile>
#include <QLoggingCategory>
#include <QString>
#include <QStringList>

#include <taglib/fileref.h>
#include <taglib/flacfile.h>
#include <taglib/id3v2tag.h>
#include <taglib/mpegfile.h>
#include <taglib/tpropertymap.h>
#include <taglib/tstring.h>
#include <taglib/unsynchronizedlyricsframe.h>

namespace kshadowlrc
{

namespace
{

QString toQString(const TagLib::String &value)
{
    // TagLib decodes every container's text into UTF-16; to8Bit(true) renders
    // it as UTF-8 so the bytes survive the trip into a QString.
    return QString::fromUtf8(value.to8Bit(true));
}

/*! True for the property keys TagLib uses for lyrics, in any container. */
bool isLyricsProperty(const QString &key)
{
    const QString upper = key.toUpper();
    if (upper == QLatin1String("LYRICS") || upper.startsWith(QLatin1String("LYRICS:"))) {
        return true;
    }
    // Monkey's Audio / APE, and taggers that follow the Ogg spelling for the
    // same thing, expose unreconciled lyrics as UNSYNCEDLYRICS.
    return upper == QLatin1String("UNSYNCEDLYRICS");
}

void collectPropertyLyrics(const TagLib::PropertyMap &properties, QStringList &out)
{
    for (const auto &entry : properties) {
        if (!isLyricsProperty(toQString(entry.first))) {
            continue;
        }
        for (const TagLib::String &value : entry.second) {
            out.append(toQString(value));
        }
    }
}

/*!
 * The ID3v2 tag of \p file when the container can carry one directly, so USLT
 * frames whose description is not a valid property key can still be read.
 */
const TagLib::ID3v2::Tag *id3v2TagOf(TagLib::File *file)
{
    if (auto *mpeg = dynamic_cast<TagLib::MPEG::File *>(file)) {
        return mpeg->ID3v2Tag(false);
    }
    if (auto *flac = dynamic_cast<TagLib::FLAC::File *>(file)) {
        return flac->ID3v2Tag(false);
    }
    return nullptr;
}

/*! Append every USLT frame's text; report whether a SYLT frame was seen too. */
void collectId3v2Lyrics(const TagLib::ID3v2::Tag *tag, QStringList &out, bool &sawSylt)
{
    for (TagLib::ID3v2::Frame *frame : tag->frameList("USLT")) {
        if (auto *lyrics = dynamic_cast<TagLib::ID3v2::UnsynchronizedLyricsFrame *>(frame)) {
            out.append(toQString(lyrics->text()));
        }
    }
    sawSylt = sawSylt || !tag->frameList("SYLT").isEmpty();
}

} // namespace

std::optional<QString> readLyricsText(const QString &path)
{
    const QByteArray encoded = QFile::encodeName(path);
    // readAudioProperties = false: only the tag is of interest here.
    TagLib::FileRef file(encoded.constData(), false);
    if (file.isNull() || !file.file()) {
        qCDebug(lcLyrics) << "tag read: TagLib could not open" << path;
        return std::nullopt;
    }

    QStringList candidates;
    bool sawSylt = false;

    collectPropertyLyrics(file.properties(), candidates);
    if (const TagLib::ID3v2::Tag *id3 = id3v2TagOf(file.file())) {
        collectId3v2Lyrics(id3, candidates, sawSylt);
    }

    if (sawSylt) {
        qCDebug(lcLyrics) << "tag read: ignoring synchronised lyrics (SYLT) in" << path;
    }

    for (QString text : candidates) {
        if (text.startsWith(QChar(0xFEFF))) {
            text.remove(0, 1);
        }
        if (!text.trimmed().isEmpty()) {
            return text;
        }
    }
    return std::nullopt;
}

} // namespace kshadowlrc
