// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 KShadowLrc Contributors
#pragma once

#include "lyrictypes.h"

#include <QString>

#include <optional>

namespace kshadowlrc
{

/*! A lyric pair restored from the on-disk cache, plus the user's saved offset. */
struct CacheEntry {
    LyricStore store;
    qint64 offsetMs = 0;
};

/*!
 * On-disk lyric cache.
 *
 * Files live under \c QStandardPaths::CacheLocation (app name \c kshadowlrc).
 * The key is the MD5 of the normalised \c title-artists-album-lengthMs string;
 * the file is stored in a three-level directory split of the digest followed by
 * \c <hash>.json, mirroring waylyrics' layout.
 *
 * The payload is \c { "lyric": ..., "tlyric": ..., "offset": ... }, where the
 * lyric fields are the raw LRC text. A fresh fetch is written with offset 0; a
 * read restores whatever offset the user saved.
 */
namespace LyricCache
{

/*! Directory that holds the cache tree (not created here). */
QString cacheDir();

/*! Stable hex digest for \p track; empty when the track has no title. */
QString cacheKey(const TrackMeta &track);

/*! Absolute path of \p track's cache file; empty when unkeyable. */
QString cachePath(const TrackMeta &track);

/*! Read a cached entry, or \c std::nullopt on miss/unreadable/corrupt file. */
std::optional<CacheEntry> lookup(const TrackMeta &track);

/*!
 * Write \p store for \p track, storing \p offsetMs (default 0 for a fresh
 * fetch). An empty store (no lyric text) is not cached. Returns whether a file
 * was written.
 */
bool store(const TrackMeta &track, const LyricStore &store, qint64 offsetMs = 0);

} // namespace LyricCache

} // namespace kshadowlrc
