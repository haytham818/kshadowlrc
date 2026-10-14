// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 KShadowLrc Contributors

#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

namespace kshadowlrc
{

/*!
 * The connected outputs, for QML's output picker.
 *
 * names() lists the primary output first, then the remaining outputs in Qt's own
 * order. primaryName() is the output that is primary right now, or an empty
 * string when there is none. Both notify through changed(), which fires on the
 * three QGuiApplication signals that can alter the list - a screen appearing or
 * going away, and the primary output moving - so a binding on either property
 * stays live across a hotplug.
 */
class Outputs : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QStringList names READ names NOTIFY changed)
    Q_PROPERTY(QString primaryName READ primaryName NOTIFY changed)

public:
    explicit Outputs(QObject *parent = nullptr);

    QStringList names() const;
    QString primaryName() const;

Q_SIGNALS:
    void changed();
};

} // namespace kshadowlrc
