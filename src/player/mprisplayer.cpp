// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 KShadowLrc Contributors

#include "mprisplayer.h"

#include "mprismeta.h"

#include <QDBusConnection>
#include <QDBusPendingReply>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(lcMprisPlayer, "kshadowlrc.player.mpris")

namespace kshadowlrc
{

using namespace mpris;

MprisPlayer::MprisPlayer(QString wellKnownName, QString ownerName, QObject *parent)
    : QObject(parent)
    , m_wellKnownName(std::move(wellKnownName))
    , m_ownerName(std::move(ownerName))
{
}

MprisPlayer::~MprisPlayer() = default;

void MprisPlayer::subscribe()
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) {
        qCWarning(lcMprisPlayer) << "no session bus; cannot subscribe to" << m_wellKnownName;
        return;
    }
    if (!bus.connect(m_ownerName,
                     kMprisObjectPath,
                     kPropertiesInterface,
                     QStringLiteral("PropertiesChanged"),
                     this,
                     SLOT(onPropertiesChanged(QString, QVariantMap, QStringList)))) {
        qCWarning(lcMprisPlayer) << "failed to subscribe to PropertiesChanged for" << m_wellKnownName;
    }
    if (!bus.connect(m_ownerName, kMprisObjectPath, kPlayerInterface, QStringLiteral("Seeked"), this, SLOT(onSeeked(qlonglong)))) {
        qCWarning(lcMprisPlayer) << "failed to subscribe to Seeked for" << m_wellKnownName;
    }
}

void MprisPlayer::unsubscribe()
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    bus.disconnect(m_ownerName,
                   kMprisObjectPath,
                   kPropertiesInterface,
                   QStringLiteral("PropertiesChanged"),
                   this,
                   SLOT(onPropertiesChanged(QString, QVariantMap, QStringList)));
    bus.disconnect(m_ownerName, kMprisObjectPath, kPlayerInterface, QStringLiteral("Seeked"), this, SLOT(onSeeked(qlonglong)));
}

void MprisPlayer::refresh()
{
    callAsync<QVariantMap>(m_ownerName,
                           kMprisObjectPath,
                           kPropertiesInterface,
                           QStringLiteral("GetAll"),
                           {kPlayerInterface},
                           this,
                           [this](const QDBusPendingReply<QVariantMap> &reply) {
                               if (reply.isError()) {
                                   qCDebug(lcMprisPlayer) << "GetAll failed for" << m_wellKnownName << ':' << reply.error().message();
                                   Q_EMIT refreshFailed();
                                   return;
                               }
                               const QVariantMap properties = reply.value();
                               applyProperties(properties);
                               if (properties.contains(QStringLiteral("Position"))) {
                                   const qint64 positionUs = properties.value(QStringLiteral("Position")).toLongLong();
                                   m_positionUs = positionUs;
                                   Q_EMIT positionReported(positionUs);
                               } else {
                                   // Not every player exposes Position in GetAll.
                                   requestPosition();
                               }
                           });
}

void MprisPlayer::requestPosition()
{
    callAsync<QVariant>(m_ownerName,
                        kMprisObjectPath,
                        kPropertiesInterface,
                        QStringLiteral("Get"),
                        {kPlayerInterface, QStringLiteral("Position")},
                        this,
                        [this](const QDBusPendingReply<QVariant> &reply) {
                            if (reply.isError()) {
                                return;
                            }
                            const qint64 positionUs = unwrapDbusVariant(reply.value()).toLongLong();
                            m_positionUs = positionUs;
                            Q_EMIT positionReported(positionUs);
                        });
}

void MprisPlayer::applyProperties(const QVariantMap &properties)
{
    bool changed = false;

    if (properties.contains(QStringLiteral("PlaybackStatus"))) {
        const PlaybackStatus status = playbackStatusFromString(properties.value(QStringLiteral("PlaybackStatus")).toString());
        if (status != m_status) {
            m_status = status;
            changed = true;
        }
    }
    if (properties.contains(QStringLiteral("Metadata"))) {
        const QVariantMap metadata = variantMapFromDbus(properties.value(QStringLiteral("Metadata")));
        if (metadata != m_metadata) {
            m_metadata = metadata;
            changed = true;
        }
    }
    if (changed) {
        Q_EMIT propertiesRefreshed();
    }
}

void MprisPlayer::onPropertiesChanged(const QString &interface, const QVariantMap &changedProperties, const QStringList &invalidatedProperties)
{
    Q_UNUSED(invalidatedProperties)
    if (interface != kPlayerInterface) {
        return;
    }

    bool changed = false;
    if (changedProperties.contains(QStringLiteral("PlaybackStatus"))) {
        const PlaybackStatus status = playbackStatusFromString(changedProperties.value(QStringLiteral("PlaybackStatus")).toString());
        if (status != m_status) {
            m_status = status;
            changed = true;
        }
    }
    if (changedProperties.contains(QStringLiteral("Metadata"))) {
        // PropertiesChanged carries the whole new Metadata dictionary.
        const QVariantMap metadata = variantMapFromDbus(changedProperties.value(QStringLiteral("Metadata")));
        if (metadata != m_metadata) {
            m_metadata = metadata;
            changed = true;
        }
    }
    if (changedProperties.contains(QStringLiteral("Position"))) {
        const qint64 positionUs = changedProperties.value(QStringLiteral("Position")).toLongLong();
        m_positionUs = positionUs;
        Q_EMIT positionReported(positionUs);
    }
    if (changed) {
        Q_EMIT propertiesRefreshed();
    }
}

void MprisPlayer::onSeeked(qlonglong positionUs)
{
    m_positionUs = positionUs;
    Q_EMIT seeked(positionUs);
}

} // namespace kshadowlrc
