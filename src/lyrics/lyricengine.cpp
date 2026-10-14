// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 KShadowLrc Contributors

#include "lyrics/lyricengine.h"

#include "kshadowlrc.h"
#include "lyrics/localsource.h"
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

void LyricEngine::setLocalSource(LocalSource *source)
{
    m_localSource = source;
}

void LyricEngine::setSourceOrder(const QStringList &order)
{
    m_sourceOrder = order;
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
    m_offset = 0;
    setLastError(QString());

    m_origin.clear();
    m_translation.clear();
    Q_EMIT lyricChanged();
    Q_EMIT lyricsChanged(m_origin, m_translation);

    if (!track.isValid()) {
        setState(State::NotFound);
        updateCurrent(true);
        return;
    }

    m_steps = resolveSteps();
    m_stepIndex = 0;
    m_currentProviderIndex = -1;
    m_candidateIndex = 0;
    m_searchesStarted = false;
    m_queryPending = false;
    m_walkDone = false;
    m_candidatesByProvider.clear();
    m_searchesAnswered.clear();
    m_localFallbackOrigin.clear();
    m_localFallbackTranslation.clear();
    m_localFallbackSet = false;

    advanceWalk();
}

QStringList LyricEngine::effectiveOrder() const
{
    static const QStringList defaults = {QStringLiteral("netease"), QStringLiteral("lrclib"), QStringLiteral("local")};
    const QStringList requested = m_sourceOrder.isEmpty() ? defaults : m_sourceOrder;

    const auto isKnown = [this](const QString &id) {
        if (id == QLatin1String("local")) {
            return true;
        }
        for (const LyricProvider *provider : m_providers) {
            if (provider && provider->name() == id) {
                return true;
            }
        }
        return false;
    };

    QStringList order;
    const auto appendKnown = [&order, &isKnown](const QStringList &ids) {
        for (const QString &id : ids) {
            if (!id.isEmpty() && !order.contains(id) && isKnown(id)) {
                order.append(id);
            }
        }
    };
    appendKnown(requested);
    appendKnown(defaults);
    return order;
}

QList<LyricEngine::Step> LyricEngine::resolveSteps() const
{
    QList<Step> steps;
    for (const QString &id : effectiveOrder()) {
        if (id == QLatin1String("local")) {
            steps.append({Step::Local, -1});
            continue;
        }
        for (int i = 0; i < m_providers.size(); ++i) {
            if (m_providers.at(i) && m_providers.at(i)->name() == id) {
                steps.append({Step::Provider, i});
                break;
            }
        }
    }
    return steps;
}

void LyricEngine::advanceWalk()
{
    // Provider answers can arrive synchronously (tests, cached providers), which
    // re-enters here while the loop is running. The running loop picks the new
    // state up after the emitting call returns.
    if (m_walking) {
        return;
    }
    m_walking = true;

    for (;;) {
        if (m_walkDone) {
            break;
        }
        if (m_stepIndex >= m_steps.size()) {
            finishWalk();
            break;
        }

        const Step step = m_steps.at(m_stepIndex);
        if (step.kind == Step::Local) {
            if (tryLocalStep()) {
                break;
            }
            ++m_stepIndex;
            continue;
        }

        const int providerIndex = step.providerIndex;
        if (!m_searchesStarted) {
            // The cache sits immediately before the first provider step, after
            // any local step that preceded it: a cached lyric came from a
            // provider, so it must not outrank a user's own local file.
            if (tryCacheStep()) {
                break;
            }
            startProviderSearches();
        }

        // A provider step always searches, so it is answered eventually; until
        // then, nothing later can be decided.
        if (!m_searchesAnswered.contains(providerIndex)) {
            break;
        }

        if (m_currentProviderIndex != providerIndex) {
            m_currentProviderIndex = providerIndex;
            m_candidateIndex = 0;
        }

        // A candidate for this provider is already being fetched: an unrelated
        // answer (another provider's search) must not re-issue the same query.
        if (m_queryPending) {
            break;
        }

        if (tryNextCandidate(providerIndex)) {
            if (m_walkDone) {
                break; // an answer arrived synchronously and found the lyric
            }
            if (m_queryPending) {
                break; // the answer is on its way
            }
            continue; // a synchronous empty answer: try the next candidate
        }

        ++m_stepIndex;
    }

    m_walking = false;
}

void LyricEngine::finishWalk()
{
    m_walkDone = true;
    m_queryPending = false;

    // Content wins over both NotFound and Error, exactly as a provider's plain
    // lyric does: applyFoundLyric() clears any failure recorded on the way here.
    if (m_localFallbackSet) {
        qCDebug(lcLyrics) << "using deferred local lyric for" << m_track.title;
        applyFoundLyric(m_localFallbackOrigin, m_localFallbackTranslation);
        return;
    }

    if (m_lastError.isEmpty()) {
        setState(State::NotFound);
    } else {
        setState(State::Error);
    }
    updateCurrent(true);
}

bool LyricEngine::tryLocalStep()
{
    if (!m_localSource) {
        return false;
    }
    const std::optional<QString> localPath = localPathFromSongId(m_track.uniqueSongId);
    if (!localPath) {
        return false;
    }

    const std::optional<LyricStore> store = m_localSource->load(*localPath);
    if (!store || !store->hasAny()) {
        return false;
    }

    const Lyric origin = parseLrc(store->lyric);
    if (origin.isEmpty() || isPlaceholderLyric(origin)) {
        return false;
    }

    const Lyric translation = store->tlyric.isEmpty() ? Lyric{} : pairTranslation(origin, parseLrc(store->tlyric));

    // A local lyric that cannot be synced to playback must not stop the walk: it
    // would shadow a timed lyric from a provider. Keep it as a fallback for the
    // case where nothing else yields content, though.
    if (origin.kind != LyricKind::Timed) {
        qCDebug(lcLyrics) << "deferring untimed local lyric for" << m_track.title;
        m_localFallbackOrigin = origin;
        m_localFallbackTranslation = translation;
        m_localFallbackSet = true;
        return false;
    }

    // Deliberately not cached: editing the .lrc must take effect on the next play.
    applyFoundLyric(origin, translation);
    return true;
}

bool LyricEngine::tryCacheStep()
{
    const std::optional<CacheEntry> cached = LyricCache::lookup(m_track);
    if (!cached) {
        return false;
    }
    const Lyric origin = parseLrc(cached->store.lyric);
    const Lyric translation = pairTranslation(origin, parseLrc(cached->store.tlyric));
    if (origin.isEmpty() || isPlaceholderLyric(origin)) {
        return false;
    }

    m_offset = cached->offsetMs;
    applyFoundLyric(origin, translation);
    qCDebug(lcLyrics) << "cache hit for" << m_track.title;
    return true;
}

void LyricEngine::startProviderSearches()
{
    m_searchesStarted = true;
    setState(State::Loading);

    for (int i = 0; i < m_providers.size(); ++i) {
        LyricProvider *provider = m_providers.at(i);
        if (!provider) {
            continue;
        }
        // Only providers that appear in the order are searched at all.
        const bool listed = std::any_of(m_steps.cbegin(), m_steps.cend(), [i](const Step &step) {
            return step.kind == Step::Provider && step.providerIndex == i;
        });
        if (!listed) {
            continue;
        }

        const quint64 requestId = m_nextRequestId++;
        m_pendingSearches.insert(requestId, {m_generation, i});
        provider->searchSong(requestId, m_track, kSearchLimit);
    }
}

bool LyricEngine::tryNextCandidate(int providerIndex)
{
    if (providerIndex < 0 || providerIndex >= m_providers.size()) {
        return false;
    }
    LyricProvider *provider = m_providers.at(providerIndex);
    if (!provider) {
        return false;
    }

    const QList<RankedSong> candidates = m_candidatesByProvider.value(providerIndex);
    if (m_candidateIndex >= candidates.size()) {
        return false;
    }

    const RankedSong candidate = candidates.at(m_candidateIndex);
    const quint64 requestId = m_nextRequestId++;
    m_pendingLyrics.insert(requestId, {m_generation, candidate.song.id, providerIndex});
    m_queryPending = true;
    provider->queryLyric(requestId, candidate.song.id);
    return true;
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
        m_candidatesByProvider.insert(providerIndex, rankCandidates(m_track, results, providerIndex));
    }
    m_searchesAnswered.insert(providerIndex);

    advanceWalk();
}

void LyricEngine::onSearchFailed(quint64 requestId, const QString &error)
{
    const auto it = m_pendingSearches.find(requestId);
    if (it == m_pendingSearches.end() || it->generation != m_generation) {
        return;
    }
    const int providerIndex = it->providerIndex;
    m_pendingSearches.erase(it);
    qCDebug(lcLyrics) << "provider search failed:" << error;
    m_searchesAnswered.insert(providerIndex);
    if (!error.isEmpty()) {
        recordFailure(error);
    }

    advanceWalk();
}

void LyricEngine::onLyricFinished(quint64 requestId, const QString &songId, const kshadowlrc::LyricStore &store)
{
    Q_UNUSED(songId)
    const auto it = m_pendingLyrics.find(requestId);
    if (it == m_pendingLyrics.end() || it->generation != m_generation) {
        return;
    }
    m_pendingLyrics.erase(it);
    m_queryPending = false;
    ++m_candidateIndex;

    if (store.hasAny()) {
        const Lyric origin = parseLrc(store.lyric);
        if (!origin.isEmpty() && !isPlaceholderLyric(origin)) {
            const Lyric translation = store.tlyric.isEmpty() ? Lyric{} : pairTranslation(origin, parseLrc(store.tlyric));
            // A fresh provider fetch is cached with offset 0 (the user's own offset is theirs).
            LyricCache::store(m_track, store, 0);
            applyFoundLyric(origin, translation);
            advanceWalk();
            return;
        }
    }

    advanceWalk();
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
    m_queryPending = false;
    ++m_candidateIndex;
    if (!error.isEmpty()) {
        recordFailure(error);
    }

    advanceWalk();
}

void LyricEngine::applyFoundLyric(const Lyric &origin, const Lyric &translation)
{
    m_origin = origin;
    m_translation = translation;
    m_walkDone = true;
    m_queryPending = false;
    setLastError(QString()); // a resolution that succeeds is not a failure
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
    m_walkDone = true;
    m_queryPending = false;
    m_localFallbackOrigin.clear();
    m_localFallbackTranslation.clear();
    m_localFallbackSet = false;
    setLastError(QString());
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
    m_steps.clear();
    m_stepIndex = 0;
    m_currentProviderIndex = -1;
    m_candidateIndex = 0;
    m_searchesStarted = false;
    m_queryPending = false;
    m_walkDone = true;
    m_candidatesByProvider.clear();
    m_searchesAnswered.clear();
    m_localFallbackOrigin.clear();
    m_localFallbackTranslation.clear();
    m_localFallbackSet = false;
    m_offset = 0;
    setLastError(QString());
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

void LyricEngine::recordFailure(const QString &error)
{
    if (m_lastError.isEmpty()) {
        qCWarning(lcLyrics) << "lyric resolution failure:" << error;
        setLastError(error);
    }
}

void LyricEngine::setLastError(const QString &error)
{
    if (m_lastError == error) {
        return;
    }
    m_lastError = error;
    Q_EMIT lastErrorChanged(m_lastError);
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
