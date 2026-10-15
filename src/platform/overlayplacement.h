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
 * The pointer's real movement between two reported samples, given where the
 * overlay's own surface was at each of them.
 *
 * What the platform reports is not a screen coordinate: it is the pointer's
 * position *inside our own surface* (plus a constant origin Qt believes in).
 * The difference of two readings is therefore the pointer's real movement
 * *minus* the movement of the surface itself - and the surface is exactly what a
 * position drag moves, so that share is missing from every reading. Both terms
 * here are taken over the same interval: the change in the reading plus the
 * change in the surface position the drag had pushed over that interval. The
 * compositor applies a surface position with some delay, but a constant delay
 * cancels between the two terms, so their sum is the pointer's real movement
 * exactly, whatever that delay is.
 *
 * Two wrong turns are worth naming because both were really taken. Applying the
 * reading's change to the top-left captured at grab time counts the surface's
 * own movement twice and the box travels half as far as the pointer. And keeping
 * the movement since the grab as a value, adding it again on every poll to a
 * top-left that keeps advancing, runs away the moment the pointer stops: the
 * reading then freezes, but the poll keeps firing, so the last frozen movement
 * is added over and over and the box walks off screen.
 *
 * The caller must only call this when a new sample has arrived; a pointer that
 * has stopped then contributes a zero movement.
 */
QPoint dragPointerDelta(QPoint reportedNow, QPoint reportedBefore, QPoint surfaceNow, QPoint surfaceBefore);

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
