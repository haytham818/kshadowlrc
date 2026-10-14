// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 KShadowLrc Contributors

#pragma once

#include "kshadowlrcsettings.h"

#include <KSharedConfig>

namespace kshadowlrc
{

/*!
 * Create the application settings object.
 *
 * The generated KShadowLrcSettings only raises its *Changed signals; on its own
 * it never writes the config file. The object returned here has every one of
 * those signals wired to KConfigSkeleton::save(), so assigning to a property
 * both notifies the UI and persists the change to disk immediately.
 *
 * The backing file is kshadowlrcrc in the standard KDE config location. A
 * caller that needs a different location points XDG_CONFIG_HOME at another
 * directory before the first call; to use one explicit file instead, use the
 * KSharedConfig overload below.
 *
 * \param parent optional QObject parent; the caller owns the object otherwise.
 */
KShadowLrcSettings *createSettings(QObject *parent = nullptr);

/*!
 * Create the settings object backed by a specific config file.
 *
 * \param config the KConfig to read from and write to; must not be null.
 * \param parent optional QObject parent.
 */
KShadowLrcSettings *createSettings(const KSharedConfig::Ptr &config, QObject *parent = nullptr);

} // namespace kshadowlrc
