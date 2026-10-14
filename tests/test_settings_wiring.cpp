// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 KShadowLrc Contributors
//
// Offline checks for the settings -> controller / tracker wiring:
//   - a settings value drives the controller's display mode and upcoming-line
//     count;
//   - disabling a provider is respected by the engine;
//   - the lyric offset round-trips between tracker and settings without
//     oscillating (each direction emits exactly once per change).
//
// XDG_CONFIG_HOME is pointed at a temporary directory before the first config
// is opened, so the developer's real kshadowlrcrc is never touched. No network,
// no GUI.

#include "config/settings.h"
#include "lyrics/controller.h"
#include "lyrics/lyricengine.h"
#include "player/playertracker.h"

#include <QCoreApplication>
#include <QTemporaryDir>

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

/*! Names of the providers currently handed to the engine. */
QStringList providerNames(LyricsController &controller)
{
    QStringList names;
    const QList<LyricProvider *> providers = controller.engine()->providers();
    for (const LyricProvider *provider : providers) {
        names.append(provider->name());
    }
    return names;
}

} // namespace

int main(int argc, char **argv)
{
    QTemporaryDir tempDir;
    if (!tempDir.isValid()) {
        std::fprintf(stderr, "FAIL: could not create a temporary config directory\n");
        return 1;
    }
    // Must happen before QCoreApplication/KSharedConfig look at the paths.
    qputenv("XDG_CONFIG_HOME", tempDir.path().toUtf8());
    qputenv("XDG_CONFIG_DIRS", tempDir.path().toUtf8());

    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("kshadowlrc-settings-wiring-test"));

    std::unique_ptr<KShadowLrcSettings> settings(createSettings());
    LyricsController controller;
    PlayerTracker tracker;
    controller.setPlayerTracker(&tracker);
    controller.setSettings(settings.get());

    int trackerOffsetEmits = 0;
    int settingsOffsetEmits = 0;
    QObject::connect(&tracker, &PlayerTracker::lyricOffsetChanged, &app, [&trackerOffsetEmits] {
        ++trackerOffsetEmits;
    });
    QObject::connect(settings.get(), &KShadowLrcSettings::lyricOffsetMsChanged, &app, [&settingsOffsetEmits] {
        ++settingsOffsetEmits;
    });

    // --- defaults read from the (empty) config file -------------------------
    CHECK(controller.displayMode() == 2); // Both
    CHECK(providerNames(controller) == QStringList({QStringLiteral("netease"), QStringLiteral("lrclib")}));
    CHECK(settings->lyricOffsetMs() == 0);
    CHECK(trackerOffsetEmits == 0); // wiring startup does not emit
    CHECK(settingsOffsetEmits == 0);

    // --- displayMode drives the controller ----------------------------------
    settings->setDisplayMode(KShadowLrcSettings::EnumDisplayMode::Original);
    CHECK(controller.displayMode() == 0);
    settings->setDisplayMode(KShadowLrcSettings::EnumDisplayMode::Translation);
    CHECK(controller.displayMode() == 1);
    settings->setDisplayMode(KShadowLrcSettings::EnumDisplayMode::Both);
    CHECK(controller.displayMode() == 2);
    std::printf("runtime: settings.displayMode Original/Translation/Both -> controller.displayMode() 0/1/2 (ok)\n");

    // --- visibleLines drives the upcoming-line count ------------------------
    controller.setTestMode(); // 5 demo lines; the current line is line 0
    settings->setVisibleLines(3);
    CHECK(controller.upcomingLines().size() == 3);
    std::printf("runtime: settings.visibleLines -> %d, controller.upcomingLines().size() = %lld\n",
                settings->visibleLines(),
                static_cast<long long>(controller.upcomingLines().size()));
    settings->setVisibleLines(1);
    CHECK(controller.upcomingLines().size() == 1);
    std::printf("runtime: settings.visibleLines -> %d, controller.upcomingLines().size() = %lld\n",
                settings->visibleLines(),
                static_cast<long long>(controller.upcomingLines().size()));
    settings->setVisibleLines(10); // clamped; only 4 lines remain after line 0
    CHECK(controller.upcomingLines().size() == 4);
    std::printf("runtime: settings.visibleLines -> %d (clamped), controller.upcomingLines().size() = %lld\n",
                settings->visibleLines(),
                static_cast<long long>(controller.upcomingLines().size()));

    // --- provider switches are respected ------------------------------------
    settings->setNeteaseEnabled(false);
    CHECK(providerNames(controller) == QStringList({QStringLiteral("lrclib")}));
    std::printf("runtime: settings.neteaseEnabled -> false, engine providers = [%s]\n",
                providerNames(controller).join(QStringLiteral(", ")).toUtf8().constData());
    settings->setLrclibEnabled(false);
    CHECK(providerNames(controller).isEmpty());
    std::printf("runtime: settings.lrclibEnabled -> false, engine providers = [%s]\n",
                providerNames(controller).join(QStringLiteral(", ")).toUtf8().constData());
    settings->setNeteaseEnabled(true);
    CHECK(providerNames(controller) == QStringList({QStringLiteral("netease")}));
    settings->setLrclibEnabled(true);
    CHECK(providerNames(controller) == QStringList({QStringLiteral("netease"), QStringLiteral("lrclib")}));
    std::printf("runtime: both providers re-enabled, engine providers = [%s]\n", providerNames(controller).join(QStringLiteral(", ")).toUtf8().constData());

    // --- offset round-trip, each direction exactly once ---------------------
    tracker.setLyricOffsetMs(500);
    QCoreApplication::processEvents();
    CHECK(trackerOffsetEmits == 1);
    CHECK(settingsOffsetEmits == 1);
    CHECK(settings->lyricOffsetMs() == 500);
    CHECK(controller.engine()->lyricOffsetMs() == 500);
    std::printf(
        "runtime: tracker.lyricOffsetMs=500 -> settings=%d engine=%lld "
        "(tracker emits=%d, settings emits=%d)\n",
        settings->lyricOffsetMs(),
        static_cast<long long>(controller.engine()->lyricOffsetMs()),
        trackerOffsetEmits,
        settingsOffsetEmits);

    settings->setLyricOffsetMs(700);
    QCoreApplication::processEvents();
    CHECK(trackerOffsetEmits == 2);
    CHECK(settingsOffsetEmits == 2);
    CHECK(tracker.lyricOffsetMs() == 700);
    CHECK(controller.engine()->lyricOffsetMs() == 700);
    std::printf(
        "runtime: settings.lyricOffsetMs=700 -> tracker=%lld engine=%lld "
        "(tracker emits=%d, settings emits=%d)\n",
        static_cast<long long>(tracker.lyricOffsetMs()),
        static_cast<long long>(controller.engine()->lyricOffsetMs()),
        trackerOffsetEmits,
        settingsOffsetEmits);

    // Re-writing the same value must not emit anything: that is the proof the
    // two-way connection cannot spin.
    tracker.setLyricOffsetMs(700);
    settings->setLyricOffsetMs(700);
    QCoreApplication::processEvents();
    CHECK(trackerOffsetEmits == 2);
    CHECK(settingsOffsetEmits == 2);
    std::printf(
        "runtime: rewriting the same offset 700 emits nothing "
        "(tracker emits=%d, settings emits=%d) -> no oscillation\n",
        trackerOffsetEmits,
        settingsOffsetEmits);

    if (g_failures == 0) {
        std::printf("test_settings_wiring: all checks passed\n");
        return 0;
    }
    std::fprintf(stderr, "test_settings_wiring: %d check(s) failed\n", g_failures);
    return 1;
}
