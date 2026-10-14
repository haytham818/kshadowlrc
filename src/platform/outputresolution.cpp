// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 KShadowLrc Contributors

#include "platform/outputresolution.h"

namespace kshadowlrc
{

int resolveOutputIndex(const QString &name, const QStringList &available, int primaryIndex)
{
    if (available.isEmpty()) {
        return -1;
    }

    // An unset output is not a missing one: it is the documented "primary" case.
    if (!name.isEmpty()) {
        const int index = available.indexOf(name);
        if (index >= 0) {
            return index;
        }
    }

    if (primaryIndex >= 0 && primaryIndex < available.size()) {
        return primaryIndex;
    }

    return 0;
}

} // namespace kshadowlrc
