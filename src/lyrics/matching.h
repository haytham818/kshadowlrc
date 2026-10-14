// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 KShadowLrc Contributors
#pragma once

#include "lyrictypes.h"

#include <QList>
#include <QString>

namespace kshadowlrc
{

/*!
 * Default duration tolerance for the length fast path.
 *
 * waylyrics requires strict length equality against the player's length; that
 * is brittle because providers round or report a slightly different duration.
 * We accept a candidate whose length is within +/- this many milliseconds of
 * the track. 3000 ms is wide enough to absorb the rounding seen on LRCLib
 * (which reports fractional seconds) yet still rejects a different edit or a
 * live version of a few seconds' difference.
 */
inline constexpr qint64 kDurationToleranceMs = 3000;

/*!
 * How well a candidate matches the track.
 *
 * \c rank mirrors waylyrics' weights, ordered best-first:
 *   0 - a good fuzzy hit (weighted bigram similarity is convincing),
 *   1 - a length-tolerance "exact-ish" hit (duration matches, title fuzzy),
 *   2 - a weak/fallback hit.
 * \c score is the weighted bigram similarity in [0, 1].
 */
struct SongMatch {
    int rank = 2;
    double score = 0.0;
};

/*! A candidate with its provider and match quality, ready to be ranked. */
struct RankedSong {
    SongInfo song;
    int providerIndex = -1;
    SongMatch match;
};

/*!
 * Normalise a string for comparison: case-fold, then drop every character that
 * is not a letter or a digit (so whitespace and punctuation vanish).
 *
 * LIMITATION: unlike the Rust reference we do NOT convert Traditional Chinese
 * to Simplified (that needs OpenCC/zhconv, out of scope for v1). A track tagged
 * in Traditional Chinese therefore scores lower against a Simplified candidate
 * than it would in waylyrics.
 */
QString normalizeForMatch(const QString &text);

/*!
 * Bigram-set similarity: \c 2*|A∩B| / (|A|+|B|) over the adjacent-character
 * bigram sets of \p a and \p b. Both empty -> 1.0, otherwise a zero
 * denominator yields 0.0 (or 1.0 when the normalised strings are equal).
 */
double bigramSimilarity(const QString &a, const QString &b);

/*!
 * Weighted similarity of a candidate against the track. When both album and
 * artists are known the weights are title 0.4 / album 0.4 / singer 0.2; with
 * only one of them the remaining weight folds into the title (0.7/0.3 and
 * 0.8/0.2), matching waylyrics' \c fuzzy_match_song.
 */
double weightedSimilarity(const TrackMeta &track, const SongInfo &candidate);

/*! True when both lengths are known and differ by at most \p toleranceMs. */
bool withinDurationTolerance(const TrackMeta &track, const SongInfo &candidate, qint64 toleranceMs = kDurationToleranceMs);

/*!
 * Score one candidate against the track.
 *
 * A convincing fuzzy score (>= 0.6) wins outright (rank 0). Otherwise a
 * duration match inside the tolerance makes it a rank-1 hit, and everything
 * else falls back to rank 2.
 */
SongMatch matchLikelyLyric(const TrackMeta &track, const SongInfo &candidate, qint64 toleranceMs = kDurationToleranceMs);

/*!
 * Score and order every candidate best-first: rank ascending, then score
 * descending. The sort is stable, so equal-quality candidates keep the
 * provider's original order.
 */
QList<RankedSong> rankCandidates(const TrackMeta &track, const QList<SongInfo> &candidates, int providerIndex, qint64 toleranceMs = kDurationToleranceMs);

} // namespace kshadowlrc
