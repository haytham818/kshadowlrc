// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 KShadowLrc Contributors

#pragma once

#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>

QT_BEGIN_NAMESPACE
class QAction;
class QKeySequence;
class QMenu;
QT_END_NAMESPACE

class KStatusNotifierItem;

namespace kshadowlrc
{

class LyricsController;

/*! Tray icon + global shortcuts: the control surface for a window that has no
 *  window decorations, no taskbar entry and is click-through. */
class ControlService : public QObject
{
    Q_OBJECT

public:
    /*! One offset step, in milliseconds. */
    static constexpr int OffsetStepMs = 500;
    /*! The offset is clamped to +/- this many milliseconds. */
    static constexpr int MaxOffsetMs = 10000;

    /*!
     * \param controller the lyric bridge the offset actions drive. Required and
     *        borrowed: the control surface is meaningless without one, so it
     *        must not be null, and ControlService never takes ownership (the
     *        parent owns it and must outlive this object).
     * \param parent optional QObject parent.
     */
    explicit ControlService(LyricsController *controller, QObject *parent = nullptr);
    ~ControlService() override;

    /*! The action registered under \p id, or nullptr when there is no such
     *  action. Exists so the test can trigger the very actions the menu and the
     *  shortcuts trigger. */
    QAction *action(const QString &id) const;

    /*! Follow the overlay's visibility so the show/hide action reads right. */
    void setOverlayVisible(bool visible);

public Q_SLOTS:
    /*! Move the lyric offset one step earlier, clamped. */
    void offsetEarlier();
    /*! Move the lyric offset one step later, clamped. */
    void offsetLater();
    /*! Set the lyric offset back to zero. */
    void resetOffset();

Q_SIGNALS:
    /*! The user asked for the settings window; the parent opens it. */
    void settingsRequested();
    /*! The user asked to show/hide the overlay; the parent owns the surface. */
    void overlayVisibilityToggleRequested();
    /*! The user asked the application to quit. */
    void quitRequested();

private:
    /*! Create the shared actions and connect them to the signals/slots. */
    void createActions();
    /*! Build the context menu out of the shared actions. */
    void createTray();
    /*! Register \p action under \p sequence with KGlobalAccel, warning (never
     *  stealing) when the key is already held by another component. */
    void registerShortcut(QAction *action, const QKeySequence &sequence);
    /*! The one place the offset is clamped before it reaches the controller. */
    void clampAndSetOffset(qint64 proposedMs);
    /*! Re-label and re-check the toggle action to match m_overlayVisible. */
    void updateToggleAction();
    /*! Push the current lyric state and offset into the tray tool tip. */
    void updateToolTip();

    LyricsController *m_controller = nullptr;
    QHash<QString, QAction *> m_actions;
    KStatusNotifierItem *m_tray = nullptr;
    QMenu *m_menu = nullptr;
    bool m_overlayVisible = true;
};

/*!
 * Whether \a holderComponents (the componentUniqueName()s KGlobalAccel reports
 * for a key) include a component other than \a ourComponent.
 *
 * KGlobalAccel::isGlobalShortcutAvailable() does not exclude the component
 * passed to it, so a key this application registered itself is reported as
 * taken on every later start. Asking globalShortcutsByKey() for the actual
 * holders and comparing their names tells a real conflict from our own entry.
 */
bool shortcutHeldByAnotherComponent(const QStringList &holderComponents, const QString &ourComponent);

} // namespace kshadowlrc
