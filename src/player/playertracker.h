// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "lyrictypes.h"

#include <QDateTime>
#include <QObject>
#include <QtGlobal>

namespace kshadowlrc
{

enum class PlaybackStatus {
    Unknown,
    Playing,
    Paused,
    Stopped,
};

/*!
 * A track position together with the instant it was observed.
 *
 * The lyric clock is derived from this rather than from a position polled every
 * frame: MPRIS position updates are coarse and expensive, so the overlay
 * interpolates from the anchor and re-anchors on every fresh report.
 *
 * This is a raw snapshot and says nothing about whether the playhead was moving.
 * Interpolating from it is only correct while the player is advancing, so callers
 * that want a position to sync lyrics to should ask PlayerTracker::elapsedMs()
 * instead of reading this directly.
 */
struct PositionAnchor {
    qint64 positionMs = 0; //!< track position as reported by the player
    QDateTime at; //!< when it was reported; invalid until first seen

    bool isValid() const
    {
        return at.isValid();
    }

    /*!
     * Estimated track position right now. When `lengthMs` is greater than zero
     * the estimate is clamped to the track length.
     */
    qint64 elapsedMs(qint64 lengthMs = 0) const;
};

/*!
 * Follows the active MPRIS player: which player, what is playing, and where the
 * playhead is. It owns the whole D-Bus side and is the only thing the lyric
 * engine needs to know about the desktop's media players.
 *
 * Construct it, connect the signals, then call start(). All signals are emitted
 * on the thread the object lives on.
 */
class PlayerTracker : public QObject
{
    Q_OBJECT

public:
    explicit PlayerTracker(QObject *parent = nullptr);
    ~PlayerTracker() override;

    void start();
    void stop();

    bool hasPlayer() const;
    /*! MPRIS bus name owner, for logging and for the UI's player label. */
    QString playerName() const;
    PlaybackStatus status() const;
    TrackMeta track() const;
    PositionAnchor anchor() const;

    /*!
     * The track position a lyric clock should use right now: advancing while
     * playing, and **exactly constant** while paused or stopped. Zero when there
     * is no player.
     *
     * This exists because PositionAnchor alone cannot express "paused": its
     * elapsedMs() would creep forward between the polls that re-anchor it and
     * snap back afterwards, which makes the current lyric line advance a beat and
     * then jump back. Anything syncing lyrics asks for this instead.
     */
    qint64 elapsedMs() const;

    /*!
     * User-visible lyric timing offset in milliseconds; positive shows lyrics
     * earlier than the player reports them. Persisting it is the settings
     * layer's job, not this class's.
     */
    qint64 lyricOffsetMs() const;
    void setLyricOffsetMs(qint64 ms);

    /*! How often the track and position are refreshed, in milliseconds. */
    int syncIntervalMs() const;
    void setSyncIntervalMs(int ms);

    /*! Re-scan for players and reconnect to the best candidate right now. */
    void reconnect();

Q_SIGNALS:
    void playerConnected(const QString &playerName);
    void playerDisconnected();
    void trackChanged(const kshadowlrc::TrackMeta &track);
    void statusChanged(kshadowlrc::PlaybackStatus status);
    /*!
     * The playhead moved discontinuously (seek, resume, track change) and the
     * lyric clock must be re-derived; anchor() is already up to date when this
     * arrives.
     */
    void positionJumped();
    void lyricOffsetChanged(qint64 ms);
};

} // namespace kshadowlrc
