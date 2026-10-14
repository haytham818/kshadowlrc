// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 KShadowLrc Contributors

#include "lyrics/lyricengine.h"

#include "kshadowlrc.h"
#include "lyrics/lrcparser.h"
#include "lyrics/lyriccache.h"

#include <QSet>
#include <QStringList>

#include <algorithm>

namespace kshadowlrc
{

namespace
{

constexpr int kSearchLimit = 20;

/*! Placeholder text a provider returns to say "there is no lyric". */
bool isPlaceholderText(const QString &raw)
{
    const QString normalised = normalizeForMatch(raw);
    if (normalised.isEmpty()) {
        return false;
    }

    static const QSet<QString> exact = {
        QStringLiteral("暂无歌词"),
        QStringLiteral("纯音乐"),
        QStringLiteral("纯音乐请欣赏"),
        QStringLiteral("此歌曲为没有填词的纯音乐"),
        QStringLiteral("该歌曲还没有歌词"),
        QStringLiteral("没有歌词"),
        QStringLiteral("instrumental"),
        QStringLiteral("nolyrics"),
        QStringLiteral("nolrc"),
    };
    if (exact.contains(normalised)) {
        return true;
    }

    // Substring guard for the common NetEase phrasings.
    return normalised.contains(QStringLiteral("暂无歌词")) || normalised.contains(QStringLiteral("没有填词"));
}

/*! True when every non-empty line is a no-lyric sentinel. */
bool isPlaceholderLyric(const Lyric &lyric)
{
    bool sawText = false;
    for (const LyricLine &line : lyric.lines) {
        if (line.text.trimmed().isEmpty()) {
            continue;
        }
        sawText = true;
        if (!isPlaceholderText(line.text)) {
            return false;
        }
    }
    return sawText;
}

} // namespace

LyricEngine::LyricEngine(QObject *parent)
    : QObject(parent)
{
}

void LyricEngine::setProviders(QList<LyricProvider *> providers)
{
    for (LyricProvider *provider : m_providers) {
        if (provider) {
            disconnect(provider, nullptr, this, nullptr);
        }
    }
    m_providers = std::move(providers);
    for (LyricProvider *provider : m_providers) {
        if (!provider) {
            continue;
        }
        connect(provider, &LyricProvider::searchFinished, this, &LyricEngine::onSearchFinished);
        connect(provider, &LyricProvider::searchFailed, this, &LyricEngine::onSearchFailed);
        connect(provider, &LyricProvider::lyricFinished, this, &LyricEngine::onLyricFinished);
        connect(provider, &LyricProvider::lyricFailed, this, &LyricEngine::onLyricFailed);
    }
}

void LyricEngine::setTrack(const TrackMeta &track)
{
    if (m_hasProcessedTrack && track.sameSong(m_track)) {
        return;
    }
    m_hasProcessedTrack = true;

    m_track = track;
    ++m_generation;
    m_pendingSearches.clear();
    m_pendingLyrics.clear();
    m_candidates.clear();
    m_searchesPending = 0;
    m_queryIndex = 0;
    m_offset = 0;

    m_origin.clear();
    m_translation.clear();
    Q_EMIT lyricChanged();
    Q_EMIT lyricsChanged(m_origin, m_translation);

    if (!track.isValid()) {
        setState(State::NotFound);
        updateCurrent(true);
        return;
    }

    if (const std::optional<CacheEntry> cached = LyricCache::lookup(track)) {
        const Lyric origin = parseLrc(cached->store.lyric);
        const Lyric translation = pairTranslation(origin, parseLrc(cached->store.tlyric));
        if (!origin.isEmpty() && !isPlaceholderLyric(origin)) {
            m_offset = cached->offsetMs;
            applyFoundLyric(origin, translation);
            qCDebug(lcLyrics) << "cache hit for" << track.title;
            return;
        }
    }

    if (m_providers.isEmpty()) {
        setState(State::NotFound);
        updateCurrent(true);
        return;
    }

    setState(State::Loading);
    startSearch();
}

void LyricEngine::startSearch()
{
    m_searchesPending = m_providers.size();
    for (int i = 0; i < m_providers.size(); ++i) {
        LyricProvider *provider = m_providers.at(i);
        if (!provider) {
            --m_searchesPending;
            continue;
        }
        const quint64 requestId = m_nextRequestId++;
        m_pendingSearches.insert(requestId, {m_generation, i});
        provider->searchSong(requestId, m_track, kSearchLimit);
    }
    if (m_searchesPending <= 0) {
        finishSearchPhase();
    }
}

void LyricEngine::onSearchFinished(quint64 requestId, const QList<kshadowlrc::SongInfo> &results)
{
    const auto it = m_pendingSearches.find(requestId);
    if (it == m_pendingSearches.end() || it->generation != m_generation) {
        return; // stale answer
    }
    const int providerIndex = it->providerIndex;
    m_pendingSearches.erase(it);

    if (!results.isEmpty() && providerIndex >= 0 && providerIndex < m_providers.size()) {
        m_candidates.append(rankCandidates(m_track, results, providerIndex));
    }

    if (--m_searchesPending <= 0) {
        finishSearchPhase();
    }
}

void LyricEngine::onSearchFailed(quint64 requestId, const QString &error)
{
    const auto it = m_pendingSearches.find(requestId);
    if (it == m_pendingSearches.end() || it->generation != m_generation) {
        return;
    }
    qCDebug(lcLyrics) << "provider search failed:" << error;
    m_pendingSearches.erase(it);
    if (--m_searchesPending <= 0) {
        finishSearchPhase();
    }
}

void LyricEngine::finishSearchPhase()
{
    if (m_candidates.isEmpty()) {
        setState(State::NotFound);
        return;
    }

    std::stable_sort(m_candidates.begin(), m_candidates.end(), [](const RankedSong &a, const RankedSong &b) {
        if (a.match.rank != b.match.rank) {
            return a.match.rank < b.match.rank;
        }
        if (a.match.score != b.match.score) {
            return a.match.score > b.match.score;
        }
        return a.providerIndex < b.providerIndex;
    });

    m_queryIndex = 0;
    queryNextCandidate();
}

void LyricEngine::queryNextCandidate()
{
    if (m_queryIndex >= m_candidates.size()) {
        setState(State::NotFound);
        return;
    }

    const RankedSong candidate = m_candidates.at(m_queryIndex++);
    LyricProvider *provider =
        (candidate.providerIndex >= 0 && candidate.providerIndex < m_providers.size()) ? m_providers.at(candidate.providerIndex) : nullptr;
    if (!provider) {
        queryNextCandidate();
        return;
    }

    const quint64 requestId = m_nextRequestId++;
    m_pendingLyrics.insert(requestId, {m_generation, candidate.song.id});
    provider->queryLyric(requestId, candidate.song.id);
}

void LyricEngine::onLyricFinished(quint64 requestId, const QString &songId, const kshadowlrc::LyricStore &store)
{
    Q_UNUSED(songId)
    const auto it = m_pendingLyrics.find(requestId);
    if (it == m_pendingLyrics.end() || it->generation != m_generation) {
        return;
    }
    m_pendingLyrics.erase(it);

    if (!store.hasAny()) {
        queryNextCandidate();
        return;
    }

    const Lyric origin = parseLrc(store.lyric);
    if (origin.isEmpty() || isPlaceholderLyric(origin)) {
        queryNextCandidate();
        return;
    }

    const Lyric translation = store.tlyric.isEmpty() ? Lyric{} : pairTranslation(origin, parseLrc(store.tlyric));

    // A fresh fetch is cached with offset 0 (the user's own offset is theirs).
    LyricCache::store(m_track, store, 0);

    applyFoundLyric(origin, translation);
}

void LyricEngine::onLyricFailed(quint64 requestId, const QString &songId, const QString &error)
{
    Q_UNUSED(songId)
    const auto it = m_pendingLyrics.find(requestId);
    if (it == m_pendingLyrics.end() || it->generation != m_generation) {
        return;
    }
    qCDebug(lcLyrics) << "provider lyric fetch failed:" << error;
    m_pendingLyrics.erase(it);
    queryNextCandidate();
}

void LyricEngine::applyFoundLyric(const Lyric &origin, const Lyric &translation)
{
    m_origin = origin;
    m_translation = translation;
    setState(State::Found);
    Q_EMIT lyricChanged();
    Q_EMIT lyricsChanged(m_origin, m_translation);
    updateCurrent(true);
}

void LyricEngine::setLyric(const Lyric &origin, const Lyric &translation, const TrackMeta &track)
{
    ++m_generation; // cancel anything in flight
    m_pendingSearches.clear();
    m_pendingLyrics.clear();
    m_candidates.clear();
    m_searchesPending = 0;
    if (track.isValid()) {
        m_track = track;
        m_hasProcessedTrack = true;
    }

    m_origin = origin;
    m_translation = translation;
    setState(origin.isEmpty() && translation.isEmpty() ? State::NotFound : State::Found);
    Q_EMIT lyricChanged();
    Q_EMIT lyricsChanged(m_origin, m_translation);
    updateCurrent(true);
}

void LyricEngine::clear()
{
    ++m_generation;
    m_pendingSearches.clear();
    m_pendingLyrics.clear();
    m_candidates.clear();
    m_searchesPending = 0;
    m_queryIndex = 0;
    m_offset = 0;
    // Forget the track as well as its lyrics. The processed-track guard exists so
    // a repeated setTrack() for the song already loaded does not restart the
    // search; leaving it set here would swallow the next setTrack() for that same
    // song, so "cleared" would really mean "cannot be reloaded".
    m_track = TrackMeta{};
    m_hasProcessedTrack = false;
    m_origin.clear();
    m_translation.clear();
    setState(State::Idle);
    Q_EMIT lyricChanged();
    Q_EMIT lyricsChanged(m_origin, m_translation);
    updateCurrent(true);
}

void LyricEngine::setElapsed(qint64 ms)
{
    if (m_elapsed == ms) {
        return;
    }
    m_elapsed = ms;
    updateCurrent(false);
}

void LyricEngine::setLyricOffsetMs(qint64 ms)
{
    if (m_offset == ms) {
        return;
    }
    m_offset = ms;
    updateCurrent(true);
}

void LyricEngine::setDisplayMode(LyricDisplayMode mode)
{
    if (m_mode == mode) {
        return;
    }
    m_mode = mode;
    updateCurrent(true);
}

int LyricEngine::currentIndex() const
{
    if (m_origin.kind != LyricKind::Timed || m_origin.lines.isEmpty()) {
        return -1;
    }
    return findNextLyric(m_origin, m_elapsed + m_offset);
}

QString LyricEngine::lineText(int index) const
{
    if (index < 0 || index >= m_origin.lines.size()) {
        return {};
    }
    return m_origin.lines.at(index).text;
}

QString LyricEngine::translationText(int index) const
{
    if (index < 0 || index >= m_translation.lines.size()) {
        return {};
    }
    return m_translation.lines.at(index).text;
}

QStringList LyricEngine::lineWindow(int from, int count) const
{
    QStringList window;
    const int end = std::min(static_cast<int>(m_origin.lines.size()), from + count);
    for (int i = std::max(0, from); i < end; ++i) {
        window.append(m_origin.lines.at(i).text);
    }
    return window;
}

QStringList LyricEngine::translationWindow(int from, int count) const
{
    QStringList window;
    const int end = std::min(static_cast<int>(m_translation.lines.size()), from + count);
    for (int i = std::max(0, from); i < end; ++i) {
        window.append(m_translation.lines.at(i).text);
    }
    return window;
}

QString LyricEngine::currentText() const
{
    if (m_origin.kind == LyricKind::NoTimestamp) {
        // No timing information: show the whole plain body.
        QStringList texts;
        for (const LyricLine &line : m_origin.lines) {
            if (!line.text.trimmed().isEmpty()) {
                texts.append(line.text);
            }
        }
        return texts.join(QLatin1Char('\n'));
    }
    if (m_origin.kind != LyricKind::Timed) {
        return {};
    }

    const int idx = currentIndex();
    if (idx < 0) {
        return {};
    }

    // An empty line is an instrumental gap: keep the previous line visible.
    int visible = idx;
    while (visible >= 0 && m_origin.lines.at(visible).text.trimmed().isEmpty()) {
        --visible;
    }
    const QString origin = visible >= 0 ? m_origin.lines.at(visible).text : QString();
    const QString translation = translationText(idx);

    switch (m_mode) {
    case LyricDisplayMode::Original:
        return origin;
    case LyricDisplayMode::Translation:
        return translation.isEmpty() ? origin : translation;
    case LyricDisplayMode::Both:
        return origin.isEmpty() ? translation : origin;
    }
    return origin;
}

QString LyricEngine::currentTranslation() const
{
    if (m_mode != LyricDisplayMode::Both || m_origin.kind != LyricKind::Timed) {
        return {};
    }
    const int idx = currentIndex();
    if (idx < 0) {
        return {};
    }
    return translationText(idx);
}

void LyricEngine::setState(State state)
{
    if (m_state == state) {
        return;
    }
    m_state = state;
    Q_EMIT stateChanged(m_state);
}

void LyricEngine::updateCurrent(bool force)
{
    const int index = currentIndex();
    const QString text = currentText();
    const QString translation = currentTranslation();
    if (!force && index == m_lastIndex && text == m_lastText && translation == m_lastTranslation) {
        return;
    }
    m_lastIndex = index;
    m_lastText = text;
    m_lastTranslation = translation;
    Q_EMIT currentIndexChanged(index);
    Q_EMIT currentLineChanged(text, translation);
}

} // namespace kshadowlrc
