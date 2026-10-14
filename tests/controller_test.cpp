// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 KShadowLrc Contributors
//
// Offline checks for LyricsController's display gate and text selection: the
// "no player => no text" rule, the demo path, and how the current/upcoming line
// and their translation follow LyricDisplayMode. No network, no player, no GUI.

#include "lyrics/controller.h"
#include "lyrics/lyricengine.h"
#include "player/playertracker.h"

#include <QStandardPaths>
#include <QTest>

using namespace kshadowlrc;

class ControllerTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void noPlayerNoText();
    void trackerWithoutPlayer();
    void demoSelection();
    void loadedLyricSuppressedWithoutDriver();
    void detachTracker();
};

void ControllerTest::initTestCase()
{
    QCoreApplication::setApplicationName(QStringLiteral("kshadowlrc"));
    QStandardPaths::setTestModeEnabled(true);
}

/*! Nothing is playing and no demo is on: the overlay must show nothing. */
void ControllerTest::noPlayerNoText()
{
    LyricsController controller;
    QVERIFY(controller.currentText().isEmpty());
    QVERIFY(controller.translationText().isEmpty());
    QVERIFY(controller.upcomingLines().isEmpty());
    QVERIFY(controller.state() == 0); // idle, not "not found"

    // The engine is empty too, so there is nothing stale waiting to leak out.
    QVERIFY(controller.engine()->lineCount() == 0);
}

/*! A tracker that has found no player is equivalent to no player at all. */
void ControllerTest::trackerWithoutPlayer()
{
    LyricsController controller;
    PlayerTracker tracker; // constructed but not started(): never scans
    controller.setPlayerTracker(&tracker);

    QVERIFY(!tracker.hasPlayer());
    QVERIFY(controller.currentText().isEmpty());
    QVERIFY(controller.translationText().isEmpty());
    QVERIFY(controller.upcomingLines().isEmpty());
    QVERIFY(controller.state() == 0);
}

/*! The demo path still works standalone and its text follows displayMode. */
void ControllerTest::demoSelection()
{
    LyricsController controller;
    controller.setTestMode();

    QVERIFY(controller.displayMode() == 2); // Both by default
    QVERIFY(controller.currentText() == QStringLiteral("Yesterday, all my troubles seemed so far away"));
    QVERIFY(controller.translationText() == QStringLiteral("昨日，一切烦恼都离我远去"));
    // The next origin line, regardless of the translation shown under the current.
    QVERIFY(controller.upcomingLines() == QStringList({QStringLiteral("Now it looks as though they're here to stay")}));

    // Move the clock to the second line.
    controller.setElapsed(6000);
    QVERIFY(controller.currentText() == QStringLiteral("Now it looks as though they're here to stay"));
    QVERIFY(controller.upcomingLines() == QStringList({QStringLiteral("Oh, I believe in yesterday")}));

    // Original only: no translation anywhere.
    controller.setDisplayMode(0);
    QVERIFY(controller.currentText() == QStringLiteral("Now it looks as though they're here to stay"));
    QVERIFY(controller.translationText().isEmpty());
    QVERIFY(controller.upcomingLines() == QStringList({QStringLiteral("Oh, I believe in yesterday")}));

    // Translation only: the translation becomes the primary text, current and next.
    controller.setDisplayMode(1);
    QVERIFY(controller.currentText() == QStringLiteral("如今它们却似要长驻于此"));
    QVERIFY(controller.translationText().isEmpty());
    QVERIFY(controller.upcomingLines() == QStringList({QStringLiteral("哦，我相信昨日")}));

    // Both again.
    controller.setDisplayMode(2);
    QVERIFY(controller.currentText() == QStringLiteral("Now it looks as though they're here to stay"));
    QVERIFY(controller.translationText() == QStringLiteral("如今它们却似要长驻于此"));
}

/*!
 * The heart of the integration brief: a loaded lyric is not shown unless a demo
 * or a live (non-stopped) player is driving the overlay.
 */
void ControllerTest::loadedLyricSuppressedWithoutDriver()
{
    LyricsController controller;
    controller.setTestMode();
    QVERIFY(!controller.currentText().isEmpty());
    QVERIFY(controller.engine()->lineCount() == 5); // the demo lyric really is loaded

    controller.stopDemo();
    // The engine still holds the lyric, but nothing is driving it: show nothing.
    QVERIFY(controller.engine()->lineCount() == 5);
    QVERIFY(controller.currentText().isEmpty());
    QVERIFY(controller.translationText().isEmpty());
    QVERIFY(controller.upcomingLines().isEmpty());
    QVERIFY(controller.state() == 0);
}

/*! Detaching a tracker returns the controller to the "nothing to show" state. */
void ControllerTest::detachTracker()
{
    LyricsController controller;
    PlayerTracker tracker;
    controller.setPlayerTracker(&tracker);
    controller.setPlayerTracker(nullptr);
    QVERIFY(controller.currentText().isEmpty());
    QVERIFY(controller.state() == 0);
}

QTEST_GUILESS_MAIN(ControllerTest)

#include "controller_test.moc"
