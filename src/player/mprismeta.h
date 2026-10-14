// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 KShadowLrc Contributors

#pragma once

#include "playertracker.h"

#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVariantMap>

namespace kshadowlrc
{
namespace mpris
{

/*!
 * Demarshals a `Metadata`-shaped value. QtDBus hands back the nested `a{sv}`
 * of `Properties.GetAll` as a lazy QDBusArgument, so `QVariant::toMap()` alone
 * would silently yield an empty map.
 */
QVariantMap variantMapFromDbus(const QVariant &value);

/*! An unknown or absent PlaybackStatus maps to PlaybackStatus::Unknown. */
PlaybackStatus playbackStatusFromString(const QString &status);

/*!
 * `mpris:length` as reported by MPRIS, in microseconds, converted to
 * milliseconds. Missing, unparsable or non-positive values yield 0.
 */
qint64 lengthUsToMs(const QVariant &value);

/*!
 * `xesam:artist`, which MPRIS specifies as `as` but which players sometimes
 * emit as a bare string. Anything else yields an empty list rather than
 * aborting the track.
 */
QStringList artistsFromMetadata(const QVariantMap &metadata);

/*!
 * The identity used for the lyric cache: the `xesam:url` when it is a
 * `file://` URL, otherwise the stringified `mpris:trackid`, otherwise empty.
 */
QString uniqueSongIdFromMetadata(const QVariantMap &metadata);

/*!
 * Maps an MPRIS `Metadata` dictionary onto TrackMeta. Every field is
 * optional: a missing or unexpectedly typed field is left at its default
 * instead of dropping the whole track.
 */
TrackMeta trackMetaFromMpris(const QVariantMap &metadata);

/*!
 * Unwraps a D-Bus variant (as returned by `Properties.Get`) to its payload.
 * Values that arrive already unwrapped are returned unchanged.
 */
QVariant unwrapDbusVariant(const QVariant &value);

} // namespace mpris
} // namespace kshadowlrc
