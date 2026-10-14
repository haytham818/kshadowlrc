// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 KShadowLrc Contributors

#pragma once

#include <QLoggingCategory>

/*!
 * Shared logging categories.
 *
 * Each area declares the category it owns with Q_LOGGING_CATEGORY in its own
 * .cpp; here they are only declared so any translation unit can log through
 * them. Named "kshadowlrc.<area>" so that
 * QT_LOGGING_RULES="kshadowlrc.*.debug=true" works.
 */
Q_DECLARE_LOGGING_CATEGORY(lcApp)
Q_DECLARE_LOGGING_CATEGORY(lcOverlay)
Q_DECLARE_LOGGING_CATEGORY(lcLyrics)
