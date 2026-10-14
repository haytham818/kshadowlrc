// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 KShadowLrc Contributors
//
// Output selection is deliberately pure string logic, so it can be pinned down
// without a compositor, a second monitor, or anything on screen.

#include "platform/outputresolution.h"

#include <QStringList>
#include <cstdio>

using kshadowlrc::resolveOutputIndex;

namespace
{

int failures = 0;

void check(const char *what, int actual, int expected)
{
    if (actual != expected) {
        std::printf("  FAIL %s: expected %d, got %d\n", what, expected, actual);
        ++failures;
    }
}

} // namespace

int main()
{
    // This machine, as an example: eDP-2 is primary and listed first.
    const QStringList both{QStringLiteral("eDP-2"), QStringLiteral("HDMI-A-1")};

    // No outputs at all: nothing to attach to.
    check("empty screen list", resolveOutputIndex(QString(), {}, 0), -1);
    check("empty screen list, no primary", resolveOutputIndex(QStringLiteral("HDMI-A-1"), {}, -1), -1);

    // An unset name means the primary output, not "the first one".
    check("unset name takes primary", resolveOutputIndex(QString(), both, 1), 1);
    check("unset name, primary is first", resolveOutputIndex(QString(), both, 0), 0);
    check("unset name without primary marker", resolveOutputIndex(QString(), both, -1), 0);

    // A configured name wins over the primary output.
    check("exact match on secondary", resolveOutputIndex(QStringLiteral("HDMI-A-1"), both, 0), 1);
    check("exact match on primary", resolveOutputIndex(QStringLiteral("eDP-2"), both, 1), 0);

    // An unknown name degrades to the primary output rather than to nothing.
    check("unknown name falls back to primary", resolveOutputIndex(QStringLiteral("DP-9"), both, 1), 1);
    check("unknown name without primary marker", resolveOutputIndex(QStringLiteral("DP-9"), both, -1), 0);

    // A near miss must not match: attaching to the wrong monitor silently is worse
    // than the visible fallback.
    check("prefix does not match", resolveOutputIndex(QStringLiteral("HDMI"), both, 0), 0);
    check("case difference does not match", resolveOutputIndex(QStringLiteral("hdmi-a-1"), both, 0), 0);

    // A stale configuration that outlives the monitor it named.
    check("disconnected output falls back", resolveOutputIndex(QStringLiteral("HDMI-A-1"), {QStringLiteral("eDP-2")}, 0), 0);

    if (failures == 0) {
        std::printf("output_resolution_test: fallback, exact-match and near-miss cases all passed\n");
    }
    return failures == 0 ? 0 : 1;
}
