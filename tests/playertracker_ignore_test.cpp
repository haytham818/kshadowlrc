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
//
// It also pins the published known-player list: ignored players stay on it,
// flagged with the rule that catches them; the label is the player's Identity;
// and "active" tracks the followed player.

#include "playertracker.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusObjectPath>
#include <QProcess>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QString>
#include <QStringList>
#include <QTest>
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

/*! A minimal in-process MPRIS player, playing from the moment it appears. */
class FakePlayer : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2.Player")
    Q_PROPERTY(QString PlaybackStatus READ playbackStatus)
    Q_PROPERTY(QVariantMap Metadata READ metadata)
    Q_PROPERTY(qlonglong Position READ positionUs)
    // The root interface's Identity. One object carries both interfaces, as a
    // real MPRIS player does; QtDBus answers Properties.GetAll for either
    // interface name with every exported property, so the tracker's
    // root-interface read finds it. Deliberately different from the bus name, so
    // the known list proves the label is the Identity and not the name.
    Q_PROPERTY(QString Identity READ identity)

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

    QString identity() const
    {
        return QStringLiteral("Test Player");
    }
};

} // namespace

class PlayerTrackerIgnoreTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void inner();
    void knownPlayers();
};

void PlayerTrackerIgnoreTest::inner()
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) {
        QTextStream(stdout) << "SKIP: session bus unavailable" << Qt::endl;
        return;
    }

    const QString serviceName = QStringLiteral("org.mpris.MediaPlayer2.kshadowlrc_ignore_test");
    if (!bus.registerService(serviceName)) {
        QFAIL(qPrintable(QStringLiteral("could not register %1").arg(serviceName)));
    }

    FakePlayer fake;
    if (!bus.registerObject(QString::fromLatin1(kMprisObjectPath), &fake, QDBusConnection::ExportAllContents)) {
        bus.unregisterService(serviceName);
        QFAIL("could not export the fake player");
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
                         QVERIFY2(tracker.ignoredPlayers().isEmpty(), "an empty ignore list is stored and read back as empty");
                         tracker.start();
                     }});

    // An empty list ignores nothing, so the sole player is picked up as usual.
    steps.push_back({300, [&] {
                         QVERIFY2(tracker.hasPlayer(), "empty list ignores nothing: the only player is selected");
                         QVERIFY2(tracker.playerName() == serviceName, "the selected player is the fake");
                         QVERIFY2(connectedA == 1, "playerConnected emitted once");
                     }});

    // (b) Ignore the currently-playing, sole player. No NameOwnerChanged or any
    // other bus event is delivered between this call and the assertions.
    steps.push_back({0, [&] {
                         tracker.setIgnoredPlayers({ignoreEntry});
                         QVERIFY2(tracker.ignoredPlayers() == QStringList{ignoreEntry}, "the ignore list is stored and read back");
                         QVERIFY2(!tracker.hasPlayer(), "ignoring the active player drops it synchronously, with no bus event");
                         QVERIFY2(disconnectedA == 1, "playerDisconnected emitted on the runtime change");
                     }});

    // (c) The immediate re-scan must not bring the ignored player back.
    steps.push_back({300, [&] {
                         QVERIFY2(!tracker.hasPlayer(), "the ignored player is not selected again by the immediate re-scan");
                         QVERIFY2(connectedA == 1, "no second playerConnected after the re-scan");
                         QVERIFY2(disconnectedA == 1, "no extra disconnect");
                     }});

    // Clearing the list must let the player through again, still without a bus
    // event: the change itself re-evaluates the current player.
    steps.push_back({0, [&] {
                         tracker.setIgnoredPlayers({});
                     }});

    steps.push_back({300, [&] {
                         QVERIFY2(tracker.hasPlayer(), "clearing the list picks the player back up without a bus event");
                         QVERIFY2(tracker.playerName() == serviceName, "the re-selected player is the fake");
                         QVERIFY2(connectedA == 2, "playerConnected emitted again after the player is allowed again");
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
                         QVERIFY2(!trackerStartingIgnored->hasPlayer(), "a player ignored before start() is never selected, even as the only playing player");
                         QVERIFY2(connectedB == 0, "no playerConnected for the ignored player");
                     }});

    steps.push_back({0, [&] {
                         trackerStartingIgnored->reconnect();
                     }});

    steps.push_back({300, [&] {
                         QVERIFY2(!trackerStartingIgnored->hasPlayer(), "a re-scan never selects the ignored player");
                         QVERIFY2(connectedB == 0, "the re-scan emits no playerConnected for the ignored player");
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
}

void PlayerTrackerIgnoreTest::knownPlayers()
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) {
        QTextStream(stdout) << "SKIP: session bus unavailable" << Qt::endl;
        return;
    }

    const QString serviceName = QStringLiteral("org.mpris.MediaPlayer2.kshadowlrc_known_test");
    if (!bus.registerService(serviceName)) {
        QFAIL(qPrintable(QStringLiteral("could not register %1").arg(serviceName)));
    }

    FakePlayer fake;
    // The Identity rides on the same object; a real player is one object with
    // both interfaces.
    if (!bus.registerObject(QString::fromLatin1(kMprisObjectPath), &fake, QDBusConnection::ExportAllContents)) {
        bus.unregisterService(serviceName);
        QFAIL("could not export the fake player");
    }

    const QString ignoreEntry = QStringLiteral("kshadowlrc_known_test");

    PlayerTracker tracker;
    tracker.setSyncIntervalMs(1000);
    QSignalSpy knownSpy(&tracker, &PlayerTracker::knownPlayersChanged);

    struct Step {
        int delayMs;
        std::function<void()> action;
    };
    std::vector<Step> steps;

    steps.push_back({0, [&] {
                         tracker.setIgnoredPlayers({});
                         tracker.start();
                     }});

    // (1) The sole running player is listed: labelled from the root interface's
    // Identity (not the bus name), marked active, not ignored. A player that is
    // not running is absent, so the list holds exactly this one entry.
    steps.push_back({400, [&] {
                         QVERIFY2(tracker.hasPlayer(), "the fake player is followed");
                         const QVariantList known = tracker.knownPlayers();
                         QVERIFY2(known.size() == 1, "only the running player is listed; a player that is not running is absent");
                         const QVariantMap entry = known.constFirst().toMap();
                         QVERIFY2(entry.value(QStringLiteral("name")).toString() == serviceName, "listed by its bus name");
                         QVERIFY2(entry.value(QStringLiteral("label")).toString() == QStringLiteral("Test Player"), "label is the Identity, not the bus name");
                         QVERIFY2(entry.value(QStringLiteral("ignored")).toBool() == false, "not ignored yet");
                         QVERIFY2(entry.value(QStringLiteral("rule")).toString().isEmpty(), "no ignore rule matches yet");
                         QVERIFY2(entry.value(QStringLiteral("active")).toBool() == true, "the followed player is marked active");
                         QVERIFY2(knownSpy.count() >= 1, "the list was published");
                     }});

    // (2) Ignoring it keeps it listed, flags it with the matching rule, and
    // clears active at once, without any bus event.
    steps.push_back({0, [&] {
                         const int before = knownSpy.count();
                         tracker.setIgnoredPlayers({ignoreEntry});
                         const QVariantList known = tracker.knownPlayers();
                         QVERIFY2(known.size() == 1, "the ignored player stays in the list");
                         const QVariantMap entry = known.constFirst().toMap();
                         QVERIFY2(entry.value(QStringLiteral("ignored")).toBool() == true, "the ignored flag follows the list");
                         QVERIFY2(entry.value(QStringLiteral("rule")).toString() == ignoreEntry, "the matching entry is reported as the rule");
                         QVERIFY2(entry.value(QStringLiteral("active")).toBool() == false, "ignoring the followed player clears active");
                         QVERIFY2(knownSpy.count() > before, "changing the ignore list republishes the list");
                     }});

    // (3) A re-scan must keep it: candidates are collected even when ignored.
    steps.push_back({300, [&] {
                         const QVariantList known = tracker.knownPlayers();
                         QVERIFY2(known.size() == 1, "the ignored player survives a re-scan");
                         const QVariantMap entry = known.constFirst().toMap();
                         QVERIFY2(entry.value(QStringLiteral("ignored")).toBool(), "still flagged ignored after the re-scan");
                         QVERIFY2(entry.value(QStringLiteral("rule")).toString() == ignoreEntry, "the rule survives the re-scan");
                     }});

    // (4) Allowing it again clears ignored and rule at once; active returns once
    // the immediate re-scan has picked it back up.
    steps.push_back({0, [&] {
                         tracker.setIgnoredPlayers({});
                         const QVariantList known = tracker.knownPlayers();
                         QVERIFY2(known.size() == 1, "the player is still listed after un-ignoring");
                         const QVariantMap entry = known.constFirst().toMap();
                         QVERIFY2(entry.value(QStringLiteral("ignored")).toBool() == false, "clearing the list clears the ignored flag");
                         QVERIFY2(entry.value(QStringLiteral("rule")).toString().isEmpty(), "and the rule");
                     }});

    steps.push_back({300, [&] {
                         QVERIFY2(tracker.hasPlayer(), "the player is followed again");
                         const QVariantList known = tracker.knownPlayers();
                         QVERIFY2(known.size() == 1, "still a single entry");
                         QVERIFY2(known.constFirst().toMap().value(QStringLiteral("active")).toBool() == true, "active follows the re-scan");
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
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    const bool inner = argc > 1 && QString::fromLocal8Bit(argv[1]) == QLatin1String("--inner");
    if (inner) {
        // Drop the --inner marker before QTest parses the arguments: it would
        // reject it as an unknown option and fail the run.
        for (int i = 1; i < argc - 1; ++i) {
            argv[i] = argv[i + 1];
        }
        --argc;
        PlayerTrackerIgnoreTest tc;
        return QTest::qExec(&tc, argc, argv);
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
