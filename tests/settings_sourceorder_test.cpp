// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 KShadowLrc Contributors
//
// Offline test for the Providers/sourceOrder settings key: its default and its
// round-trip through disk. XDG_CONFIG_HOME and XDG_CONFIG_DIRS are pointed at a
// private temporary directory before the first KSharedConfig is opened, so the
// developer's real kshadowlrcrc can be neither read nor written. The point of
// the disk round-trip is to catch a key that is missing from settings.cpp's
// persistChangesOn(), which would otherwise silently never be saved.

#include "config/settings.h"

#include <QCoreApplication>
#include <QTemporaryDir>
#include <QTest>

#include <KSharedConfig>

#include <cstdio>
#include <memory>

using namespace kshadowlrc;

class SettingsSourceOrderTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void defaultIsNeteaseLrclibLocal();
    void roundTripsThroughDisk();
};

void SettingsSourceOrderTest::initTestCase()
{
    // main() points XDG_CONFIG_HOME here before the application object exists.
    QVERIFY(!qgetenv("XDG_CONFIG_HOME").isEmpty());
}

void SettingsSourceOrderTest::defaultIsNeteaseLrclibLocal()
{
    std::unique_ptr<KShadowLrcSettings> settings(createSettings());
    QCOMPARE(settings->sourceOrder(), QStringList({QStringLiteral("netease"), QStringLiteral("lrclib"), QStringLiteral("local")}));

    // Change it so the next slot can prove it reaches disk.
    settings->setSourceOrder({QStringLiteral("lrclib"), QStringLiteral("local")});
    QCOMPARE(settings->sourceOrder(), QStringList({QStringLiteral("lrclib"), QStringLiteral("local")}));
}

void SettingsSourceOrderTest::roundTripsThroughDisk()
{
    KSharedConfig::openConfig(QStringLiteral("kshadowlrcrc"))->reparseConfiguration();
    std::unique_ptr<KShadowLrcSettings> settings(createSettings());

    // If settings.cpp does not connect sourceOrderChanged to save(), this reads
    // back the default instead of what was written.
    QCOMPARE(settings->sourceOrder(), QStringList({QStringLiteral("lrclib"), QStringLiteral("local")}));
}

int main(int argc, char **argv)
{
    QTemporaryDir tempDir;
    if (!tempDir.isValid()) {
        std::fprintf(stderr, "FAIL: could not create a temporary config directory\n");
        return 1;
    }
    // Must happen before QCoreApplication/KSharedConfig look at the paths.
    qputenv("XDG_CONFIG_HOME", tempDir.path().toUtf8());
    qputenv("XDG_CONFIG_DIRS", tempDir.path().toUtf8());

    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("kshadowlrc-sourceorder-test"));

    SettingsSourceOrderTest tc;
    return QTest::qExec(&tc, argc, argv);
}

#include "settings_sourceorder_test.moc"
