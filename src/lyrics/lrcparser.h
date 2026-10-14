// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 KShadowLrc Contributors
#pragma once

#include "lyrictypes.h"

#include <QString>

namespace kshadowlrc
{

/*!
 * Parse LRC text into a \c Lyric.
 *
 * Supported timestamp shapes: \c [mm:ss.xxx], \c [mm:ss.xx], \c [mm:ss] and the
 * lenient \c [mm:ss:xx] (the third group is read as a fraction, as the Rust
 * reference does). One source line may carry several timestamps, which each
 * become their own \c LyricLine sharing the line's text.
 *
 * Lines whose leading bracket content is not a timestamp (\c [ar:...], \c
 * [ti:...], \c [offset:...], \c [SAYONARA], ...) are dropped. Inline extended
 * timestamps (\c <mm:ss.ms>) are stripped from the text but the line survives.
 * Empty-text lines are kept: they are instrumental gaps.
 *
 * The returned lines are sorted by \c startMs with a *stable* sort so equal
 * timestamps keep their input order (needed for translation pairing).
 *
 * \c LyricKind::Timed when at least one timestamped line exists,
 * \c LyricKind::NoTimestamp for a non-empty body with no timestamp at all, and
 * \c LyricKind::None for empty/whitespace input.
 */
Lyric parseLrc(const QString &text);

/*!
 * Remove every extended timestamp tag (\c <mm:ss.ms>) from \p text, leaving all
 * other content untouched. A tag matches when it is \c < + 1-2 digits + ':' +
 * 1-2 digits + '.' + 1-3 digits + '>' (7 to 11 characters total); anything else
 * that merely looks like \c <...> is preserved.
 */
QString stripExtendedTimestamps(const QString &text);

/*!
 * Index of the last line whose \c startMs is <= \p elapsedMs, or -1 before the
 * first line. \p lyric is assumed sorted (parseLrc guarantees it).
 */
int findNextLyric(const Lyric &lyric, qint64 elapsedMs);

/*!
 * Build a translation \c Lyric aligned to \p origin by index: result line \c i
 * carries the translation that shares its timestamp with \c origin line \c i,
 * or empty text when there is none. Empty translation lines are ignored (a
 * non-empty text is required to pair).
 *
 * Returns an empty (\c LyricKind::None) lyric when \p origin is not timed or no
 * translation line matched.
 */
Lyric pairTranslation(const Lyric &origin, const Lyric &translation);

} // namespace kshadowlrc
