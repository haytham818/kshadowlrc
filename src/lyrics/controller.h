// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 KShadowLrc Contributors

#pragma once

#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>

#include "lyrics/localsource.h"
#include "lyrics/lyricengine.h"
#include "lyrics/pinstore.h"
#include "lyrics/provider.h"

#include <optional>

namespace kshadowlrc
{

class KShadowLrcSettings;
class LRCLibProvider;
class LyricSearch;
class NeteaseProvider;
class PlayerTracker;

/*! Forward-declared so the header need not pull in the player module. */
enum class PlaybackStatus;

/*!
 * QML bridge between the lyric engine and the overlay.
 *
 * It owns both lyric providers (LRCLib and NetEase) and a fast clock that
 * pushes PlayerTracker::elapsedMs() into the engine. The order they are tried
 * in is the settings' sourceOrder, pushed into the engine; the order they are
 * handed over in does not matter.
 *
 * Both a PlayerTracker and a KShadowLrcSettings are optional: without them the
 * controller still drives the built-in demo (\ref setTestMode). Injected, the
 * settings govern the display mode, the enabled providers, the source order,
 * the number of upcoming lines and the lyric offset; the tracker supplies the
 * playhead.
 *
 * QML binds to \c currentText / \c translationText (the current line and its
 * translation) and \c upcomingLines (the following lines, dimmed).
 */
class LyricsController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString currentText READ currentText NOTIFY currentTextChanged)
    Q_PROPERTY(QString translationText READ translationText NOTIFY translationTextChanged)
    Q_PROPERTY(QStringList upcomingLines READ upcomingLines NOTIFY upcomingLinesChanged)
    Q_PROPERTY(int state READ state NOTIFY stateChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)
    Q_PROPERTY(QString lyricStatus READ lyricStatus NOTIFY lyricStatusChanged)
    Q_PROPERTY(int displayMode READ displayMode WRITE setDisplayMode NOTIFY displayModeChanged)
    Q_PROPERTY(qint64 lyricOffset READ lyricOffset WRITE setLyricOffset NOTIFY lyricOffsetChanged)
    Q_PROPERTY(QString trackQuery READ trackQuery NOTIFY trackQueryChanged)
    Q_PROPERTY(QString pinnedDescription READ pinnedDescription NOTIFY pinnedDescriptionChanged)
    Q_PROPERTY(QString pinStatus READ pinStatus NOTIFY pinStatusChanged)

public:
    explicit LyricsController(QObject *parent = nullptr);
    ~LyricsController() override;

    QString currentText() const;
    QString translationText() const;
    /*! Upcoming line texts, honouring displayMode and the visibleLines setting. */
    QStringList upcomingLines() const;

    /*! 0 = idle, 1 = loading, 2 = found, 3 = not found, 4 = error. */
    int state() const;
    /*! First transport/protocol failure string of the current resolution, or empty. */
    QString lastError() const;
    /*!
     * The sentence for state() alone: what the lyrics are doing, in words. The
     * settings window shows it and puts lastError() under it as a separate,
     * quieter line. It comes from src/lyrics/lyricstatus.h, the one home for the
     * wording, which the tray tool tip also goes through.
     */
    QString lyricStatus() const;

    int displayMode() const;
    qint64 lyricOffset() const;

    /*!
     * "title artist" of the playing track, or empty when nothing is playing.
     * The search window prefills its field with it: a starting point the user
     * edits, never a query that is run behind their back.
     */
    QString trackQuery() const;

    /*!
     * Where the pinned lyric of the playing track came from, in words, or empty
     * when the track has no pin. A pin outranks the cache and every source.
     */
    QString pinnedDescription() const;

    /*!
     * What the last pin attempt has to say, or empty: that it is fetching, or
     * why the choice could not be pinned. A diagnostic line, so the window
     * shows it the way it shows lastError -- quietly, never as the headline.
     * Success is not announced here; pinnedDescription() carries it.
     */
    QString pinStatus() const;

    /*! The manual search this controller fans out to; QML binds to it directly. */
    LyricSearch *search() const
    {
        return m_search;
    }

    /*! Pin the result at \p index of search()->results() to the playing track. */
    Q_INVOKABLE void pinSearchResult(int index);
    /*! Drop the pin of the playing track; normal resolution takes over again. */
    Q_INVOKABLE void forgetPinnedLyric();

    /*! Engine accessor for the C++ integration lane. */
    LyricEngine *engine()
    {
        return &m_engine;
    }

    /*!
     * Take display mode, enabled providers, upcoming-line count and lyric offset
     * from \p settings, and follow them from then on. Borrowed, never owned;
     * null detaches and restores the standalone defaults.
     */
    void setSettings(KShadowLrcSettings *settings);

    /*!
     * Follow \p tracker: its elapsed time drives the lyric clock, its track
     * changes start a fetch, and its disconnect blanks the overlay. Passing
     * null detaches (the demo path keeps working standalone). The tracker is
     * borrowed, never owned.
     */
    void setPlayerTracker(PlayerTracker *tracker);

    /*! Load the built-in demo lyric and start its local clock. */
    Q_INVOKABLE void setTestMode();
    /*! Stop the demo clock. The demo lyric stops being shown. */
    Q_INVOKABLE void stopDemo();

    /*! Playback position in milliseconds, driven by the player lane. */
    Q_INVOKABLE void setElapsed(qint64 ms);
    /*! User lyric offset in milliseconds; positive shows lines earlier. */
    Q_INVOKABLE void setLyricOffset(qint64 ms);
    /*! 0 = original, 1 = translation, 2 = both. */
    Q_INVOKABLE void setDisplayMode(int mode);

Q_SIGNALS:
    void currentTextChanged();
    void translationTextChanged();
    void upcomingLinesChanged();
    void stateChanged();
    void lastErrorChanged();
    void lyricStatusChanged();
    void displayModeChanged();
    void lyricOffsetChanged();
    void trackQueryChanged();
    void pinnedDescriptionChanged();
    void pinStatusChanged();

private:
    void advanceDemo();
    void onClockTick();

    void onPlayerConnected();
    void onPlayerDisconnected();
    void onTrackChanged(const kshadowlrc::TrackMeta &track);
    void onStatusChanged(kshadowlrc::PlaybackStatus status);
    void onLyricOffsetChanged(qint64 ms);

    /*! Whether the overlay should show engine text at all: demo, or a live
     * player that is not stopped. */
    bool displayActive() const;
    /*! Re-notify QML after the display gate flips without the engine changing. */
    void updateDisplay();
    /*! Re-notify and log the upcoming lines after the current line changes. */
    void updateUpcoming();

    /*! Primary text of \p index's line for the current display mode. */
    QString lineTextForMode(int index) const;
    /*! Clamped visibleLines: how many upcoming lines to expose. */
    int upcomingLineCount() const;

    /*! Hand the enabled providers to the engine and reload the current song. */
    void applyProviders();
    /*! Force a fresh fetch for whatever is loaded (clear() forgets the track). */
    void reloadCurrentTrack();
    /*! Bring tracker, engine and settings offset back into agreement. */
    void applyConfiguredOffset();

    /*!
     * A pin is fetched asynchronously, so the request remembers what the user
     * picked and which song they picked it for: by the time the lyric arrives,
     * the player may well be on another track.
     */
    struct PendingPin {
        TrackMeta track;
        QString source;
        QString provider;
        QString songId;
    };

    void onPinLyricFinished(quint64 requestId, const QString &songId, const kshadowlrc::LyricStore &store);
    void onPinLyricFailed(quint64 requestId, const QString &songId, const QString &error);
    /*! Read the pin of \p track from the store and hand it to the engine. */
    void applyPinFor(const TrackMeta &track);
    /*! Recompose trackQuery and pinnedDescription after a track change. */
    void updatePinState();
    void setPinStatus(const QString &status);
    /*! The provider a search row names, or null when we do not have it. */
    LyricProvider *providerForName(const QString &name) const;

    void startClock();
    void stopClock();

    LyricEngine m_engine;
    // Reads the sidecar or the embedded tag of the playing file. It lives here
    // because the engine only borrows its source, and because a caller that
    // wants a different one (a test) can hand the engine a stub instead.
    LocalFiles m_localFiles;
    NeteaseProvider *m_netease = nullptr;
    LRCLibProvider *m_lrclib = nullptr;
    // The manual search (the "search lyrics" window) is a second consumer of the
    // same providers. It fans out one query and shows what each returns; it
    // never picks for the user.
    LyricSearch *m_search = nullptr;
    PlayerTracker *m_tracker = nullptr;
    KShadowLrcSettings *m_settings = nullptr;

    // Pin state: which pin the current track has (none when it has no pin, or
    // when the stored record carries no text), what the window shows about it,
    // and the fetches in flight.
    QHash<quint64, PendingPin> m_pendingPins;
    quint64 m_nextPinRequestId = 1;
    std::optional<pinstore::Pin> m_currentPin;
    QString m_trackQuery;
    QString m_pinnedDescription;
    QString m_pinStatus;

    QTimer m_demoTimer;
    qint64 m_demoElapsed = 0;
    qint64 m_demoEnd = 0;
    bool m_demoActive = false;

    QTimer m_clockTimer;
    bool m_playerActive = false;
};

} // namespace kshadowlrc
