// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 KShadowLrc Contributors
#pragma once

#include "lyrictypes.h"
#include "matching.h"
#include "provider.h"

#include <QHash>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

namespace kshadowlrc
{

/*! Which side of a timed lyric the UI shows prominently. */
enum class LyricDisplayMode {
    Original, //!< original line only
    Translation, //!< translation when present, else the original
    Both, //!< original primary, translation secondary
};

/*!
 * Owns the lyric pipeline: cache, provider search, ranking, parsing and
 * translation pairing. It knows nothing about MPRIS or any concrete provider.
 *
 * Providers are injected with setProviders() and used on the main thread only.
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
        Found, //!< a timed or plain lyric is loaded
        NotFound, //!< searched and found nothing usable
        Error, //!< a transport/protocol failure
    };
    Q_ENUM(State)

    explicit LyricEngine(QObject *parent = nullptr);

    /*! Providers are borrowed, never owned. Order is not significant. */
    void setProviders(QList<LyricProvider *> providers);
    QList<LyricProvider *> providers() const
    {
        return m_providers;
    }

    /*!
     * Point the engine at a new track. A cache hit resolves immediately;
     * otherwise every provider is searched concurrently, candidates are ranked
     * and the best ones are queried in order until one yields content.
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
     * lyric hint), bypassing cache and providers. An empty \p origin still sets
     * State::NotFound. An empty \p track leaves the current track untouched.
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
    /*! The parsed lyrics were replaced (found, cleared or injected). */
    void lyricChanged();
    /*! The parsed lyrics themselves, for a windowed UI to render. */
    void lyricsChanged(const kshadowlrc::Lyric &origin, const kshadowlrc::Lyric &translation);

private:
    void onSearchFinished(quint64 requestId, const QList<kshadowlrc::SongInfo> &results);
    void onSearchFailed(quint64 requestId, const QString &error);
    void onLyricFinished(quint64 requestId, const QString &songId, const kshadowlrc::LyricStore &store);
    void onLyricFailed(quint64 requestId, const QString &songId, const QString &error);

    void startSearch();
    void finishSearchPhase();
    void queryNextCandidate();
    void applyFoundLyric(const Lyric &origin, const Lyric &translation);
    void setState(State state);
    void updateCurrent(bool force);

    TrackMeta m_track;
    QList<LyricProvider *> m_providers;
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
    };
    QHash<quint64, PendingSearch> m_pendingSearches;
    QHash<quint64, PendingLyric> m_pendingLyrics;

    QList<RankedSong> m_candidates;
    int m_searchesPending = 0;
    int m_queryIndex = 0;
};

} // namespace kshadowlrc
