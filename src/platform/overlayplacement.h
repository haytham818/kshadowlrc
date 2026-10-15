// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 KShadowLrc Contributors

#pragma once

#include <LayerShellQt/Window>

#include <QMargins>
#include <QPoint>
#include <QPointF>
#include <QSize>

namespace kshadowlrc
{

/*!
 * Where the overlay text box sits, as a point: its horizontal centre and its
 * top edge, each a percentage of the screen. This is the whole geometry of the
 * overlay in one offline-testable place: nothing here touches a QWindow, a
 * compositor or a QObject, so every rule below can be checked without a running
 * desktop.
 *
 * The point exists because the box is only ever as large as the lyrics, so it
 * changes size whenever the lyrics change. The layer-shell margins are derived
 * from the point *and the current box* on every such change (see
 * OverlayWindow::applyAnchors), which keeps the centre and the top edge - the
 * line being read - exactly where the point says. Anchoring to a screen edge
 * instead would pin only that edge and let the text grow away from it.
 *
 * The box a Placement describes is the text box, never the (possibly padded)
 * surface: see OverlayWindow for why the padding must not leak in.
 */
struct Placement {
    double horizontalPercent = 50.0; //!< Box centre, as a percentage of the screen width.
    double verticalPercent = 80.0; //!< Box top edge, as a percentage of the screen height.
};

bool operator==(const Placement &lhs, const Placement &rhs);
bool operator!=(const Placement &lhs, const Placement &rhs);

/*! The layer-shell state a Placement maps to: the anchor set and the margins. */
struct PlacementAnchors {
    LayerShellQt::Window::Anchors anchors;
    QMargins margins;
};

/*!
 * The box's top-left in screen coordinates, for \a placement.
 *
 * The screen origin is its top-left; \a box is the text box, \a screen the
 * screen it lives on. The x puts the box's centre on the screen's
 * \a placement.horizontalPercent; the y puts the box's top edge on the screen's
 * \a placement.verticalPercent. An empty screen (0 width or height, e.g. before
 * a screen is known) yields (0, 0).
 */
QPoint placementBoxTopLeft(const Placement &placement, QSize box, QSize screen);

/*!
 * The Placement a box at \a boxTopLeft describes: the inverse of
 * placementBoxTopLeft().
 *
 * With \a magnetPx above zero, a box whose centre falls within that many pixels
 * of the screen's middle snaps to exactly 50 %, so a dragged box lands back on
 * the default look easily. The comparison is doubled, so a half pixel of
 * rounding cannot decide it. There is no vertical magnet. A zero screen
 * dimension cannot be inverted and yields the defaults.
 */
Placement placementFor(QPoint boxTopLeft, QSize box, QSize screen, int magnetPx);

/*!
 * \a boxTopLeft clamped so the box stays fully inside \a screen when it fits:
 * both axes floor at zero and stop at the screen edge less the box. A box larger
 * than the screen is pinned to the origin rather than pushed off.
 */
QPoint clampToScreen(QPoint boxTopLeft, QSize box, QSize screen);

/*!
 * The box's top-left a position drag should ask for: where the drag has already
 * put the box, \a pushedTopLeft, displaced by \a reportedDelta - the movement
 * the platform reports for the pointer since the drag was grabbed.
 *
 * What the platform reports is not a screen coordinate. On Wayland it is Qt's
 * idea of the window's own origin plus the pointer's position inside the
 * surface, and the surface is the very thing a position drag moves. So the
 * reported displacement is the pointer's real movement *minus* the movement of
 * the surface itself. To follow the pointer 1:1 that share has to be added
 * back, and the best estimate of where the surface is right now is the top-left
 * last pushed to it - hence \a pushedTopLeft is the base the displacement goes
 * on.
 *
 * Applying the same displacement to the top-left captured at grab time instead
 * counts the surface's own movement twice: the box then travels half as far as
 * the pointer and never catches up. That is the drag bug this replaces.
 */
QPoint draggedBoxTopLeft(QPoint pushedTopLeft, QPoint reportedDelta);

/*!
 * The layer-shell anchors and margins that put the box's top-left at
 * \a boxTopLeft.
 *
 * Always AnchorTop | AnchorLeft: the surface is anchored by its top-left corner
 * and never stretched on either axis, so it stays exactly as large as its
 * desired size and grows downward and to the right from that point. \a padding
 * is the surface's padding on each side (0 outside adjusting mode): the surface
 * extends beyond the text box by it, so its margins are the box's top-left
 * minus the padding, never below zero. Nothing else is set.
 */
PlacementAnchors anchorsFor(QPoint boxTopLeft, int padding = 0);

/*!
 * The font size \a startPoints becomes after dragging \a delta.
 *
 * The box's top edge is pinned, so the box grows downward: dragging down
 * (positive y) grows the text, dragging up shrinks it. Only the vertical
 * component matters and it scales proportionally: a drag of half the scale
 * changes the size by half. The result is clamped to [8, 160] points.
 */
int fontSizeForDrag(int startPoints, QPointF delta);

} // namespace kshadowlrc
