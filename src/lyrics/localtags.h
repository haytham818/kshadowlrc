// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 KShadowLrc Contributors

#pragma once

#include <QString>

#include <optional>

namespace kshadowlrc
{

/*!
 * The lyrics text carried by \p path's own metadata tag, if any.
 *
 * This is the only place in the tree that speaks TagLib; callers see a plain
 * QString and never a TagLib type. Reads the Vorbis comment \c LYRICS field
 * (FLAC/Ogg) and its \c UNSYNCEDLYRICS spelling (APE/Monkey's Audio and some
 * taggers), an ID3v2 USLT frame (via the \c LYRICS / \c LYRICS:<description>
 * property keys and by iterating the frames directly, since a description that
 * is not a valid property key would otherwise be dropped), and MP4 \c ©lyr.
 *
 * Synchronised lyrics (ID3v2 SYLT) are deliberately ignored: they carry their
 * own clock and this project re-syncs every lyric against playback anyway.
 *
 * \p path is a filesystem path, not a URL. A UTF-8 BOM is stripped from the
 * returned text. Returns nullopt when the file has no lyric, carries only
 * whitespace, or cannot be read.
 */
std::optional<QString> readLyricsText(const QString &path);

} // namespace kshadowlrc
