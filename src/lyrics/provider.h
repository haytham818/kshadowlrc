// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "lyrictypes.h"

#include <QObject>

namespace kshadowlrc
{

/*!
 * A source of lyrics (NetEase, LRCLib, ...).
 *
 * Every call is asynchronous and carries a caller-supplied `requestId` that is
 * echoed back in the result signals, so a caller can keep several requests in
 * flight and match answers without depending on emission order. Each call must
 * emit exactly one of its two result signals, never both and never neither.
 *
 * Implementations are used from the main thread only.
 */
class LyricProvider : public QObject
{
    Q_OBJECT

public:
    explicit LyricProvider(QObject *parent = nullptr);
    ~LyricProvider() override;

    /*! Stable identifier used in configuration, e.g. "netease" / "lrclib". */
    virtual QString name() const = 0;

    /*! Human-readable, translated name for the UI. */
    virtual QString displayName() const = 0;

    /*!
     * Ask for candidate songs for `track`. Implementations build the search
     * query themselves from the track metadata; they must not require a title
     * to be present and should degrade gracefully when only some fields exist.
     */
    virtual void searchSong(quint64 requestId, const TrackMeta &track, int limit) = 0;

    /*! Fetch the raw lyric for a song id previously returned by searchSong(). */
    virtual void queryLyric(quint64 requestId, const QString &songId) = 0;

    /*!
     * True when `id` is already a song id this provider understands, so the
     * track can be looked up directly instead of searched for. This backs the
     * "the player already knows the song id" fast path.
     */
    virtual bool isLikelySongId(const QString &id) const;

Q_SIGNALS:
    /*! `results` is in the provider's own order; ranking is the caller's job. */
    void searchFinished(quint64 requestId, const QList<kshadowlrc::SongInfo> &results);
    void searchFailed(quint64 requestId, const QString &error);

    void lyricFinished(quint64 requestId, const QString &songId, const kshadowlrc::LyricStore &store);
    void lyricFailed(quint64 requestId, const QString &songId, const QString &error);
};

} // namespace kshadowlrc
