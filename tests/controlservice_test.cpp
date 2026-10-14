// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 KShadowLrc Contributors

// Offline checks for ControlService: the tray icon plus the global shortcuts
// that nudge the lyric offset, and the show/hide, settings and quit requests.
//
// It proves, without any GUI and without touching the developer's desktop:
//   - every documented action resolves, and an unknown id is null;
//   - the offset step/clamp/reset rules through the very actions the menu and
//     the shortcuts trigger, and that the value is persisted in the settings;
//   - the settings/quit/overlay-toggle requests fire exactly once each;
//   - setOverlayVisible() re-labels the toggle action;
//   - the tray tool tip carries the lyric state above the offset, follows every
//     state change through the controller, and puts the failure diagnostic on
//     its own line;
//   - lyricstatus::statusText() maps each of the five controller states to its
//     agreed sentence: the one home for the wording, which both the tool tip
//     and the settings window go through.
//
// KGlobalAccel's daemon is deliberately not asserted on: a bare
// `dbus-run-session` has no kglobalacceld, so what it reports there says
// nothing. Like the other bus tests, this re-executes itself under
// `dbus-run-session` so no real session bus is involved, and points
// XDG_CONFIG_HOME/XDG_CONFIG_DIRS/XDG_CACHE_HOME at a private temporary
// directory so neither KGlobalAccel, the tray nor the lyric cache can write into
// the developer's real config. A KStatusNotifierItem needs a bus and a
// QApplication, so the platform plugin is the offscreen one.

#include "config/settings.h"
#include "lyrics/controller.h"
#include "lyrics/lyricstatus.h"
#include "lyrics/lyrictypes.h"
#include "system/controlservice.h"

#include <KStatusNotifierItem>

#include <QAction>
#include <QApplication>
#include <QCoreApplication>
#include <QMenu>
#include <QProcess>
#include <QStandardPaths>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QTest>
#include <QTextStream>

#include <cstdio>
#include <memory>

using namespace kshadowlrc;

namespace
{

const QString kEarlier = QStringLiteral("lyrics-offset-earlier");
const QString kLater = QStringLiteral("lyrics-offset-later");
const QString kReset = QStringLiteral("lyrics-offset-reset");
const QString kToggle = QStringLiteral("toggle-overlay");
const QString kSettings = QStringLiteral("settings");
const QString kQuit = QStringLiteral("quit");

/*!
 * A provider that answers only when the test tells it to, so the engine stays
 * in Loading until then. That is what lets the tool tip be walked through
 * Loading and Error without a network and without an event loop: the walk runs
 * to the first unanswered search and stops there.
 */
class SilentProvider : public LyricProvider
{
public:
    QString name() const override
    {
        return QStringLiteral("stub");
    }
    QString displayName() const override
    {
        return QStringLiteral("Stub");
    }

    void searchSong(quint64 requestId, const TrackMeta &track, int limit) override
    {
        Q_UNUSED(track)
        Q_UNUSED(limit)
        m_searchId = requestId;
    }

    void queryLyric(quint64 requestId, const QString &songId) override
    {
        Q_UNUSED(songId)
        m_queryId = requestId;
    }

    void failSearch(const QString &error)
    {
        Q_EMIT searchFailed(m_searchId, error);
    }

    quint64 m_searchId = 0;
    quint64 m_queryId = 0;
};

/*! A track the engine will walk for; nothing here comes from a real file. */
TrackMeta toolTipTrack()
{
    TrackMeta track;
    track.title = QStringLiteral("ControlService Tool Tip Song");
    track.album = QStringLiteral("ControlService Test Album");
    track.artists = {QStringLiteral("ControlService Test Artist")};
    track.lengthMs = 200000;
    track.uniqueSongId = QStringLiteral("file:///tmp/kshadowlrc-controlservice-test.mp3");
    return track;
}

} // namespace

class ControlServiceTest : public QObject
{
    Q_OBJECT

public:
    ControlServiceTest();

private Q_SLOTS:
    void actionsResolve();
    void offsetStepsAndUndoes();
    void offsetClampsAtBothLimits();
    void resetReturnsToZero();
    void directSlotsTakeTheClampedPath();
    void requestsFireExactlyOnce();
    void overlayVisibilityRelabelsTheToggle();
    void trayToolTipAndSharedMenu();
    void trayToolTipFollowsLyricState();
    void statusTextPinsEachState();
    void conflictHelperIgnoresOurOwnRegistration();

private:
    std::unique_ptr<KShadowLrcSettings> m_settings;
    LyricsController m_controller;
    ControlService m_service;

    QAction *m_earlier = nullptr;
    QAction *m_later = nullptr;
    QAction *m_reset = nullptr;
    QAction *m_toggle = nullptr;
    QAction *m_settingsAction = nullptr;
    QAction *m_quit = nullptr;
    KStatusNotifierItem *m_tray = nullptr;
};

ControlServiceTest::ControlServiceTest()
    : m_settings(createSettings())
    , m_service(&m_controller)
{
    m_controller.setSettings(m_settings.get());

    m_earlier = m_service.action(kEarlier);
    m_later = m_service.action(kLater);
    m_reset = m_service.action(kReset);
    m_toggle = m_service.action(kToggle);
    m_settingsAction = m_service.action(kSettings);
    m_quit = m_service.action(kQuit);

    // The tray is a child of the service; finding it is how the test reaches
    // the menu and the tool tip without widening the public interface.
    m_tray = m_service.findChild<KStatusNotifierItem *>();
}

void ControlServiceTest::actionsResolve()
{
    // 1. Every documented action exists and is the live one; an unknown id is
    //    null rather than a dangling pointer.
    QVERIFY(m_earlier != nullptr);
    QVERIFY(m_later != nullptr);
    QVERIFY(m_reset != nullptr);
    QVERIFY(m_toggle != nullptr);
    QVERIFY(m_settingsAction != nullptr);
    QVERIFY(m_quit != nullptr);
    QVERIFY(m_service.action(QStringLiteral("nope")) == nullptr);
    QVERIFY(m_earlier->objectName() == kEarlier);
    QVERIFY(m_toggle->isCheckable());
    QVERIFY(m_tray != nullptr);
}

void ControlServiceTest::offsetStepsAndUndoes()
{
    // 2. A single step moves the offset by exactly one step and the opposite
    //    action undoes it.
    QVERIFY(m_controller.lyricOffset() == 0);
    m_earlier->trigger();
    QVERIFY(m_controller.lyricOffset() == -ControlService::OffsetStepMs);
    m_later->trigger();
    QVERIFY(m_controller.lyricOffset() == 0);

    // The same value lands in the settings object, so it persists.
    m_earlier->trigger();
    QVERIFY(m_settings->lyricOffsetMs() == -ControlService::OffsetStepMs);
    m_later->trigger();
    QVERIFY(m_settings->lyricOffsetMs() == 0);
}

void ControlServiceTest::offsetClampsAtBothLimits()
{
    // 3. Repeated triggering clamps at both limits and never goes past them.
    const int stepsToLimit = ControlService::MaxOffsetMs / ControlService::OffsetStepMs;
    for (int i = 0; i < stepsToLimit + 5; ++i) {
        m_earlier->trigger();
    }
    QVERIFY(m_controller.lyricOffset() == -ControlService::MaxOffsetMs);
    QVERIFY(m_settings->lyricOffsetMs() == -ControlService::MaxOffsetMs);

    for (int i = 0; i < 2 * stepsToLimit + 5; ++i) {
        m_later->trigger();
    }
    QVERIFY(m_controller.lyricOffset() == ControlService::MaxOffsetMs);
    for (int i = 0; i < 5; ++i) {
        m_later->trigger();
    }
    QVERIFY(m_controller.lyricOffset() == ControlService::MaxOffsetMs);
}

void ControlServiceTest::resetReturnsToZero()
{
    // 4. Reset returns to zero through both the action and the slot, and
    //    persists.
    m_reset->trigger();
    QVERIFY(m_controller.lyricOffset() == 0);
    QVERIFY(m_settings->lyricOffsetMs() == 0);
    m_earlier->trigger();
    QVERIFY(m_controller.lyricOffset() == -ControlService::OffsetStepMs);
    m_service.resetOffset();
    QVERIFY(m_controller.lyricOffset() == 0);
    QVERIFY(m_settings->lyricOffsetMs() == 0);
}

void ControlServiceTest::directSlotsTakeTheClampedPath()
{
    // 5. The direct slots take the same clamped path as the actions.
    m_service.offsetLater();
    QVERIFY(m_controller.lyricOffset() == ControlService::OffsetStepMs);
    QVERIFY(m_settings->lyricOffsetMs() == ControlService::OffsetStepMs);
    m_service.offsetEarlier();
    QVERIFY(m_controller.lyricOffset() == 0);
    QVERIFY(m_settings->lyricOffsetMs() == 0);
}

void ControlServiceTest::requestsFireExactlyOnce()
{
    // 6. Each request signal fires exactly once per trigger.
    int settingsCount = 0;
    int quitCount = 0;
    int toggleCount = 0;
    QObject::connect(&m_service, &ControlService::settingsRequested, &m_service, [&settingsCount] {
        ++settingsCount;
    });
    QObject::connect(&m_service, &ControlService::quitRequested, &m_service, [&quitCount] {
        ++quitCount;
    });
    QObject::connect(&m_service, &ControlService::overlayVisibilityToggleRequested, &m_service, [&toggleCount] {
        ++toggleCount;
    });

    m_settingsAction->trigger();
    QVERIFY(settingsCount == 1);
    m_quit->trigger();
    QVERIFY(quitCount == 1);
    m_toggle->trigger();
    QVERIFY(toggleCount == 1);
}

void ControlServiceTest::overlayVisibilityRelabelsTheToggle()
{
    // 7. setOverlayVisible() drives the toggle action's readable state.
    m_service.setOverlayVisible(false);
    QVERIFY(!m_toggle->isChecked());
    QVERIFY(m_toggle->text() == QStringLiteral("Show Lyrics"));
    m_service.setOverlayVisible(true);
    QVERIFY(m_toggle->isChecked());
    QVERIFY(m_toggle->text() == QStringLiteral("Hide Lyrics"));
}

void ControlServiceTest::trayToolTipAndSharedMenu()
{
    // 8. The tray's own objects. Creating the item needs a bus but not a
    //    StatusNotifier host, so even on the private bus the menu and the tool
    //    tip are readable here.
    if (!m_tray) {
        QFAIL("the service created no tray item");
    }

    //    The tool tip subtitle stacks the lyric state above the offset, and the
    //    offset line tracks the value including a negative one. Idle is the
    //    state here: no player and no demo, so there is nothing to show.
    m_reset->trigger();
    QVERIFY(m_tray->toolTipSubTitle() == QStringLiteral("Nothing is playing.\nOffset: 0"));
    m_earlier->trigger();
    QVERIFY(m_controller.lyricOffset() == -ControlService::OffsetStepMs);
    QVERIFY(m_tray->toolTipSubTitle() == QStringLiteral("Nothing is playing.\nOffset: -0.5 s"));
    m_earlier->trigger();
    m_earlier->trigger();
    QVERIFY(m_controller.lyricOffset() == -3 * ControlService::OffsetStepMs);
    QVERIFY(m_tray->toolTipSubTitle() == QStringLiteral("Nothing is playing.\nOffset: -1.5 s"));
    m_reset->trigger();
    QVERIFY(m_controller.lyricOffset() == 0);
    QVERIFY(m_tray->toolTipSubTitle() == QStringLiteral("Nothing is playing.\nOffset: 0"));

    //    The context menu holds exactly the six documented actions, in order,
    //    with the three separators between the groups -- and those six entries
    //    are the very QAction instances action() returns, which is what "the
    //    menu and the shortcuts share one action" means.
    QMenu *menu = m_tray->contextMenu();
    QVERIFY(menu != nullptr);
    const QList<QAction *> entries = menu ? menu->actions() : QList<QAction *>();
    QVERIFY(entries.size() == 9);
    if (entries.size() == 9) {
        QVERIFY(entries.at(0) == m_toggle);
        QVERIFY(entries.at(1)->isSeparator());
        QVERIFY(entries.at(2) == m_earlier);
        QVERIFY(entries.at(3) == m_later);
        QVERIFY(entries.at(4) == m_reset);
        QVERIFY(entries.at(5)->isSeparator());
        QVERIFY(entries.at(6) == m_settingsAction);
        QVERIFY(entries.at(7)->isSeparator());
        QVERIFY(entries.at(8) == m_quit);
    }
}

void ControlServiceTest::trayToolTipFollowsLyricState()
{
    // 9. The tool tip's first line is the lyric state, and it follows the
    //    controller: the tray is redrawn through the state and error signals,
    //    not only through the offset. Every state is reached along the
    //    controller's own path -- the demo gate for idle and found, the engine
    //    directly for the rest -- so nothing here needs MPRIS or the network.
    if (!m_tray) {
        QFAIL("the service created no tray item");
    }

    //    Idle: no player and no demo, so the state is the controller's gate 0.
    QVERIFY(m_controller.state() == 0);
    QVERIFY(m_tray->toolTipSubTitle() == QStringLiteral("Nothing is playing.\nOffset: 0"));

    //    Found: the demo lyric goes in through the controller, so the new line
    //    arrives on the controller's stateChanged signal.
    m_controller.setTestMode();
    QVERIFY(m_controller.state() == 2);
    QVERIFY(m_tray->toolTipSubTitle() == QStringLiteral("Lyrics found.\nOffset: 0"));

    //    Not found: the engine was answered with nothing at all, which is a
    //    different story from a failure and gets its own line.
    m_controller.engine()->setLyric(Lyric(), Lyric());
    QVERIFY(m_controller.state() == 3);
    QVERIFY(m_tray->toolTipSubTitle() == QStringLiteral("No lyrics for this song.\nOffset: 0"));

    //    Loading, then error: a provider that has not answered yet keeps the
    //    walk in flight, and failing it is a transport failure. The diagnostic
    //    takes a line of its own under the state, never the state's place.
    SilentProvider provider;
    m_controller.engine()->setProviders({&provider});
    m_controller.engine()->setSourceOrder({QStringLiteral("stub")});
    m_controller.engine()->setTrack(toolTipTrack());
    QVERIFY(m_controller.state() == 1);
    QVERIFY(m_tray->toolTipSubTitle() == QStringLiteral("Looking for lyrics…\nOffset: 0"));

    provider.failSearch(QStringLiteral("network unreachable"));
    QVERIFY(m_controller.state() == 4);
    QVERIFY(m_controller.lastError() == QStringLiteral("network unreachable"));
    QVERIFY(m_tray->toolTipSubTitle() == QStringLiteral("Something went wrong while looking for lyrics.\nnetwork unreachable\nOffset: 0"));

    // The provider was a stack object: take it out of the engine before it dies,
    // and park the demo so nothing later inherits a half-finished resolution.
    m_controller.engine()->setProviders({});
    m_controller.stopDemo();
}

void ControlServiceTest::statusTextPinsEachState()
{
    // 10. The one home for the wording, both entry points. stateText() is the
    //     sentence on its own -- LyricsController::lyricStatus hands it to the
    //     settings window -- and statusText() appends the failure diagnostic as
    //     a second line for the tray tool tip, which cannot style anything. Pin
    //     the whole mapping here, so a reworded or reordered state is caught at
    //     the source rather than discovered on screen.
    const QStringList sentences = {
        QStringLiteral("Nothing is playing."),
        QStringLiteral("Looking for lyrics…"),
        QStringLiteral("Lyrics found."),
        QStringLiteral("No lyrics for this song."),
        QStringLiteral("Something went wrong while looking for lyrics."),
    };
    for (int state = 0; state < sentences.size(); ++state) {
        QCOMPARE(lyricstatus::stateText(state), sentences.at(state));
        QCOMPARE(lyricstatus::statusText(state, QString()), sentences.at(state));
    }

    // statusText() adds the diagnostic as a second line, and only for a failure:
    // a successful state carries no second line even when an error is handed in.
    QCOMPARE(lyricstatus::statusText(4, QStringLiteral("network unreachable")),
             QStringLiteral("Something went wrong while looking for lyrics.\nnetwork unreachable"));
    QCOMPARE(lyricstatus::statusText(2, QStringLiteral("network unreachable")), QStringLiteral("Lyrics found."));
}

void ControlServiceTest::conflictHelperIgnoresOurOwnRegistration()
{
    // 11. The conflict helper. A key held only by our own component is not a
    //    conflict: globalShortcutsByKey() reports our own registration as a
    //    holder, which is exactly what made isGlobalShortcutAvailable() lie.
    const QString ourComponent = QStringLiteral(KSHADOWLRC_APP_ID);
    QVERIFY(!shortcutHeldByAnotherComponent({}, ourComponent));
    QVERIFY(!shortcutHeldByAnotherComponent({ourComponent}, ourComponent));
    QVERIFY(shortcutHeldByAnotherComponent({ourComponent, QStringLiteral("kwin")}, ourComponent));
    QVERIFY(shortcutHeldByAnotherComponent({QStringLiteral("kwin")}, ourComponent));
}

int main(int argc, char **argv)
{
    // Must be set before the QApplication; a KStatusNotifierItem needs a
    // QApplication but there is no screen in a test.
    qputenv("QT_QPA_PLATFORM", "offscreen");

    const bool inner = argc > 1 && QString::fromLocal8Bit(argv[1]) == QLatin1String("--inner");
    if (inner) {
        // Before anything can open a config: keep KGlobalAccel and the tray out
        // of the developer's real kshadowlrcrc and kglobalshortcutsrc, and keep
        // the lyric cache (QStandardPaths::CacheLocation) out of their cache.
        QTemporaryDir tempDir;
        if (!tempDir.isValid()) {
            std::fprintf(stderr, "FAIL: could not create a temporary config directory\n");
            return 1;
        }
        qputenv("XDG_CONFIG_HOME", tempDir.path().toUtf8());
        qputenv("XDG_CONFIG_DIRS", tempDir.path().toUtf8());
        qputenv("XDG_CACHE_HOME", tempDir.path().toUtf8());

        QApplication app(argc, argv);
        // The application id is what lets the conflict helper tell our own
        // shortcut registration from somebody else's.
        QCoreApplication::setApplicationName(QStringLiteral(KSHADOWLRC_APP_ID));

        // QTest::qExec parses argv and would reject the --inner marker the
        // outer process added, so drop it (always argv[1]) in place first.
        for (int i = 1; i + 1 < argc; ++i) {
            argv[i] = argv[i + 1];
        }
        --argc;

        ControlServiceTest tc;
        return QTest::qExec(&tc, argc, argv);
    }

    QCoreApplication app(argc, argv);

    // Not yet on a private bus: re-exec under dbus-run-session so no real
    // status-notifier host or global shortcut daemon can interfere.
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

#include "controlservice_test.moc"
