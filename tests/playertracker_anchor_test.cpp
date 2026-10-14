// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 KShadowLrc Contributors

// Deterministic, offline checks for PositionAnchor::elapsedMs interpolation and
// clamping. No D-Bus connection and no live player are involved.

#include "playertracker.h"

#include <QDateTime>
#include <QTest>

using kshadowlrc::PositionAnchor;

class PlayerTrackerAnchorTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void invalidAnchorReturnsRawPosition();
    void freshAnchorStaysAtReportedPosition();
    void anchorAdvancesWithWallClock();
    void clampedToTrackLength();
    void zeroLengthHasNoUpperClamp();
    void lowerBoundPinnedToZero();
    void reanchorPinsToReportedPosition();
};

void PlayerTrackerAnchorTest::invalidAnchorReturnsRawPosition()
{
    // An anchor that was never seen returns its raw position, unclamped.
    PositionAnchor anchor;
    anchor.positionMs = 4200;
    QVERIFY2(!anchor.isValid(), "default anchor is invalid");
    QVERIFY2(anchor.elapsedMs() == 4200, "invalid at -> returns positionMs");
    QVERIFY2(anchor.elapsedMs(1000) == 4200, "invalid at ignores the length clamp");
}

void PlayerTrackerAnchorTest::freshAnchorStaysAtReportedPosition()
{
    // A freshly taken anchor advances by the wall-clock delta.
    PositionAnchor anchor;
    anchor.positionMs = 1000;
    anchor.at = QDateTime::currentDateTimeUtc();

    const qint64 elapsed = anchor.elapsedMs();
    QVERIFY2(elapsed >= 1000 && elapsed <= 1100, "fresh anchor stays at the reported position");
}

void PlayerTrackerAnchorTest::anchorAdvancesWithWallClock()
{
    PositionAnchor anchor;
    anchor.positionMs = 1000;
    anchor.at = QDateTime::currentDateTimeUtc().addMSecs(-500);

    const qint64 elapsed = anchor.elapsedMs();
    QVERIFY2(elapsed >= 1480 && elapsed <= 1600, "anchor advances with elapsed wall time (~1500 ms)");
}

void PlayerTrackerAnchorTest::clampedToTrackLength()
{
    // The estimate is clamped to the track length when one is known.
    PositionAnchor anchor;
    anchor.positionMs = 1000;
    anchor.at = QDateTime::currentDateTimeUtc().addMSecs(-500);

    QVERIFY2(anchor.elapsedMs(1200) == 1200, "clamped to lengthMs at the upper bound");
    QVERIFY2(anchor.elapsedMs(1000000) >= 1480, "not clamped below the length");
}

void PlayerTrackerAnchorTest::zeroLengthHasNoUpperClamp()
{
    // Without a known length there is no upper clamp.
    PositionAnchor anchor;
    anchor.positionMs = 5000;
    anchor.at = QDateTime::currentDateTimeUtc().addMSecs(-1000);

    const qint64 elapsed = anchor.elapsedMs(0);
    QVERIFY2(elapsed >= 5980 && elapsed <= 6100, "lengthMs 0 -> no upper clamp");
}

void PlayerTrackerAnchorTest::lowerBoundPinnedToZero()
{
    // The lower bound is pinned to zero.
    PositionAnchor anchor;
    anchor.positionMs = -5000;
    anchor.at = QDateTime::currentDateTimeUtc();

    QVERIFY2(anchor.elapsedMs() == 0, "negative estimate clamped to 0");
    QVERIFY2(anchor.elapsedMs(10000) == 0, "negative estimate clamped to 0 with a length too");
}

void PlayerTrackerAnchorTest::reanchorPinsToReportedPosition()
{
    // Re-anchoring (what the tracker does on every paused poll) keeps the value
    // at the paused position instead of letting it run away.
    PositionAnchor anchor;
    anchor.positionMs = 30000;
    anchor.at = QDateTime::currentDateTimeUtc().addMSecs(-2500);
    const qint64 stale = anchor.elapsedMs();

    anchor.at = QDateTime::currentDateTimeUtc(); // refresh on poll
    const qint64 refreshed = anchor.elapsedMs();
    QVERIFY2(stale >= 32480, "un-refreshed anchor drifts forward");
    QVERIFY2(refreshed >= 30000 && refreshed <= 30050, "refreshing at pins the estimate to the reported position");
}

QTEST_GUILESS_MAIN(PlayerTrackerAnchorTest)

#include "playertracker_anchor_test.moc"
