// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 KShadowLrc Contributors
//
// Output selection is deliberately pure string logic, so it can be pinned down
// without a compositor, a second monitor, or anything on screen.

#include "platform/outputresolution.h"

#include <QStringList>
#include <QTest>

using kshadowlrc::resolveOutputIndex;

class OutputResolutionTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void emptyScreenList();
    void unsetNameTakesPrimary();
    void configuredNameWinsOverPrimary();
    void unknownNameFallsBackToPrimary();
    void nearMissDoesNotMatch();
    void disconnectedOutputFallsBack();
};

void OutputResolutionTest::emptyScreenList()
{
    // No outputs at all: nothing to attach to.
    QCOMPARE(resolveOutputIndex(QString(), {}, 0), -1);
    QCOMPARE(resolveOutputIndex(QStringLiteral("HDMI-A-1"), {}, -1), -1);
}

void OutputResolutionTest::unsetNameTakesPrimary()
{
    // This machine, as an example: eDP-2 is primary and listed first.
    const QStringList both{QStringLiteral("eDP-2"), QStringLiteral("HDMI-A-1")};

    // An unset name means the primary output, not "the first one".
    QCOMPARE(resolveOutputIndex(QString(), both, 1), 1);
    QCOMPARE(resolveOutputIndex(QString(), both, 0), 0);
    QCOMPARE(resolveOutputIndex(QString(), both, -1), 0);
}

void OutputResolutionTest::configuredNameWinsOverPrimary()
{
    const QStringList both{QStringLiteral("eDP-2"), QStringLiteral("HDMI-A-1")};

    // A configured name wins over the primary output.
    QCOMPARE(resolveOutputIndex(QStringLiteral("HDMI-A-1"), both, 0), 1);
    QCOMPARE(resolveOutputIndex(QStringLiteral("eDP-2"), both, 1), 0);
}

void OutputResolutionTest::unknownNameFallsBackToPrimary()
{
    const QStringList both{QStringLiteral("eDP-2"), QStringLiteral("HDMI-A-1")};

    // An unknown name degrades to the primary output rather than to nothing.
    QCOMPARE(resolveOutputIndex(QStringLiteral("DP-9"), both, 1), 1);
    QCOMPARE(resolveOutputIndex(QStringLiteral("DP-9"), both, -1), 0);
}

void OutputResolutionTest::nearMissDoesNotMatch()
{
    const QStringList both{QStringLiteral("eDP-2"), QStringLiteral("HDMI-A-1")};

    // A near miss must not match: attaching to the wrong monitor silently is worse
    // than the visible fallback.
    QCOMPARE(resolveOutputIndex(QStringLiteral("HDMI"), both, 0), 0);
    QCOMPARE(resolveOutputIndex(QStringLiteral("hdmi-a-1"), both, 0), 0);
}

void OutputResolutionTest::disconnectedOutputFallsBack()
{
    // A stale configuration that outlives the monitor it named.
    QCOMPARE(resolveOutputIndex(QStringLiteral("HDMI-A-1"), {QStringLiteral("eDP-2")}, 0), 0);
}

QTEST_GUILESS_MAIN(OutputResolutionTest)

#include "output_resolution_test.moc"
