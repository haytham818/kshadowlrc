// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 KShadowLrc Contributors

#pragma once

#include <QRegularExpression>
#include <QString>
#include <QStringList>

namespace kshadowlrc
{

/*!
 * Whether one ignore-list entry matches an MPRIS bus name.
 *
 * The two forms are chosen by whether the entry contains a wildcard:
 *
 *  - An entry containing `*` or `?` is a case-insensitive glob matched against
 *    the **whole** bus name, so it stays case-insensitive even though D-Bus
 *    names are case-sensitive. The whole-name anchoring matters: `firefox*`
 *    matches `firefox.instance1` but **not** `org.mpris.MediaPlayer2.firefox`;
 *    to ignore every Firefox regardless of instance, write
 *    `org.mpris.MediaPlayer2.firefox*`; to ignore everything, write
 *    `org.mpris.MediaPlayer2.*`.
 *
 *  - An entry without a wildcard matches case-insensitively when any of these
 *    holds, which is the old prefix behaviour plus a segment shorthand:
 *
 *      1. it equals the whole bus name:
 *         `org.mpris.MediaPlayer2.playerctld` matches itself;
 *      2. it is a dot-boundary prefix of the bus name, i.e. the bus name is the
 *         entry followed by a `.`: `org.mpris.MediaPlayer2.firefox` matches
 *         `org.mpris.MediaPlayer2.firefox.instance1`;
 *      3. it equals one dot-separated segment of the bus name: `firefox`
 *         matches `org.mpris.MediaPlayer2.firefox.instance1`.
 *
 *    Form 2 means the interface prefix `org.mpris.MediaPlayer2` matches every
 *    MPRIS player -- that is deliberate, it is how "ignore everything" is
 *    written without a glob. Form 3 is a shorthand and never a substring match:
 *    `firefox` does not match `org.mpris.MediaPlayer2.firefoxes`, `fire` does
 *    not match `firefox`, and the boundary must be a dot, so
 *    `org.mpris.MediaPlayer2.firefox` does not match
 *    `org.mpris.MediaPlayer2.firefoxinstance`.
 *
 * Case is ignored in both forms: D-Bus names are case-sensitive, but a user
 * typing `firefox` in a settings field should not have to match `MediaPlayer2`'s
 * capitals.
 *
 * An empty entry matches nothing.
 */
inline bool ignoredPatternMatches(const QString &pattern, const QString &busName)
{
    if (pattern.isEmpty()) {
        return false;
    }
    if (pattern.contains(QLatin1Char('*')) || pattern.contains(QLatin1Char('?'))) {
        // wildcardToRegularExpression() returns an anchored, whole-string
        // pattern; the session bus names we test never contain '/', so the
        // default conversion is exactly a glob over the whole bus name.
        const QRegularExpression expression(QRegularExpression::wildcardToRegularExpression(pattern), QRegularExpression::CaseInsensitiveOption);
        return expression.match(busName).hasMatch();
    }
    // No wildcard: the whole name, a dot-boundary prefix of it, or one segment.
    if (busName.compare(pattern, Qt::CaseInsensitive) == 0) {
        return true;
    }
    if (busName.size() > pattern.size() && busName.startsWith(pattern, Qt::CaseInsensitive) && busName.at(pattern.size()) == QLatin1Char('.')) {
        return true;
    }
    const QStringList segments = busName.split(QLatin1Char('.'));
    for (const QString &segment : segments) {
        if (segment.compare(pattern, Qt::CaseInsensitive) == 0) {
            return true;
        }
    }
    return false;
}

/*!
 * Whether `busName` is ignored by any entry of `patterns`.
 *
 * An empty list ignores nothing: that is a legitimate configuration, not an
 * error.
 */
inline bool isIgnoredPlayer(const QStringList &patterns, const QString &busName)
{
    for (const QString &pattern : patterns) {
        if (ignoredPatternMatches(pattern, busName)) {
            return true;
        }
    }
    return false;
}

} // namespace kshadowlrc
