// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 KShadowLrc Contributors

#pragma once

#include "lyrictypes.h"

#include <QString>

#include <optional>

namespace kshadowlrc
{

/*!
 * The file `uniqueSongId` names, when it names a local one.
 *
 * A local track is identified by its `file://` URL (see
 * TrackMeta::uniqueSongId), which is the only thing tying the playing track to
 * a file on disk: the URL is percent-encoded, so it has to be decoded before it
 * can be opened.
 */
std::optional<QString> localPathFromSongId(const QString &uniqueSongId);

/*!
 * The lyric the playing track's own file carries.
 *
 * Deliberately synchronous: the work is a couple of stats plus at most one tag
 * read, and the engine has to have the answer before it can decide whether the
 * network is worth touching at all.
 *
 * Implementations are used from the main thread only.
 */
class LocalSource
{
public:
    virtual ~LocalSource() = default;

    /*!
     * The raw lyric pair found at `localPath`, which comes from
     * localPathFromSongId(). Nullopt when the file carries no lyric at all.
     *
     * Returning text that does not parse into a timed lyric is harmless: the
     * engine parses it like any other source and keeps looking when the result
     * cannot be synced to playback.
     */
    virtual std::optional<LyricStore> load(const QString &localPath) const = 0;
};

/*!
 * The real thing: the sidecar `<stem>.lrc` with `<stem>.zh.lrc` as its
 * translation first, the file's own tag second.
 *
 * The sidecar wins because it is the copy the user edited last, and it is
 * looked for first because that costs a stat while reading a tag does not.
 */
class LocalFiles : public LocalSource
{
public:
    std::optional<LyricStore> load(const QString &localPath) const override;
};

} // namespace kshadowlrc
