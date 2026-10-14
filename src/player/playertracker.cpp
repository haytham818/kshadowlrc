// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 KShadowLrc Contributors

#include "playertracker.h"

#include "mprismeta.h"
#include "mprisplayer.h"
#include "playermatch.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QHash>
#include <QList>
#include <QLoggingCategory>
#include <QSet>
#include <QTimer>
#include <QVariant>

#include <iterator>
#include <utility>

Q_LOGGING_CATEGORY(lcPlayerTracker, "kshadowlrc.player")

namespace kshadowlrc
{

using namespace mpris;

qint64 PositionAnchor::elapsedMs(qint64 lengthMs) const
{
    if (!at.isValid()) {
        return positionMs;
    }
    const qint64 elapsed = positionMs + (QDateTime::currentDateTimeUtc().toMSecsSinceEpoch() - at.toMSecsSinceEpoch());
    if (elapsed < 0) {
        return 0;
    }
    if (lengthMs > 0 && elapsed > lengthMs) {
        return lengthMs;
    }
    return elapsed;
}

namespace
{

/*! Default poll interval. The named default the settings layer will override. */
constexpr int kDefaultSyncIntervalMs = 1000;
/*! Coalescing window for NameOwnerChanged storms during e.g. a player restart. */
constexpr int kScanDebounceMs = 150;
/*! Safety re-scan while no player is connected, in case a signal was missed. */
constexpr int kIdleRescanMs = 10000;
/*! A fresh position this far from the interpolated estimate is a jump. */
constexpr qint64 kJumpThresholdMs = 250;
/*! Consecutive failed refreshes before the active player is considered dead. */
constexpr int kMaxRefreshFailures = 3;

/*!
 * Players skipped out of the box: Firefox and playerctld, which report garbage
 * or mirror another player. These are the built-in defaults of the
 * `ignoredPlayers` setting; the settings layer replaces them with the user's
 * list. Each entry follows the matching rules in playermatch.h: with no
 * wildcard, `org.mpris.MediaPlayer2.firefox` matches the instance-suffixed bus
 * name `org.mpris.MediaPlayer2.firefox.instance1` as a dot-boundary prefix.
 */
const QStringList kDefaultIgnoredPlayers = {
    QStringLiteral("org.mpris.MediaPlayer2.firefox"),
    QStringLiteral("org.mpris.MediaPlayer2.playerctld"),
};

/*! Dynamic property holding the tracker's state (the frozen header cannot). */
const char kPrivateProperty[] = "kshadowlrc_playerTrackerPrivate";

struct ScanResult {
    QString wellKnown;
    QString owner;
    PlaybackStatus status = PlaybackStatus::Unknown;
    QVariantMap metadata;
    bool ok = false;
};

/*!
 * The tracker's state.
 *
 * playertracker.h is frozen and declares no members, so the state lives here
 * and is reached through a dynamic property on the tracker object. QObject
 * children (timer, active player) are parented to the tracker and die with it.
 */
struct PlayerTrackerPrivate {
    QTimer *timer = nullptr;
    QTimer *scanDebounce = nullptr;
    MprisPlayer *active = nullptr;
    PlaybackStatus status = PlaybackStatus::Unknown;
    TrackMeta track;
    PositionAnchor anchor;
    /*!
     * The track position a lyric clock holds while the player is not advancing.
     * Captured when the state leaves Playing (and on a seek while paused), then
     * returned verbatim so the clock cannot drift between position polls.
     * `frozenValid` is false until a real position is known for the current
     * pause (e.g. we connected to a player that was already paused).
     */
    qint64 frozenElapsedMs = 0;
    bool frozenValid = false;
    /*! Whether a real position has been reported for the current track. */
    bool positionKnown = false;
    qint64 lyricOffsetMs = 0;
    int syncIntervalMs = kDefaultSyncIntervalMs;
    QStringList ignoredPlayers = kDefaultIgnoredPlayers;
    bool started = false;
    bool scanning = false;
    quint64 scanId = 0;
    QList<ScanResult> scanResults;
    int ownerPending = 0;
    int propsPending = 0;
    qint64 lastScanMs = 0;
    int refreshFailures = 0;
    bool suppressNextJump = false;
    QHash<QString, QVariantMap> seenMetadata;
    QHash<QString, qint64> activityMs;
};

PlayerTrackerPrivate *privOf(const PlayerTracker *tracker)
{
    return static_cast<PlayerTrackerPrivate *>(tracker->property(kPrivateProperty).value<void *>());
}

int statusRank(PlaybackStatus status)
{
    switch (status) {
    case PlaybackStatus::Playing:
        return 3;
    case PlaybackStatus::Paused:
        return 2;
    case PlaybackStatus::Stopped:
        return 1;
    case PlaybackStatus::Unknown:
        break;
    }
    return 0;
}

void startScan(PlayerTracker *tracker);
void clearActive(PlayerTracker *tracker);
void applyStatus(PlayerTracker *tracker, PlaybackStatus status);
void applyMetadata(PlayerTracker *tracker, const QVariantMap &metadata);
void applyPosition(PlayerTracker *tracker, qint64 positionUs);
void onSeeked(PlayerTracker *tracker, qint64 positionUs);
void scheduleScan(PlayerTracker *tracker);
void finishScan(PlayerTracker *tracker, quint64 scanId);
void requestProperties(PlayerTracker *tracker, quint64 scanId);
void setActivePlayer(PlayerTracker *tracker, const ScanResult &result);

void onTimer(PlayerTracker *tracker)
{
    auto *priv = privOf(tracker);
    if (!priv->started) {
        return;
    }
    if (priv->active) {
        priv->active->refresh();
        return;
    }
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (now - priv->lastScanMs >= kIdleRescanMs) {
        startScan(tracker);
    }
}

void scheduleScan(PlayerTracker *tracker)
{
    auto *priv = privOf(tracker);
    if (!priv->started) {
        return;
    }
    priv->scanDebounce->start();
}

void onNameOwnerChanged(PlayerTracker *tracker, const QString &name, const QString &oldOwner, const QString &newOwner)
{
    Q_UNUSED(oldOwner)
    if (!name.startsWith(kMprisPrefix)) {
        return;
    }
    auto *priv = privOf(tracker);
    if (!priv->started) {
        return;
    }
    if (priv->active && newOwner.isEmpty() && (name == priv->active->wellKnownName() || name == priv->active->ownerName())) {
        qCDebug(lcPlayerTracker) << "active player" << name << "vanished; rescanning";
        clearActive(tracker);
    }
    scheduleScan(tracker);
}

void startScan(PlayerTracker *tracker)
{
    auto *priv = privOf(tracker);
    const quint64 scanId = ++priv->scanId;
    priv->scanResults.clear();
    priv->ownerPending = 0;
    priv->propsPending = 0;
    priv->scanning = true;
    priv->lastScanMs = QDateTime::currentMSecsSinceEpoch();

    callAsync<QStringList>(kBusService,
                           kBusPath,
                           kBusInterface,
                           QStringLiteral("ListNames"),
                           {},
                           tracker,
                           [tracker, scanId](const QDBusPendingReply<QStringList> &reply) {
                               auto *priv = privOf(tracker);
                               if (scanId != priv->scanId) {
                                   return;
                               }
                               if (reply.isError()) {
                                   qCDebug(lcPlayerTracker) << "ListNames failed:" << reply.error().message();
                                   finishScan(tracker, scanId);
                                   return;
                               }

                               QStringList candidates;
                               const QStringList names = reply.value();
                               for (const QString &name : names) {
                                   if (name.startsWith(kMprisPrefix) && !isIgnoredPlayer(priv->ignoredPlayers, name)) {
                                       candidates.append(name);
                                   }
                               }
                               if (candidates.isEmpty()) {
                                   finishScan(tracker, scanId);
                                   return;
                               }

                               priv->ownerPending = candidates.size();
                               for (const QString &name : candidates) {
                                   callAsync<QString>(kBusService,
                                                      kBusPath,
                                                      kBusInterface,
                                                      QStringLiteral("GetNameOwner"),
                                                      {name},
                                                      tracker,
                                                      [tracker, scanId, name](const QDBusPendingReply<QString> &ownerReply) {
                                                          auto *priv = privOf(tracker);
                                                          if (scanId != priv->scanId) {
                                                              return;
                                                          }
                                                          ScanResult result;
                                                          result.wellKnown = name;
                                                          result.owner = ownerReply.isError() ? name : ownerReply.value();
                                                          priv->scanResults.append(result);
                                                          if (--priv->ownerPending == 0) {
                                                              requestProperties(tracker, scanId);
                                                          }
                                                      });
                               }
                           });
}

void requestProperties(PlayerTracker *tracker, quint64 scanId)
{
    auto *priv = privOf(tracker);
    priv->propsPending = priv->scanResults.size();
    if (priv->propsPending == 0) {
        finishScan(tracker, scanId);
        return;
    }

    for (int i = 0; i < priv->scanResults.size(); ++i) {
        const QString owner = priv->scanResults.at(i).owner;
        callAsync<QVariantMap>(owner,
                               kMprisObjectPath,
                               kPropertiesInterface,
                               QStringLiteral("GetAll"),
                               {kPlayerInterface},
                               tracker,
                               [tracker, scanId, i](const QDBusPendingReply<QVariantMap> &reply) {
                                   auto *priv = privOf(tracker);
                                   if (scanId != priv->scanId) {
                                       return;
                                   }
                                   if (!reply.isError() && i < priv->scanResults.size()) {
                                       const QVariantMap properties = reply.value();
                                       ScanResult &result = priv->scanResults[i];
                                       result.status = playbackStatusFromString(properties.value(QStringLiteral("PlaybackStatus")).toString());
                                       result.metadata = variantMapFromDbus(properties.value(QStringLiteral("Metadata")));
                                       result.ok = true;
                                   }
                                   if (--priv->propsPending == 0) {
                                       finishScan(tracker, scanId);
                                   }
                               });
    }
}

void finishScan(PlayerTracker *tracker, quint64 scanId)
{
    auto *priv = privOf(tracker);
    if (scanId != priv->scanId) {
        return;
    }
    priv->scanning = false;

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    QSet<QString> present;
    for (const ScanResult &result : std::as_const(priv->scanResults)) {
        if (!result.ok) {
            continue;
        }
        present.insert(result.wellKnown);
        const bool isNew = !priv->seenMetadata.contains(result.wellKnown);
        const bool changed = priv->seenMetadata.value(result.wellKnown) != result.metadata;
        if (isNew || changed) {
            priv->activityMs[result.wellKnown] = now;
            priv->seenMetadata[result.wellKnown] = result.metadata;
        }
    }
    for (auto it = priv->seenMetadata.begin(); it != priv->seenMetadata.end();) {
        it = present.contains(it.key()) ? std::next(it) : priv->seenMetadata.erase(it);
    }
    for (auto it = priv->activityMs.begin(); it != priv->activityMs.end();) {
        it = present.contains(it.key()) ? std::next(it) : priv->activityMs.erase(it);
    }

    const ScanResult *best = nullptr;
    int bestRank = -1;
    qint64 bestActivity = -1;
    for (const ScanResult &result : std::as_const(priv->scanResults)) {
        if (!result.ok || isIgnoredPlayer(priv->ignoredPlayers, result.wellKnown)) {
            // Re-checked here, not only when the candidates were collected: the
            // ignore list can change while this scan's replies are in flight.
            continue;
        }
        const int rank = statusRank(result.status);
        const qint64 activity = priv->activityMs.value(result.wellKnown, 0);
        bool better = rank > bestRank;
        if (rank == bestRank) {
            if (activity > bestActivity) {
                better = true;
            } else if (activity == bestActivity && priv->active && result.wellKnown == priv->active->wellKnownName()) {
                better = true;
            }
        }
        if (better) {
            best = &result;
            bestRank = rank;
            bestActivity = activity;
        }
    }

    if (best) {
        setActivePlayer(tracker, *best);
    } else {
        clearActive(tracker);
    }
}

void setActivePlayer(PlayerTracker *tracker, const ScanResult &result)
{
    auto *priv = privOf(tracker);
    if (isIgnoredPlayer(priv->ignoredPlayers, result.wellKnown)) {
        // The single choke point: an ignored player is never selected, whatever
        // path led here.
        clearActive(tracker);
        return;
    }
    if (priv->active && priv->active->wellKnownName() == result.wellKnown && priv->active->ownerName() == result.owner) {
        return;
    }

    clearActive(tracker);

    priv->active = new MprisPlayer(result.wellKnown, result.owner, tracker);
    priv->refreshFailures = 0;
    priv->suppressNextJump = false;
    priv->status = PlaybackStatus::Unknown;
    priv->track = TrackMeta{};
    priv->anchor = PositionAnchor{};
    priv->frozenElapsedMs = 0;
    priv->frozenValid = false;
    priv->positionKnown = false;

    QObject::connect(priv->active, &MprisPlayer::propertiesRefreshed, tracker, [tracker] {
        auto *priv = privOf(tracker);
        if (!priv->active) {
            return;
        }
        applyStatus(tracker, priv->active->status());
        applyMetadata(tracker, priv->active->metadata());
    });
    QObject::connect(priv->active, &MprisPlayer::positionReported, tracker, [tracker](qint64 positionUs) {
        applyPosition(tracker, positionUs);
    });
    QObject::connect(priv->active, &MprisPlayer::seeked, tracker, [tracker](qint64 positionUs) {
        onSeeked(tracker, positionUs);
    });
    QObject::connect(priv->active, &MprisPlayer::refreshFailed, tracker, [tracker] {
        auto *priv = privOf(tracker);
        if (++priv->refreshFailures < kMaxRefreshFailures) {
            return;
        }
        priv->refreshFailures = 0;
        qCDebug(lcPlayerTracker) << "active player stopped answering; rescanning";
        clearActive(tracker);
        scheduleScan(tracker);
    });

    priv->active->subscribe();
    qCInfo(lcPlayerTracker) << "connected to" << result.wellKnown << "owner" << result.owner;

    Q_EMIT tracker->playerConnected(result.wellKnown);
    applyStatus(tracker, result.status);
    applyMetadata(tracker, result.metadata);

    priv->active->refresh();
    if (priv->started) {
        priv->timer->start();
    }
}

void clearActive(PlayerTracker *tracker)
{
    auto *priv = privOf(tracker);
    if (!priv->active) {
        return;
    }
    const QString name = priv->active->wellKnownName();
    priv->active->unsubscribe();
    priv->active->deleteLater();
    priv->active = nullptr;
    priv->status = PlaybackStatus::Unknown;
    priv->track = TrackMeta{};
    priv->anchor = PositionAnchor{};
    priv->frozenElapsedMs = 0;
    priv->frozenValid = false;
    priv->positionKnown = false;
    priv->suppressNextJump = false;
    priv->refreshFailures = 0;
    qCInfo(lcPlayerTracker) << "disconnected from" << name;
    Q_EMIT tracker->playerDisconnected();
}

void applyStatus(PlayerTracker *tracker, PlaybackStatus status)
{
    auto *priv = privOf(tracker);
    if (status == priv->status) {
        return;
    }
    const PlaybackStatus previous = priv->status;
    priv->status = status;

    const QDateTime now = QDateTime::currentDateTimeUtc();
    switch (status) {
    case PlaybackStatus::Stopped:
        // A stopped player has no meaningful playhead: the clock deliberately
        // sits at the start (0) and stays there, whatever a stale position says.
        priv->anchor.positionMs = 0;
        priv->anchor.at = now;
        priv->frozenElapsedMs = 0;
        priv->frozenValid = true;
        break;
    case PlaybackStatus::Paused:
        // Capture exactly where the interpolation has reached, then hold it.
        if (priv->anchor.isValid() && priv->positionKnown) {
            priv->frozenElapsedMs = priv->anchor.elapsedMs(priv->track.lengthMs);
            priv->anchor.positionMs = priv->frozenElapsedMs;
            priv->anchor.at = now;
            priv->frozenValid = true;
        } else {
            // No real position seen yet (e.g. connected while already paused):
            // adopt the first one the player reports.
            priv->frozenElapsedMs = 0;
            priv->frozenValid = false;
        }
        break;
    case PlaybackStatus::Playing:
    case PlaybackStatus::Unknown:
        // Re-anchor so the clock advances from now, and resume from exactly the
        // frozen position so a pause/resume does not jump.
        if (priv->anchor.isValid()) {
            if (priv->frozenValid) {
                priv->anchor.positionMs = priv->frozenElapsedMs;
            }
            priv->anchor.at = now;
        }
        priv->frozenValid = false;
        break;
    }

    Q_EMIT tracker->statusChanged(status);
    if (previous != PlaybackStatus::Unknown) {
        Q_EMIT tracker->positionJumped();
    }
}

void applyMetadata(PlayerTracker *tracker, const QVariantMap &metadata)
{
    auto *priv = privOf(tracker);
    const TrackMeta next = trackMetaFromMpris(metadata);
    if (priv->track.sameSong(next)) {
        // Same song: refresh derived fields (length, ids) without signalling.
        priv->track = next;
        return;
    }

    const bool hadTrack = priv->track.isValid();
    priv->track = next;
    priv->anchor.positionMs = 0;
    priv->anchor.at = QDateTime::currentDateTimeUtc();
    priv->frozenElapsedMs = 0;
    priv->frozenValid = false;
    priv->positionKnown = false;
    // The first position fix after a track change is not a separate jump; the
    // track change itself is the discontinuity we report.
    priv->suppressNextJump = true;

    qCInfo(lcPlayerTracker) << "track:" << priv->track.title << "-" << priv->track.artists.join(QStringLiteral(", "));
    Q_EMIT tracker->trackChanged(priv->track);
    if (hadTrack) {
        Q_EMIT tracker->positionJumped();
    }
}

void applyPosition(PlayerTracker *tracker, qint64 positionUs)
{
    if (positionUs < 0) {
        return;
    }
    auto *priv = privOf(tracker);
    const qint64 positionMs = positionUs / 1000;
    const QDateTime now = QDateTime::currentDateTimeUtc();

    if (priv->status == PlaybackStatus::Stopped) {
        // A stopped player has no meaningful playhead, and some keep reporting
        // the last position; keep the anchor at zero without signalling.
        priv->anchor.positionMs = 0;
        priv->anchor.at = now;
        return;
    }

    if (priv->status != PlaybackStatus::Playing) {
        // Paused (or still unknown): the lyric clock must not move. Adopt the
        // first position we are ever told (a player met while already paused),
        // then hold it; later reports only refresh the raw anchor.
        if (!priv->frozenValid) {
            priv->frozenElapsedMs = positionMs;
            priv->frozenValid = true;
        }
        priv->positionKnown = true;
        priv->anchor.positionMs = positionMs;
        priv->anchor.at = now;
        return;
    }

    const qint64 lengthMs = priv->track.lengthMs;
    const bool suppressJump = priv->suppressNextJump;
    priv->suppressNextJump = false;

    if (!priv->anchor.isValid()) {
        priv->positionKnown = true;
        priv->anchor.positionMs = positionMs;
        priv->anchor.at = now;
        return;
    }

    priv->positionKnown = true;
    const qint64 estimate = priv->anchor.elapsedMs(lengthMs);
    const bool jumped = qAbs(positionMs - estimate) > kJumpThresholdMs;

    priv->anchor.positionMs = positionMs;
    priv->anchor.at = now;

    if (jumped && !suppressJump) {
        Q_EMIT tracker->positionJumped();
    }
}

void onSeeked(PlayerTracker *tracker, qint64 positionUs)
{
    if (positionUs < 0) {
        return;
    }
    auto *priv = privOf(tracker);
    priv->suppressNextJump = false;
    const qint64 positionMs = positionUs / 1000;
    const QDateTime now = QDateTime::currentDateTimeUtc();

    if (priv->status == PlaybackStatus::Stopped) {
        // Stopped stays at 0 regardless of where the player claims to be.
        priv->anchor.positionMs = 0;
        priv->anchor.at = now;
        priv->frozenElapsedMs = 0;
        priv->frozenValid = true;
        return;
    }

    priv->positionKnown = true;
    priv->anchor.positionMs = positionMs;
    priv->anchor.at = now;
    if (priv->status != PlaybackStatus::Playing) {
        // A seek while paused moves the frozen clock to the seek target.
        priv->frozenElapsedMs = positionMs;
        priv->frozenValid = true;
    }
    Q_EMIT tracker->positionJumped();
}

} // namespace

PlayerTracker::PlayerTracker(QObject *parent)
    : QObject(parent)
{
    auto *priv = new PlayerTrackerPrivate;
    setProperty(kPrivateProperty, QVariant::fromValue(static_cast<void *>(priv)));

    priv->timer = new QTimer(this);
    priv->timer->setInterval(priv->syncIntervalMs);
    connect(priv->timer, &QTimer::timeout, this, [this] {
        onTimer(this);
    });

    priv->scanDebounce = new QTimer(this);
    priv->scanDebounce->setSingleShot(true);
    priv->scanDebounce->setInterval(kScanDebounceMs);
    connect(priv->scanDebounce, &QTimer::timeout, this, [this] {
        startScan(this);
    });

    if (QDBusConnectionInterface *bus = QDBusConnection::sessionBus().interface()) {
        connect(bus, &QDBusConnectionInterface::serviceOwnerChanged, this, [this](const QString &name, const QString &oldOwner, const QString &newOwner) {
            onNameOwnerChanged(this, name, oldOwner, newOwner);
        });
    } else {
        qCWarning(lcPlayerTracker) << "no session bus; PlayerTracker cannot discover players";
    }
}

PlayerTracker::~PlayerTracker()
{
    delete privOf(this);
}

void PlayerTracker::start()
{
    auto *priv = privOf(this);
    if (priv->started) {
        return;
    }
    priv->started = true;
    priv->timer->start();
    startScan(this);
}

void PlayerTracker::stop()
{
    auto *priv = privOf(this);
    if (!priv->started) {
        return;
    }
    priv->started = false;
    priv->timer->stop();
    priv->scanDebounce->stop();
    ++priv->scanId; // invalidate callbacks still in flight
    priv->scanning = false;
    clearActive(this);
}

bool PlayerTracker::hasPlayer() const
{
    return privOf(this)->active != nullptr;
}

QString PlayerTracker::playerName() const
{
    const auto *priv = privOf(this);
    return priv->active ? priv->active->wellKnownName() : QString();
}

PlaybackStatus PlayerTracker::status() const
{
    return privOf(this)->status;
}

TrackMeta PlayerTracker::track() const
{
    return privOf(this)->track;
}

PositionAnchor PlayerTracker::anchor() const
{
    return privOf(this)->anchor;
}

qint64 PlayerTracker::elapsedMs() const
{
    const auto *priv = privOf(this);
    if (!priv->active) {
        return 0;
    }
    switch (priv->status) {
    case PlaybackStatus::Playing:
        // Genuinely advancing: interpolate from the freshly re-anchored snapshot.
        return priv->anchor.elapsedMs(priv->track.lengthMs);
    case PlaybackStatus::Stopped:
        // Deliberate: a stopped player has no playhead, so the clock sits at 0.
        return 0;
    case PlaybackStatus::Paused:
    case PlaybackStatus::Unknown:
        // Exactly constant between polls: the position frozen when playback
        // stopped, not the raw anchor (which would creep forward and snap back).
        return priv->frozenElapsedMs;
    }
    return 0;
}

qint64 PlayerTracker::lyricOffsetMs() const
{
    return privOf(this)->lyricOffsetMs;
}

void PlayerTracker::setLyricOffsetMs(qint64 ms)
{
    auto *priv = privOf(this);
    if (priv->lyricOffsetMs == ms) {
        return;
    }
    priv->lyricOffsetMs = ms;
    Q_EMIT lyricOffsetChanged(ms);
}

int PlayerTracker::syncIntervalMs() const
{
    return privOf(this)->syncIntervalMs;
}

void PlayerTracker::setSyncIntervalMs(int ms)
{
    auto *priv = privOf(this);
    const int interval = qMax(1, ms);
    if (priv->syncIntervalMs == interval) {
        return;
    }
    priv->syncIntervalMs = interval;
    priv->timer->setInterval(interval);
}

QStringList PlayerTracker::ignoredPlayers() const
{
    return privOf(this)->ignoredPlayers;
}

void PlayerTracker::setIgnoredPlayers(const QStringList &patterns)
{
    auto *priv = privOf(this);
    if (priv->ignoredPlayers == patterns) {
        return;
    }
    const bool replacedActive = priv->active && isIgnoredPlayer(patterns, priv->active->wellKnownName());
    priv->ignoredPlayers = patterns;
    qCInfo(lcPlayerTracker) << "ignored players:" << (patterns.isEmpty() ? QStringList{QStringLiteral("<none>")} : patterns);

    if (!priv->started) {
        return;
    }
    // A freshly ignored active player must go at once, and a freshly allowed one
    // must be pickable at once: re-scan now instead of waiting for the next
    // NameOwnerChanged. reconnect() is the existing mechanism, reused.
    if (replacedActive) {
        clearActive(this);
    }
    reconnect();
}

void PlayerTracker::reconnect()
{
    auto *priv = privOf(this);
    priv->scanDebounce->stop();
    startScan(this);
}

} // namespace kshadowlrc
