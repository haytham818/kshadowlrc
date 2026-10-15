// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 KShadowLrc Contributors
//
// Offline test for the pure overlay geometry in src/platform/overlayplacement.*.
//
// Nothing here touches a QWindow or a compositor: placement, clamping, the font
// mapping and the layer-shell anchor conversion are all plain values, so every
// rule is checked directly.

#include "platform/overlayplacement.h"

#include <QPointF>
#include <QTest>

using namespace kshadowlrc;

namespace
{

constexpr int kScreenWidth = 1920;
constexpr int kScreenHeight = 1080;
const QSize kScreen(kScreenWidth, kScreenHeight);
const QSize kBox(400, 100);
constexpr int kMagnetPx = 24;

} // namespace

class OverlayPlacementTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void boxTopLeftForEachLandingSpot();
    void placementRoundTripsForInteriorPositions();
    void magnetSnapsToHorizontalCentre();
    void clampingKeepsTheBoxOnScreen();
    void fontDragScalesProportionallyAndClamps();
    void anchorsForEachLandingSpot();
    void centreProducesNoHorizontalAnchorOrMargin();
    void paddingShiftsTheSurfaceMargins();
};

void OverlayPlacementTest::boxTopLeftForEachLandingSpot()
{
    // 1. The six landing spots: three horizontal anchors by two vertical ones.
    //    A margin is measured from its own edge; Center ignores it.
    QCOMPARE(placementBoxTopLeft({VerticalAnchor::Top, 64, HorizontalAnchor::Left, 30}, kBox, kScreen), QPoint(30, 64));
    QCOMPARE(placementBoxTopLeft({VerticalAnchor::Top, 64, HorizontalAnchor::Center, 0}, kBox, kScreen), QPoint(760, 64));
    QCOMPARE(placementBoxTopLeft({VerticalAnchor::Top, 64, HorizontalAnchor::Right, 30}, kBox, kScreen), QPoint(1490, 64));
    QCOMPARE(placementBoxTopLeft({VerticalAnchor::Bottom, 64, HorizontalAnchor::Left, 30}, kBox, kScreen), QPoint(30, 916));
    QCOMPARE(placementBoxTopLeft({VerticalAnchor::Bottom, 64, HorizontalAnchor::Center, 0}, kBox, kScreen), QPoint(760, 916));
    QCOMPARE(placementBoxTopLeft({VerticalAnchor::Bottom, 64, HorizontalAnchor::Right, 30}, kBox, kScreen), QPoint(1490, 916));
}

void OverlayPlacementTest::placementRoundTripsForInteriorPositions()
{
    // 2. A box away from the centre and away from the half-way split describes
    //    the very placement that put it there.
    const QList<Placement> interior = {
        {VerticalAnchor::Top, 64, HorizontalAnchor::Left, 30},
        {VerticalAnchor::Top, 64, HorizontalAnchor::Center, 0},
        {VerticalAnchor::Top, 64, HorizontalAnchor::Right, 30},
        {VerticalAnchor::Bottom, 64, HorizontalAnchor::Left, 30},
        {VerticalAnchor::Bottom, 64, HorizontalAnchor::Right, 30},
    };
    for (const Placement &placement : interior) {
        const QPoint topLeft = placementBoxTopLeft(placement, kBox, kScreen);
        QVERIFY2(placementFor(topLeft, kBox, kScreen, kMagnetPx) == placement,
                 qPrintable(QStringLiteral("round-trip failed for %1,%2,%3,%4")
                                .arg(static_cast<int>(placement.vertical))
                                .arg(placement.verticalMargin)
                                .arg(static_cast<int>(placement.horizontal))
                                .arg(placement.horizontalMargin)));
    }
}

void OverlayPlacementTest::magnetSnapsToHorizontalCentre()
{
    // 3. Within the magnet of the horizontal centre the box snaps to Center. The
    //    centre is 960, so box x 736..784 has its centre within 24 px of it.
    QCOMPARE(placementFor(QPoint(760, 64), kBox, kScreen, kMagnetPx).horizontal, HorizontalAnchor::Center);
    QCOMPARE(placementFor(QPoint(736, 64), kBox, kScreen, kMagnetPx).horizontal, HorizontalAnchor::Center);
    QCOMPARE(placementFor(QPoint(784, 64), kBox, kScreen, kMagnetPx).horizontal, HorizontalAnchor::Center);

    // A single pixel further and it anchors left/right with the margin measured
    // from that edge.
    const Placement justLeft = placementFor(QPoint(735, 64), kBox, kScreen, kMagnetPx);
    QCOMPARE(justLeft.horizontal, HorizontalAnchor::Left);
    QCOMPARE(justLeft.horizontalMargin, 735);

    const Placement justRight = placementFor(QPoint(785, 64), kBox, kScreen, kMagnetPx);
    QCOMPARE(justRight.horizontal, HorizontalAnchor::Right);
    QCOMPARE(justRight.horizontalMargin, 735);

    // A zero magnet never snaps something that is not exactly centred.
    QCOMPARE(placementFor(QPoint(760, 64), kBox, kScreen, 0).horizontal, HorizontalAnchor::Center);
    QCOMPARE(placementFor(QPoint(761, 64), kBox, kScreen, 0).horizontal, HorizontalAnchor::Right);
}

void OverlayPlacementTest::clampingKeepsTheBoxOnScreen()
{
    // 4. A margin below zero floors at zero; a margin past the far edge is pulled
    //    back so the whole box stays inside.
    const Placement floored = clampToScreen({VerticalAnchor::Top, -10, HorizontalAnchor::Left, -5}, kBox, kScreen);
    QCOMPARE(floored, (Placement{VerticalAnchor::Top, 0, HorizontalAnchor::Left, 0}));

    const Placement pulledBack = clampToScreen({VerticalAnchor::Top, 2000, HorizontalAnchor::Right, 2000}, kBox, kScreen);
    QCOMPARE(pulledBack, (Placement{VerticalAnchor::Top, 980, HorizontalAnchor::Right, 1520}));

    const Placement bottomPulledBack = clampToScreen({VerticalAnchor::Bottom, 5000, HorizontalAnchor::Center, 0}, kBox, kScreen);
    QCOMPARE(bottomPulledBack, (Placement{VerticalAnchor::Bottom, 980, HorizontalAnchor::Center, 0}));

    // The already-valid case is left alone.
    const Placement fine{VerticalAnchor::Bottom, 64, HorizontalAnchor::Center, 0};
    QCOMPARE(clampToScreen(fine, kBox, kScreen), fine);

    // A box larger than the screen is pinned to zero rather than pushed off.
    const Placement oversized = clampToScreen({VerticalAnchor::Top, 100, HorizontalAnchor::Left, 100}, QSize(2000, 1200), kScreen);
    QCOMPARE(oversized, (Placement{VerticalAnchor::Top, 0, HorizontalAnchor::Left, 0}));
}

void OverlayPlacementTest::fontDragScalesProportionallyAndClamps()
{
    // 5. Away from the anchored edge grows. With Bottom that is dragging up; a
    //    full 200 px doubles it, half the way halves it. A sideways drag changes
    //    nothing.
    QCOMPARE(fontSizeForDrag(32, QPointF(0, 0), VerticalAnchor::Bottom), 32);
    QCOMPARE(fontSizeForDrag(32, QPointF(0, -200), VerticalAnchor::Bottom), 64);
    QCOMPARE(fontSizeForDrag(32, QPointF(0, 100), VerticalAnchor::Bottom), 16);
    QCOMPARE(fontSizeForDrag(32, QPointF(500, 0), VerticalAnchor::Bottom), 32);

    // With Top it is the other way round: dragging down -- which visibly makes a
    // top-anchored box taller -- grows it, and dragging up shrinks it.
    QCOMPARE(fontSizeForDrag(32, QPointF(0, 100), VerticalAnchor::Top), 48);
    QCOMPARE(fontSizeForDrag(32, QPointF(0, -100), VerticalAnchor::Top), 16);

    // Both clamps, for both anchors.
    QCOMPARE(fontSizeForDrag(100, QPointF(0, -200), VerticalAnchor::Bottom), 160);
    QCOMPARE(fontSizeForDrag(32, QPointF(0, 200), VerticalAnchor::Bottom), 8);
    QCOMPARE(fontSizeForDrag(100, QPointF(0, 200), VerticalAnchor::Top), 160);
    QCOMPARE(fontSizeForDrag(32, QPointF(0, -200), VerticalAnchor::Top), 8);
}

void OverlayPlacementTest::anchorsForEachLandingSpot()
{
    // 6. Each landing spot maps to its anchor flag and its own margin (padding 0,
    //    the non-adjusting case).
    const PlacementAnchors topLeft = anchorsFor({VerticalAnchor::Top, 64, HorizontalAnchor::Left, 30});
    QVERIFY(topLeft.anchors.testFlag(LayerShellQt::Window::AnchorTop));
    QVERIFY(!topLeft.anchors.testFlag(LayerShellQt::Window::AnchorBottom));
    QVERIFY(topLeft.anchors.testFlag(LayerShellQt::Window::AnchorLeft));
    QVERIFY(!topLeft.anchors.testFlag(LayerShellQt::Window::AnchorRight));
    QCOMPARE(topLeft.margins, QMargins(30, 64, 0, 0));

    const PlacementAnchors bottomRight = anchorsFor({VerticalAnchor::Bottom, 64, HorizontalAnchor::Right, 30});
    QVERIFY(bottomRight.anchors.testFlag(LayerShellQt::Window::AnchorBottom));
    QVERIFY(bottomRight.anchors.testFlag(LayerShellQt::Window::AnchorRight));
    QVERIFY(!bottomRight.anchors.testFlag(LayerShellQt::Window::AnchorTop));
    QVERIFY(!bottomRight.anchors.testFlag(LayerShellQt::Window::AnchorLeft));
    QCOMPARE(bottomRight.margins, QMargins(0, 0, 30, 64));

    // The default look: bottom, 64 px above the edge, centred.
    const PlacementAnchors defaultLook = anchorsFor({VerticalAnchor::Bottom, 64, HorizontalAnchor::Center, 0});
    QVERIFY(defaultLook.anchors.testFlag(LayerShellQt::Window::AnchorBottom));
    QCOMPARE(defaultLook.margins, QMargins(0, 0, 0, 64));
}

void OverlayPlacementTest::centreProducesNoHorizontalAnchorOrMargin()
{
    // 7. Center is expressed by the absence of a left/right anchor and of any
    //    horizontal margin, whatever margin value happens to be carried.
    const PlacementAnchors centred = anchorsFor({VerticalAnchor::Top, 10, HorizontalAnchor::Center, 999});
    QVERIFY(!centred.anchors.testFlag(LayerShellQt::Window::AnchorLeft));
    QVERIFY(!centred.anchors.testFlag(LayerShellQt::Window::AnchorRight));
    QCOMPARE(centred.margins.left(), 0);
    QCOMPARE(centred.margins.right(), 0);
}

void OverlayPlacementTest::paddingShiftsTheSurfaceMargins()
{
    // 8. While adjusting the surface is padded, so its margin from the anchored
    //    edge is the text box's margin minus the padding, never below zero.
    const PlacementAnchors padded = anchorsFor({VerticalAnchor::Bottom, 64, HorizontalAnchor::Left, 64}, 24);
    QCOMPARE(padded.margins.bottom(), 40);
    QCOMPARE(padded.margins.left(), 40);

    const PlacementAnchors flush = anchorsFor({VerticalAnchor::Bottom, 10, HorizontalAnchor::Right, 5}, 24);
    QCOMPARE(flush.margins.bottom(), 0);
    QCOMPARE(flush.margins.right(), 0);

    // A centred box keeps no horizontal margin under padding either.
    const PlacementAnchors centred = anchorsFor({VerticalAnchor::Top, 30, HorizontalAnchor::Center, 0}, 24);
    QVERIFY(!centred.anchors.testFlag(LayerShellQt::Window::AnchorLeft));
    QVERIFY(!centred.anchors.testFlag(LayerShellQt::Window::AnchorRight));
    QCOMPARE(centred.margins.left(), 0);
    QCOMPARE(centred.margins.right(), 0);
    QCOMPARE(centred.margins.top(), 6);
}

QTEST_GUILESS_MAIN(OverlayPlacementTest)

#include "overlayplacement_test.moc"
