// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 KShadowLrc Contributors

#pragma once

#include "platform/overlayplacement.h"

#include <LayerShellQt/Window>

#include <QPoint>
#include <QQuickView>
#include <QRegion>
#include <QSize>
#include <QString>
#include <QTimer>

QT_BEGIN_NAMESPACE
class QScreen;
QT_END_NAMESPACE

namespace kshadowlrc
{

/*!
 * The floating lyrics overlay: a wlr-layer-shell surface that stays above every
 * window with no user-configured window rules.
 *
 * Anchored to the screen's top-left corner and positioned by the point a
 * Placement describes - the text box's horizontal centre and its top edge - so
 * the margins are re-derived from that point and the current box on every
 * content-size change. The box takes its size from the lyric content, which QML
 * reports through setContentSize(); a longer or shorter line therefore never
 * moves the text the user is reading. It takes no keyboard focus and, by
 * default, no pointer input (clicks pass through to whatever is underneath).
 *
 * In adjusting mode the surface instead takes pointer input, asks for keyboard
 * interactivity OnDemand so Esc can arrive, and lets the user drag its position
 * and font size. That drag is driven by the window's own poll of the reported
 * pointer position, not by QML's translation delta: the surface follows the
 * pointer, so the pointer barely moves within the window and that signal is no
 * trigger for a smooth drag (see m_dragPoll). A changed anchor set is committed
 * to the compositor as soon as it is pushed rather than left to the next
 * rendered frame (see applyAnchors()). That mode is runtime-only: it never
 * reaches the settings file.
 *
 * The drag reads QCursor::pos(). Under Wayland Qt builds that value from the
 * pointer's surface-local coordinates plus the window origin Qt believes in, and
 * a layer surface is never told its real position, so the absolute value is
 * offset from the true screen position -- it is not a screen coordinate. Its
 * difference across a gesture is not the pointer's own movement either: it also
 * carries whatever the surface itself moved by, and the surface is exactly what
 * the drag moves. applyDragDelta() corrects for that: it adds the delta to the
 * top-left actually pushed, the best estimate of where the compositor has put
 * the surface, so the surface's own movement cancels out and the box tracks the
 * pointer 1:1.
 */
class OverlayWindow : public QQuickView
{
    Q_OBJECT
    Q_PROPERTY(bool adjusting READ isAdjusting NOTIFY adjustingChanged)
    /*! Font size while a font drag is in flight; 0 means "use the setting". */
    Q_PROPERTY(int adjustingFontSize READ adjustingFontSize NOTIFY adjustingFontSizeChanged)
    /*! The padding the surface carries on each side while adjusting, in px. */
    Q_PROPERTY(int adjustPadding READ adjustPadding CONSTANT)

public:
    /*! Padding on each side of the text box while adjusting, in logical pixels. */
    static constexpr int AdjustPadding = 24;
    /*! The placement box is never smaller than this while adjusting, so an empty
     *  overlay still has something to grab. */
    static constexpr QSize MinAdjustBox = QSize(240, 80);
    /*! A released position drag within this many px of the middle snaps to 50 %. */
    static constexpr int AdjustMagnetPx = 24;

    explicit OverlayWindow(QWindow *parent = nullptr);

    /*!
     * The placement point the surface is positioned by: the text box's
     * horizontal centre and its top edge, as percentages of the screen. Setting
     * it applies the anchors at once; during a drag it is updated live and only
     * the release is reported through placementEdited().
     */
    void setPlacement(const Placement &placement);
    Placement placement() const
    {
        return m_placement;
    }

    /*! The base font size a font drag scales from, kept in step with the setting. */
    void setFontSize(int points);
    int fontSize() const
    {
        return m_fontSize;
    }

    /*!
     * When true (the default), the surface publishes a 1x1 input region so all
     * pointer input passes through to the window underneath. Adjusting mode
     * temporarily overrides it with the full input region.
     */
    void setClickThrough(bool enabled);
    bool clickThrough() const
    {
        return m_clickThrough;
    }

    /*! True while the overlay is in adjusting mode. Runtime state only. */
    bool isAdjusting() const
    {
        return m_adjusting;
    }

    /*! The font size a font drag has reached; 0 when no drag has set one. */
    int adjustingFontSize() const
    {
        return m_adjustingFontSize;
    }

    /*! The padding the surface carries on each side while adjusting, in px. */
    int adjustPadding() const
    {
        return AdjustPadding;
    }

    /*!
     * The input region the surface publishes: a 1x1 mask while click-through,
     * the full (empty) region otherwise. Exposed so it can be asserted without
     * a compositor.
     */
    QRegion inputRegion() const
    {
        return m_inputRegion;
    }

    /*!
     * The keyboard interactivity the surface requests: None normally, OnDemand
     * while adjusting so Esc can arrive. Exposed for the same reason.
     */
    LayerShellQt::Window::KeyboardInteractivity keyboardInteractivity() const
    {
        return m_keyboardInteractivity;
    }

    /*!
     * The surface size last pushed to the layer surface (or to the QWindow when
     * there is none). Exposed only so its behaviour can be asserted without a
     * compositor: offscreen there is no layer surface to query, so the size the
     * window decided on is read back here instead.
     */
    QSize pushedSurfaceSize() const
    {
        return m_pushedSize;
    }

    /*!
     * Whether the drag's reported-cursor poll is running. Exposed only so the
     * poll's lifetime can be asserted without a compositor: offscreen the cursor
     * cannot move, so a drag driven through the public API never sees the
     * pointer change and the poll's own start/stop is read back here instead.
     */
    bool dragPolling() const
    {
        return m_dragPoll.isActive();
    }

    /*!
     * The box the placement maths run on when no drag is in flight: the text
     * box, raised to MinAdjustBox while adjusting so an empty overlay is still
     * grabbable. Never the padded surface, so the padding cannot leak into a
     * stored margin. (While a position drag is in flight the frozen grab-time
     * box stands in; see applyContentSize().)
     */
    QSize adjustmentBox() const;

    /*! Enter adjusting mode: full input, OnDemand keyboard, padded surface. */
    Q_INVOKABLE void beginAdjusting();
    /*!
     * Leave adjusting mode. \p commit false (Esc) discards the in-flight drag and
     * leaves the settings alone; the caller re-applies the stored settings.
     */
    Q_INVOKABLE void endAdjusting(bool commit);

    /*!
     * Start a drag. \p handlesFont picks the font-size gesture; otherwise it
     * moves the box. The origin is taken from QCursor::pos(), which under Wayland
     * is not a screen position: it is the pointer's surface-local coordinates
     * plus the window origin Qt believes in (see the class comment). The gestures
     * work from the difference against this value, and that difference carries
     * the surface's own movement because the surface is what the drag moves;
     * applyDragDelta() adds that movement back. Also starts the poll that keeps
     * reading the reported cursor until the drag ends.
     */
    Q_INVOKABLE void beginDrag(bool handlesFont);
    /*!
     * Feed the current cursor's report to applyDragDelta(). Called by the drag
     * poll every few milliseconds, and once immediately by QML's
     * onActiveTranslationChanged. Recomputing while the pointer has not moved
     * does no work: both setters compare before pushing.
     */
    Q_INVOKABLE void updateDrag();
    /*!
     * The drag's arithmetic, for a pointer the platform reports as having moved
     * \a reportedDelta since the drag was grabbed. updateDrag() is only this fed
     * with the platform's own report; the movement is an argument so a test can
     * drive a whole gesture, which the offscreen cursor cannot (it cannot move).
     */
    void applyDragDelta(const QPoint &reportedDelta);
    /*!
     * Finish the drag, stopping the poll and reporting the result through the
     * *Edited signals. A gesture that changed nothing reports nothing, so only a
     * real edit reaches the settings.
     */
    Q_INVOKABLE void endDrag();

    /*!
     * Called from QML when the lyric layout changes, so the layer surface sizes
     * itself to the rendered lyrics on its free axis. It always receives the
     * text's size, never the adjusting padding.
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
    void adjustingChanged();
    void adjustingFontSizeChanged();
    /*! The user released a position drag: the box's new placement point. */
    void placementEdited(double horizontalPercent, double verticalPercent);
    /*! The user released a font drag: the new font size in points. */
    void fontSizeEdited(int points);

    void outputNameChanged(const QString &name);
    void activeOutputChanged(const QString &name);

private:
    void applyLayerShell();
    void applyAnchors();
    /*! Push the desired surface size: the text box, padded while adjusting. */
    void applyContentSize();
    /*! Request the current keyboard interactivity on the layer surface. */
    void applyKeyboardInteractivity();
    /*!
     * Publish the input region (the click-through mask). A \p force re-sends the
     * mask even when it is unchanged, which a rebuilt surface needs: the region
     * lives on the wl_surface that went away with the old platform window.
     */
    void applyInputRegion(bool force = false);
    /*!
     * Commit the raw wl_surface so a just-pushed input region or layer-shell
     * state (anchors/margins) takes effect now, without waiting for Qt's next
     * rendered frame. A no-op before the surface is exposed, when the shell
     * integration still owns its commits.
     */
    void commitSurface();
    /*! Resolve the configured output against the live screen list and apply it. */
    void resolveOutput();
    /*!
     * Recreate the platform window (and its layer surface) so it binds to the
     * current screen and re-reads the whole layer-shell state. Only valid while
     * the overlay is mapped; the mapped state is restored.
     */
    void rebuildSurface();
    /*! Set adjustingFontSize, emitting only when it really changes. */
    void setAdjustingFontSize(int points);
    /*! The screen the placement maths run against, or a null size with none. */
    QSize screenSize() const;

    LayerShellQt::Window *m_layerWindow = nullptr;
    Placement m_placement;
    int m_fontSize = 32;
    bool m_clickThrough = true;
    QRegion m_inputRegion;
    LayerShellQt::Window::KeyboardInteractivity m_keyboardInteractivity = LayerShellQt::Window::KeyboardInteractivityNone;
    QSize m_contentSize = QSize(1, 1);

    // The layer state last pushed, so a redundant apply is skipped. rebuildSurface()
    // deliberately re-reads all of it from the surviving LayerShellQt::Window, so
    // the caches stay valid across a rebuild and nothing needs invalidating there.
    bool m_sizePushed = false;
    QSize m_pushedSize;
    bool m_anchorsPushed = false;
    LayerShellQt::Window::Anchors m_pushedAnchors;
    QMargins m_pushedMargins;

    // Adjusting mode. None of this reaches the settings file.
    bool m_adjusting = false;
    int m_adjustingFontSize = 0;
    bool m_dragging = false;
    bool m_dragHandlesFont = false;
    QPoint m_dragOriginCursor;
    Placement m_dragStartPlacement;
    QSize m_dragBox = QSize(1, 1);
    int m_dragStartFontSize = 0;

    // Polls the reported cursor while a drag is in flight. The surface follows
    // the pointer, so QML's translation delta barely changes and cannot be the
    // drag's trigger; this timer is. Started in beginDrag(), stopped in endDrag()
    // and on a cancelled exit (Esc).
    QTimer m_dragPoll;

    QString m_outputName;
    QScreen *m_screen = nullptr;
};

} // namespace kshadowlrc
