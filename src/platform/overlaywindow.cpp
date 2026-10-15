// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 KShadowLrc Contributors

#include "platform/overlaywindow.h"

#include "kshadowlrc.h"
#include "platform/outputresolution.h"

#include <LayerShellQt/Window>

#include <QCursor>
#include <QGuiApplication>
#include <QLoggingCategory>
#include <QPoint>
#include <QScreen>
#include <QTimer>
#include <qpa/qplatformnativeinterface.h>

#include <wayland-client.h>

#include <cmath>

Q_LOGGING_CATEGORY(lcOverlay, "kshadowlrc.platform.overlay")

namespace kshadowlrc
{

OverlayWindow::OverlayWindow(QWindow *parent)
    : QQuickView(parent)
{
    // No opaque plate: only the rendered text shows.
    setColor(Qt::transparent);
    setResizeMode(QQuickView::SizeRootObjectToView);

    // While a drag is in flight, read the global cursor on a fast timer instead
    // of waiting for QML's translation signal: the surface follows the pointer,
    // so the pointer barely moves within the window and that signal fires only
    // when the compositor's repositioning lags. updateDrag() itself is unchanged
    // and stays the one place the gesture is applied.
    m_dragPoll.setTimerType(Qt::PreciseTimer);
    m_dragPoll.setInterval(8);
    connect(&m_dragPoll, &QTimer::timeout, this, &OverlayWindow::updateDrag);

    m_layerWindow = LayerShellQt::Window::get(this);
    if (!m_layerWindow) {
        qCCritical(lcOverlay) << "LayerShellQt::Window::get() returned null; the overlay is not a layer-shell surface";
        return;
    }

    applyLayerShell();
    applyAnchors();

    // A non-zero size before the first map so the surface is never 0-tall while
    // QML is still measuring; setContentSize() refines it on Component.onCompleted.
    m_layerWindow->setDesiredSize(QSize(1, 120));

    // Publish the initial (click-through) input region before show(); Qt's own
    // layer-shell configure sequence then carries it, so no manual commit yet.
    applyInputRegion();

    // Resolve the configured output now: the layer surface binds its output when
    // it is first created, so the screen has to be right before the first show().
    // A later change rebuilds the surface; see resolveOutput().
    // QGuiApplication::instance() is typed as QCoreApplication*, so cast to reach
    // the screen signals.
    if (auto *guiApp = qobject_cast<QGuiApplication *>(QCoreApplication::instance())) {
        connect(guiApp, &QGuiApplication::screenAdded, this, [this](QScreen *) {
            resolveOutput();
        });
        connect(guiApp, &QGuiApplication::screenRemoved, this, [this](QScreen *) {
            resolveOutput();
        });
    }
    resolveOutput();
}

void OverlayWindow::setOutputName(const QString &name)
{
    if (m_outputName == name) {
        return;
    }
    m_outputName = name;
    Q_EMIT outputNameChanged(m_outputName);
    resolveOutput();
}

QString OverlayWindow::activeOutputName() const
{
    return m_screen ? m_screen->name() : QString();
}

void OverlayWindow::resolveOutput()
{
    if (!m_layerWindow) {
        return;
    }

    const QList<QScreen *> screens = QGuiApplication::screens();
    QStringList names;
    names.reserve(screens.size());

    const QScreen *primary = QGuiApplication::primaryScreen();
    int primaryIndex = -1;
    for (int i = 0; i < screens.size(); ++i) {
        names.append(screens.at(i)->name());
        if (screens.at(i) == primary) {
            primaryIndex = i;
        }
    }

    const int index = resolveOutputIndex(m_outputName, names, primaryIndex);
    QScreen *chosen = index >= 0 ? screens.at(index) : nullptr;

    // Report an unhonourable request before the no-op check below: the fallback is
    // only reached through that check, and staying quiet about it would look like
    // the configured output had been honoured.
    if (!m_outputName.isEmpty() && !names.contains(m_outputName)) {
        qCWarning(lcOverlay) << "output" << m_outputName << "is not connected; using" << (chosen ? chosen->name() : QStringLiteral("<none>"));
    }

    if (chosen == m_screen) {
        return;
    }

    m_screen = chosen;

    if (!m_screen) {
        qCWarning(lcOverlay) << "no output is available; the overlay has nothing to attach to";
        Q_EMIT activeOutputChanged(QString());
        return;
    }

    // Point both the layer surface and the QWindow at the chosen screen. The
    // QWindow's screen decides the device pixel ratio and which screen the QML
    // scene is composed for; LayerShellQt reads its own copy when it creates the
    // layer surface.
    m_layerWindow->setScreen(m_screen);
    QWindow::setScreen(m_screen);

    if (isVisible()) {
        // Neither setScreen() moves a mapped Wayland window: the layer surface
        // fixes its output when it is created and the protocol has no request to
        // change it, so rebuilding the surface is the only way to honour the new
        // output. isVisible() implies a platform window exists, so this cannot
        // fire before the first map; a hidden overlay simply picks the new screen
        // up when it is next shown.
        rebuildSurface();
    }

    qCDebug(lcOverlay) << "active output" << m_screen->name() << m_screen->geometry();
    Q_EMIT activeOutputChanged(m_screen->name());
}

void OverlayWindow::rebuildSurface()
{
    // destroy() drops the platform window and its wl_surface; the following show()
    // creates a fresh one. LayerShellQt::Window survives - it is a child of this
    // QWindow - and re-installs the layer-shell integration on the new surface, so
    // the rebuilt one is a layer surface just like the original.
    //
    // QWaylandLayerSurface's constructor re-reads all of the layer state from that
    // surviving Window: the layer, the namespace (scope), the anchors, the
    // margins, the exclusive zone and edge, the keyboard interactivity, the
    // desired size, and the output screen set by the caller above. None of it has
    // to be re-asserted here.
    //
    // The input region is not layer state - it lives on the wl_surface that just
    // went away - so force the mask onto the new platform window rather than
    // trusting it to be re-applied; that is what keeps the surface click-through
    // across a rebuild.
    destroy();
    applyInputRegion(true);
    show();

    qCDebug(lcOverlay) << "rebuilt the layer surface on" << m_screen->name();
}

void OverlayWindow::applyLayerShell()
{
    // Overlay layer is the topmost layer-shell layer (above Dock/Top), which is
    // what "always above every window" needs.
    m_layerWindow->setLayer(LayerShellQt::Window::LayerOverlay);

    // KWin derives a layer surface's window type only from its namespace
    // (layershellv1window.cpp scopeToType): "dock" is the one that maps to
    // WindowType::Dock. Dock windows are the type KWin exempts from "Show
    // Desktop" (Workspace::breaksShowingDesktop() hides normal/dialog windows),
    // they never appear in the taskbar, and they take focus only when their
    // keyboard interactivity asks for it. Any branded namespace (e.g.
    // "kshadowlrc") would become a Normal window: shown in the taskbar and
    // hidden by Show Desktop. The trade-off is that isDock() consumers (slide,
    // slideback, magiclamp, ...) treat it as a panel, which is the intended
    // behaviour for a permanently-visible overlay.
    m_layerWindow->setScope(QStringLiteral("dock"));

    // No keyboard focus: the overlay never steals typing from the active window.
    // Adjusting mode raises this to OnDemand; see beginAdjusting().
    applyKeyboardInteractivity();
    // Reserve no screen space, so maximized windows are unaffected.
    m_layerWindow->setExclusiveZone(0);
    // Keep the surface alive across output changes.
    m_layerWindow->setCloseOnDismissed(false);
}

void OverlayWindow::applyKeyboardInteractivity()
{
    if (!m_layerWindow) {
        return;
    }
    m_layerWindow->setKeyboardInteractivity(m_keyboardInteractivity);
}

void OverlayWindow::setPlacement(const Placement &placement)
{
    if (m_placement == placement) {
        return;
    }
    m_placement = placement;
    applyAnchors();
    if (isVisible()) {
        // Pulse a frame so the new anchor set is committed promptly.
        update();
    }
}

void OverlayWindow::setFontSize(int points)
{
    m_fontSize = points;
}

QSize OverlayWindow::adjustmentBox() const
{
    if (!m_adjusting) {
        return m_contentSize;
    }
    // With nothing playing the content size is 0 and the surface would collapse
    // to something with nothing to grab, so adjusting works on a minimum box.
    return QSize(qMax(m_contentSize.width(), MinAdjustBox.width()), qMax(m_contentSize.height(), MinAdjustBox.height()));
}

QSize OverlayWindow::screenSize() const
{
    return m_screen ? m_screen->geometry().size() : QSize();
}

void OverlayWindow::setClickThrough(bool enabled)
{
    if (m_clickThrough == enabled) {
        return;
    }
    m_clickThrough = enabled;
    applyInputRegion();
}

void OverlayWindow::beginAdjusting()
{
    if (m_adjusting) {
        return;
    }
    m_adjusting = true;

    // Take pointer input so a frame and its handles are grabbable, and ask for
    // the keyboard on demand so Esc can arrive. Both override the configured
    // values for as long as adjusting lasts.
    applyInputRegion();
    m_keyboardInteractivity = LayerShellQt::Window::KeyboardInteractivityOnDemand;
    applyKeyboardInteractivity();

    // The surface grows by the padding on each side while the placement still
    // describes the text box; re-apply both so the box stays put.
    applyContentSize();
    applyAnchors();

    Q_EMIT adjustingChanged();
    qCDebug(lcOverlay) << "adjusting mode entered";
}

void OverlayWindow::endAdjusting(bool commit)
{
    if (!m_adjusting) {
        return;
    }

    if (m_dragging) {
        if (commit) {
            endDrag();
        } else {
            // Esc: drop the in-flight drag without reporting it. The settings
            // were never touched, so the caller re-applies the stored values.
            m_dragging = false;
            m_dragPoll.stop();
        }
    }

    m_adjusting = false;
    setAdjustingFontSize(0);
    m_keyboardInteractivity = LayerShellQt::Window::KeyboardInteractivityNone;
    applyKeyboardInteractivity();

    // Back to the configured click-through, the unpadded size and the stored
    // placement's margins.
    applyInputRegion();
    applyContentSize();
    applyAnchors();

    Q_EMIT adjustingChanged();
    qCDebug(lcOverlay) << "adjusting mode left, committed" << commit;
}

void OverlayWindow::beginDrag(bool handlesFont)
{
    if (!m_adjusting || m_dragging) {
        return;
    }

    m_dragging = true;
    m_dragHandlesFont = handlesFont;
    // Global screen coordinates: the surface follows the cursor, so its own
    // local coordinates would barely move and the drag would stall or jitter.
    m_dragOriginCursor = QCursor::pos();
    // The point at grab time is the frame of reference the position gesture is
    // compared against on release.
    m_dragStartPlacement = m_placement;

    if (handlesFont) {
        m_dragStartFontSize = m_adjustingFontSize > 0 ? m_adjustingFontSize : m_fontSize;
    } else {
        // The box size is captured once: while the lyric text keeps updating the
        // box changes size, and the surface is frozen at this box for as long as
        // the drag lasts so nothing resizes under the pointer.
        m_dragBox = adjustmentBox();
    }

    // Take the drag over from QML's translation signal: read the global cursor
    // ourselves until the drag ends.
    m_dragPoll.start();
}

void OverlayWindow::updateDrag()
{
    if (!m_dragging) {
        return;
    }

    const QPointF delta = QCursor::pos() - m_dragOriginCursor;

    if (m_dragHandlesFont) {
        // The top edge is pinned, so the box grows downward: dragging down grows
        // the text. There is no anchor to consult any more, so nothing can flip
        // the sign under the pointer.
        setAdjustingFontSize(fontSizeForDrag(m_dragStartFontSize, delta));
        return;
    }

    const QSize screen = screenSize();
    const QPoint moved = placementBoxTopLeft(m_dragStartPlacement, m_dragBox, screen) + QPoint(qRound(delta.x()), qRound(delta.y()));
    // Clamped so the box can never be dragged half off-screen, then turned into
    // the point. No live magnet: snapping while the drag is in flight is what
    // makes the position jump within AdjustMagnetPx of the middle; the magnet is
    // applied once, on release.
    setPlacement(placementFor(clampToScreen(moved, m_dragBox, screen), m_dragBox, screen, 0));
}

void OverlayWindow::endDrag()
{
    if (!m_dragging) {
        return;
    }
    m_dragging = false;
    // The poll exists only to feed updateDrag() while a drag is in flight.
    m_dragPoll.stop();

    // The release is the only moment anything reaches the settings, so no disk
    // write happens while the pointer is moving -- and a gesture that changed
    // nothing reports nothing, so only a real edit reaches the settings.
    if (m_dragHandlesFont) {
        const int points = m_adjustingFontSize > 0 ? m_adjustingFontSize : m_dragStartFontSize;
        if (points != m_dragStartFontSize) {
            Q_EMIT fontSizeEdited(points);
        }
        return;
    }

    // Apply the magnet once, on release, so a release near the middle lands
    // exactly at 50 %. The live point the drag showed is kept otherwise:
    // re-deriving a point that is already exact would only add rounding. Without
    // a screen there is nothing to magnetise against.
    const QSize screen = screenSize();
    if (!screen.isEmpty()) {
        const QPoint topLeft = placementBoxTopLeft(m_placement, m_dragBox, screen);
        if (placementFor(topLeft, m_dragBox, screen, AdjustMagnetPx).horizontalPercent == 50.0) {
            Placement snapped = m_placement;
            snapped.horizontalPercent = 50.0;
            setPlacement(snapped);
        }
    }

    if (m_placement != m_dragStartPlacement) {
        Q_EMIT placementEdited(m_placement.horizontalPercent, m_placement.verticalPercent);
    }

    // The surface was frozen at the grab-time box for as long as the drag lasted;
    // now that it is over, re-apply the real content size.
    applyContentSize();
}

void OverlayWindow::setAdjustingFontSize(int points)
{
    if (m_adjustingFontSize == points) {
        return;
    }
    m_adjustingFontSize = points;
    Q_EMIT adjustingFontSizeChanged();
}

void OverlayWindow::applyAnchors()
{
    if (!m_layerWindow) {
        return;
    }

    // The margins are derived from the point and the box the surface is sized to
    // right now, so the text box's centre and top edge stay on the point even as
    // the box grows or shrinks with the lyrics. While a position drag is in
    // flight that box is the frozen grab-time one, matching applyContentSize().
    const QSize box = m_dragging && !m_dragHandlesFont ? m_dragBox : adjustmentBox();
    const QPoint topLeft = placementBoxTopLeft(m_placement, box, screenSize());
    const PlacementAnchors state = anchorsFor(topLeft, m_adjusting ? AdjustPadding : 0);

    if (m_anchorsPushed && state.anchors == m_pushedAnchors && state.margins == m_pushedMargins) {
        return;
    }
    m_layerWindow->setAnchors(state.anchors);
    m_layerWindow->setMargins(state.margins);
    m_pushedAnchors = state.anchors;
    m_pushedMargins = state.margins;
    m_anchorsPushed = true;
}

void OverlayWindow::applyInputRegion(bool force)
{
    // TRAP: an empty QRegion() clears the mask, which makes the surface accept
    // ALL input — the opposite of click-through. So "no input" is expressed as a
    // 1x1 region in the corner, never as an empty one. Conversely, "full input"
    // is exactly the cleared (empty) mask.
    //
    // Adjusting temporarily overrides the click-through setting with full input;
    // between drags and after exit the setting is back in charge.
    const bool clickThroughNow = m_clickThrough && !m_adjusting;
    const QRegion region = clickThroughNow ? QRegion(0, 0, 1, 1) : QRegion();

    if (!force && region == m_inputRegion) {
        return;
    }

    setMask(region);
    m_inputRegion = region;
    qCDebug(lcOverlay) << (clickThroughNow ? "input region: 1x1 (click-through)" : "input region: full surface");
    commitSurface();
}

void OverlayWindow::commitSurface()
{
    // Before the first map the shell integration owns the commits; a manual
    // commit then is wrong. Only a mapped surface's input-region change needs
    // publishing now: QWindow::setMask() sends wl_surface.set_input_region
    // without committing, and the region is double-buffered, so the compositor
    // keeps the old one until the surface is committed.
    if (!isExposed()) {
        return;
    }

    auto *nativeInterface = QGuiApplication::platformNativeInterface();
    if (!nativeInterface) {
        return;
    }

    if (auto *surface = static_cast<wl_surface *>(nativeInterface->nativeResourceForWindow(QByteArrayLiteral("surface"), this))) {
        wl_surface_commit(surface);
    }
}

void OverlayWindow::setContentSize(qreal width, qreal height)
{
    // Always the text's size, never the adjusting padding: callers describe the
    // text box, and applyContentSize() adds the padding to the surface itself.
    m_contentSize = QSize(qMax(1, static_cast<int>(std::ceil(width))), qMax(1, static_cast<int>(std::ceil(height))));
    applyContentSize();
}

void OverlayWindow::applyContentSize()
{
    // While a position drag is in flight the surface is frozen at the box it was
    // grabbed with: the music can advance and the lyrics change size, but the
    // box under the pointer must not resize or the grab would jump. endDrag()
    // re-applies the real content size once the drag is over.
    QSize size = m_dragging && !m_dragHandlesFont ? m_dragBox : adjustmentBox();
    if (m_adjusting) {
        size += QSize(2 * AdjustPadding, 2 * AdjustPadding);
    }

    if (!m_sizePushed || size != m_pushedSize) {
        if (m_layerWindow) {
            m_layerWindow->setDesiredSize(size);
        } else {
            resize(size);
        }
        m_sizePushed = true;
        m_pushedSize = size;
    }

    // The box width follows the lyrics, so the left margin has to be re-derived
    // from the point on every content-size change: without this the horizontal
    // centre would slide whenever a line of a different length is shown. This is
    // the core of the "the text does not drift" fix.
    applyAnchors();
}

} // namespace kshadowlrc
