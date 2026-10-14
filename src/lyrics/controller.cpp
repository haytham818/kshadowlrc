// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 KShadowLrc Contributors

#include "lyrics/controller.h"

#include "config/settings.h"
#include "kshadowlrc.h"
#include "lyrics/lrcparser.h"
#include "lyrics/lyricstatus.h"
#include "lyrics/providers/lrclib.h"
#include "lyrics/providers/netease.h"
#include "player/playertracker.h"

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
    if (!displayActive() || m_engine.originLyric().kind != LyricKind::Timed) {
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
    m_engine.clear();
    m_engine.setTrack(track);
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
}

void LyricsController::onPlayerDisconnected()
{
    m_playerActive = false;
    stopClock();
    m_engine.clear();
    qCDebug(lcLyrics) << "player disconnected; lyric cleared";
    updateDisplay();
}

void LyricsController::onTrackChanged(const kshadowlrc::TrackMeta &track)
{
    qCDebug(lcLyrics) << "track changed:" << track.title << "-" << track.artists.join(QStringLiteral(", "));
    m_engine.setTrack(track);
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
