// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 KShadowLrc Contributors

#pragma once

#include "playertracker.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

#include <utility>

namespace kshadowlrc
{
namespace mpris
{

inline const QString kBusService = QStringLiteral("org.freedesktop.DBus");
inline const QString kBusPath = QStringLiteral("/org/freedesktop/DBus");
inline const QString kBusInterface = QStringLiteral("org.freedesktop.DBus");
inline const QString kPropertiesInterface = QStringLiteral("org.freedesktop.DBus.Properties");
inline const QString kMprisPrefix = QStringLiteral("org.mpris.MediaPlayer2.");
inline const QString kPlayerInterface = QStringLiteral("org.mpris.MediaPlayer2.Player");
inline const QString kMprisObjectPath = QStringLiteral("/org/mpris/MediaPlayer2");

/*! Timeout for a single D-Bus round trip; a wedged player must not stall us. */
inline constexpr int kCallTimeoutMs = 6000;

/*!
 * Fires a D-Bus method call without waiting for the reply.
 *
 * The callback is invoked on `context`'s thread once the reply (or an error)
 * arrives, and is skipped entirely if `context` is destroyed first. Templated
 * on the reply type so the caller reads a nicely typed QDBusPendingReply.
 */
template<typename T, typename Func>
void callAsync(const QString &service,
               const QString &path,
               const QString &interface,
               const QString &method,
               const QVariantList &arguments,
               QObject *context,
               Func &&callback)
{
    QDBusMessage message = QDBusMessage::createMethodCall(service, path, interface, method);
    message.setArguments(arguments);
    const QDBusPendingCall pending = QDBusConnection::sessionBus().asyncCall(message, kCallTimeoutMs);
    auto *watcher = new QDBusPendingCallWatcher(pending, context);
    QObject::connect(watcher, &QDBusPendingCallWatcher::finished, context, [watcher, callback = std::forward<Func>(callback)]() {
        const QDBusPendingReply<T> reply = *watcher;
        watcher->deleteLater();
        callback(reply);
    });
}

} // namespace mpris

/*!
 * Owns the D-Bus side of one MPRIS player: the well-known bus name and its
 * unique owner, the cached properties, and the signal subscriptions.
 *
 * Signals are subscribed on the unique owner name because PropertiesChanged
 * and Seeked are emitted by the owning connection, not the well-known alias.
 */
class MprisPlayer : public QObject
{
    Q_OBJECT

public:
    MprisPlayer(QString wellKnownName, QString ownerName, QObject *parent = nullptr);
    ~MprisPlayer() override;

    QString wellKnownName() const
    {
        return m_wellKnownName;
    }
    QString ownerName() const
    {
        return m_ownerName;
    }

    PlaybackStatus status() const
    {
        return m_status;
    }
    QVariantMap metadata() const
    {
        return m_metadata;
    }
    /*! Last position in microseconds, or -1 when it has never been seen. */
    qint64 positionUs() const
    {
        return m_positionUs;
    }

    void subscribe();
    void unsubscribe();

    /*! Asks the player for its current properties and position. */
    void refresh();

Q_SIGNALS:
    /*! Cached PlaybackStatus and/or Metadata changed. */
    void propertiesRefreshed();
    /*! A fresh track position, in microseconds. */
    void positionReported(qint64 positionUs);
    /*! The player reported a seek (position jump), in microseconds. */
    void seeked(qint64 positionUs);
    /*! A refresh round trip failed; the player may be gone or unresponsive. */
    void refreshFailed();

private Q_SLOTS:
    void onPropertiesChanged(const QString &interface, const QVariantMap &changedProperties, const QStringList &invalidatedProperties);
    void onSeeked(qlonglong positionUs);

private:
    void applyProperties(const QVariantMap &properties);
    void requestPosition();

    QString m_wellKnownName;
    QString m_ownerName;
    PlaybackStatus m_status = PlaybackStatus::Unknown;
    QVariantMap m_metadata;
    qint64 m_positionUs = -1;
};

} // namespace kshadowlrc
