// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 KShadowLrc Contributors

// Deterministic, offline checks for PositionAnchor::elapsedMs interpolation and
// clamping. No D-Bus connection and no live player are involved.

#include "playertracker.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QTextStream>

using kshadowlrc::PositionAnchor;

namespace
{

int g_failures = 0;

void check(bool condition, const char *what)
{
    QTextStream out(stdout);
    out << (condition ? QStringLiteral("ok:   ") : QStringLiteral("FAIL: ")) << QString::fromUtf8(what) << Qt::endl;
    if (!condition) {
        ++g_failures;
    }
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QTextStream out(stdout);

    // An anchor that was never seen returns its raw position, unclamped.
    {
        PositionAnchor anchor;
        anchor.positionMs = 4200;
        check(!anchor.isValid(), "default anchor is invalid");
        check(anchor.elapsedMs() == 4200, "invalid at -> returns positionMs");
        check(anchor.elapsedMs(1000) == 4200, "invalid at ignores the length clamp");
    }

    // A freshly taken anchor advances by the wall-clock delta.
    {
        PositionAnchor anchor;
        anchor.positionMs = 1000;
        anchor.at = QDateTime::currentDateTimeUtc();

        const qint64 elapsed = anchor.elapsedMs();
        check(elapsed >= 1000 && elapsed <= 1100, "fresh anchor stays at the reported position");
    }

    {
        PositionAnchor anchor;
        anchor.positionMs = 1000;
        anchor.at = QDateTime::currentDateTimeUtc().addMSecs(-500);

        const qint64 elapsed = anchor.elapsedMs();
        check(elapsed >= 1480 && elapsed <= 1600, "anchor advances with elapsed wall time (~1500 ms)");
    }

    // The estimate is clamped to the track length when one is known.
    {
        PositionAnchor anchor;
        anchor.positionMs = 1000;
        anchor.at = QDateTime::currentDateTimeUtc().addMSecs(-500);

        check(anchor.elapsedMs(1200) == 1200, "clamped to lengthMs at the upper bound");
        check(anchor.elapsedMs(1000000) >= 1480, "not clamped below the length");
    }

    // Without a known length there is no upper clamp.
    {
        PositionAnchor anchor;
        anchor.positionMs = 5000;
        anchor.at = QDateTime::currentDateTimeUtc().addMSecs(-1000);

        const qint64 elapsed = anchor.elapsedMs(0);
        check(elapsed >= 5980 && elapsed <= 6100, "lengthMs 0 -> no upper clamp");
    }

    // The lower bound is pinned to zero.
    {
        PositionAnchor anchor;
        anchor.positionMs = -5000;
        anchor.at = QDateTime::currentDateTimeUtc();

        check(anchor.elapsedMs() == 0, "negative estimate clamped to 0");
        check(anchor.elapsedMs(10000) == 0, "negative estimate clamped to 0 with a length too");
    }

    // Re-anchoring (what the tracker does on every paused poll) keeps the value
    // at the paused position instead of letting it run away.
    {
        PositionAnchor anchor;
        anchor.positionMs = 30000;
        anchor.at = QDateTime::currentDateTimeUtc().addMSecs(-2500);
        const qint64 stale = anchor.elapsedMs();

        anchor.at = QDateTime::currentDateTimeUtc(); // refresh on poll
        const qint64 refreshed = anchor.elapsedMs();
        check(stale >= 32480, "un-refreshed anchor drifts forward");
        check(refreshed >= 30000 && refreshed <= 30050, "refreshing at pins the estimate to the reported position");
    }

    out << (g_failures == 0 ? QStringLiteral("PASS") : QStringLiteral("FAIL")) << QStringLiteral(": position anchor (") << g_failures
        << QStringLiteral(" failures)") << Qt::endl;
    return g_failures == 0 ? 0 : 1;
}
