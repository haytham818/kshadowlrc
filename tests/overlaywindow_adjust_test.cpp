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
// The offscreen cursor cannot move, so a drag driven only through the public
// API is motionless here: that is what the "a motionless click emits nothing"
// check relies on. The platform's report of a moving pointer is modelled as an
// argument instead (applyPointerSample), so a whole gesture can still be driven
// offline -- see positionDragFollowsThePointerUnderThePlatformsReport. Where a
// committed edit is under test the live point is set directly through
// setPlacement().

#include "config/settings.h"
#include "platform/overlaywindow.h"

#include <QCursor>
#include <QGuiApplication>
#include <QSignalSpy>
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
    void positionDragFollowsThePointerUnderThePlatformsReport();
    void wheelAdjustsTheFontSizeInNotches();
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

void OverlayWindowAdjustTest::positionDragFollowsThePointerUnderThePlatformsReport()
{
    // A position drag, driven offline through the one public seam that exists for
    // it. The offscreen cursor cannot move, so updateDrag() -- which is just
    // applyPointerSample() fed QCursor::pos() -- would feed one frozen sample
    // forever; instead the platform's report is modelled here as an argument and
    // the whole gesture is played through applyPointerSample().
    //
    // The model is the one the field logs show: QCursor::pos() is the window
    // origin Qt believes in (a constant under Wayland, because a layer surface is
    // never told its real position) plus the pointer's place inside the surface,
    // and the surface is exactly what the drag moves. So a report is
    //   s_k = pointer_k - P_k
    // where P_k is the top-left the drag had pushed when the sample was generated
    // -- read back before the call through placementBoxTopLeft(). That reading
    // carries the surface's own movement, which is why the drag puts it back: a
    // real drag of 4883 events showed a surface-local cursor span of 811 px and a
    // margin span of 811 px against a real pointer travel of 1622 px, exactly
    // half. The rewrite accumulates that movement on an unclamped ideal and
    // clamps only what it pushes, so the box tracks the pointer 1:1 and the clamp
    // stays transparent: pushed off the edge and back, the box returns exactly
    // under the pointer.
    //
    // The regression that matters most here is the pointer stopping. Field log: a
    // 1.2 s window with 0 wl_pointer.motion events, 150 set_margin requests and
    // margin top rising 1 px every ~8 ms (+150 px in all), while the sample was
    // frozen -- the frozen reading was being re-added on every poll and the box
    // walked off screen. A whole run of identical samples is fed below and must
    // move nothing.
    OverlayWindow overlay;
    overlay.setContentSize(400, 100);
    overlay.beginAdjusting();

    QVERIFY(overlay.screen());
    const QSize screen = overlay.screen()->geometry().size();
    QVERIFY(!screen.isEmpty());

    // The box is frozen at the grab-time size by beginDrag(false); take it before
    // the grab, exactly as a position drag does.
    const QSize box = overlay.adjustmentBox();

    const Placement grabPlacement{50.0, 80.0};
    overlay.setPlacement(grabPlacement);
    const QPoint grabTopLeft = placementBoxTopLeft(grabPlacement, box, screen);

    // The reading at the grab, as the platform reports it: the offscreen cursor
    // can be read but not moved, so it fixes the frame's constant origin. Hold on
    // to it before the grab, which reads the same value into the drag.
    const QPoint reportedAtGrab = QCursor::pos();
    overlay.beginDrag(false);

    // The surface's top-left the window has pushed, as the compositor would see
    // it: the placement maths read back on the same box and screen.
    const auto pushedTopLeft = [&overlay, box, screen] {
        return placementBoxTopLeft(overlay.placement(), box, screen);
    };
    // The absolute pointer in the frame the platform reports, offset from the
    // screen by the same constant the reading carries, so s_k = pointer_k - P_k
    // comes out exactly right at the grab (s_0 = reportedAtGrab).
    const QPoint pointerAtGrab = reportedAtGrab + grabTopLeft;
    // The top-left the box should be at after a pointer at pointerNow, clamped as
    // the drag clamps: the grab plus the pointer's whole travel, pinned to screen.
    const auto expectedTopLeft = [&](QPoint pointerNow) {
        return clampToScreen(grabTopLeft + (pointerNow - pointerAtGrab), box, screen);
    };

    // A pointer that keeps moving, including one step far past the right edge and
    // a later step back inside: the clamp must pin the box and then let it return
    // exactly under the pointer. The last step pushes past the bottom edge too.
    const QPoint offRight(screen.width() + 600, pointerAtGrab.y() - 20);
    const QPoint backInside(screen.width() / 2 + 100, pointerAtGrab.y() - 20);
    const QPoint nearBottom(backInside.x(), screen.height() + 300);
    const QList<QPoint> steps = {
        pointerAtGrab + QPoint(30, -10),
        pointerAtGrab + QPoint(70, -30),
        offRight,
        backInside,
        nearBottom,
    };

    for (int i = 0; i < int(steps.size()); ++i) {
        const QPoint pointerNow = steps.at(i);
        // The report the platform would make, measured against the surface pushed
        // so far -- read before the call, exactly as the drag reads it.
        const QPoint sample = pointerNow - pushedTopLeft();
        overlay.applyPointerSample(sample);
        QCOMPARE(pushedTopLeft(), expectedTopLeft(pointerNow));

        if (i == 0) {
            // The freeze regression, and the one that really bites. It sits on an
            // in-bounds step, where neither the ideal nor the pushed top-left is
            // clamped: if the frozen reading were re-added the box would move and
            // this would fail. At a screen edge the clamp pins the pushed value
            // whatever the arithmetic does, so the same assertion there would hold
            // even with the guard gone -- the edge would hide the movement. The
            // pointer stops here: the platform keeps reporting the same sample and
            // the drag poll keeps firing, and none of it may move the box.
            const Placement placementWhileStopped = overlay.placement();
            const QPoint pushedWhileStopped = pushedTopLeft();
            for (int poll = 0; poll < 50; ++poll) {
                overlay.applyPointerSample(sample);
                QCOMPARE(overlay.placement(), placementWhileStopped);
                QCOMPARE(pushedTopLeft(), pushedWhileStopped);
            }
        }

        if (i == 2) {
            // Supplementary, after the off-edge step: the same freeze with the box
            // pinned at the edge, covering that combination. On its own it proves
            // little -- the clamp keeps the pushed value green either way -- so the
            // in-bounds freeze above carries the bite.
            const Placement placementWhileStopped = overlay.placement();
            const QPoint pushedWhileStopped = pushedTopLeft();
            for (int poll = 0; poll < 50; ++poll) {
                overlay.applyPointerSample(sample);
                QCOMPARE(overlay.placement(), placementWhileStopped);
                QCOMPARE(pushedTopLeft(), pushedWhileStopped);
            }
        }
    }
}

void OverlayWindowAdjustTest::wheelAdjustsTheFontSizeInNotches()
{
    // 11. A wheel notch on the block resizes the text, and it is applied and
    //     reported at once: a wheel has no release, so unlike a corner drag there
    //     is no single "report at the end". The stored setting is 32, so one
    //     notch is fontSizeForWheel(32, 1) = 34, reported exactly once.
    {
        OverlayWindow overlay;
        overlay.setFontSize(32);
        QSignalSpy edits(&overlay, &OverlayWindow::fontSizeEdited);
        overlay.beginAdjusting();

        overlay.nudgeFontSize(OverlayWindow::WheelUnitsPerNotch);

        QCOMPARE(overlay.adjustingFontSize(), fontSizeForWheel(32, 1));
        QCOMPARE(overlay.adjustingFontSize(), 34);
        QCOMPARE(edits.count(), 1);
        QCOMPARE(edits.constFirst().constFirst().toInt(), 34);
    }

    // A trackpad or a high-resolution wheel sends less than a notch at a time.
    // The units bank up, so three 40-unit turns (40 + 40 + 40 = 120) are one
    // notch: the first two do nothing and report nothing, the third steps.
    {
        OverlayWindow overlay;
        overlay.setFontSize(32);
        QSignalSpy edits(&overlay, &OverlayWindow::fontSizeEdited);
        overlay.beginAdjusting();

        overlay.nudgeFontSize(40);
        QCOMPARE(overlay.adjustingFontSize(), 0); // nothing banked into a notch yet
        QCOMPARE(edits.count(), 0);
        overlay.nudgeFontSize(40);
        QCOMPARE(overlay.adjustingFontSize(), 0);
        QCOMPARE(edits.count(), 0);
        overlay.nudgeFontSize(40);
        QCOMPARE(overlay.adjustingFontSize(), fontSizeForWheel(32, 1));
        QCOMPARE(overlay.adjustingFontSize(), 34);
        QCOMPARE(edits.count(), 1);
    }

    // Reversing mid-notch cancels the banked units: +40 then -40 leaves the
    // accumulator at zero, so nothing moves and nothing is reported.
    {
        OverlayWindow overlay;
        overlay.setFontSize(32);
        QSignalSpy edits(&overlay, &OverlayWindow::fontSizeEdited);
        overlay.beginAdjusting();

        overlay.nudgeFontSize(40);
        overlay.nudgeFontSize(-40);

        QCOMPARE(overlay.adjustingFontSize(), 0);
        QCOMPARE(edits.count(), 0);
    }

    // Outside adjusting mode a wheel turn is not an edit: it would write a size
    // that is on screen nowhere. It must do and report nothing.
    {
        OverlayWindow overlay;
        overlay.setFontSize(32);
        QSignalSpy edits(&overlay, &OverlayWindow::fontSizeEdited);

        overlay.nudgeFontSize(OverlayWindow::WheelUnitsPerNotch);

        QCOMPARE(overlay.adjustingFontSize(), 0);
        QCOMPARE(edits.count(), 0);
    }

    // The upper bound holds, and once the live size reaches it further notches
    // report nothing. The edit is wired back into the setting, exactly as the
    // application does, so a later notch compares against the new stored value.
    {
        OverlayWindow overlay;
        overlay.setFontSize(100);
        QObject::connect(&overlay, &OverlayWindow::fontSizeEdited, &overlay, &OverlayWindow::setFontSize);
        QSignalSpy edits(&overlay, &OverlayWindow::fontSizeEdited);
        overlay.beginAdjusting();

        // Roll well past 160 one notch at a time.
        for (int i = 0; i < 12; ++i) {
            overlay.nudgeFontSize(OverlayWindow::WheelUnitsPerNotch);
        }
        QCOMPARE(overlay.adjustingFontSize(), 160);

        // At the bound the value already equals the stored setting, so a further
        // notch changes nothing and reports nothing.
        const int reportsAtBound = edits.count();
        for (int i = 0; i < 5; ++i) {
            overlay.nudgeFontSize(OverlayWindow::WheelUnitsPerNotch);
        }
        QCOMPARE(overlay.adjustingFontSize(), 160);
        QCOMPARE(edits.count(), reportsAtBound);
    }
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
