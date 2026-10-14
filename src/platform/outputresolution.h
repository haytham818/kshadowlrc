// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 KShadowLrc Contributors

#pragma once

#include <QString>
#include <QStringList>

namespace kshadowlrc
{

/*!
 * Pick the index of the output the overlay should live on.
 *
 * `available` holds the output names in the order they are to be considered and
 * `primaryIndex` is the index of the primary output, or -1 when there is none.
 *
 * An empty `name` means "whichever output is primary". A name that is not in
 * `available` also falls back to the primary output, so a configuration that
 * outlives the monitor it names degrades to something usable instead of putting
 * the overlay nowhere. Matching is exact: a partial name never matches, because
 * silently attaching to the wrong monitor is worse than the fallback.
 *
 * Returns -1 only when there are no outputs at all.
 *
 * Pure string logic on purpose: the caller resolves the index against the live
 * screen list, which keeps this testable without a running compositor.
 */
int resolveOutputIndex(const QString &name, const QStringList &available, int primaryIndex);

} // namespace kshadowlrc
