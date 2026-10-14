// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 KShadowLrc Contributors

#pragma once

#include <QQuickView>
#include <QRegion>
#include <QString>

QT_BEGIN_NAMESPACE
class QScreen;
namespace LayerShellQt
{
class Window;
}
QT_END_NAMESPACE

namespace kshadowlrc
{

/*!
 * The floating lyrics overlay: a wlr-layer-shell surface that stays above every
 * window with no user-configured window rules.
 *
 * Anchors one screen edge plus both cross-axis edges, so it is stretched along
 * that edge; the free axis follows the lyric content, which QML reports through
 * setContentSize(). It takes no keyboard focus and, by default, no pointer
 * input (clicks pass through to whatever is underneath).
 */
class OverlayWindow : public QQuickView
{
    Q_OBJECT

public:
    enum class Edge {
        Top,
        Bottom,
        Left,
        Right,
    };
    Q_ENUM(Edge)

    explicit OverlayWindow(QWindow *parent = nullptr);

    /*! Screen edge the overlay is anchored to. Default: Bottom. */
    void setEdge(Edge edge);
    Edge edge() const
    {
        return m_edge;
    }

    /*! Margin (px) between the overlay and its anchored screen edge. */
    void setMargin(int margin);
    int margin() const
    {
        return m_margin;
    }

    /*!
     * When true (the default), the surface publishes a 1x1 input region so all
     * pointer input passes through to the window underneath.
     */
    void setClickThrough(bool enabled);
    bool clickThrough() const
    {
        return m_clickThrough;
    }

    /*!
     * Called from QML when the lyric layout changes, so the layer surface sizes
     * itself to the rendered lyrics on its free axis.
     */
    Q_INVOKABLE void setContentSize(qreal width, qreal height);

    /*!
     * Name of the output the overlay should live on, as reported by QScreen
     * (e.g. "HDMI-A-1"). Empty means "the primary output". An unknown name also
     * falls back to the primary output; see resolveOutputIndex().
     *
     * A mapped layer surface cannot be re-targeted: the protocol has no request
     * for it and the output is fixed when the surface is created. So a change
     * made while the overlay is on screen recreates the layer surface on the new
     * output, and one made while it is hidden takes effect on the next map.
     */
    void setOutputName(const QString &name);
    QString outputName() const
    {
        return m_outputName;
    }

    /*!
     * The output actually in use: the resolved screen, or an empty string while
     * no output is available. Differs from outputName() when that one fell back.
     */
    QString activeOutputName() const;

Q_SIGNALS:
    void outputNameChanged(const QString &name);
    void activeOutputChanged(const QString &name);

private:
    void applyLayerShell();
    void applyAnchors();
    /*!
     * Publish the input region (the click-through mask). A \p force re-sends the
     * mask even when it is unchanged, which a rebuilt surface needs: the region
     * lives on the wl_surface that went away with the old platform window.
     */
    void applyInputRegion(bool force = false);
    /*! Commit the raw wl_surface so a changed input region takes effect now. */
    void commitSurface();
    /*! Resolve the configured output against the live screen list and apply it. */
    void resolveOutput();
    /*!
     * Recreate the platform window (and its layer surface) so it binds to the
     * current screen and re-reads the whole layer-shell state. Only valid while
     * the overlay is mapped; the mapped state is restored.
     */
    void rebuildSurface();

    LayerShellQt::Window *m_layerWindow = nullptr;
    Edge m_edge = Edge::Bottom;
    int m_margin = 64;
    bool m_clickThrough = true;
    QRegion m_inputRegion;
    QString m_outputName;
    QScreen *m_screen = nullptr;
};

} // namespace kshadowlrc
