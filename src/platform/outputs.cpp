// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 KShadowLrc Contributors

#include "platform/outputs.h"

#include <QGuiApplication>
#include <QScreen>

namespace kshadowlrc
{

Outputs::Outputs(QObject *parent)
    : QObject(parent)
{
    // A hotplug changes the list; so does the primary output moving. Both
    // properties notify through changed(), so both are re-evaluated.
    if (auto *app = qGuiApp) {
        connect(app, &QGuiApplication::screenAdded, this, &Outputs::changed);
        connect(app, &QGuiApplication::screenRemoved, this, &Outputs::changed);
        connect(app, &QGuiApplication::primaryScreenChanged, this, &Outputs::changed);
    }
}

QStringList Outputs::names() const
{
    const QList<QScreen *> screens = QGuiApplication::screens();
    const QScreen *primary = QGuiApplication::primaryScreen();

    // Primary first, then the rest in Qt's own order. Built on demand rather than
    // cached: it is a handful of strings, and recomputing keeps the list and the
    // primary name consistent within a call without a second cache to invalidate.
    QStringList names;
    names.reserve(screens.size());
    if (primary) {
        names.append(primary->name());
    }
    for (const QScreen *screen : screens) {
        if (screen != primary) {
            names.append(screen->name());
        }
    }
    return names;
}

QString Outputs::primaryName() const
{
    const QScreen *primary = QGuiApplication::primaryScreen();
    return primary ? primary->name() : QString();
}

} // namespace kshadowlrc
