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
//   - setOverlayVisible() re-labels the toggle action.
//
// KGlobalAccel's daemon is deliberately not asserted on: a bare
// `dbus-run-session` has no kglobalacceld, so what it reports there says
// nothing. Like the other bus tests, this re-executes itself under
// `dbus-run-session` so no real session bus is involved, and points
// XDG_CONFIG_HOME/XDG_CONFIG_DIRS at a private temporary directory so neither
// KGlobalAccel nor the tray can write into the real config. A
// KStatusNotifierItem needs a bus and a QApplication, so the platform plugin is
// the offscreen one.

#include "config/settings.h"
#include "lyrics/controller.h"
#include "system/controlservice.h"

#include <KStatusNotifierItem>

#include <QAction>
#include <QApplication>
#include <QCoreApplication>
#include <QMenu>
#include <QProcess>
#include <QStandardPaths>
#include <QString>
#include <QTemporaryDir>
#include <QTextStream>

#include <cstdio>
#include <memory>

using namespace kshadowlrc;

namespace
{

int g_failures = 0;

#define CHECK(cond)                                                                                                                                            \
    do {                                                                                                                                                       \
        if (!(cond)) {                                                                                                                                         \
            ++g_failures;                                                                                                                                      \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                                                               \
        }                                                                                                                                                      \
    } while (0)

const QString kEarlier = QStringLiteral("lyrics-offset-earlier");
const QString kLater = QStringLiteral("lyrics-offset-later");
const QString kReset = QStringLiteral("lyrics-offset-reset");
const QString kToggle = QStringLiteral("toggle-overlay");
const QString kSettings = QStringLiteral("settings");
const QString kQuit = QStringLiteral("quit");

int runInner()
{
    QCoreApplication::setApplicationName(QStringLiteral(KSHADOWLRC_APP_ID));

    std::unique_ptr<KShadowLrcSettings> settings(createSettings());
    LyricsController controller;
    controller.setSettings(settings.get());

    ControlService service(&controller);

    // 1. Every documented action exists and is the live one; an unknown id is
    //    null rather than a dangling pointer.
    QAction *earlier = service.action(kEarlier);
    QAction *later = service.action(kLater);
    QAction *reset = service.action(kReset);
    QAction *toggle = service.action(kToggle);
    QAction *settingsAction = service.action(kSettings);
    QAction *quit = service.action(kQuit);
    CHECK(earlier != nullptr);
    CHECK(later != nullptr);
    CHECK(reset != nullptr);
    CHECK(toggle != nullptr);
    CHECK(settingsAction != nullptr);
    CHECK(quit != nullptr);
    CHECK(service.action(QStringLiteral("nope")) == nullptr);
    CHECK(earlier->objectName() == kEarlier);
    CHECK(toggle->isCheckable());

    // The tray is a child of the service; finding it is how the test reaches
    // the menu and the tool tip without widening the public interface.
    auto *tray = service.findChild<KStatusNotifierItem *>();
    CHECK(tray != nullptr);

    // 2. A single step moves the offset by exactly one step and the opposite
    //    action undoes it.
    CHECK(controller.lyricOffset() == 0);
    earlier->trigger();
    CHECK(controller.lyricOffset() == -ControlService::OffsetStepMs);
    later->trigger();
    CHECK(controller.lyricOffset() == 0);

    // The same value lands in the settings object, so it persists.
    earlier->trigger();
    CHECK(settings->lyricOffsetMs() == -ControlService::OffsetStepMs);
    later->trigger();
    CHECK(settings->lyricOffsetMs() == 0);

    // 3. Repeated triggering clamps at both limits and never goes past them.
    const int stepsToLimit = ControlService::MaxOffsetMs / ControlService::OffsetStepMs;
    for (int i = 0; i < stepsToLimit + 5; ++i) {
        earlier->trigger();
    }
    CHECK(controller.lyricOffset() == -ControlService::MaxOffsetMs);
    CHECK(settings->lyricOffsetMs() == -ControlService::MaxOffsetMs);

    for (int i = 0; i < 2 * stepsToLimit + 5; ++i) {
        later->trigger();
    }
    CHECK(controller.lyricOffset() == ControlService::MaxOffsetMs);
    for (int i = 0; i < 5; ++i) {
        later->trigger();
    }
    CHECK(controller.lyricOffset() == ControlService::MaxOffsetMs);

    // 4. Reset returns to zero through both the action and the slot, and
    //    persists.
    reset->trigger();
    CHECK(controller.lyricOffset() == 0);
    CHECK(settings->lyricOffsetMs() == 0);
    earlier->trigger();
    CHECK(controller.lyricOffset() == -ControlService::OffsetStepMs);
    service.resetOffset();
    CHECK(controller.lyricOffset() == 0);
    CHECK(settings->lyricOffsetMs() == 0);

    // 5. The direct slots take the same clamped path as the actions.
    service.offsetLater();
    CHECK(controller.lyricOffset() == ControlService::OffsetStepMs);
    CHECK(settings->lyricOffsetMs() == ControlService::OffsetStepMs);
    service.offsetEarlier();
    CHECK(controller.lyricOffset() == 0);
    CHECK(settings->lyricOffsetMs() == 0);

    // 6. Each request signal fires exactly once per trigger.
    int settingsCount = 0;
    int quitCount = 0;
    int toggleCount = 0;
    QObject::connect(&service, &ControlService::settingsRequested, &service, [&settingsCount] {
        ++settingsCount;
    });
    QObject::connect(&service, &ControlService::quitRequested, &service, [&quitCount] {
        ++quitCount;
    });
    QObject::connect(&service, &ControlService::overlayVisibilityToggleRequested, &service, [&toggleCount] {
        ++toggleCount;
    });

    settingsAction->trigger();
    CHECK(settingsCount == 1);
    quit->trigger();
    CHECK(quitCount == 1);
    toggle->trigger();
    CHECK(toggleCount == 1);

    // 7. setOverlayVisible() drives the toggle action's readable state.
    service.setOverlayVisible(false);
    CHECK(!toggle->isChecked());
    CHECK(toggle->text() == QStringLiteral("Show Lyrics"));
    service.setOverlayVisible(true);
    CHECK(toggle->isChecked());
    CHECK(toggle->text() == QStringLiteral("Hide Lyrics"));

    // 8. The tray's own objects. Creating the item needs a bus but not a
    //    StatusNotifier host, so even on the private bus the menu and the tool
    //    tip are readable here.
    //
    //    The tool tip subtitle tracks the offset, including a negative value,
    //    and returns to zero on reset.
    if (tray) {
        reset->trigger();
        CHECK(tray->toolTipSubTitle() == QStringLiteral("Offset: 0"));
        earlier->trigger();
        CHECK(controller.lyricOffset() == -ControlService::OffsetStepMs);
        CHECK(tray->toolTipSubTitle() == QStringLiteral("Offset: -0.5 s"));
        earlier->trigger();
        earlier->trigger();
        CHECK(controller.lyricOffset() == -3 * ControlService::OffsetStepMs);
        CHECK(tray->toolTipSubTitle() == QStringLiteral("Offset: -1.5 s"));
        reset->trigger();
        CHECK(controller.lyricOffset() == 0);
        CHECK(tray->toolTipSubTitle() == QStringLiteral("Offset: 0"));

        //    The context menu holds exactly the six documented actions, in order,
        //    with the three separators between the groups -- and those six entries
        //    are the very QAction instances action() returns, which is what "the
        //    menu and the shortcuts share one action" means.
        QMenu *menu = tray->contextMenu();
        CHECK(menu != nullptr);
        const QList<QAction *> entries = menu ? menu->actions() : QList<QAction *>();
        CHECK(entries.size() == 9);
        if (entries.size() == 9) {
            CHECK(entries.at(0) == toggle);
            CHECK(entries.at(1)->isSeparator());
            CHECK(entries.at(2) == earlier);
            CHECK(entries.at(3) == later);
            CHECK(entries.at(4) == reset);
            CHECK(entries.at(5)->isSeparator());
            CHECK(entries.at(6) == settingsAction);
            CHECK(entries.at(7)->isSeparator());
            CHECK(entries.at(8) == quit);
        }
    }

    // 9. The conflict helper. A key held only by our own component is not a
    //    conflict: globalShortcutsByKey() reports our own registration as a
    //    holder, which is exactly what made isGlobalShortcutAvailable() lie.
    const QString ourComponent = QStringLiteral(KSHADOWLRC_APP_ID);
    CHECK(!shortcutHeldByAnotherComponent({}, ourComponent));
    CHECK(!shortcutHeldByAnotherComponent({ourComponent}, ourComponent));
    CHECK(shortcutHeldByAnotherComponent({ourComponent, QStringLiteral("kwin")}, ourComponent));
    CHECK(shortcutHeldByAnotherComponent({QStringLiteral("kwin")}, ourComponent));

    if (g_failures == 0) {
        std::printf(
            "controlservice_test: all checks passed (actions, offset step/clamp/reset, signals, overlay toggle, tray tool tip + shared menu,"
            " conflict helper)\n");
        return 0;
    }
    std::fprintf(stderr, "controlservice_test: %d check(s) failed\n", g_failures);
    return 1;
}

} // namespace

int main(int argc, char **argv)
{
    // Must be set before the QApplication; a KStatusNotifierItem needs a
    // QApplication but there is no screen in a test.
    qputenv("QT_QPA_PLATFORM", "offscreen");

    const bool inner = argc > 1 && QString::fromLocal8Bit(argv[1]) == QLatin1String("--inner");
    if (inner) {
        // Before anything can open a config: keep KGlobalAccel and the tray out
        // of the developer's real kshadowlrcrc and kglobalshortcutsrc.
        QTemporaryDir tempDir;
        if (!tempDir.isValid()) {
            std::fprintf(stderr, "FAIL: could not create a temporary config directory\n");
            return 1;
        }
        qputenv("XDG_CONFIG_HOME", tempDir.path().toUtf8());
        qputenv("XDG_CONFIG_DIRS", tempDir.path().toUtf8());

        QApplication app(argc, argv);
        return runInner();
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
