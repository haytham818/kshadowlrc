// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 KShadowLrc Contributors

// Pins PlayerTracker::elapsedMs() down: advancing while playing, and *exactly*
// constant while paused (no drift between polls). The only way to exercise the
// real transition logic is to drive a real PlayerTracker, so this test brings up
// its own MPRIS player on a private session bus (via `dbus-run-session`) so it
// never touches the user's players. It is fully offline and does no GUI work.

#include "playertracker.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QElapsedTimer>
#include <QProcess>
#include <QStandardPaths>
#include <QString>
#include <QStringList>
#include <QTextStream>
#include <QTimer>
#include <QVariantMap>

#include <functional>
#include <vector>

using kshadowlrc::PlaybackStatus;
using kshadowlrc::PlayerTracker;

namespace
{

// Mirrors mprisplayer.h; kept local so the test does not depend on the wrapper.
const char kMprisObjectPath[] = "/org/mpris/MediaPlayer2";
const char kPlayerInterface[] = "org.mpris.MediaPlayer2.Player";
const char kPropertiesInterface[] = "org.freedesktop.DBus.Properties";

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

void check(bool condition, const QString &what)
{
    report(condition, what);
}

/*!
 * A minimal in-process MPRIS player. Methods/properties are exported by QtDBus's
 * standard adaptor; PropertiesChanged and Seeked are sent by hand so the test
 * controls exactly when each occurs.
 */
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
        m_clock.start();
    }

    QString playbackStatus() const
    {
        return m_status;
    }

    QVariantMap metadata() const
    {
        QVariantMap meta;
        meta.insert(QStringLiteral("xesam:title"), QStringLiteral("Clock Test"));
        meta.insert(QStringLiteral("xesam:artist"), QStringList{QStringLiteral("Fixer")});
        meta.insert(QStringLiteral("mpris:length"), qlonglong(215000000));
        meta.insert(QStringLiteral("mpris:trackid"), QVariant::fromValue(QDBusObjectPath(QStringLiteral("/clock/track1"))));
        return meta;
    }

    qlonglong positionUs() const
    {
        if (m_status == QLatin1String("Playing")) {
            return m_baseUs + m_clock.elapsed() * 1000;
        }
        return m_frozenUs;
    }

    void pause()
    {
        m_frozenUs = positionUs();
        m_status = QStringLiteral("Paused");
        emitPlaybackStatusChanged();
    }

    void resume()
    {
        m_baseUs = m_frozenUs;
        m_clock.restart();
        m_status = QStringLiteral("Playing");
        emitPlaybackStatusChanged();
    }

    void stop()
    {
        m_frozenUs = 0;
        m_status = QStringLiteral("Stopped");
        emitPlaybackStatusChanged();
    }

    void seekToUs(qint64 us)
    {
        m_baseUs = us;
        m_frozenUs = us;
        m_clock.restart();
        QDBusMessage signal =
            QDBusMessage::createSignal(QString::fromLatin1(kMprisObjectPath), QString::fromLatin1(kPlayerInterface), QStringLiteral("Seeked"));
        signal << QVariant::fromValue<qlonglong>(us);
        QDBusConnection::sessionBus().send(signal);
    }

private:
    void emitPlaybackStatusChanged()
    {
        QVariantMap changed;
        changed.insert(QStringLiteral("PlaybackStatus"), m_status);
        QDBusMessage signal =
            QDBusMessage::createSignal(QString::fromLatin1(kMprisObjectPath), QString::fromLatin1(kPropertiesInterface), QStringLiteral("PropertiesChanged"));
        signal << QString::fromLatin1(kPlayerInterface) << changed << QStringList{};
        QDBusConnection::sessionBus().send(signal);
    }

    QString m_status = QStringLiteral("Playing");
    QElapsedTimer m_clock;
    qint64 m_baseUs = 0;
    qint64 m_frozenUs = 0;
};

int runInner()
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) {
        QTextStream(stdout) << "SKIP: session bus unavailable" << Qt::endl;
        return 0;
    }

    const QString serviceName = QStringLiteral("org.mpris.MediaPlayer2.kshadowlrc_clock_test");
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

    PlayerTracker tracker;
    tracker.setSyncIntervalMs(1000);

    // Values sampled across steps. Kept in the enclosing scope of the runner.
    qint64 playingA = 0;
    qint64 playingB = 0;
    qint64 paused1 = 0;
    qint64 paused2 = 0;
    qint64 paused3 = 0;
    qint64 afterSeek = 0;
    qint64 resumeA = 0;
    qint64 resumeB = 0;

    struct Step {
        int delayMs;
        std::function<void()> action;
    };

    std::vector<Step> steps;

    steps.push_back({0, [&] {
                         check(tracker.elapsedMs() == 0, "no player before start(): elapsedMs() == 0");
                         check(!tracker.hasPlayer(), "no player before start()");
                         tracker.start();
                     }});

    steps.push_back({300, [&] {
                         check(tracker.hasPlayer(), "player discovered and connected");
                         check(tracker.playerName() == serviceName, "active player is the fake");
                         check(tracker.status() == PlaybackStatus::Playing, "status is Playing");
                         playingA = tracker.elapsedMs();
                     }});

    steps.push_back({600, [&] {
                         playingB = tracker.elapsedMs();
                         check(playingB > playingA, QStringLiteral("elapsedMs() advances while playing (%1 -> %2)").arg(playingA).arg(playingB));
                         check(playingB - playingA <= 3000, "played interval is plausible, not a leap");
                     }});

    steps.push_back({0, [&] {
                         fake.pause();
                     }});

    steps.push_back({250, [&] {
                         check(tracker.status() == PlaybackStatus::Paused, "status is Paused");
                         paused1 = tracker.elapsedMs();
                         check(paused1 > 0, QStringLiteral("paused position captured (%1 ms) is non-zero").arg(paused1));
                     }});

    // The case that motivated the method: two reads separated by more than one
    // poll interval (the tracker re-anchors its raw anchor meanwhile) must be
    // bit-identical, not merely close.
    steps.push_back({1400, [&] {
                         paused2 = tracker.elapsedMs();
                         check(paused1 == paused2, QStringLiteral("paused elapsedMs() is bit-identical across a poll (%1 vs %2)").arg(paused1).arg(paused2));
                     }});

    steps.push_back({1200, [&] {
                         paused3 = tracker.elapsedMs();
                         check(paused3 == paused1,
                               QStringLiteral("paused elapsedMs() still bit-identical after another poll (%1 vs %2)").arg(paused1).arg(paused3));
                     }});

    steps.push_back({0, [&] {
                         fake.seekToUs(60000000);
                     }});

    steps.push_back({250, [&] {
                         afterSeek = tracker.elapsedMs();
                         check(afterSeek == 60000, QStringLiteral("seek while paused sets elapsedMs() exactly (%1 vs 60000)").arg(afterSeek));
                         check(afterSeek != paused3, "seek while paused actually changed the frozen position");
                     }});

    steps.push_back({0, [&] {
                         fake.resume();
                     }});

    steps.push_back({200, [&] {
                         resumeA = tracker.elapsedMs();
                         check(resumeA >= 60000, QStringLiteral("resume starts from the paused position, not behind (%1 >= 60000)").arg(resumeA));
                         check(resumeA <= 60000 + 1500, QStringLiteral("resume does not jump ahead (%1 <= 61500)").arg(resumeA));
                     }});

    steps.push_back({600, [&] {
                         resumeB = tracker.elapsedMs();
                         check(resumeB > resumeA, QStringLiteral("elapsedMs() advances again after resume (%1 -> %2)").arg(resumeA).arg(resumeB));
                     }});

    steps.push_back({0, [&] {
                         fake.stop();
                     }});

    steps.push_back({250, [&] {
                         check(tracker.status() == PlaybackStatus::Stopped, "status is Stopped");
                         check(tracker.elapsedMs() == 0, "stopped elapsedMs() is exactly 0");
                     }});

    steps.push_back({0, [&] {
                         bus.unregisterService(serviceName);
                     }});

    steps.push_back({400, [&] {
                         check(!tracker.hasPlayer(), "player disconnect is observed");
                         check(tracker.elapsedMs() == 0, "no player -> elapsedMs() == 0");
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
    out << (g_failures == 0 ? QStringLiteral("PASS") : QStringLiteral("FAIL")) << QStringLiteral(": player clock (") << g_failures
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

#include "playertracker_clock_test.moc"
