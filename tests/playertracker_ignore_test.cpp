// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 KShadowLrc Contributors

// Exercises PlayerTracker's configurable ignore list against a real tracker.
// Like playertracker_clock_test.cpp it brings up its own MPRIS player on a
// private session bus (via `dbus-run-session`) so it never touches the user's
// players; it is fully offline and does no GUI work.
//
// It shows the three things the ignore list must guarantee:
//   (a) an ignored player is not selected even when it is the only one playing;
//   (b) ignoring the currently-playing player at runtime switches away from it
//       immediately, without any bus event to trigger the change;
//   (c) a re-scan never re-selects an ignored player, and clearing the list lets
//       it through again.

#include "playertracker.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusObjectPath>
#include <QProcess>
#include <QStandardPaths>
#include <QString>
#include <QStringList>
#include <QTextStream>
#include <QTimer>
#include <QVariantMap>

#include <functional>
#include <vector>

using kshadowlrc::PlayerTracker;

namespace
{

// Mirrors mprisplayer.h; kept local so the test does not depend on the wrapper.
const char kMprisObjectPath[] = "/org/mpris/MediaPlayer2";

int g_failures = 0;

void report(bool condition, const QString &what)
{
    QTextStream out(stdout);
    out << (condition ? QStringLiteral("ok:   ") : QStringLiteral("FAIL: ")) << what << Qt::endl;
    if (!condition) {
        ++g_failures;
    }
}

void check(bool condition, const char *what)
{
    report(condition, QString::fromUtf8(what));
}

/*! A minimal in-process MPRIS player, playing from the moment it appears. */
class FakePlayer : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2.Player")
    Q_PROPERTY(QString PlaybackStatus READ playbackStatus)
    Q_PROPERTY(QVariantMap Metadata READ metadata)
    Q_PROPERTY(qlonglong Position READ positionUs)

public:
    explicit FakePlayer(QObject *parent = nullptr)
        : QObject(parent)
    {
    }

    QString playbackStatus() const
    {
        return QStringLiteral("Playing");
    }

    QVariantMap metadata() const
    {
        QVariantMap meta;
        meta.insert(QStringLiteral("xesam:title"), QStringLiteral("Ignore Test"));
        meta.insert(QStringLiteral("xesam:artist"), QStringList{QStringLiteral("Fixer")});
        meta.insert(QStringLiteral("mpris:length"), qlonglong(180000000));
        meta.insert(QStringLiteral("mpris:trackid"), QVariant::fromValue(QDBusObjectPath(QStringLiteral("/ignore/track1"))));
        return meta;
    }

    qlonglong positionUs() const
    {
        return 0;
    }
};

int runInner()
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) {
        QTextStream(stdout) << "SKIP: session bus unavailable" << Qt::endl;
        return 0;
    }

    const QString serviceName = QStringLiteral("org.mpris.MediaPlayer2.kshadowlrc_ignore_test");
    if (!bus.registerService(serviceName)) {
        QTextStream(stdout) << "FAIL: could not register " << serviceName << Qt::endl;
        return 1;
    }

    FakePlayer fake;
    if (!bus.registerObject(QString::fromLatin1(kMprisObjectPath), &fake, QDBusConnection::ExportAllContents)) {
        QTextStream(stdout) << "FAIL: could not export the fake player" << Qt::endl;
        bus.unregisterService(serviceName);
        return 1;
    }

    // The name the test ignores. It is deliberately a bare segment, and the
    // player is "Playing", so the only reason it is ever skipped is the list.
    const QString ignoreEntry = QStringLiteral("kshadowlrc_ignore_test");

    PlayerTracker tracker;
    tracker.setSyncIntervalMs(1000);

    int connectedA = 0;
    int disconnectedA = 0;
    QObject::connect(&tracker, &PlayerTracker::playerConnected, &tracker, [&connectedA](const QString &) {
        ++connectedA;
    });
    QObject::connect(&tracker, &PlayerTracker::playerDisconnected, &tracker, [&disconnectedA] {
        ++disconnectedA;
    });

    PlayerTracker *trackerStartingIgnored = nullptr;
    int connectedB = 0;

    struct Step {
        int delayMs;
        std::function<void()> action;
    };
    std::vector<Step> steps;

    steps.push_back({0, [&] {
                         tracker.setIgnoredPlayers({});
                         check(tracker.ignoredPlayers().isEmpty(), "an empty ignore list is stored and read back as empty");
                         tracker.start();
                     }});

    // An empty list ignores nothing, so the sole player is picked up as usual.
    steps.push_back({300, [&] {
                         check(tracker.hasPlayer(), "empty list ignores nothing: the only player is selected");
                         check(tracker.playerName() == serviceName, "the selected player is the fake");
                         check(connectedA == 1, "playerConnected emitted once");
                     }});

    // (b) Ignore the currently-playing, sole player. No NameOwnerChanged or any
    // other bus event is delivered between this call and the assertions.
    steps.push_back({0, [&] {
                         tracker.setIgnoredPlayers({ignoreEntry});
                         check(tracker.ignoredPlayers() == QStringList{ignoreEntry}, "the ignore list is stored and read back");
                         check(!tracker.hasPlayer(), "ignoring the active player drops it synchronously, with no bus event");
                         check(disconnectedA == 1, "playerDisconnected emitted on the runtime change");
                     }});

    // (c) The immediate re-scan must not bring the ignored player back.
    steps.push_back({300, [&] {
                         check(!tracker.hasPlayer(), "the ignored player is not selected again by the immediate re-scan");
                         check(connectedA == 1, "no second playerConnected after the re-scan");
                         check(disconnectedA == 1, "no extra disconnect");
                     }});

    // Clearing the list must let the player through again, still without a bus
    // event: the change itself re-evaluates the current player.
    steps.push_back({0, [&] {
                         tracker.setIgnoredPlayers({});
                     }});

    steps.push_back({300, [&] {
                         check(tracker.hasPlayer(), "clearing the list picks the player back up without a bus event");
                         check(tracker.playerName() == serviceName, "the re-selected player is the fake");
                         check(connectedA == 2, "playerConnected emitted again after the player is allowed again");
                     }});

    // (a) A tracker that starts with the player already ignored never selects
    // it, even though it is the only player and it is playing.
    steps.push_back({0, [&] {
                         tracker.stop();
                         trackerStartingIgnored = new PlayerTracker;
                         trackerStartingIgnored->setSyncIntervalMs(1000);
                         trackerStartingIgnored->setIgnoredPlayers({ignoreEntry});
                         QObject::connect(trackerStartingIgnored, &PlayerTracker::playerConnected, trackerStartingIgnored, [&connectedB](const QString &) {
                             ++connectedB;
                         });
                         trackerStartingIgnored->start();
                     }});

    steps.push_back({400, [&] {
                         check(!trackerStartingIgnored->hasPlayer(), "a player ignored before start() is never selected, even as the only playing player");
                         check(connectedB == 0, "no playerConnected for the ignored player");
                     }});

    steps.push_back({0, [&] {
                         trackerStartingIgnored->reconnect();
                     }});

    steps.push_back({300, [&] {
                         check(!trackerStartingIgnored->hasPlayer(), "a re-scan never selects the ignored player");
                         check(connectedB == 0, "the re-scan emits no playerConnected for the ignored player");
                         delete trackerStartingIgnored;
                         trackerStartingIgnored = nullptr;
                     }});

    QCoreApplication *app = QCoreApplication::instance();
    int index = 0;
    std::function<void()> runNext;
    runNext = [&]() {
        if (index >= static_cast<int>(steps.size())) {
            bus.unregisterObject(QString::fromLatin1(kMprisObjectPath));
            bus.unregisterService(serviceName);
            app->quit();
            return;
        }
        const Step step = steps[index++];
        QTimer::singleShot(step.delayMs, app, [&runNext, step] {
            step.action();
            runNext();
        });
    };
    QTimer::singleShot(0, app, runNext);
    app->exec();

    QTextStream out(stdout);
    out << (g_failures == 0 ? QStringLiteral("PASS") : QStringLiteral("FAIL")) << QStringLiteral(": player ignore list (") << g_failures
        << QStringLiteral(" failures)") << Qt::endl;
    return g_failures == 0 ? 0 : 1;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    const bool inner = argc > 1 && QString::fromLocal8Bit(argv[1]) == QLatin1String("--inner");
    if (inner) {
        return runInner();
    }

    // Not yet on a private bus: re-exec under dbus-run-session so no real player
    // on the user's session can interfere (or be interfered with).
    const QString runner = QStandardPaths::findExecutable(QStringLiteral("dbus-run-session"));
    if (runner.isEmpty()) {
        QTextStream(stdout) << "SKIP: dbus-run-session not found" << Qt::endl;
        return 0;
    }

    QProcess process;
    process.setProcessChannelMode(QProcess::ForwardedChannels);
    process.start(runner, {QStringLiteral("--"), QCoreApplication::applicationFilePath(), QStringLiteral("--inner")});
    if (!process.waitForStarted(5000)) {
        QTextStream(stdout) << "SKIP: could not start dbus-run-session" << Qt::endl;
        return 0;
    }
    process.waitForFinished(-1);
    return process.exitStatus() == QProcess::NormalExit ? process.exitCode() : 2;
}

#include "playertracker_ignore_test.moc"
