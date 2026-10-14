// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 KShadowLrc Contributors

#include "system/controlservice.h"

#include "lyrics/controller.h"

#include <KGlobalAccel>
#include <KLocalizedString>
#include <KStatusNotifierItem>

#include <QAction>
#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QIcon>
#include <QKeySequence>
#include <QLoggingCategory>
#include <QMenu>

#include <algorithm>
#include <cmath>

Q_LOGGING_CATEGORY(lcSystem, "kshadowlrc.system")

namespace kshadowlrc
{

namespace
{

// The action ids are the stable keys KGlobalAccel writes into the user's
// kglobalshortcutsrc. They must never drift; the menu and the shortcuts share
// one action per id.
QString offsetEarlierId()
{
    return QStringLiteral("lyrics-offset-earlier");
}

QString offsetLaterId()
{
    return QStringLiteral("lyrics-offset-later");
}

QString offsetResetId()
{
    return QStringLiteral("lyrics-offset-reset");
}

QString toggleOverlayId()
{
    return QStringLiteral("toggle-overlay");
}

QString settingsId()
{
    return QStringLiteral("settings");
}

QString quitId()
{
    return QStringLiteral("quit");
}

/*! "Offset: 0" or "Offset: +1.5 s" / "Offset: -0.5 s". */
QString offsetLabel(qint64 ms)
{
    if (ms == 0) {
        return i18nc("@info:tooltip", "Offset: 0");
    }
    return i18nc("@info:tooltip lyric offset",
                 "Offset: %1%2 s",
                 ms > 0 ? QStringLiteral("+") : QStringLiteral("-"),
                 QString::number(std::abs(ms) / 1000.0, 'f', 1));
}

} // namespace

bool shortcutHeldByAnotherComponent(const QStringList &holderComponents, const QString &ourComponent)
{
    return std::any_of(holderComponents.cbegin(), holderComponents.cend(), [&ourComponent](const QString &holder) {
        return holder != ourComponent;
    });
}

ControlService::ControlService(LyricsController *controller, QObject *parent)
    : QObject(parent)
    , m_controller(controller)
{
    Q_ASSERT(m_controller);

    createActions();
    createTray();
    updateToggleAction();
    updateToolTip();

    connect(m_controller, &LyricsController::lyricOffsetChanged, this, &ControlService::updateToolTip);

    // Register the three global shortcuts. Each action is shared with the menu,
    // so the menu entry and the key press take exactly the same path.
    registerShortcut(m_actions.value(offsetEarlierId()), QKeySequence(Qt::META | Qt::ALT | Qt::Key_BracketLeft));
    registerShortcut(m_actions.value(offsetLaterId()), QKeySequence(Qt::META | Qt::ALT | Qt::Key_BracketRight));
    registerShortcut(m_actions.value(offsetResetId()), QKeySequence(Qt::META | Qt::ALT | Qt::Key_Backslash));
}

ControlService::~ControlService() = default;

QAction *ControlService::action(const QString &id) const
{
    return m_actions.value(id, nullptr);
}

void ControlService::createActions()
{
    // The three offset actions back both the tray menu and the global
    // shortcuts; the text is what System Settings shows for the shortcut.
    auto *earlier = new QAction(i18nc("@action global shortcut", "Lyrics Earlier"), this);
    earlier->setObjectName(offsetEarlierId());
    connect(earlier, &QAction::triggered, this, &ControlService::offsetEarlier);
    m_actions.insert(earlier->objectName(), earlier);

    auto *later = new QAction(i18nc("@action global shortcut", "Lyrics Later"), this);
    later->setObjectName(offsetLaterId());
    connect(later, &QAction::triggered, this, &ControlService::offsetLater);
    m_actions.insert(later->objectName(), later);

    auto *reset = new QAction(i18nc("@action global shortcut", "Reset Lyrics Offset"), this);
    reset->setObjectName(offsetResetId());
    connect(reset, &QAction::triggered, this, &ControlService::resetOffset);
    m_actions.insert(reset->objectName(), reset);

    // No default shortcut: this one lives in the menu and on a left click only.
    auto *toggle = new QAction(i18nc("@action global shortcut", "Hide Lyrics"), this);
    toggle->setObjectName(toggleOverlayId());
    toggle->setCheckable(true);
    connect(toggle, &QAction::triggered, this, &ControlService::overlayVisibilityToggleRequested);
    m_actions.insert(toggle->objectName(), toggle);

    auto *settings = new QAction(i18nc("@action global shortcut", "Configure KShadowLrc…"), this);
    settings->setObjectName(settingsId());
    connect(settings, &QAction::triggered, this, &ControlService::settingsRequested);
    m_actions.insert(settings->objectName(), settings);

    auto *quit = new QAction(i18nc("@action global shortcut", "Quit"), this);
    quit->setObjectName(quitId());
    connect(quit, &QAction::triggered, this, &ControlService::quitRequested);
    m_actions.insert(quit->objectName(), quit);
}

void ControlService::createTray()
{
    m_menu = new QMenu();
    m_menu->addAction(m_actions.value(toggleOverlayId()));
    m_menu->addSeparator();
    m_menu->addAction(m_actions.value(offsetEarlierId()));
    m_menu->addAction(m_actions.value(offsetLaterId()));
    m_menu->addAction(m_actions.value(offsetResetId()));
    m_menu->addSeparator();
    m_menu->addAction(m_actions.value(settingsId()));
    m_menu->addSeparator();
    m_menu->addAction(m_actions.value(quitId()));

    m_tray = new KStatusNotifierItem(this);
    m_tray->setCategory(KStatusNotifierItem::ApplicationStatus);
    m_tray->setTitle(i18n("KShadowLrc"));
    m_tray->setStandardActionsEnabled(false);
    // Active keeps a control surface for a click-through, taskbar-less window
    // always reachable in the tray.
    m_tray->setStatus(KStatusNotifierItem::Active);
    // The menu is handed over: the tray takes ownership and deletes it.
    m_tray->setContextMenu(m_menu);

    // Prefer the theme name (installed, theme-aware); the compiled-in resource
    // is the fallback for the build tree and for themes without our icon.
    const QString appId = QStringLiteral(KSHADOWLRC_APP_ID);
    if (QIcon::fromTheme(appId).isNull()) {
        m_tray->setIconByPixmap(QIcon(QStringLiteral(":/icons/io.github.haytham818.kshadowlrc.svg")));
    } else {
        m_tray->setIconByName(appId);
    }

    m_tray->setToolTip(appId, i18n("KShadowLrc"), offsetLabel(m_controller->lyricOffset()));

    connect(m_tray, &KStatusNotifierItem::activateRequested, this, [this](bool, const QPoint &) {
        Q_EMIT overlayVisibilityToggleRequested();
    });

    // Left click is only one way in; the keys and the menu work without a host,
    // so the tray being absent must not disable the control surface.
    QDBusConnectionInterface *bus = QDBusConnection::sessionBus().interface();
    if (!bus || !bus->isServiceRegistered(QStringLiteral("org.kde.StatusNotifierWatcher"))) {
        qCWarning(lcSystem) << "no StatusNotifier host on the session bus; the tray icon is unavailable (the global shortcuts still work)";
    }
}

void ControlService::registerShortcut(QAction *action, const QKeySequence &sequence)
{
    if (!action) {
        return;
    }
    const QString ourComponent = QStringLiteral(KSHADOWLRC_APP_ID);

    // globalShortcutsByKey() reports our own registration as a holder, so
    // filter it out: only a different component is a real conflict. Warn but do
    // not steal -- the key is the user's to reassign, not ours to take.
    const QList<KGlobalShortcutInfo> holders = KGlobalAccel::globalShortcutsByKey(sequence, KGlobalAccel::Equal);
    QStringList holderComponents;
    QStringList otherComponents;
    for (const KGlobalShortcutInfo &holder : holders) {
        holderComponents.append(holder.componentUniqueName());
        if (holder.componentUniqueName() != ourComponent) {
            otherComponents.append(QStringLiteral("%1 (%2)").arg(holder.componentUniqueName(), holder.componentFriendlyName()));
        }
    }
    if (shortcutHeldByAnotherComponent(holderComponents, ourComponent)) {
        qCWarning(lcSystem) << "global shortcut" << sequence.toString() << "for" << action->objectName()
                            << "is already taken by other components:" << otherComponents.join(QStringLiteral(", ")) << "; not stealing it";
    }

    auto *kga = KGlobalAccel::self();
    kga->setDefaultShortcut(action, {sequence});
    kga->setShortcut(action, {sequence}); // Autoloading: a rebound key survives

    const QList<QKeySequence> stored = kga->globalShortcut(QCoreApplication::applicationName(), action->objectName());
    QStringList keys;
    keys.reserve(stored.size());
    for (const QKeySequence &key : stored) {
        keys.append(key.toString());
    }
    qCInfo(lcSystem) << "registered global shortcut" << action->objectName() << "default" << sequence.toString() << "resolved" << keys;
}

void ControlService::clampAndSetOffset(qint64 proposedMs)
{
    const qint64 clamped = std::clamp(proposedMs, -static_cast<qint64>(MaxOffsetMs), static_cast<qint64>(MaxOffsetMs));
    m_controller->setLyricOffset(clamped);
    qCInfo(lcSystem) << "lyric offset ->" << clamped << "ms";
}

void ControlService::offsetEarlier()
{
    clampAndSetOffset(m_controller->lyricOffset() - OffsetStepMs);
}

void ControlService::offsetLater()
{
    clampAndSetOffset(m_controller->lyricOffset() + OffsetStepMs);
}

void ControlService::resetOffset()
{
    clampAndSetOffset(0);
}

void ControlService::setOverlayVisible(bool visible)
{
    m_overlayVisible = visible;
    updateToggleAction();
}

void ControlService::updateToggleAction()
{
    QAction *toggle = m_actions.value(toggleOverlayId());
    if (!toggle) {
        return;
    }
    toggle->setChecked(m_overlayVisible);
    toggle->setText(m_overlayVisible ? i18nc("@action global shortcut", "Hide Lyrics") : i18nc("@action global shortcut", "Show Lyrics"));
}

void ControlService::updateToolTip()
{
    if (!m_tray) {
        return;
    }
    m_tray->setToolTip(QStringLiteral(KSHADOWLRC_APP_ID), i18n("KShadowLrc"), offsetLabel(m_controller->lyricOffset()));
}

} // namespace kshadowlrc
