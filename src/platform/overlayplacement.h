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
 * Where the overlay text box is anchored, and how far it sits from the screen
 * edges, as four plain values. This is the whole geometry of the overlay in one
 * offline-testable place: nothing here touches a QWindow, a compositor or a
 * QObject, so every rule below can be checked without a running desktop.
 *
 * The box a Placement describes is the text box, never the (possibly padded)
 * surface: see OverlayWindow for why the padding must not leak in.
 */

/*! Vertical anchor. There is no vertical centre: it is Top or Bottom only. */
enum class VerticalAnchor {
    Top,
    Bottom,
};

enum class HorizontalAnchor {
    Left,
    Center,
    Right,
};

struct Placement {
    VerticalAnchor vertical;
    int verticalMargin;
    HorizontalAnchor horizontal;
    int horizontalMargin;
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
 * screen it lives on. Horizontal Center ignores its margin (the compositor
 * centres the box), the other anchors measure their margin from their edge.
 */
QPoint placementBoxTopLeft(const Placement &placement, QSize box, QSize screen);

/*!
 * The Placement a box at \a boxTopLeft describes.
 *
 * Left/Right by which half of the screen the box's centre falls in, Top/Bottom
 * likewise. Within \a magnetPx of the horizontal centre the box snaps to Center
 * (whose margin is 0), so a dragged box lands back on the default look easily.
 * There is no vertical magnet and no vertical centre.
 */
Placement placementFor(QPoint boxTopLeft, QSize box, QSize screen, int magnetPx);

/*!
 * \a placement with any margin below zero or pushing the box past an edge
 * clamped, so the box stays fully inside \a screen.
 */
Placement clampToScreen(const Placement &placement, QSize box, QSize screen);

/*!
 * The font size \a startPoints becomes after dragging \a delta.
 *
 * The rule is "away from the anchored edge grows": with \a vertical Bottom a
 * drag up (negative y) grows the text and a drag down shrinks it; with Top it is
 * the other way round, so dragging down (which visibly makes a top-anchored box
 * taller) grows it. The direction is passed in rather than assumed, so the
 * gesture matches the edge the box hangs from.
 *
 * Only the vertical component matters and it scales proportionally: a drag of
 * half the scale changes the size by half. The result is clamped to [8, 160]
 * points.
 */
int fontSizeForDrag(int startPoints, QPointF delta, VerticalAnchor vertical);

/*!
 * The layer-shell anchors and margins for \a placement.
 *
 * \a padding is the surface's padding on each side (0 outside adjusting mode):
 * the surface extends beyond the text box by it, so the surface's margin from
 * the anchored edge is the text box's margin minus the padding, never below
 * zero. Horizontal Center produces no left/right anchor and no horizontal
 * margin, which is how the compositor centres the surface.
 */
PlacementAnchors anchorsFor(const Placement &placement, int padding = 0);

} // namespace kshadowlrc
