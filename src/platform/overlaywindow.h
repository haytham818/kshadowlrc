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
     * The choice is applied when the surface is created, so a change made after
     * the overlay is on screen does not move it — it is picked up on the next
     * start. Re-targeting a live surface would mean recreating the layer surface,
     * which is not implemented.
     */
    void setOutputName(const QString &name);
    QString outputName() const
    {
        return m_outputName;
    }

    /*!
     * The output actually in use, or an empty string while the overlay has not
     * been mapped yet. Differs from outputName() when that one fell back.
     */
    QString activeOutputName() const;

Q_SIGNALS:
    void outputNameChanged(const QString &name);
    void activeOutputChanged(const QString &name);

private:
    void applyLayerShell();
    void applyAnchors();
    void applyInputRegion();
    /*! Commit the raw wl_surface so a changed input region takes effect now. */
    void commitSurface();
    /*! Resolve the configured output against the live screen list and apply it. */
    void resolveOutput();

    LayerShellQt::Window *m_layerWindow = nullptr;
    Edge m_edge = Edge::Bottom;
    int m_margin = 64;
    bool m_clickThrough = true;
    QRegion m_inputRegion;
    QString m_outputName;
    QScreen *m_screen = nullptr;
};

} // namespace kshadowlrc
