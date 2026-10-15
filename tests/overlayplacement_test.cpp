// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 KShadowLrc Contributors
//
// Offline test for the pure overlay geometry in src/platform/overlayplacement.*.
//
// Nothing here touches a QWindow or a compositor: the point model, its inverse,
// the magnet, clamping, the position-drag mapping, the layer-shell anchor
// conversion and the font mapping are all plain values, so every rule is
// checked directly.

#include "platform/overlayplacement.h"

#include <QPointF>
#include <QTest>

using namespace kshadowlrc;

namespace
{

constexpr int kScreenWidth = 1920;
constexpr int kScreenHeight = 1080;
const QSize kScreen(kScreenWidth, kScreenHeight);
const QSize kBox(400, 150);
constexpr int kMagnetPx = 24;

} // namespace

class OverlayPlacementTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void boxTopLeftFromPercentages();
    void placementInverseRoundTrips();
    void magnetSnapsToCentre();
    void clampingKeepsTheBoxOnScreen();
    void anchorsAlwaysTopLeft();
    void paddingShiftsTheSurfaceMargins();
    void fontDragGrowsDownwardAndClamps();
    void fontWheelStepsProportionallyWithAFloorPerNotch();
    void onePointHoldsForAnyBoxSize();
    void positionDragFollowsTheReportedPointerExactly();
};

void OverlayPlacementTest::boxTopLeftFromPercentages()
{
    // 1. The point maps to the box's top-left: x so the centre lands on the
    //    horizontal percentage, y as the top edge on the vertical one.
    QCOMPARE(placementBoxTopLeft({50.0, 80.0}, kBox, kScreen), QPoint(760, 864));
    QCOMPARE(placementBoxTopLeft({25.0, 25.0}, kBox, kScreen), QPoint(280, 270));
    QCOMPARE(placementBoxTopLeft({0.0, 0.0}, kBox, kScreen), QPoint(-200, 0));
    QCOMPARE(placementBoxTopLeft({100.0, 100.0}, kBox, kScreen), QPoint(1720, 1080));

    // An empty screen (no screen known yet) has nothing to place against.
    QCOMPARE(placementBoxTopLeft({50.0, 80.0}, kBox, QSize()), QPoint(0, 0));
}

void OverlayPlacementTest::placementInverseRoundTrips()
{
    // 2. A point is exactly what placementBoxTopLeft() put there: the two are
    //    inverses for any box whose half-size lands on a whole pixel.
    const QList<Placement> points = {
        {50.0, 80.0},
        {25.0, 25.0},
        {0.0, 0.0},
        {100.0, 100.0},
        {12.5, 62.5},
    };
    for (const Placement &point : points) {
        const QPoint topLeft = placementBoxTopLeft(point, kBox, kScreen);
        QVERIFY2(placementFor(topLeft, kBox, kScreen, 0) == point,
                 qPrintable(QStringLiteral("round-trip failed for %1,%2").arg(point.horizontalPercent).arg(point.verticalPercent)));
    }

    // A zero screen cannot be inverted: the defaults stand.
    QCOMPARE(placementFor(QPoint(123, 456), kBox, QSize(), 0), (Placement{50.0, 80.0}));
}

void OverlayPlacementTest::magnetSnapsToCentre()
{
    // 3. Within the magnet of the screen's middle the box snaps to exactly 50 %.
    //    The middle is 960, so a 400-wide box with x in 736..784 is within 24 px.
    QCOMPARE(placementFor(QPoint(760, 100), kBox, kScreen, kMagnetPx).horizontalPercent, 50.0);
    QCOMPARE(placementFor(QPoint(736, 100), kBox, kScreen, kMagnetPx).horizontalPercent, 50.0);
    QCOMPARE(placementFor(QPoint(784, 100), kBox, kScreen, kMagnetPx).horizontalPercent, 50.0);

    // A single pixel further and the exact percentage is kept.
    QVERIFY(qFuzzyCompare(placementFor(QPoint(735, 100), kBox, kScreen, kMagnetPx).horizontalPercent, 100.0 * 935 / 1920));
    QVERIFY(qFuzzyCompare(placementFor(QPoint(785, 100), kBox, kScreen, kMagnetPx).horizontalPercent, 100.0 * 985 / 1920));

    // A zero magnet never snaps something that is not exactly centred.
    QCOMPARE(placementFor(QPoint(760, 100), kBox, kScreen, 0).horizontalPercent, 50.0);
    QVERIFY(placementFor(QPoint(761, 100), kBox, kScreen, 0).horizontalPercent != 50.0);

    // There is no vertical magnet: the top edge is taken as it is.
    QVERIFY(qFuzzyCompare(placementFor(QPoint(760, 534), kBox, kScreen, kMagnetPx).verticalPercent, 100.0 * 534 / 1080));
}

void OverlayPlacementTest::clampingKeepsTheBoxOnScreen()
{
    // 4. Both axes floor at zero and stop where the box would leave the screen.
    QCOMPARE(clampToScreen(QPoint(-10, -5), kBox, kScreen), QPoint(0, 0));
    QCOMPARE(clampToScreen(QPoint(2000, 2000), kBox, kScreen), QPoint(1520, 930));

    // The already-valid case is left alone.
    QCOMPARE(clampToScreen(QPoint(30, 64), kBox, kScreen), QPoint(30, 64));

    // A box larger than the screen is pinned to the origin rather than pushed off.
    QCOMPARE(clampToScreen(QPoint(100, 100), QSize(2000, 1200), kScreen), QPoint(0, 0));
}

void OverlayPlacementTest::anchorsAlwaysTopLeft()
{
    // 5. The surface is anchored by its top-left corner and never stretched, with
    //    margins equal to the box's top-left.
    const PlacementAnchors a = anchorsFor(QPoint(30, 64));
    QVERIFY(a.anchors.testFlag(LayerShellQt::Window::AnchorTop));
    QVERIFY(a.anchors.testFlag(LayerShellQt::Window::AnchorLeft));
    QVERIFY(!a.anchors.testFlag(LayerShellQt::Window::AnchorBottom));
    QVERIFY(!a.anchors.testFlag(LayerShellQt::Window::AnchorRight));
    QCOMPARE(a.margins, QMargins(30, 64, 0, 0));

    const PlacementAnchors b = anchorsFor(QPoint(760, 864));
    QCOMPARE(b.margins, QMargins(760, 864, 0, 0));

    // Never a right or bottom margin, whatever the point.
    QCOMPARE(b.margins.right(), 0);
    QCOMPARE(b.margins.bottom(), 0);
}

void OverlayPlacementTest::paddingShiftsTheSurfaceMargins()
{
    // 6. While adjusting the surface is padded, so its margins are the box's
    //    top-left minus the padding, never below zero.
    const PlacementAnchors padded = anchorsFor(QPoint(64, 64), 24);
    QCOMPARE(padded.margins, QMargins(40, 40, 0, 0));

    const PlacementAnchors flush = anchorsFor(QPoint(5, 10), 24);
    QCOMPARE(flush.margins, QMargins(0, 0, 0, 0));

    // Each axis floors on its own.
    const PlacementAnchors mixed = anchorsFor(QPoint(30, 100), 24);
    QCOMPARE(mixed.margins, QMargins(6, 76, 0, 0));
}

void OverlayPlacementTest::fontDragGrowsDownwardAndClamps()
{
    // 7. The top edge is pinned, so the box grows downward: dragging down grows
    //    the text, dragging up shrinks it, and a sideways drag changes nothing. A
    //    full 200 px doubles it, half the way halves it.
    QCOMPARE(fontSizeForDrag(32, QPointF(0, 0)), 32);
    QCOMPARE(fontSizeForDrag(32, QPointF(0, 100)), 48);
    QCOMPARE(fontSizeForDrag(32, QPointF(0, -100)), 16);
    QCOMPARE(fontSizeForDrag(32, QPointF(500, 0)), 32);

    // Proportional: the same drag scales any start size by the same factor.
    QCOMPARE(fontSizeForDrag(48, QPointF(0, 100)), 72);

    // Both clamps.
    QCOMPARE(fontSizeForDrag(100, QPointF(0, 200)), 160);
    QCOMPARE(fontSizeForDrag(32, QPointF(0, -200)), 8);
    QCOMPARE(fontSizeForDrag(100, QPointF(0, 400)), 160);
    QCOMPARE(fontSizeForDrag(32, QPointF(0, -400)), 8);
}

void OverlayPlacementTest::fontWheelStepsProportionallyWithAFloorPerNotch()
{
    // 8. A wheel notch scales the size by 5 %, the corner handle's proportional
    //    feel. One notch on 32 pt: 32 x 1.05 = 33.6 -> 34, and 32 x 0.95 = 30.4
    //    -> 30. No clamp is in play at this size.
    QCOMPARE(fontSizeForWheel(32, 1), 34);
    QCOMPARE(fontSizeForWheel(32, -1), 30);

    // Several notches at once compound: 32 x 1.05^3 = 37.044 -> 37.
    QCOMPARE(fontSizeForWheel(32, 3), 37);

    // Zero notches is the value unchanged.
    QCOMPARE(fontSizeForWheel(32, 0), 32);

    // The minimum bound is the handle's own: 160 up cannot grow, 8 down cannot
    // shrink, whatever the step.
    QCOMPARE(fontSizeForWheel(160, 1), 160);
    QCOMPARE(fontSizeForWheel(160, 3), 160);
    QCOMPARE(fontSizeForWheel(8, -10), 8);

    // A notch moves at least a whole point where the proportion alone rounds it
    // away. 8 x 1.05 = 8.4 -> 8 and 8 x 0.95 = 7.6 -> 8 would both look dead, so
    // the floor gives 9 up and 7 down; the lower bound pulls the 7 back to 8.
    QCOMPARE(fontSizeForWheel(8, 1), 9);
    QCOMPARE(fontSizeForWheel(8, -1), 8);
    // At 9 pt the floor is visible on its own, clear of any clamp: 9 x 0.95 =
    // 8.55 -> 9 without it, 8 with it.
    QCOMPARE(fontSizeForWheel(9, -1), 8);

    // The two directions are symmetric where no floor bites: +1 then -1 returns
    // the start (34 x 0.95 = 32.3 -> 32).
    QCOMPARE(fontSizeForWheel(fontSizeForWheel(32, 1), -1), 32);
    QCOMPARE(fontSizeForWheel(fontSizeForWheel(100, 1), -1), 100);
    QCOMPARE(fontSizeForWheel(100, 1), 105);
    QCOMPARE(fontSizeForWheel(100, -1), 95);
}

void OverlayPlacementTest::onePointHoldsForAnyBoxSize()
{
    // 8. The point of the point model: for one fixed placement, boxes of very
    //    different sizes share the same horizontal centre and the same top edge in
    //    screen coordinates. That is "the text does not drift" as an assertion.
    const Placement point{40.0, 30.0};
    const QList<QSize> boxes = {QSize(400, 150), QSize(900, 60), QSize(1200, 40)};

    const QPoint first = placementBoxTopLeft(point, boxes.constFirst(), kScreen);
    const int centreX = first.x() + boxes.constFirst().width() / 2;
    const int topEdge = first.y();

    for (const QSize &box : boxes) {
        const QPoint topLeft = placementBoxTopLeft(point, box, kScreen);
        QCOMPARE(topLeft.x() + box.width() / 2, centreX);
        QCOMPARE(topLeft.y(), topEdge);
        // The point also survives the round trip for every box, so a drag that
        // grabbed any of them stores the same point back.
        QCOMPARE(placementFor(topLeft, box, kScreen, 0), point);
    }
}

void OverlayPlacementTest::positionDragFollowsTheReportedPointerExactly()
{
    // 9. A position drag must follow the pointer 1:1 even though the platform
    //    never reports a screen coordinate. On Wayland the report is Qt's idea of
    //    the window origin (a layer surface sits at the screen origin) plus the
    //    pointer *inside the surface*, and the surface is exactly what the drag
    //    moves. So a single reading is the pointer's real position *minus* the
    //    surface's, and the surface's own movement over the same interval has to
    //    be added back for the pointer's real movement to come out.
    //
    //    The live log that drove the fix, on a 2560x1440 screen: over one drag the
    //    pointer's in-surface coordinate spanned 811 px and the margins we asked
    //    for spanned 811 px, while the pointer really travelled 1622 px -- the
    //    reading's share is missing, so without putting it back the box moves at
    //    half speed. A later log caught the opposite failure: a 1.2 s window with
    //    0 wl_pointer.motion events, 150 set_margin requests and margin top rising
    //    1 px every ~8 ms (+150 px in all), while the sample itself was frozen --
    //    the frozen reading was being added again on every poll. Both failures are
    //    guarded below.
    const QSize screen(2560, 1440);
    const QSize box(400, 100);
    const Placement placement{50.0, 40.0};

    const QPoint grabTopLeft = placementBoxTopLeft(placement, box, screen);
    const QPoint pointerAtGrab = grabTopLeft + QPoint(100, 50);

    // Four steps of different sizes, the pointer reversing on the second one.
    // x sums to 1622 while the even-positioned steps sum to 811, so the grab-time
    // base below lands exactly on half the trip; the small y wiggle nets 40. All
    // integers, so every comparison in this test is exact.
    const QList<QPoint> steps = {
        QPoint(300, -40),
        QPoint(-200, 60),
        QPoint(511, 60),
        QPoint(1011, -40),
    };

    // 9a. The rewrite, sample by sample. s_k is what the compositor reports for
    //     the pointer at pointer_k: the pointer minus the surface top-left P_k the
    //     drag had already pushed when the sample was generated. We push each
    //     step's result before taking the next sample, so P_k is the top-left
    //     after the previous step. Both differences in dragPointerDelta() are
    //     taken over the same interval, so the compositor's own application delay
    //     cancels and the ideal advances by the pointer's real movement - exactly
    //     the step, never half of it and never a stale reading's worth.
    QPoint idealTopLeft = grabTopLeft;
    QPoint pushedTopLeft = grabTopLeft; // P_k, the top-left pushed before this sample
    QPoint previousSample = pointerAtGrab - grabTopLeft; // s_0, the reading at the grab
    QPoint previousPushed = grabTopLeft; // P_0

    QPoint pointer = pointerAtGrab;
    for (const QPoint &step : steps) {
        pointer += step;
        const QPoint surfaceNow = pushedTopLeft; // P_k
        const QPoint sample = pointer - surfaceNow; // s_k = G_k - P_k

        idealTopLeft += dragPointerDelta(sample, previousSample, surfaceNow, previousPushed);

        previousSample = sample;
        previousPushed = surfaceNow;
        pushedTopLeft = idealTopLeft;

        QCOMPARE(idealTopLeft, grabTopLeft + (pointer - pointerAtGrab));
    }

    // 9b. The failure this rewrite is about, as a negative control. The previous
    //     rule kept the movement reported since the grab as a value and added it
    //     again on every poll to the top-left the last push had put there,
    //     pushed += (s_k - s_0). While the pointer moves that is 1:1; a pointer
    //     that stops is the failure. Model the field exactly: the pointer moved
    //     1 px since the grab and then stopped, so the reading freezes - and
    //     because the platform sends no new motion event it stays frozen however
    //     far we push. The poll runs on at 8 ms, so the frozen movement is added
    //     over and over.
    const QPoint frozenDelta(1, 0); // the frozen reading in the log, 1 px
    QPoint buggyTopLeft = grabTopLeft;
    for (int poll = 0; poll < 150; ++poll) {
        buggyTopLeft += frozenDelta; // the old pushed += (s_k - s_0)
    }
    // +150 px with the pointer standing still: this is the box flying off screen.
    QCOMPARE(buggyTopLeft - grabTopLeft, QPoint(150, 0));

    // The rewrite, handed the same frozen sample again and again, moves nothing:
    // the sample is unchanged, so the caller takes no step, and the movement the
    // arithmetic would compute is zero besides.
    QCOMPARE(dragPointerDelta(previousSample, previousSample, pushedTopLeft, pushedTopLeft), QPoint(0, 0));

    // 9c. The half-speed failure, the other negative control. Applying the
    //     reading's change to the top-left captured at grab time counts the
    //     surface's own movement twice. Same steps, but each reading is now taken
    //     against the top-left the grab-time rule itself had pushed - the live
    //     bug - so the run diverges exactly as it did on screen.
    QPoint grabBasedTopLeft = grabTopLeft;
    pointer = pointerAtGrab;
    for (const QPoint &step : steps) {
        pointer += step;
        const QPoint reportedDelta = (pointer - grabBasedTopLeft) - (pointerAtGrab - grabTopLeft);
        grabBasedTopLeft = grabTopLeft + reportedDelta;
    }

    // It is about half the pointer's travel from the grab - 811 against 1622 on
    // x, as in the log. The relation is approximate in general (the lag grows by
    // half a step each event), so allow a pixel or two.
    const QPoint oldTravel = grabBasedTopLeft - grabTopLeft;
    const QPoint realTravel = pointer - pointerAtGrab;
    QVERIFY(qAbs(oldTravel.x() - realTravel.x() / 2) <= 2);
    QVERIFY(qAbs(oldTravel.y() - realTravel.y() / 2) <= 2);

    // And it is behind the pointer, not ahead of it.
    QVERIFY(oldTravel.x() < realTravel.x());
}

QTEST_GUILESS_MAIN(OverlayPlacementTest)

#include "overlayplacement_test.moc"
