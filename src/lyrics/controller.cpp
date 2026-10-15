// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 KShadowLrc Contributors

#include "lyrics/controller.h"

#include "config/settings.h"
#include "kshadowlrc.h"
#include "lyrics/lrcparser.h"
#include "lyrics/lyricsearch.h"
#include "lyrics/lyricstatus.h"
#include "lyrics/providers/lrclib.h"
#include "lyrics/providers/netease.h"
#include "player/playertracker.h"

#include <KLocalizedString>

#include <QLoggingCategory>

#include <algorithm>

Q_LOGGING_CATEGORY(lcLyrics, "kshadowlrc.lyrics")

namespace kshadowlrc
{

namespace
{

constexpr int kDemoIntervalMs = 200;
// The MPRIS poll runs about once a second; the lyric clock has to be far
// faster so a line change lands on time. elapsedMs() is cheap and constant
// while paused, so 40 ms costs little.
constexpr int kClockIntervalMs = 40;
// visibleLines is clamped to this range rather than trusting the file blindly.
constexpr int kMinUpcomingLines = 0;
constexpr int kMaxUpcomingLines = 10;
// Upcoming lines shown by the demo/standalone path, which has no settings.
constexpr int kDefaultUpcomingLines = 1;

// A short built-in lyric so the overlay can be exercised with no player and no
// network. Timestamps line up so the translation pairs by index.
const auto kDemoOrigin = QStringLiteral(
    "[00:00.00] Yesterday, all my troubles seemed so far away\n"
    "[00:05.00] Now it looks as though they're here to stay\n"
    "[00:10.00] Oh, I believe in yesterday\n"
    "[00:15.00] Suddenly, I'm not half the man I used to be\n"
    "[00:20.00] There's a shadow hanging over me\n");

const auto kDemoTranslation = QStringLiteral(
    "[00:00.00] 昨日，一切烦恼都离我远去\n"
    "[00:05.00] 如今它们却似要长驻于此\n"
    "[00:10.00] 哦，我相信昨日\n"
    "[00:15.00] 突然间，我已不再是曾经的我\n"
    "[00:20.00] 一片阴影笼罩着我\n");

constexpr qint64 kDemoEndMs = 25000;

/*!
 * The prefill the search window starts from: what the player said is playing.
 * Empty when nothing is, so the window can say so rather than searching for
 * nothing; the user is expected to edit whatever comes back.
 */
QString queryTextFor(const TrackMeta &track)
{
    if (!track.isValid()) {
        return {};
    }
    QStringList parts;
    const QString title = track.title.trimmed();
    if (!title.isEmpty()) {
        parts.append(title);
    }
    for (const QString &artist : track.artists) {
        const QString trimmed = artist.trimmed();
        if (!trimmed.isEmpty()) {
            parts.append(trimmed);
        }
    }
    return parts.join(QLatin1Char(' '));
}

/*!
 * Whether a stored pin is worth applying at all: it carries text, and that text
 * can follow the music. A record that cannot be shown would otherwise claim the
 * song is pinned and then resolve it the ordinary way.
 */
bool pinIsShowable(const pinstore::Pin &pin)
{
    return pin.store.hasAny() && canFollowMusic(parseLrc(pin.store.lyric));
}

} // namespace

LyricsController::LyricsController(QObject *parent)
    : QObject(parent)
{
    m_netease = new NeteaseProvider(this);
    m_lrclib = new LRCLibProvider(this);
    // The order they are handed over in is not significant: setSourceOrder() and
    // the settings decide which is tried first.
    m_engine.setProviders({m_netease, m_lrclib});
    // The file's own lyric, tried wherever the settings put "local" in the order.
    m_engine.setLocalSource(&m_localFiles);
    // The overlay shows a primary line plus a translation; keep both by default.
    m_engine.setDisplayMode(LyricDisplayMode::Both);

    // The manual search ("search lyrics" in the tray) asks the same providers.
    // Which of them it lists first is decided in applyProviders(), where the
    // engine's order is already resolved.
    m_search = new LyricSearch(this);
    // A pin fetches one lyric through the provider the chosen row came from, so
    // both providers' lyric signals end up here. The engine's own fetches carry
    // request ids this object has never seen and fall through.
    const QList<LyricProvider *> providers = {m_netease, m_lrclib};
    for (LyricProvider *provider : providers) {
        connect(provider, &LyricProvider::lyricFinished, this, &LyricsController::onPinLyricFinished);
        connect(provider, &LyricProvider::lyricFailed, this, &LyricsController::onPinLyricFailed);
    }

    connect(&m_engine, &LyricEngine::currentLineChanged, this, [this](const QString &text, const QString &translation) {
        qCDebug(lcLyrics) << "line:" << text << (translation.isEmpty() ? QString() : QStringLiteral("/ ") + translation);
        Q_EMIT currentTextChanged();
        Q_EMIT translationTextChanged();
        updateUpcoming();
    });
    connect(&m_engine, &LyricEngine::stateChanged, this, [this](LyricEngine::State state) {
        qCDebug(lcLyrics) << "engine state:" << static_cast<int>(state);
        Q_EMIT stateChanged();
        Q_EMIT lyricStatusChanged();
    });
    connect(&m_engine, &LyricEngine::lastErrorChanged, this, [this](const QString &error) {
        qCDebug(lcLyrics) << "engine last error:" << (error.isEmpty() ? QStringLiteral("<none>") : error);
        Q_EMIT lastErrorChanged();
        Q_EMIT lyricStatusChanged();
    });

    m_demoTimer.setInterval(kDemoIntervalMs);
    connect(&m_demoTimer, &QTimer::timeout, this, &LyricsController::advanceDemo);

    m_clockTimer.setInterval(kClockIntervalMs);
    connect(&m_clockTimer, &QTimer::timeout, this, &LyricsController::onClockTick);
}

LyricsController::~LyricsController() = default;

bool LyricsController::displayActive() const
{
    return m_demoActive || m_playerActive;
}

QString LyricsController::currentText() const
{
    // No player and no demo is deliberately empty: a stale line from a finished
    // song must never be left on screen pretending to be current.
    return displayActive() ? m_engine.currentText() : QString();
}

QString LyricsController::translationText() const
{
    return displayActive() ? m_engine.currentTranslation() : QString();
}

QStringList LyricsController::upcomingLines() const
{
    if (!displayActive() || !canFollowMusic(m_engine.originLyric())) {
        return {};
    }
    const int count = upcomingLineCount();
    const int start = m_engine.currentIndex() + 1;
    QStringList upcoming;
    for (int i = 0; i < count; ++i) {
        const int index = start + i;
        if (index < 0 || index >= m_engine.lineCount()) {
            break;
        }
        const QString text = lineTextForMode(index);
        if (text.trimmed().isEmpty()) {
            continue;
        }
        upcoming.append(text);
    }
    return upcoming;
}

QString LyricsController::lineTextForMode(int index) const
{
    if (index < 0 || index >= m_engine.lineCount()) {
        return {};
    }
    const QString origin = m_engine.lineText(index);
    const QString translation = m_engine.translationText(index);
    switch (m_engine.displayMode()) {
    case LyricDisplayMode::Original:
        return origin;
    case LyricDisplayMode::Translation:
        return translation.isEmpty() ? origin : translation;
    case LyricDisplayMode::Both:
        return origin.isEmpty() ? translation : origin;
    }
    return origin;
}

int LyricsController::upcomingLineCount() const
{
    const int configured = m_settings ? m_settings->visibleLines() : kDefaultUpcomingLines;
    return std::clamp(configured, kMinUpcomingLines, kMaxUpcomingLines);
}

int LyricsController::state() const
{
    if (!displayActive()) {
        return 0; // idle, not "not found": there is simply nothing to show
    }
    return static_cast<int>(m_engine.state());
}

QString LyricsController::lastError() const
{
    return m_engine.lastError();
}

QString LyricsController::lyricStatus() const
{
    // The sentence for the state, on its own. The settings window shows the
    // failure diagnostic as a separate, quieter line -- tray tool tips cannot
    // style anything, which is why the tool tip composes the two together
    // through lyricstatus::statusText() instead.
    return lyricstatus::stateText(state());
}

int LyricsController::displayMode() const
{
    switch (m_engine.displayMode()) {
    case LyricDisplayMode::Original:
        return 0;
    case LyricDisplayMode::Translation:
        return 1;
    case LyricDisplayMode::Both:
        return 2;
    }
    return 0;
}

qint64 LyricsController::lyricOffset() const
{
    return m_engine.lyricOffsetMs();
}

void LyricsController::setSettings(KShadowLrcSettings *settings)
{
    if (m_settings == settings) {
        return;
    }
    if (m_settings) {
        disconnect(m_settings, nullptr, this, nullptr);
    }
    m_settings = settings;
    if (!m_settings) {
        return;
    }

    connect(m_settings, &KShadowLrcSettings::displayModeChanged, this, [this] {
        setDisplayMode(m_settings->displayMode());
    });
    connect(m_settings, &KShadowLrcSettings::visibleLinesChanged, this, &LyricsController::updateUpcoming);
    connect(m_settings, &KShadowLrcSettings::neteaseEnabledChanged, this, &LyricsController::applyProviders);
    connect(m_settings, &KShadowLrcSettings::lrclibEnabledChanged, this, &LyricsController::applyProviders);
    connect(m_settings, &KShadowLrcSettings::sourceOrderChanged, this, &LyricsController::applyProviders);
    connect(m_settings, &KShadowLrcSettings::lyricOffsetMsChanged, this, &LyricsController::applyConfiguredOffset);

    setDisplayMode(m_settings->displayMode());
    applyProviders();
    applyConfiguredOffset();
}

void LyricsController::applyProviders()
{
    QList<LyricProvider *> providers;
    if (!m_settings || m_settings->neteaseEnabled()) {
        providers.append(m_netease);
    }
    if (!m_settings || m_settings->lrclibEnabled()) {
        providers.append(m_lrclib);
    }
    m_engine.setProviders(providers);
    m_engine.setSourceOrder(m_settings ? m_settings->sourceOrder() : QStringList{});

    QStringList names;
    names.reserve(providers.size());
    for (const LyricProvider *provider : providers) {
        names.append(provider->name());
    }
    qCDebug(lcLyrics) << "providers enabled:" << (names.isEmpty() ? QStringLiteral("<none>") : names.join(QStringLiteral(", ")))
                      << "order:" << m_engine.sourceOrder();

    // The manual search lists one group per provider, in the order the walk
    // would try them: ask the engine, which owns that rule, rather than
    // reimplementing it here.
    m_search->setProviders(m_engine.providersInSourceOrder());

    reloadCurrentTrack();
}

void LyricsController::reloadCurrentTrack()
{
    // clear() forgets the track, so copy it first; reloading only makes sense
    // when a song is actually loaded (not for the demo, which has no track).
    const TrackMeta track = m_engine.track();
    if (!track.isValid()) {
        return;
    }
    qCDebug(lcLyrics) << "re-fetching" << track.title << "after a provider/order change";
    // clear() keeps the pinned lyric deliberately (a pin belongs to the song,
    // not to the walk), so re-read it only when it may have changed.
    applyPinFor(track);
    m_engine.clear();
    m_engine.setTrack(track);
    updatePinState();
}

QString LyricsController::trackQuery() const
{
    return m_trackQuery;
}

QString LyricsController::pinnedDescription() const
{
    return m_pinnedDescription;
}

QString LyricsController::pinStatus() const
{
    return m_pinStatus;
}

void LyricsController::pinSearchResult(int index)
{
    const QVariantList results = m_search->results();
    if (index < 0 || index >= results.size()) {
        setPinStatus(i18nc("@info:status", "That entry is no longer in the list."));
        return;
    }

    const QVariantMap row = results.at(index).toMap();
    const TrackMeta track = m_engine.track();
    if (!track.isValid()) {
        setPinStatus(i18nc("@info:status", "Nothing is playing, so there is nothing to pin these lyrics to."));
        return;
    }
    const QString key = pinstore::keyFor(track);
    if (key.isEmpty()) {
        setPinStatus(i18nc("@info:status", "This song has nothing to pin its lyrics to: the player reported no name and no length."));
        return;
    }

    LyricProvider *provider = providerForName(row.value(QStringLiteral("provider")).toString());
    if (!provider) {
        setPinStatus(i18nc("@info:status", "That entry comes from a source this build does not have."));
        return;
    }

    PendingPin pending;
    pending.track = track;
    pending.provider = row.value(QStringLiteral("provider")).toString();
    pending.songId = row.value(QStringLiteral("id")).toString();
    // The words the window shows once this is pinned: which source it came from,
    // then which of its entries the user picked.
    const QString singer = row.value(QStringLiteral("singer")).toString();
    pending.source = row.value(QStringLiteral("providerLabel")).toString() + QStringLiteral(" — ") + row.value(QStringLiteral("title")).toString()
        + (singer.isEmpty() ? QString() : QStringLiteral(" / ") + singer);

    const quint64 requestId = m_nextPinRequestId++;
    m_pendingPins.insert(requestId, pending);
    qCInfo(lcLyrics) << "pinning" << pending.source << "to" << track.title;
    setPinStatus(i18nc("@info:status", "Fetching the lyric…"));
    provider->queryLyric(requestId, pending.songId);
}

void LyricsController::forgetPinnedLyric()
{
    const TrackMeta track = m_engine.track();
    if (!track.isValid()) {
        setPinStatus(i18nc("@info:status", "Nothing is playing, so there is nothing to forget."));
        return;
    }
    if (!pinstore::forget(track)) {
        setPinStatus(i18nc("@info:status", "This song has no pinned lyrics."));
        return;
    }

    qCInfo(lcLyrics) << "pin forgotten for" << track.title;
    m_currentPin.reset();
    m_engine.setPinnedLyric(QString{}, LyricStore{});
    setPinStatus(QString{});
    // Resolve the song again from scratch: with the pin gone, the walk decides.
    reloadCurrentTrack();
}

void LyricsController::onPinLyricFinished(quint64 requestId, const QString &songId, const kshadowlrc::LyricStore &store)
{
    Q_UNUSED(songId)
    const auto it = m_pendingPins.find(requestId);
    if (it == m_pendingPins.end()) {
        return; // not one of ours: this is the engine's own fetch
    }
    const PendingPin pending = it.value();
    m_pendingPins.erase(it);

    if (!store.hasAny()) {
        setPinStatus(i18nc("@info:status", "That entry carries no lyric text to pin."));
        return;
    }
    // The overlay never shows a lyric without timestamps, so pinning one would
    // store a promise it cannot keep. Say so here, where the user can pick again.
    if (!canFollowMusic(parseLrc(store.lyric))) {
        setPinStatus(i18nc("@info:status", "That lyric has no timestamps, so it cannot follow the music and is never shown."));
        return;
    }

    pinstore::Pin pin;
    pin.store = store;
    pin.source = pending.source;
    pin.provider = pending.provider;
    pin.songId = pending.songId;
    pin.title = pending.track.title;
    pin.singer = pending.track.artists.join(QStringLiteral(" / "));
    if (!pinstore::save(pending.track, pin)) {
        setPinStatus(i18nc("@info:status", "The chosen lyrics could not be saved."));
        return;
    }

    qCInfo(lcLyrics) << "pinned" << pending.source << "to" << pending.track.title;
    setPinStatus(QString{});
    // Re-resolve only while that song is still the one playing; otherwise the
    // pin waits for the player to come back to it.
    if (m_engine.track().sameSong(pending.track)) {
        reloadCurrentTrack();
    }
}

void LyricsController::onPinLyricFailed(quint64 requestId, const QString &songId, const QString &error)
{
    Q_UNUSED(songId)
    const auto it = m_pendingPins.find(requestId);
    if (it == m_pendingPins.end()) {
        return;
    }
    m_pendingPins.erase(it);
    qCWarning(lcLyrics) << "pinning failed:" << error;
    setPinStatus(i18nc("@info:status", "That lyric could not be fetched: %1", error));
}

void LyricsController::applyPinFor(const TrackMeta &track)
{
    m_currentPin.reset();
    if (track.isValid()) {
        m_currentPin = pinstore::lookup(track);
    }
    // Only a pin that can actually be shown counts: a record with no text, or one
    // that cannot follow the music, would claim the song is pinned and then
    // resolve it the ordinary way, which is the opposite of a promise.
    if (m_currentPin && !pinIsShowable(*m_currentPin)) {
        qCWarning(lcLyrics) << "ignoring a pinned record that cannot be shown for" << track.title;
        m_currentPin.reset();
    }

    if (m_currentPin) {
        qCDebug(lcLyrics) << "pinned lyric for" << track.title << ":" << m_currentPin->source;
        m_engine.setPinnedLyric(pinstore::keyFor(track), m_currentPin->store);
    } else {
        m_engine.setPinnedLyric(QString{}, LyricStore{});
    }
}

void LyricsController::updatePinState()
{
    const QString query = queryTextFor(m_engine.track());
    if (query != m_trackQuery) {
        m_trackQuery = query;
        Q_EMIT trackQueryChanged();
    }
    const QString description = (m_currentPin && m_engine.track().isValid()) ? m_currentPin->source : QString();
    if (description != m_pinnedDescription) {
        m_pinnedDescription = description;
        Q_EMIT pinnedDescriptionChanged();
    }
}

void LyricsController::setPinStatus(const QString &status)
{
    if (m_pinStatus == status) {
        return;
    }
    m_pinStatus = status;
    Q_EMIT pinStatusChanged();
}

LyricProvider *LyricsController::providerForName(const QString &name) const
{
    const QList<LyricProvider *> providers = {m_netease, m_lrclib};
    for (LyricProvider *provider : providers) {
        if (provider->name() == name) {
            return provider;
        }
    }
    return nullptr;
}

void LyricsController::applyConfiguredOffset()
{
    if (!m_settings) {
        return;
    }
    const qint64 ms = m_settings->lyricOffsetMs();
    qCDebug(lcLyrics) << "applying configured lyric offset:" << ms << "ms";
    // Only touch the tracker when it disagrees: the generated setter and the
    // tracker both guard equality, which is what keeps this from oscillating.
    if (m_tracker && m_tracker->lyricOffsetMs() != ms) {
        m_tracker->setLyricOffsetMs(ms); // emits -> onLyricOffsetChanged -> engine
        return;
    }
    if (m_engine.lyricOffsetMs() != ms) {
        m_engine.setLyricOffsetMs(ms);
        Q_EMIT lyricOffsetChanged();
    }
}

void LyricsController::setPlayerTracker(PlayerTracker *tracker)
{
    if (m_tracker == tracker) {
        return;
    }
    if (m_tracker) {
        disconnect(m_tracker, nullptr, this, nullptr);
        stopClock();
    }
    m_tracker = tracker;
    m_playerActive = false;

    if (m_tracker) {
        connect(m_tracker, &PlayerTracker::playerConnected, this, &LyricsController::onPlayerConnected);
        connect(m_tracker, &PlayerTracker::playerDisconnected, this, &LyricsController::onPlayerDisconnected);
        connect(m_tracker, &PlayerTracker::trackChanged, this, &LyricsController::onTrackChanged);
        connect(m_tracker, &PlayerTracker::statusChanged, this, &LyricsController::onStatusChanged);
        connect(m_tracker, &PlayerTracker::lyricOffsetChanged, this, &LyricsController::onLyricOffsetChanged);
        applyConfiguredOffset();
    }
    updateDisplay();
}

void LyricsController::onClockTick()
{
    if (m_tracker) {
        m_engine.setElapsed(m_tracker->elapsedMs());
    }
}

void LyricsController::onPlayerConnected()
{
    // The player is now the source of truth: drop the demo and anything it left
    // loaded so the first real line is never preceded by a demo line.
    stopDemo();
    m_engine.clear();
    m_playerActive = true;
    startClock();
    qCDebug(lcLyrics) << "player connected:" << (m_tracker ? m_tracker->playerName() : QString());
    updateDisplay();
    updatePinState();
}

void LyricsController::onPlayerDisconnected()
{
    m_playerActive = false;
    stopClock();
    m_engine.clear();
    qCDebug(lcLyrics) << "player disconnected; lyric cleared";
    updateDisplay();
    updatePinState();
}

void LyricsController::onTrackChanged(const kshadowlrc::TrackMeta &track)
{
    qCDebug(lcLyrics) << "track changed:" << track.title << "-" << track.artists.join(QStringLiteral(", "));
    // Read the pin first: the engine only skips the cache and the providers when
    // it already has the pinned lyric by the time setTrack() runs.
    applyPinFor(track);
    m_engine.setTrack(track);
    updatePinState();
}

void LyricsController::onStatusChanged(kshadowlrc::PlaybackStatus status)
{
    switch (status) {
    case PlaybackStatus::Playing:
    case PlaybackStatus::Paused:
        m_playerActive = true;
        startClock();
        break;
    case PlaybackStatus::Stopped:
        // No meaningful playhead: blank the overlay and park the clock, but keep
        // the loaded lyric so resuming the same song shows it again at once.
        m_playerActive = false;
        stopClock();
        break;
    case PlaybackStatus::Unknown:
        break;
    }
    qCDebug(lcLyrics) << "status changed:" << static_cast<int>(status) << (m_playerActive ? "(overlay live)" : "(overlay blank)");
    updateDisplay();
}

void LyricsController::onLyricOffsetChanged(qint64 ms)
{
    // From the tracker (the future keyboard shortcut). Apply it to the engine
    // and persist it, unless the settings value already agrees.
    if (m_engine.lyricOffsetMs() != ms) {
        m_engine.setLyricOffsetMs(ms);
        Q_EMIT lyricOffsetChanged();
    }
    if (m_settings && m_settings->lyricOffsetMs() != static_cast<int>(ms)) {
        m_settings->setLyricOffsetMs(static_cast<int>(ms));
    }
}

void LyricsController::setTestMode()
{
    const Lyric origin = parseLrc(kDemoOrigin);
    const Lyric translation = pairTranslation(origin, parseLrc(kDemoTranslation));
    m_engine.setLyric(origin, translation);

    m_demoElapsed = 0;
    m_demoEnd = kDemoEndMs;
    m_engine.setElapsed(m_demoElapsed);
    m_demoActive = true;
    m_demoTimer.start();
    qCDebug(lcLyrics) << "demo mode started";
    updateDisplay();
}

void LyricsController::stopDemo()
{
    if (!m_demoActive) {
        return;
    }
    m_demoTimer.stop();
    m_demoActive = false;
    updateDisplay();
}

void LyricsController::setElapsed(qint64 ms)
{
    m_engine.setElapsed(ms);
}

void LyricsController::setLyricOffset(qint64 ms)
{
    // One authoritative runtime path: through the tracker when there is one, so
    // the tracker and the persisted setting stay in step.
    if (m_tracker) {
        m_tracker->setLyricOffsetMs(ms);
        return;
    }
    if (m_engine.lyricOffsetMs() != ms) {
        m_engine.setLyricOffsetMs(ms);
        Q_EMIT lyricOffsetChanged();
    }
    if (m_settings && m_settings->lyricOffsetMs() != static_cast<int>(ms)) {
        m_settings->setLyricOffsetMs(static_cast<int>(ms));
    }
}

void LyricsController::setDisplayMode(int mode)
{
    LyricDisplayMode resolved = LyricDisplayMode::Original;
    switch (mode) {
    case 1:
        resolved = LyricDisplayMode::Translation;
        break;
    case 2:
        resolved = LyricDisplayMode::Both;
        break;
    default:
        resolved = LyricDisplayMode::Original;
        break;
    }
    if (m_engine.displayMode() == resolved) {
        return;
    }
    m_engine.setDisplayMode(resolved);
    qCDebug(lcLyrics) << "display mode:" << mode << "(0=original 1=translation 2=both)";
    Q_EMIT displayModeChanged();
    updateUpcoming();
}

void LyricsController::advanceDemo()
{
    if (!m_demoActive) {
        return;
    }
    m_demoElapsed += kDemoIntervalMs;
    if (m_demoElapsed > m_demoEnd) {
        m_demoElapsed = 0;
    }
    m_engine.setElapsed(m_demoElapsed);
}

void LyricsController::updateDisplay()
{
    // The display gate can change without the engine emitting anything (a player
    // stopping, connecting or disconnecting), so re-notify QML unconditionally.
    Q_EMIT currentTextChanged();
    Q_EMIT translationTextChanged();
    Q_EMIT stateChanged();
    Q_EMIT lyricStatusChanged();
    updateUpcoming();
}

void LyricsController::updateUpcoming()
{
    qCDebug(lcLyrics) << "upcoming lines:" << upcomingLines();
    Q_EMIT upcomingLinesChanged();
}

void LyricsController::startClock()
{
    if (m_tracker && !m_clockTimer.isActive()) {
        m_clockTimer.start();
    }
}

void LyricsController::stopClock()
{
    m_clockTimer.stop();
}

} // namespace kshadowlrc
