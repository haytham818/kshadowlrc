// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 KShadowLrc Contributors
//
// Offline checks for OverlayWindow's adjusting mode.
//
// The window is constructed with no compositor (the offscreen platform): there
// is no layer surface to position, so the assertions are all against the
// window's own members and getters -- the input region, the requested keyboard
// interactivity, the placement point and the surface size it last pushed --
// never against anything a compositor would do. The layer-shell state the window
// pushes (anchors, margins) is read back from its LayerShellQt::Window instead.
// A commit is not: commitSurface() returns early while the window is unexposed,
// and offscreen it never is, so every commit is skipped here. XDG_CONFIG_HOME
// points at a private temporary directory so the settings writes under test
// cannot reach the developer's real kshadowlrcrc.
//
// The offscreen cursor cannot move, so a drag through the public API is always
// motionless here: that is what the "a motionless click emits nothing" checks
// rely on. There is also no screen, so the release has nothing to magnetise
// against and keeps the live point; the live point a real drag would apply is
// set directly through setPlacement() where a committed edit is under test.

#include "config/settings.h"
#include "platform/overlaywindow.h"

#include <QGuiApplication>
#include <QTemporaryDir>
#include <QTest>

#include <cstdio>
#include <memory>

using namespace kshadowlrc;

namespace
{

using Settings = KShadowLrcSettings;

} // namespace

class OverlayWindowAdjustTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void enteringOverridesClickThroughAndKeyboard();
    void leavingRestoresTheConfiguredState();
    void adjustingKeepsTheTextBoxAndItsPlacement();
    void cancelledAdjustingWritesNoSettings();
    void motionlessClickEmitsNothing();
    void releasedDragWritesThePlacementOnce();
    void frozenSurfaceDuringPositionDrag();
    void dragPollLivesExactlyAsLongAsTheDrag();
    void repeatedPlacementPushesTheSameLayerStateOnce();
};

void OverlayWindowAdjustTest::enteringOverridesClickThroughAndKeyboard()
{
    // 1. A click-through overlay takes full pointer input while adjusting and
    //    asks for the keyboard on demand so Esc can arrive.
    OverlayWindow overlay;
    QVERIFY(overlay.clickThrough());
    QCOMPARE(overlay.inputRegion(), QRegion(0, 0, 1, 1));
    QCOMPARE(overlay.keyboardInteractivity(), LayerShellQt::Window::KeyboardInteractivityNone);
    QVERIFY(!overlay.isAdjusting());

    overlay.beginAdjusting();

    QVERIFY(overlay.isAdjusting());
    QCOMPARE(overlay.inputRegion(), QRegion()); // full input region
    QCOMPARE(overlay.keyboardInteractivity(), LayerShellQt::Window::KeyboardInteractivityOnDemand);
    QCOMPARE(overlay.adjustPadding(), 24);
}

void OverlayWindowAdjustTest::leavingRestoresTheConfiguredState()
{
    // 2. Leaving restores exactly the configured click-through and None keyboard,
    //    whether the exit committed or was cancelled.
    for (const bool commit : {true, false}) {
        OverlayWindow overlay;
        overlay.beginAdjusting();
        QCOMPARE(overlay.inputRegion(), QRegion());

        overlay.endAdjusting(commit);

        QVERIFY(!overlay.isAdjusting());
        QCOMPARE(overlay.inputRegion(), QRegion(0, 0, 1, 1));
        QCOMPARE(overlay.keyboardInteractivity(), LayerShellQt::Window::KeyboardInteractivityNone);
        QCOMPARE(overlay.adjustingFontSize(), 0);
    }

    // The configured click-through of false is restored too, not just the default.
    OverlayWindow nonClickThrough;
    nonClickThrough.setClickThrough(false);
    QCOMPARE(nonClickThrough.inputRegion(), QRegion());
    nonClickThrough.beginAdjusting();
    nonClickThrough.endAdjusting(true);
    QCOMPARE(nonClickThrough.inputRegion(), QRegion());
}

void OverlayWindowAdjustTest::adjustingKeepsTheTextBoxAndItsPlacement()
{
    // 3. The placement continuously describes the text box: adjusting pads the
    //    surface but never touches the stored point, and an empty overlay gets
    //    the minimum box to grab.
    OverlayWindow overlay;
    overlay.setPlacement(Placement{50.0, 80.0});
    overlay.setContentSize(400, 100);
    QCOMPARE(overlay.adjustmentBox(), QSize(400, 100));

    overlay.beginAdjusting();
    QCOMPARE(overlay.placement(), (Placement{50.0, 80.0}));
    QCOMPARE(overlay.adjustmentBox(), QSize(400, 100));

    // Nothing playing: the content collapses, so the adjusting box holds its
    // minimum.
    overlay.setContentSize(0, 0);
    QCOMPARE(overlay.adjustmentBox(), QSize(240, 80));

    // Leaving drops back to the raw content size.
    overlay.endAdjusting(true);
    QCOMPARE(overlay.adjustmentBox(), QSize(1, 1));
}

void OverlayWindowAdjustTest::cancelledAdjustingWritesNoSettings()
{
    // 4. A drag the user cancels with Esc must not reach the settings file, even
    //    though it moved the overlay live while it was in flight.
    std::unique_ptr<Settings> settings(kshadowlrc::createSettings());
    const int originalFontSize = settings->fontSize();

    OverlayWindow overlay;
    QObject::connect(&overlay, &OverlayWindow::placementEdited, settings.get(), [&settings](double horizontalPercent, double verticalPercent) {
        settings->setHorizontalPosition(horizontalPercent);
        settings->setVerticalPosition(verticalPercent);
    });
    QObject::connect(&overlay, &OverlayWindow::fontSizeEdited, settings.get(), [&settings](int points) {
        settings->setFontSize(points);
    });

    overlay.beginAdjusting();
    overlay.beginDrag(false);
    overlay.setPlacement(Placement{12.5, 62.5});
    overlay.endAdjusting(false);

    QVERIFY(qFuzzyCompare(settings->horizontalPosition(), 50.0));
    QVERIFY(qFuzzyCompare(settings->verticalPosition(), 80.0));
    QCOMPARE(settings->fontSize(), originalFontSize);
}

void OverlayWindowAdjustTest::motionlessClickEmitsNothing()
{
    // 5. Pressing and releasing without moving is not an edit: it must report
    //    nothing, so no identical value is written and no "edited" log line is
    //    produced for a click the user did not turn into a drag.
    OverlayWindow overlay;
    int placementEdits = 0;
    int fontEdits = 0;
    QObject::connect(&overlay, &OverlayWindow::placementEdited, &overlay, [&placementEdits] {
        ++placementEdits;
    });
    QObject::connect(&overlay, &OverlayWindow::fontSizeEdited, &overlay, [&fontEdits] {
        ++fontEdits;
    });

    overlay.setFontSize(40);
    overlay.beginAdjusting();

    // Position gesture: down and up with the cursor where it was.
    overlay.setContentSize(400, 100);
    overlay.beginDrag(false);
    overlay.updateDrag();
    overlay.endDrag();
    QCOMPARE(placementEdits, 0);

    // Font gesture: same, and the live value still shows while the pointer is
    // down even though nothing is written.
    overlay.beginDrag(true);
    overlay.updateDrag();
    QCOMPARE(overlay.adjustingFontSize(), 40);
    QCOMPARE(fontEdits, 0);
    overlay.endDrag();
    QCOMPARE(fontEdits, 0);

    // A committed exit still clears the temporary size.
    overlay.endAdjusting(true);
    QCOMPARE(overlay.adjustingFontSize(), 0);
}

void OverlayWindowAdjustTest::releasedDragWritesThePlacementOnce()
{
    // 6. The release is the one moment a changed placement is reported, and the
    //    values are the text box's point -- the adjusting padding does not leak
    //    in. The offscreen cursor cannot move and there is no screen, so the live
    //    point a real drag would have applied is applied directly, exactly as
    //    updateDrag() does.
    std::unique_ptr<Settings> settings(kshadowlrc::createSettings());

    OverlayWindow overlay;
    int edits = 0;
    QObject::connect(&overlay, &OverlayWindow::placementEdited, settings.get(), [&settings, &edits](double horizontalPercent, double verticalPercent) {
        ++edits;
        settings->setHorizontalPosition(horizontalPercent);
        settings->setVerticalPosition(verticalPercent);
    });

    overlay.beginAdjusting();
    overlay.beginDrag(false);
    overlay.setPlacement(Placement{12.5, 62.5});
    overlay.endDrag();

    QCOMPARE(edits, 1);
    QVERIFY(qFuzzyCompare(settings->horizontalPosition(), 12.5));
    QVERIFY(qFuzzyCompare(settings->verticalPosition(), 62.5));
}

void OverlayWindowAdjustTest::frozenSurfaceDuringPositionDrag()
{
    // 7. While a position drag is in flight the surface is frozen at the box it
    //    was grabbed with: the music can advance and the lyrics change size, and
    //    the surface must not resize under the pointer. After the release the
    //    real content size is applied.
    OverlayWindow overlay;
    overlay.setContentSize(400, 100);
    overlay.beginAdjusting();

    // The adjusting surface is the 400x100 text box plus 24 px on each side.
    QCOMPARE(overlay.pushedSurfaceSize(), QSize(448, 148));

    overlay.beginDrag(false);
    const QSize grabbed = overlay.pushedSurfaceSize();
    QCOMPARE(grabbed, QSize(448, 148));

    // The lyrics change size mid-drag: the surface stays as it was grabbed.
    overlay.setContentSize(900, 60);
    QCOMPARE(overlay.pushedSurfaceSize(), grabbed);

    // The release restores the real content size: the 900x60 box, raised to the
    // 80 px minimum height, plus the padding.
    overlay.endDrag();
    QCOMPARE(overlay.pushedSurfaceSize(), QSize(948, 128));
}

void OverlayWindowAdjustTest::dragPollLivesExactlyAsLongAsTheDrag()
{
    // 8. The drag is driven by the window's own poll of the global cursor, not
    //    by QML's translation signal -- the surface follows the pointer, so that
    //    signal cannot be the trigger. The poll must therefore live exactly as
    //    long as the drag: on while a gesture is in flight, off the moment it is
    //    released or cancelled, and never resurrected by a stray call.
    OverlayWindow overlay;
    QVERIFY(!overlay.dragPolling()); // never started

    // A drag cannot begin outside adjusting mode, so it starts no poll.
    overlay.beginDrag(false);
    QVERIFY(!overlay.dragPolling());

    overlay.beginAdjusting();

    // Position gesture: on for the flight, off on release.
    overlay.beginDrag(false);
    QVERIFY(overlay.dragPolling());
    overlay.endDrag();
    QVERIFY(!overlay.dragPolling());

    // A second release with no drag in flight must not resurrect it.
    overlay.endDrag();
    QVERIFY(!overlay.dragPolling());

    // Font gesture: same lifetime, and a committing exit stops it.
    overlay.beginDrag(true);
    QVERIFY(overlay.dragPolling());
    overlay.endAdjusting(true);
    QVERIFY(!overlay.dragPolling());

    // Esc with a gesture still in flight stops the poll too.
    overlay.beginAdjusting();
    overlay.beginDrag(false);
    QVERIFY(overlay.dragPolling());
    overlay.endAdjusting(false);
    QVERIFY(!overlay.dragPolling());

    // endAdjusting with no drag in flight, and a stray endDrag after it, leave
    // the poll off.
    overlay.beginAdjusting();
    overlay.endAdjusting(true);
    QVERIFY(!overlay.dragPolling());
    overlay.endDrag();
    QVERIFY(!overlay.dragPolling());
}

void OverlayWindowAdjustTest::repeatedPlacementPushesTheSameLayerStateOnce()
{
    // 9. applyAnchors() now commits the surface right after it pushes the anchor
    //    set, so the compositor sees new margins without waiting for a rendered
    //    frame. That commit is not observable here: commitSurface() returns early
    //    while !isExposed(), and the offscreen platform never exposes the window,
    //    so this test cannot assert it -- like the input-region commits in the
    //    tests above, it is simply skipped. What is asserted instead is that the
    //    placement path still reaches the same layer state: the anchors and
    //    margins the LayerShellQt::Window holds match what the placement maths
    //    dictate, and re-driving the path with an unchanged placement (or an
    //    identical content size) pushes nothing again, so the cache is intact and
    //    no commit can fire for a state that did not move. Cases 3, 6 and 7
    //    already cover the placement point, the frozen drag box and the pushed
    //    surface size, so they are not repeated here.
    OverlayWindow overlay;
    auto *layerWindow = LayerShellQt::Window::get(&overlay);
    QVERIFY(layerWindow);

    int anchorPushes = 0;
    int marginPushes = 0;
    QObject::connect(layerWindow, &LayerShellQt::Window::anchorsChanged, &overlay, [&anchorPushes] {
        ++anchorPushes;
    });
    QObject::connect(layerWindow, &LayerShellQt::Window::marginsChanged, &overlay, [&marginPushes] {
        ++marginPushes;
    });

    overlay.setContentSize(400, 100);
    overlay.beginAdjusting();

    // The anchors/margins applyAnchors() derives for the current placement, using
    // the same public helpers and the screen the window actually resolved (the
    // offscreen platform may or may not provide one).
    const auto expectedState = [&overlay] {
        const QSize screen = overlay.screen() ? overlay.screen()->geometry().size() : QSize();
        const QPoint topLeft = placementBoxTopLeft(overlay.placement(), overlay.adjustmentBox(), screen);
        return anchorsFor(topLeft, OverlayWindow::AdjustPadding);
    };

    // A placement that really moves pushes its anchors and margins.
    overlay.setPlacement(Placement{30.0, 70.0});
    PlacementAnchors expected = expectedState();
    QCOMPARE(layerWindow->anchors(), expected.anchors);
    QCOMPARE(layerWindow->margins(), expected.margins);
    const int anchorPushesAfterMove = anchorPushes;
    const int marginPushesAfterMove = marginPushes;

    // The same placement again: the setter compares and does no work.
    overlay.setPlacement(Placement{30.0, 70.0});
    QCOMPARE(anchorPushes, anchorPushesAfterMove);
    QCOMPARE(marginPushes, marginPushesAfterMove);

    // Re-applying an identical content size re-runs applyAnchors() through
    // applyContentSize(); the unchanged state must not be pushed or committed
    // again.
    overlay.setContentSize(400, 100);
    QCOMPARE(anchorPushes, anchorPushesAfterMove);
    QCOMPARE(marginPushes, marginPushesAfterMove);
    QCOMPARE(layerWindow->anchors(), expected.anchors);
    QCOMPARE(layerWindow->margins(), expected.margins);

    // A later real move still lands on the layer state the maths dictate.
    overlay.setPlacement(Placement{60.0, 20.0});
    expected = expectedState();
    QCOMPARE(overlay.placement(), (Placement{60.0, 20.0}));
    QCOMPARE(layerWindow->anchors(), expected.anchors);
    QCOMPARE(layerWindow->margins(), expected.margins);
}

int main(int argc, char **argv)
{
    QTemporaryDir tempDir;
    if (!tempDir.isValid()) {
        std::fprintf(stderr, "FAIL: could not create a temporary config directory\n");
        return 1;
    }
    // Must happen before anything can open a config file.
    qputenv("XDG_CONFIG_HOME", tempDir.path().toUtf8());
    qputenv("XDG_CONFIG_DIRS", tempDir.path().toUtf8());
    qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("offscreen"));

    QGuiApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("overlaywindow-adjust-test"));

    OverlayWindowAdjustTest tc;
    return QTest::qExec(&tc, argc, argv);
}

#include "overlaywindow_adjust_test.moc"
