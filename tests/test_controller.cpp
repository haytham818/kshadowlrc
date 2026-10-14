// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 KShadowLrc Contributors
//
// Offline checks for LyricsController's display gate and text selection: the
// "no player => no text" rule, the demo path, and how the current/upcoming line
// and their translation follow LyricDisplayMode. No network, no player, no GUI.

#include "lyrics/controller.h"
#include "lyrics/lyricengine.h"
#include "player/playertracker.h"

#include <QCoreApplication>
#include <QStandardPaths>

#include <cstdio>

using namespace kshadowlrc;

static int g_failures = 0;

#define CHECK(cond)                                                                                                                                            \
    do {                                                                                                                                                       \
        if (!(cond)) {                                                                                                                                         \
            ++g_failures;                                                                                                                                      \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                                                               \
        }                                                                                                                                                      \
    } while (0)

/*! Nothing is playing and no demo is on: the overlay must show nothing. */
static void testNoPlayerNoText()
{
    LyricsController controller;
    CHECK(controller.currentText().isEmpty());
    CHECK(controller.translationText().isEmpty());
    CHECK(controller.upcomingLines().isEmpty());
    CHECK(controller.state() == 0); // idle, not "not found"

    // The engine is empty too, so there is nothing stale waiting to leak out.
    CHECK(controller.engine()->lineCount() == 0);
}

/*! A tracker that has found no player is equivalent to no player at all. */
static void testTrackerWithoutPlayer()
{
    LyricsController controller;
    PlayerTracker tracker; // constructed but not started(): never scans
    controller.setPlayerTracker(&tracker);

    CHECK(!tracker.hasPlayer());
    CHECK(controller.currentText().isEmpty());
    CHECK(controller.translationText().isEmpty());
    CHECK(controller.upcomingLines().isEmpty());
    CHECK(controller.state() == 0);
}

/*! The demo path still works standalone and its text follows displayMode. */
static void testDemoSelection()
{
    LyricsController controller;
    controller.setTestMode();

    CHECK(controller.displayMode() == 2); // Both by default
    CHECK(controller.currentText() == QStringLiteral("Yesterday, all my troubles seemed so far away"));
    CHECK(controller.translationText() == QStringLiteral("昨日，一切烦恼都离我远去"));
    // The next origin line, regardless of the translation shown under the current.
    CHECK(controller.upcomingLines() == QStringList({QStringLiteral("Now it looks as though they're here to stay")}));

    // Move the clock to the second line.
    controller.setElapsed(6000);
    CHECK(controller.currentText() == QStringLiteral("Now it looks as though they're here to stay"));
    CHECK(controller.upcomingLines() == QStringList({QStringLiteral("Oh, I believe in yesterday")}));

    // Original only: no translation anywhere.
    controller.setDisplayMode(0);
    CHECK(controller.currentText() == QStringLiteral("Now it looks as though they're here to stay"));
    CHECK(controller.translationText().isEmpty());
    CHECK(controller.upcomingLines() == QStringList({QStringLiteral("Oh, I believe in yesterday")}));

    // Translation only: the translation becomes the primary text, current and next.
    controller.setDisplayMode(1);
    CHECK(controller.currentText() == QStringLiteral("如今它们却似要长驻于此"));
    CHECK(controller.translationText().isEmpty());
    CHECK(controller.upcomingLines() == QStringList({QStringLiteral("哦，我相信昨日")}));

    // Both again.
    controller.setDisplayMode(2);
    CHECK(controller.currentText() == QStringLiteral("Now it looks as though they're here to stay"));
    CHECK(controller.translationText() == QStringLiteral("如今它们却似要长驻于此"));
}

/*!
 * The heart of the integration brief: a loaded lyric is not shown unless a demo
 * or a live (non-stopped) player is driving the overlay.
 */
static void testLoadedLyricSuppressedWithoutDriver()
{
    LyricsController controller;
    controller.setTestMode();
    CHECK(!controller.currentText().isEmpty());
    CHECK(controller.engine()->lineCount() == 5); // the demo lyric really is loaded

    controller.stopDemo();
    // The engine still holds the lyric, but nothing is driving it: show nothing.
    CHECK(controller.engine()->lineCount() == 5);
    CHECK(controller.currentText().isEmpty());
    CHECK(controller.translationText().isEmpty());
    CHECK(controller.upcomingLines().isEmpty());
    CHECK(controller.state() == 0);
}

/*! Detaching a tracker returns the controller to the "nothing to show" state. */
static void testDetachTracker()
{
    LyricsController controller;
    PlayerTracker tracker;
    controller.setPlayerTracker(&tracker);
    controller.setPlayerTracker(nullptr);
    CHECK(controller.currentText().isEmpty());
    CHECK(controller.state() == 0);
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("kshadowlrc"));
    QStandardPaths::setTestModeEnabled(true);

    testNoPlayerNoText();
    testTrackerWithoutPlayer();
    testDemoSelection();
    testLoadedLyricSuppressedWithoutDriver();
    testDetachTracker();

    if (g_failures == 0) {
        std::printf("test_controller: all checks passed\n");
        return 0;
    }
    std::fprintf(stderr, "test_controller: %d check(s) failed\n", g_failures);
    return 1;
}
