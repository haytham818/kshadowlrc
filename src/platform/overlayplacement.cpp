// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 KShadowLrc Contributors

#include "platform/overlayplacement.h"

#include <QtGlobal>

#include <algorithm>

namespace kshadowlrc
{

namespace
{

/*! Font size the overlay never goes below or above while dragging, in points. */
constexpr int kMinFontPoints = 8;
constexpr int kMaxFontPoints = 160;

// Vertical drag, in logical pixels, that doubles (drag down) or halves (drag up)
// the font size. The scale is proportional: a drag of half this changes the size
// by half.
constexpr qreal kFontDragFullScalePx = 200.0;

} // namespace

bool operator==(const Placement &lhs, const Placement &rhs)
{
    return lhs.horizontalPercent == rhs.horizontalPercent && lhs.verticalPercent == rhs.verticalPercent;
}

bool operator!=(const Placement &lhs, const Placement &rhs)
{
    return !(lhs == rhs);
}

QPoint placementBoxTopLeft(const Placement &placement, QSize box, QSize screen)
{
    if (screen.isEmpty()) {
        // No screen to place against yet (e.g. before one is known): nothing can
        // be computed, so sit at the origin. placementFor() falls back to the
        // defaults for the same reason.
        return QPoint(0, 0);
    }

    const int x = qRound(screen.width() * placement.horizontalPercent / 100.0) - box.width() / 2;
    const int y = qRound(screen.height() * placement.verticalPercent / 100.0);
    return QPoint(x, y);
}

Placement placementFor(QPoint boxTopLeft, QSize box, QSize screen, int magnetPx)
{
    Placement placement;

    if (screen.isEmpty()) {
        // An empty screen has no percentages to derive; keep the defaults.
        return placement;
    }

    // Horizontal: the box's centre as a percentage of the screen width. Compared
    // doubled so the magnet is exact: a half pixel of rounding must not decide
    // whether the box snaps to the middle.
    placement.horizontalPercent = 100.0 * (boxTopLeft.x() + box.width() / 2.0) / screen.width();
    if (magnetPx > 0) {
        const int centreX2 = 2 * boxTopLeft.x() + box.width();
        if (qAbs(centreX2 - screen.width()) <= 2 * magnetPx) {
            placement.horizontalPercent = 50.0;
        }
    }

    // Vertical: the box's top edge as a percentage of the screen height. No
    // magnet, no centre: the top edge is what stays put.
    placement.verticalPercent = 100.0 * boxTopLeft.y() / screen.height();

    return placement;
}

QPoint clampToScreen(QPoint boxTopLeft, QSize box, QSize screen)
{
    // A box larger than the screen cannot fit; pin it to the origin rather than
    // push it off the far edge.
    const int maxX = std::max(0, screen.width() - box.width());
    const int maxY = std::max(0, screen.height() - box.height());
    return QPoint(std::clamp(boxTopLeft.x(), 0, maxX), std::clamp(boxTopLeft.y(), 0, maxY));
}

int fontSizeForDrag(int startPoints, QPointF delta)
{
    // The top edge is pinned, so the box grows downward: dragging down grows the
    // text, dragging up shrinks it.
    const qreal factor = 1.0 + delta.y() / kFontDragFullScalePx;
    const int scaled = qRound(startPoints * factor);
    return std::clamp(scaled, kMinFontPoints, kMaxFontPoints);
}

PlacementAnchors anchorsFor(QPoint boxTopLeft, int padding)
{
    PlacementAnchors result;

    // Always the top-left corner, never stretched: the surface takes its size
    // from its desired size and grows down and right from this point.
    result.anchors.setFlag(LayerShellQt::Window::AnchorTop);
    result.anchors.setFlag(LayerShellQt::Window::AnchorLeft);
    // The surface extends `padding` beyond the text box on each side, so its
    // margin from the edge is the box's top-left less the padding, never below
    // zero.
    result.margins.setTop(std::max(0, boxTopLeft.y() - padding));
    result.margins.setLeft(std::max(0, boxTopLeft.x() - padding));

    return result;
}

} // namespace kshadowlrc
