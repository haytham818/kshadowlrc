// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 KShadowLrc Contributors
#pragma once

#include "lyrictypes.h"

#include <QString>

#include <optional>

namespace kshadowlrc
{

/*!
 * Pinned lyrics: the lyric the user explicitly picked for one song, kept
 * permanently and outranking both the cache and every source.
 *
 * A pin stores the lyric text itself, not a provider reference, so it keeps
 * working offline and survives a provider renaming or removing a song. The store
 * is one hand-editable JSON object at
 * \c QStandardPaths::AppDataLocation/pins.json, deliberately not under the cache:
 * the cache is disposable content, a pin is the user's decision, so clearing the
 * cache must not clear it.
 */
namespace pinstore
{

/*!
 * The key identifying \p track: the player-supplied \c uniqueSongId when it is
 * present (a local file is therefore pinned by its path), otherwise the
 * LyricCache metadata digest. Empty when neither is available, i.e. the track
 * cannot be pinned.
 */
QString keyFor(const TrackMeta &track);

/*! Absolute path of the pin file (its directory is not created here). */
QString storePath();

/*! One pinned lyric, plus the human-facing provenance the UI shows. */
struct Pin {
    LyricStore store; //!< the lyric text itself: a pin must work offline
    QString source; //!< human text, e.g. "NetEase — 光の中へ / ヨルシカ"
    QString provider; //!< provider name(), e.g. "netease"
    QString songId; //!< the provider song id the lyric was fetched from
    QString title;
    QString singer;
};

/*!
 * The pin saved for \p track, or \c std::nullopt when there is none. The file is
 * re-read on every call (the user may have edited it by hand); a missing or
 * corrupt file yields \c std::nullopt with a warning, never an exception.
 */
std::optional<Pin> lookup(const TrackMeta &track);

/*!
 * Save \p pin for \p track, replacing any pin already there. Returns false when
 * the track has no key to save under, or the file cannot be written.
 */
bool save(const TrackMeta &track, const Pin &pin);

/*!
 * Remove \p track's pin. Returns false when the track has no key, nothing was
 * pinned for it, or the file cannot be written.
 */
bool forget(const TrackMeta &track);

} // namespace pinstore

} // namespace kshadowlrc
