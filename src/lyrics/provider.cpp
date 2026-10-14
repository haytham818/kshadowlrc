// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 KShadowLrc Contributors

#include "provider.h"

namespace kshadowlrc
{

// Only the parts of the frozen interface that no provider should have to repeat.
// This file exists mainly so AUTOMOC generates the metaobject for provider.h;
// concrete providers live in their own translation units.

LyricProvider::LyricProvider(QObject *parent)
    : QObject(parent)
{
}

LyricProvider::~LyricProvider() = default;

bool LyricProvider::isLikelySongId(const QString &id) const
{
    Q_UNUSED(id)
    return false;
}

} // namespace kshadowlrc
