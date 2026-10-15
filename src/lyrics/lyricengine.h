// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 KShadowLrc Contributors
#pragma once

#include "lyrictypes.h"
#include "matching.h"
#include "provider.h"

#include <QHash>
#include <QList>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>

namespace kshadowlrc
{

class LocalSource;

/*! Which side of a timed lyric the UI shows prominently. */
enum class LyricDisplayMode {
    Original, //!< original line only
    Translation, //!< translation when present, else the original
    Both, //!< original primary, translation secondary
};

/*!
 * Owns the lyric pipeline: an ordered walk over the enabled sources, the cache,
 * provider search, ranking, parsing and translation pairing. It knows nothing
 * about MPRIS or any concrete provider.
 *
 * Providers are injected with setProviders() and used on the main thread only.
 * The order they are tried in is the logical source order set with
 * setSourceOrder(); provider candidates are ranked by that order and, within
 * one provider, by rank/score as before.
 * \ref setTrack starts a fetch; every async answer is tagged with a request id
 * so a late reply for a previous track is dropped.
 */
class LyricEngine : public QObject
{
    Q_OBJECT

public:
    enum class State {
        Idle, //!< nothing requested yet
        Loading, //!< searching/fetching
        Found, //!< a timed lyric (one that can follow playback) is loaded
        NotFound, //!< searched and found nothing usable
        Error, //!< a transport/protocol failure
    };
    Q_ENUM(State)

    explicit LyricEngine(QObject *parent = nullptr);

    /*! Providers are borrowed, never owned. */
    void setProviders(QList<LyricProvider *> providers);
    QList<LyricProvider *> providers() const
    {
        return m_providers;
    }

    /*!
     * The local file source (sidecar or embedded tag), borrowed and never owned.
     * May be null: with no local source a \c local step is a no-op, so the
     * engine behaves correctly before the application wires one in.
     */
    void setLocalSource(LocalSource *source);
    LocalSource *localSource() const
    {
        return m_localSource;
    }

    /*!
     * Order in which logical lyric sources are tried. A recognised id is
     * \c "local" or the name() of an installed provider (\c "netease",
     * \c "lrclib"). Unknown ids are dropped; ids missing from \p order are
     * appended in the default order (\c netease, \c lrclib, \c local), and an
     * empty list restores that default outright.
     */
    void setSourceOrder(const QStringList &order);
    QStringList sourceOrder() const
    {
        return m_sourceOrder;
    }

    /*! First failure string of the current resolution, empty when there was none. */
    QString lastError() const
    {
        return m_lastError;
    }

    /*!
     * The pinned lyric override: the lyric the user explicitly chose for one
     * song, identified by \p key (see pinstore::keyFor()). When it matches the
     * track passed to setTrack(), it is applied before the cache step and before
     * any search, and no provider is asked. An empty \p key or an empty \p
     * store clears it. It is configuration, not per-track state: it survives
     * clear() until changed here.
     */
    void setPinnedLyric(const QString &key, const LyricStore &store);
    QString pinnedLyricKey() const
    {
        return m_pinnedKey;
    }
    LyricStore pinnedLyricStore() const
    {
        return m_pinnedStore;
    }

    /*!
     * The enabled providers in the order the walk tries them in: the provider
     * subset of the source order, without the cache and without the local
     * source. The manual search lists one group per provider, and it lists them
     * in this order, so what the user sees first is what the walk would try
     * first. This is the only home of the ordering rule; callers do not
     * reimplement it.
     */
    QList<LyricProvider *> providersInSourceOrder() const;

    /*!
     * Point the engine at a new track. A matching pin wins outright; otherwise
     * the walk takes the first lyric that can follow music: a timed local lyric,
     * a cache hit before the first provider, or a provider lyric. A lyric
     * without timestamps cannot follow playback, so it is not a candidate at all
     * and the walk simply keeps looking -- a song whose only lyrics are untimed
     * ends as "no lyrics". The walk stops at the first usable lyric and never
     * consults a later source.
     */
    void setTrack(const TrackMeta &track);
    const TrackMeta &track() const
    {
        return m_track;
    }

    /*! Playback position in milliseconds (without the lyric offset). */
    void setElapsed(qint64 ms);
    qint64 elapsed() const
    {
        return m_elapsed;
    }

    /*! User offset in milliseconds; positive shifts lyrics later. */
    void setLyricOffsetMs(qint64 ms);
    qint64 lyricOffsetMs() const
    {
        return m_offset;
    }

    void setDisplayMode(LyricDisplayMode mode);
    LyricDisplayMode displayMode() const
    {
        return m_mode;
    }

    /*!
     * Inject a parsed lyric pair directly (demo mode, or a player-supplied
     * lyric hint), bypassing cache and providers. A lyric that cannot follow
     * music is treated as no lyric: State::NotFound with nothing to show. An
     * empty \p track leaves the current track untouched.
     */
    void setLyric(const Lyric &origin, const Lyric &translation, const TrackMeta &track = TrackMeta());

    /*!
     * Drop the current lyric and invalidate any in-flight request. The track is
     * forgotten too, so a later setTrack() for that same song loads it again
     * rather than being taken for an already-handled repeat.
     */
    void clear();

    State state() const
    {
        return m_state;
    }
    const Lyric &originLyric() const
    {
        return m_origin;
    }
    const Lyric &translationLyric() const
    {
        return m_translation;
    }

    /*! Index of the current origin line, or -1 before the first line. */
    int currentIndex() const;
    int lineCount() const
    {
        return m_origin.lines.size();
    }

    /*! \p index's origin / translation text, empty when out of range. */
    QString lineText(int index) const;
    QString translationText(int index) const;

    /*! Up to \p count origin line texts starting at \p from. */
    QStringList lineWindow(int from, int count) const;
    /*! Up to \p count translation line texts starting at \p from. */
    QStringList translationWindow(int from, int count) const;

    /*! Primary text for the current position, honouring the display mode. */
    QString currentText() const;
    /*! Secondary (translation) text for the current position. */
    QString currentTranslation() const;

Q_SIGNALS:
    void currentLineChanged(const QString &text, const QString &translation);
    void currentIndexChanged(int index);
    void stateChanged(kshadowlrc::LyricEngine::State state);
    /*! The first failure string of a resolution changed (set, or cleared). */
    void lastErrorChanged(const QString &error);
    /*! The parsed lyrics were replaced (found, cleared or injected). */
    void lyricChanged();
    /*! The parsed lyrics themselves, for a windowed UI to render. */
    void lyricsChanged(const kshadowlrc::Lyric &origin, const kshadowlrc::Lyric &translation);

private:
    /*! One entry of the resolved source order. */
    struct Step {
        enum Kind {
            Local, //!< the local file source
            Provider, //!< m_providers[providerIndex]
        };
        Kind kind = Local;
        int providerIndex = -1;
    };

    void onSearchFinished(quint64 requestId, const QList<kshadowlrc::SongInfo> &results);
    void onSearchFailed(quint64 requestId, const QString &error);
    void onLyricFinished(quint64 requestId, const QString &songId, const kshadowlrc::LyricStore &store);
    void onLyricFailed(quint64 requestId, const QString &songId, const QString &error);

    /*! The configured order with unknown ids dropped and missing ones appended. */
    QStringList effectiveOrder() const;
    /*! The effective order mapped to concrete steps, resolved against m_providers. */
    QList<Step> resolveSteps() const;

    /*! Walk the source order until a winner is found or the walk must wait. */
    void advanceWalk();
    void finishWalk();
    /*! Try the local step; true when it produced a winner. */
    bool tryLocalStep();
    /*! Try the cache, which is only consulted just before the first provider step. */
    bool tryCacheStep();
    /*! Search every listed provider concurrently, once, when the walk reaches a provider. */
    void startProviderSearches();
    /*! Query \p providerIndex's next ranked candidate; false when it has none left. */
    bool tryNextCandidate(int providerIndex);

    void applyFoundLyric(const Lyric &origin, const Lyric &translation);
    void setState(State state);
    /*! Record the first failure of this resolution and log it at warning level. */
    void recordFailure(const QString &error);
    void setLastError(const QString &error);
    void updateCurrent(bool force);

    TrackMeta m_track;
    QList<LyricProvider *> m_providers;
    LocalSource *m_localSource = nullptr;
    QStringList m_sourceOrder;
    QString m_lastError;

    // The pinned lyric override (see setPinnedLyric()). Configuration, not
    // per-track state, so it is not reset by clear() or setTrack().
    QString m_pinnedKey;
    LyricStore m_pinnedStore;
    Lyric m_origin;
    Lyric m_translation;
    State m_state = State::Idle;
    LyricDisplayMode m_mode = LyricDisplayMode::Original;
    qint64 m_elapsed = 0;
    qint64 m_offset = 0;
    bool m_hasProcessedTrack = false;

    QString m_lastText;
    QString m_lastTranslation;
    int m_lastIndex = -2;

    quint64 m_generation = 0;
    quint64 m_nextRequestId = 1;

    struct PendingSearch {
        quint64 generation = 0;
        int providerIndex = -1;
    };
    struct PendingLyric {
        quint64 generation = 0;
        QString songId;
        int providerIndex = -1;
    };
    QHash<quint64, PendingSearch> m_pendingSearches;
    QHash<quint64, PendingLyric> m_pendingLyrics;

    // The walk's state. m_steps is rebuilt per track; the rest is reset with it.
    QList<Step> m_steps;
    int m_stepIndex = 0;
    int m_currentProviderIndex = -1;
    int m_candidateIndex = 0;
    bool m_searchesStarted = false;
    bool m_queryPending = false;
    bool m_walkDone = false;
    bool m_walking = false;
    QHash<int, QList<RankedSong>> m_candidatesByProvider;
    QSet<int> m_searchesAnswered;
};

} // namespace kshadowlrc
