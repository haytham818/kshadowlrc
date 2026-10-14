// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 KShadowLrc Contributors
//
// Header-only offline stubs shared by the lyric-engine tests: a provider that
// records its calls and can answer synchronously, and a local source returning
// a fixed store. No network, no D-Bus. Because it is a header it is not globbed
// into a test target of its own; every include must stay identical.

#pragma once

#include "lyrics/localsource.h"
#include "lyrics/provider.h"

#include <QList>
#include <QString>

#include <optional>

namespace kshadowlrc
{

/*!
 * A provider that counts searchSong()/queryLyric() calls. With autoDeliver it
 * answers synchronously from searchResults/lyricStore, which is what makes the
 * engine's walk complete within setTrack() and testable without an event loop.
 */
class StubProvider : public LyricProvider
{
public:
    explicit StubProvider(QString name)
        : m_name(std::move(name))
    {
    }

    QString name() const override
    {
        return m_name;
    }
    QString displayName() const override
    {
        return m_name;
    }

    void searchSong(quint64 requestId, const TrackMeta &track, int limit) override
    {
        Q_UNUSED(track)
        Q_UNUSED(limit)
        ++searchCalls;
        m_searchIds.append(requestId);
        if (autoDeliver) {
            Q_EMIT searchFinished(requestId, searchResults);
        }
    }

    void queryLyric(quint64 requestId, const QString &songId) override
    {
        ++queryCalls;
        m_queryIds.append(requestId);
        if (autoDeliver) {
            Q_EMIT lyricFinished(requestId, songId, lyricStore);
        }
    }

    void deliverSearch(quint64 requestId, const QList<SongInfo> &results)
    {
        Q_EMIT searchFinished(requestId, results);
    }
    void deliverLyric(quint64 requestId, const QString &songId, const LyricStore &store)
    {
        Q_EMIT lyricFinished(requestId, songId, store);
    }
    void failSearch(quint64 requestId, const QString &error)
    {
        Q_EMIT searchFailed(requestId, error);
    }
    void failLyric(quint64 requestId, const QString &songId, const QString &error)
    {
        Q_EMIT lyricFailed(requestId, songId, error);
    }

    bool autoDeliver = true;
    QList<SongInfo> searchResults;
    LyricStore lyricStore;
    int searchCalls = 0;
    int queryCalls = 0;
    QList<quint64> m_searchIds;
    QList<quint64> m_queryIds;

private:
    QString m_name;
};

/*! A LocalSource returning a caller-set store, counting load() and its path. */
class StubLocalSource : public LocalSource
{
public:
    std::optional<LyricStore> load(const QString &localPath) const override
    {
        ++loadCalls;
        lastPath = localPath;
        return store;
    }

    mutable int loadCalls = 0;
    mutable QString lastPath;
    std::optional<LyricStore> store;
};

/*! A track with a stable identity and a `file://` URL, so a local step runs. */
inline TrackMeta stubTrack(const QString &title)
{
    TrackMeta track;
    track.title = title;
    track.album = QStringLiteral("Engine Test Album");
    track.artists = {QStringLiteral("Engine Test Artist")};
    track.lengthMs = 180000;
    track.uniqueSongId = QStringLiteral("file:///tmp/kshadowlrc-engine-test.mp3");
    return track;
}

/*! A candidate matching stubTrack() closely, i.e. a rank-0 fuzzy hit. */
inline SongInfo stubCandidate(const QString &id, const QString &title)
{
    SongInfo song;
    song.id = id;
    song.title = title;
    song.singer = QStringLiteral("Engine Test Artist");
    song.album = QStringLiteral("Engine Test Album");
    song.lengthMs = 180000;
    return song;
}

/*! A timed local store; the text is irrelevant to callers that only check kind. */
inline LyricStore timedStore(const QString &line)
{
    return LyricStore{line, QString()};
}

} // namespace kshadowlrc
