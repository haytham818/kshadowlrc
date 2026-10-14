// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 KShadowLrc Contributors

#include "platform/overlaywindow.h"

#include "kshadowlrc.h"
#include "platform/outputresolution.h"

#include <LayerShellQt/Window>

#include <QGuiApplication>
#include <QLoggingCategory>
#include <QScreen>
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

    // The output has to be chosen before the surface exists, so this runs here and
    // not from show(). Later screen changes only re-resolve and warn: see
    // OverlayWindow::setOutputName().
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

    if (isVisible()) {
        // LayerShellQt binds the layer surface to the output when that surface is
        // created, which is what show() does. Re-pointing a mapped one would mean
        // recreating it, so be honest about what this does and does not do.
        qCWarning(lcOverlay) << "output changed to" << m_screen->name() << "after the overlay was mapped; it will move on the next start";
    }

    m_layerWindow->setScreen(m_screen);
    // Keep the QWindow in step too: that is what decides the device pixel ratio
    // and which screen the QML scene is composed for.
    QWindow::setScreen(m_screen);

    qCDebug(lcOverlay) << "output requested" << (m_outputName.isEmpty() ? QStringLiteral("<primary>") : m_outputName) << "resolved to" << m_screen->name()
                       << m_screen->geometry();
    Q_EMIT activeOutputChanged(m_screen->name());
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
    m_layerWindow->setKeyboardInteractivity(LayerShellQt::Window::KeyboardInteractivityNone);
    // Reserve no screen space, so maximized windows are unaffected.
    m_layerWindow->setExclusiveZone(0);
    // Keep the surface alive across output changes.
    m_layerWindow->setCloseOnDismissed(false);
}

void OverlayWindow::setEdge(Edge edge)
{
    if (m_edge == edge) {
        return;
    }
    m_edge = edge;
    applyAnchors();
    if (isVisible()) {
        // Pulse a frame so the new anchor set is committed promptly.
        update();
    }
}

void OverlayWindow::setMargin(int margin)
{
    if (m_margin == margin) {
        return;
    }
    m_margin = margin;
    applyAnchors();
    if (isVisible()) {
        update();
    }
}

void OverlayWindow::setClickThrough(bool enabled)
{
    if (m_clickThrough == enabled) {
        return;
    }
    m_clickThrough = enabled;
    applyInputRegion();
}

void OverlayWindow::applyAnchors()
{
    if (!m_layerWindow) {
        return;
    }

    // LayerShellQt declares the Anchors flags but no operator| for them, so the
    // bits are set with setFlag().
    LayerShellQt::Window::Anchors anchors;
    QMargins margins;

    switch (m_edge) {
    case Edge::Bottom:
        anchors.setFlag(LayerShellQt::Window::AnchorBottom);
        anchors.setFlag(LayerShellQt::Window::AnchorLeft);
        anchors.setFlag(LayerShellQt::Window::AnchorRight);
        margins.setBottom(m_margin);
        break;
    case Edge::Top:
        anchors.setFlag(LayerShellQt::Window::AnchorTop);
        anchors.setFlag(LayerShellQt::Window::AnchorLeft);
        anchors.setFlag(LayerShellQt::Window::AnchorRight);
        margins.setTop(m_margin);
        break;
    case Edge::Left:
        anchors.setFlag(LayerShellQt::Window::AnchorLeft);
        anchors.setFlag(LayerShellQt::Window::AnchorTop);
        anchors.setFlag(LayerShellQt::Window::AnchorBottom);
        margins.setLeft(m_margin);
        break;
    case Edge::Right:
        anchors.setFlag(LayerShellQt::Window::AnchorRight);
        anchors.setFlag(LayerShellQt::Window::AnchorTop);
        anchors.setFlag(LayerShellQt::Window::AnchorBottom);
        margins.setRight(m_margin);
        break;
    }

    m_layerWindow->setAnchors(anchors);
    m_layerWindow->setMargins(margins);
}

void OverlayWindow::applyInputRegion()
{
    // TRAP: an empty QRegion() clears the mask, which makes the surface accept
    // ALL input — the opposite of click-through. So "no input" is expressed as a
    // 1x1 region in the corner, never as an empty one. Conversely, "full input"
    // is exactly the cleared (empty) mask.
    const QRegion region = m_clickThrough ? QRegion(0, 0, 1, 1) : QRegion();

    if (region == m_inputRegion) {
        return;
    }

    setMask(region);
    m_inputRegion = region;
    qCDebug(lcOverlay) << (m_clickThrough ? "input region: 1x1 (click-through)" : "input region: full surface");
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
    const QSize size(qMax(1, static_cast<int>(std::ceil(width))), qMax(1, static_cast<int>(std::ceil(height))));

    if (m_layerWindow) {
        // The anchored (stretched) axis ignores this; only the free axis follows
        // the lyric content.
        m_layerWindow->setDesiredSize(size);
    } else {
        resize(size);
    }
}

} // namespace kshadowlrc
