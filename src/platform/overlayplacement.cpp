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

// Vertical drag, in logical pixels, that doubles (drag up) or halves (drag
// down) the font size. The scale is proportional: a drag of half this changes
// the size by half.
constexpr qreal kFontDragFullScalePx = 200.0;

} // namespace

bool operator==(const Placement &lhs, const Placement &rhs)
{
    return lhs.vertical == rhs.vertical && lhs.verticalMargin == rhs.verticalMargin && lhs.horizontal == rhs.horizontal
        && lhs.horizontalMargin == rhs.horizontalMargin;
}

bool operator!=(const Placement &lhs, const Placement &rhs)
{
    return !(lhs == rhs);
}

QPoint placementBoxTopLeft(const Placement &placement, QSize box, QSize screen)
{
    int x = 0;
    switch (placement.horizontal) {
    case HorizontalAnchor::Left:
        x = placement.horizontalMargin;
        break;
    case HorizontalAnchor::Center:
        // No left/right anchor: the compositor centres the box, so the margin
        // plays no part.
        x = (screen.width() - box.width()) / 2;
        break;
    case HorizontalAnchor::Right:
        x = screen.width() - box.width() - placement.horizontalMargin;
        break;
    }

    int y = 0;
    switch (placement.vertical) {
    case VerticalAnchor::Top:
        y = placement.verticalMargin;
        break;
    case VerticalAnchor::Bottom:
        y = screen.height() - box.height() - placement.verticalMargin;
        break;
    }

    return QPoint(x, y);
}

Placement placementFor(QPoint boxTopLeft, QSize box, QSize screen, int magnetPx)
{
    Placement placement;

    // Horizontal: which half the box's centre falls in, with a magnet around the
    // centre so a dragged box snaps back to Center.
    const int centreX2 = 2 * boxTopLeft.x() + box.width();
    const int screenCentreX2 = screen.width();
    // Compared doubled to keep the magnet exact: a half-pixel of rounding must
    // not decide whether the box snaps to the centre.
    if (qAbs(centreX2 - screenCentreX2) <= 2 * magnetPx) {
        // Centred: no margin, and no left/right anchor.
        placement.horizontal = HorizontalAnchor::Center;
        placement.horizontalMargin = 0;
    } else if (centreX2 < screenCentreX2) {
        placement.horizontal = HorizontalAnchor::Left;
        placement.horizontalMargin = boxTopLeft.x();
    } else {
        placement.horizontal = HorizontalAnchor::Right;
        placement.horizontalMargin = screen.width() - (boxTopLeft.x() + box.width());
    }

    // Vertical: which half the box's centre falls in. No magnet, no centre.
    const int centreY2 = 2 * boxTopLeft.y() + box.height();
    const int screenCentreY2 = screen.height();
    if (centreY2 < screenCentreY2) {
        placement.vertical = VerticalAnchor::Top;
        placement.verticalMargin = boxTopLeft.y();
    } else {
        placement.vertical = VerticalAnchor::Bottom;
        placement.verticalMargin = screen.height() - (boxTopLeft.y() + box.height());
    }

    return placement;
}

Placement clampToScreen(const Placement &placement, QSize box, QSize screen)
{
    Placement clamped = placement;

    // Margins are unsigned, so a negative one cannot be represented: keep the
    // box inside by flooring at zero. The upper bound keeps the far edge inside
    // too; a box larger than the screen is pinned to zero rather than pushed off.
    const int maxVertical = std::max(0, screen.height() - box.height());
    const int maxHorizontal = std::max(0, screen.width() - box.width());
    clamped.verticalMargin = std::clamp(placement.verticalMargin, 0, maxVertical);
    clamped.horizontalMargin = std::clamp(placement.horizontalMargin, 0, maxHorizontal);

    return clamped;
}

int fontSizeForDrag(int startPoints, QPointF delta, VerticalAnchor vertical)
{
    // Away from the anchored edge grows: a bottom-anchored box grows as the
    // pointer moves up, a top-anchored one as it moves down.
    const qreal direction = vertical == VerticalAnchor::Bottom ? -1.0 : 1.0;
    const qreal factor = 1.0 + direction * delta.y() / kFontDragFullScalePx;
    const int scaled = qRound(startPoints * factor);
    return std::clamp(scaled, kMinFontPoints, kMaxFontPoints);
}

PlacementAnchors anchorsFor(const Placement &placement, int padding)
{
    PlacementAnchors result;

    switch (placement.vertical) {
    case VerticalAnchor::Top:
        result.anchors.setFlag(LayerShellQt::Window::AnchorTop);
        // The surface's top edge sits `padding` above the text box's top edge.
        result.margins.setTop(std::max(0, placement.verticalMargin - padding));
        break;
    case VerticalAnchor::Bottom:
        result.anchors.setFlag(LayerShellQt::Window::AnchorBottom);
        result.margins.setBottom(std::max(0, placement.verticalMargin - padding));
        break;
    }

    switch (placement.horizontal) {
    case HorizontalAnchor::Left:
        result.anchors.setFlag(LayerShellQt::Window::AnchorLeft);
        result.margins.setLeft(std::max(0, placement.horizontalMargin - padding));
        break;
    case HorizontalAnchor::Right:
        result.anchors.setFlag(LayerShellQt::Window::AnchorRight);
        result.margins.setRight(std::max(0, placement.horizontalMargin - padding));
        break;
    case HorizontalAnchor::Center:
        // No left/right anchor and no margins: the compositor centres the
        // surface. This is the default look.
        break;
    }

    return result;
}

} // namespace kshadowlrc
