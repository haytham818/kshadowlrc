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
    //    never reports a screen coordinate. On Wayland the reported value is
    //    Qt's idea of the window origin (the layer surface sits at the screen
    //    origin) plus the pointer inside the surface - and the surface is what
    //    the drag moves. So the reported movement is the pointer's real movement
    //    *minus* the surface's own, and the base it goes on must be the top-left
    //    last pushed, never the one captured at grab time.
    //
    //    The live log that drove the fix, on a 2560x1440 screen: over one drag
    //    the pointer's in-surface coordinate spanned 811 px and the margins we
    //    asked for spanned 811 px, while the pointer really travelled 1622 px.
    //    The old grab-time base counted the surface's own movement twice.
    const QSize screen(2560, 1440);
    const QSize box(400, 100);
    const Placement placement{50.0, 40.0};

    const QPoint grabTopLeft = placementBoxTopLeft(placement, box, screen);
    const QPoint pointerAtGrab = grabTopLeft + QPoint(100, 50);

    // Four steps of different sizes, the pointer reversing on the second one.
    // x sums to 1622 while the even-positioned steps sum to 811, so the old
    // base below lands exactly on half the trip; the small y wiggle nets 40.
    // All integers, so every comparison in this test is exact.
    const QList<QPoint> steps = {
        QPoint(300, -40),
        QPoint(-200, 60),
        QPoint(511, 60),
        QPoint(1011, -40),
    };

    // A drag in progress: the top-left we have already pushed to the surface,
    // starting at the grabbed one, and the pointer's real position.
    QPoint pushedTopLeft = grabTopLeft;
    QPoint pointer = pointerAtGrab;

    for (const QPoint &step : steps) {
        pointer += step;

        // What the platform reports with the surface sitting where we last put
        // it: the pointer's position inside the surface now, less the one it had
        // when the grab began. The compositor has applied our pushes - exactly
        // what the live log showed.
        const QPoint reportedDelta = (pointer - pushedTopLeft) - (pointerAtGrab - grabTopLeft);

        pushedTopLeft = draggedBoxTopLeft(pushedTopLeft, reportedDelta);

        // 1:1: the box is where the pointer's real movement puts it, not halfway.
        QCOMPARE(pushedTopLeft, grabTopLeft + (pointer - pointerAtGrab));
    }

    // Negative control, the regression guard for this very bug. The old rule
    // applied the reported movement to the grab-time top-left,
    // grabTopLeft + reportedDelta, so it kept re-adding the surface's own
    // movement and its lag grew by half of every step: the box ends up about
    // half the trip behind. Same steps, but each reported value is now read
    // against the top-left the old rule itself had pushed - the live bug - so
    // the run diverges exactly as it did on screen.
    QPoint oldTopLeft = grabTopLeft;
    pointer = pointerAtGrab;
    for (const QPoint &step : steps) {
        pointer += step;
        const QPoint reportedDelta = (pointer - oldTopLeft) - (pointerAtGrab - grabTopLeft);
        oldTopLeft = grabTopLeft + reportedDelta;
    }

    // The old rule is about half the pointer's travel from the grab - 811
    // against 1622 on x, as in the log. The relation is approximate in general
    // (the lag grows by half a step each event), so allow a pixel or two.
    const QPoint oldTravel = oldTopLeft - grabTopLeft;
    const QPoint realTravel = pointer - pointerAtGrab;
    QVERIFY(qAbs(oldTravel.x() - realTravel.x() / 2) <= 2);
    QVERIFY(qAbs(oldTravel.y() - realTravel.y() / 2) <= 2);

    // And it is behind the pointer, not ahead of it.
    QVERIFY(oldTravel.x() < realTravel.x());
}

QTEST_GUILESS_MAIN(OverlayPlacementTest)

#include "overlayplacement_test.moc"
