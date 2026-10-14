// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QList>
#include <QString>
#include <QStringList>
#include <QtGlobal>

namespace kshadowlrc
{

/*!
 * One lyric line, positioned relative to the start of the track.
 *
 * `text` may be empty, which represents an instrumental gap rather than a
 * missing line: the overlay keeps the previous line visible in that case.
 */
struct LyricLine {
    QString text;
    qint64 startMs = 0;
};

enum class LyricKind {
    None, //!< no lyric available at all
    NoTimestamp, //!< plain text without timing information
    Timed, //!< at least one timestamped line
};

/*!
 * A parsed lyric.
 *
 * `lines` is sorted by `startMs` using a *stable* sort. Equal timestamps must
 * keep their input order, otherwise a translation that shares a timestamp with
 * its original ends up before it and the pairing breaks.
 */
struct Lyric {
    LyricKind kind = LyricKind::None;
    QList<LyricLine> lines;

    bool isEmpty() const
    {
        return lines.isEmpty();
    }
    void clear()
    {
        kind = LyricKind::None;
        lines.clear();
    }
};

/*! A candidate song as returned by a provider's search. */
struct SongInfo {
    QString id; //!< provider-specific id, passed back to queryLyric()
    QString title;
    QString singer; //!< may hold several artists, joined by the provider
    QString album;
    qint64 lengthMs = 0;
};

/*!
 * The raw lyric pair exactly as a provider returns it, before parsing.
 *
 * Every supported provider returns LRC text, so parsing stays
 * provider-agnostic and lives outside the providers.
 */
struct LyricStore {
    QString lyric; //!< original LRC text; empty when the provider has none
    QString tlyric; //!< translated LRC text; empty when there is no translation

    bool hasAny() const
    {
        return !lyric.isEmpty() || !tlyric.isEmpty();
    }
};

/*!
 * What is currently playing.
 *
 * `uniqueSongId` is the identity used for the lyric cache and for recognising
 * "same song, position moved on" — the `file://` URL for local files,
 * otherwise the provider song id. It stays empty when the player exposes
 * nothing better than title/artist, in which case caching falls back to the
 * metadata itself.
 */
struct TrackMeta {
    QString uniqueSongId;
    QString title;
    QString album;
    QStringList artists;
    qint64 lengthMs = 0;

    bool isValid() const
    {
        return !title.isEmpty() || !uniqueSongId.isEmpty();
    }

    /*! True when both describe the same song, ignoring playback position. */
    bool sameSong(const TrackMeta &other) const
    {
        return uniqueSongId == other.uniqueSongId && title == other.title && album == other.album && artists == other.artists;
    }
};

} // namespace kshadowlrc
