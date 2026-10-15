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

    // While a drag is in flight the reported cursor is read on a fast timer as
    // well as from QML's translation signal, which does fire while the pointer
    // moves but can go quiet -- the surface follows the pointer, so the pointer
    // barely moves within the window. updateDrag() stays the one place the gesture
    // is applied and applyPointerSample() holds the arithmetic it feeds; a call
    // that brings no new reading costs nothing.
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
    // The origin of the gesture in whatever coordinates Qt reports the pointer
    // in -- see the class comment: under Wayland that is the window origin Qt
    // believes in plus the pointer's place inside the surface, not a screen
    // position. The gestures work from the difference against this value.
    m_dragOriginCursor = QCursor::pos();
    // One line per gesture, and the only place the drag says where it thinks the
    // pointer is. Reading it back together with the window's believed position
    // and the screen tells a real screen position apart from that offset one
    // when a drag misbehaves; no code reads these values.
    qCDebug(lcOverlay) << "drag begin: cursor" << m_dragOriginCursor << "window" << position() << "screen" << (m_screen ? m_screen->geometry() : QRect())
                       << "dpr" << devicePixelRatio() << "handlesFont" << handlesFont;
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

        // Seed the sample sequence with the grab itself: the very reading just
        // taken, the surface top-left that reading was measured against, and the
        // unclamped ideal the drag starts from.
        m_dragPositionSample = m_dragOriginCursor;
        m_dragIdealTopLeft = placementBoxTopLeft(m_dragStartPlacement, m_dragBox, screenSize());
        m_dragSampleSurface = m_dragIdealTopLeft;
    }

    // Take the drag over from QML's translation signal: read the reported
    // pointer position ourselves until the drag ends.
    m_dragPoll.start();
}

void OverlayWindow::updateDrag()
{
    // The guard lives in applyPointerSample(), so this stays nothing but the
    // platform's own report fed to the gesture's arithmetic.
    applyPointerSample(QCursor::pos());
}

void OverlayWindow::applyPointerSample(const QPoint &reportedPointer)
{
    if (!m_dragging) {
        return;
    }

    if (m_dragHandlesFont) {
        // No compensation needed here. A font gesture pins the box's top edge --
        // anchorsFor() only ever sets a top-left margin and verticalPercent does
        // not change -- so the surface does not move vertically and the reported
        // vertical movement is already clean. The horizontal report does carry
        // the surface's own movement (the box widens about its centre, so its
        // left edge moves), which is why the gesture reads only the y component.
        setAdjustingFontSize(fontSizeForDrag(m_dragStartFontSize, reportedPointer - m_dragOriginCursor));
        return;
    }

    // No new sample: the pointer is where it was, so nothing moves. This is the
    // whole fix for a box that walked off screen. The poll keeps firing while the
    // pointer is still, but the sample it reads freezes with the pointer; adding
    // that frozen movement again on every tick sent the box flying. Field
    // evidence: a 1.2 s window with 0 wl_pointer.motion events, 150 set_margin
    // requests and margin top rising 1 px every ~8 ms (+150 px in all), while the
    // sample's x never changed.
    if (reportedPointer == m_dragPositionSample) {
        return;
    }

    const QSize screen = screenSize();
    // Where the compositor has the surface right now: the top-left the last push
    // put there, read back through the placement. This is the surface the current
    // sample was measured against.
    const QPoint surfaceNow = placementBoxTopLeft(m_placement, m_dragBox, screen);
    // The pointer's real movement since the previous sample: the change in the
    // reading plus the surface's own movement over the same interval, so the
    // surface's share is restored exactly once (see dragPointerDelta()). It goes
    // on the ideal, which is never clamped, so a box pinned at an edge keeps the
    // travel it missed and comes back exactly under the pointer.
    m_dragIdealTopLeft += dragPointerDelta(reportedPointer, m_dragPositionSample, surfaceNow, m_dragSampleSurface);

    // Clamp the pushed value only, never the ideal above. No live magnet:
    // snapping while the drag is in flight is what makes the position jump within
    // AdjustMagnetPx of the middle; the magnet is applied once, on release.
    setPlacement(placementFor(clampToScreen(m_dragIdealTopLeft, m_dragBox, screen), m_dragBox, screen, 0));

    // Remember this sample, and - for the next one - the surface it was measured
    // against, which is the top-left read above, not the just-pushed one: the
    // next step's surfaceNow already includes this step's push.
    m_dragPositionSample = reportedPointer;
    m_dragSampleSurface = surfaceNow;
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

    // The layer-shell state just pushed is double-buffered: the compositor only
    // applies it from a wl_surface.commit. LayerShellQt's setters merely send the
    // raw set_anchor/set_margin requests -- they never commit -- so without this
    // the new margins would wait for Qt's next rendered frame. During a position
    // drag the scene is deliberately frozen (applyContentSize()) and no frame
    // repaints on a margin change, so the surface would trail the pointer and
    // then catch up. A commit without a new buffer is legal and enough: it makes
    // a Content Update from the pending state even when that state was "not
    // touched" by the render, so the property-only change lands immediately.
    // commitSurface() is guarded by isExposed() so that before the first map --
    // when the shell integration owns the surface's commits and a manual one
    // would be wrong -- this is a no-op; that is exactly the guard the helper
    // already documents for the input region.
    commitSurface();
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
