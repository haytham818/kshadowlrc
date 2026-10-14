// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 KShadowLrc Contributors
//
// Offline test for the KConfigXT settings layer (src/config/).
//
// It points XDG_CONFIG_HOME and XDG_CONFIG_DIRS at a private temporary
// directory before the first KSharedConfig is opened, so the developer's real
// kshadowlrcrc can be neither read nor written. No network, no GUI.

#include "config/settings.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

#include <KConfig>
#include <KSharedConfig>

#include <cstdio>
#include <memory>

namespace
{

QString configPath(const QString &dir)
{
    return dir + QStringLiteral("/kshadowlrcrc");
}

} // namespace

class SettingsTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void defaultsAndFirstWrite();
    void roundTrip();
    void explicitFileOverload();

private:
    QString m_configDir;
};

void SettingsTest::initTestCase()
{
    // Pointed at the private directory by main() before the application object
    // existed; read it back here so the slots resolve the same path.
    m_configDir = QString::fromUtf8(qgetenv("XDG_CONFIG_HOME"));
}

void SettingsTest::defaultsAndFirstWrite()
{
    const QString path = configPath(m_configDir);

    // 1. A fresh, never-written config yields every default, and constructing
    //    and reading it must not create the file.
    std::unique_ptr<kshadowlrc::KShadowLrcSettings> settings(kshadowlrc::createSettings());

    QVERIFY2(settings->fontFamily() == QString(), "fontFamily default is empty");
    QVERIFY2(settings->fontSize() == 32, "fontSize default is 32");
    QVERIFY2(settings->textColor() == QStringLiteral("#ffffff"), "textColor default is #ffffff");
    QVERIFY2(settings->outlineEnabled() == true, "outlineEnabled default is true");
    QVERIFY2(settings->displayMode() == kshadowlrc::KShadowLrcSettings::EnumDisplayMode::Both, "displayMode default is Both");
    QVERIFY2(settings->visibleLines() == 2, "visibleLines default is 2");
    QVERIFY2(settings->edge() == kshadowlrc::KShadowLrcSettings::EnumEdge::Bottom, "edge default is Bottom");
    QVERIFY2(settings->margin() == 64, "margin default is 64");
    QVERIFY2(settings->clickThrough() == true, "clickThrough default is true");
    QVERIFY2(settings->outputName() == QString(), "outputName default is empty");
    QVERIFY2(settings->lyricOffsetMs() == 0, "lyricOffsetMs default is 0");
    QVERIFY2(settings->syncIntervalMs() == 1000, "syncIntervalMs default is 1000");
    QVERIFY2(settings->neteaseEnabled() == true, "neteaseEnabled default is true");
    QVERIFY2(settings->lrclibEnabled() == true, "lrclibEnabled default is true");

    QVERIFY2(!QFile::exists(path), "no config file exists before the first write");

    // A property change is announced and then written to disk.
    bool edgeChanged = false;
    QObject::connect(settings.get(), &kshadowlrc::KShadowLrcSettings::edgeChanged, [&edgeChanged] {
        edgeChanged = true;
    });

    settings->setFontSize(48); // General
    settings->setTextColor(QStringLiteral("#123456")); // General
    settings->setEdge(kshadowlrc::KShadowLrcSettings::EnumEdge::Top); // General
    settings->setMargin(12); // General
    settings->setLyricOffsetMs(250); // Playback
    settings->setSyncIntervalMs(500); // Playback
    settings->setNeteaseEnabled(false); // Providers
    settings->setLrclibEnabled(false); // Providers

    QVERIFY2(edgeChanged, "edgeChanged fires on a value change");
    QVERIFY2(QFile::exists(path), "the config file appears after a write");
}

void SettingsTest::roundTrip()
{
    // 2. Round-trip: a brand new settings object reads back what was written.
    //    Reparse first so the values really come from the file on disk.
    KSharedConfig::openConfig(QStringLiteral("kshadowlrcrc"))->reparseConfiguration();

    std::unique_ptr<kshadowlrc::KShadowLrcSettings> settings(kshadowlrc::createSettings());

    QVERIFY2(settings->fontSize() == 48, "fontSize round-trips (General)");
    QVERIFY2(settings->textColor() == QStringLiteral("#123456"), "textColor round-trips (General)");
    QVERIFY2(settings->edge() == kshadowlrc::KShadowLrcSettings::EnumEdge::Top, "edge round-trips (General)");
    QVERIFY2(settings->margin() == 12, "margin round-trips (General)");
    QVERIFY2(settings->lyricOffsetMs() == 250, "lyricOffsetMs round-trips (Playback)");
    QVERIFY2(settings->syncIntervalMs() == 500, "syncIntervalMs round-trips (Playback)");
    QVERIFY2(settings->neteaseEnabled() == false, "neteaseEnabled round-trips (Providers)");
    QVERIFY2(settings->lrclibEnabled() == false, "lrclibEnabled round-trips (Providers)");

    // Untouched values keep their defaults.
    QVERIFY2(settings->fontFamily() == QString(), "untouched fontFamily stays default");
    QVERIFY2(settings->displayMode() == kshadowlrc::KShadowLrcSettings::EnumDisplayMode::Both, "untouched displayMode stays default");
}

void SettingsTest::explicitFileOverload()
{
    // 3. The explicit-file overload replaces the config path entirely.
    const QString path = configPath(m_configDir);
    const QString otherPath = m_configDir + QStringLiteral("/other-kshadowlrcrc");
    auto other = KSharedConfig::openConfig(otherPath, KConfig::SimpleConfig);

    std::unique_ptr<kshadowlrc::KShadowLrcSettings> settings(kshadowlrc::createSettings(other));
    QVERIFY2(settings->margin() == 64, "a different config file starts from defaults");
    QVERIFY2(!QFile::exists(otherPath), "the replacement file is not written before a change");

    settings->setMargin(7);
    QVERIFY2(QFile::exists(otherPath), "the replacement file appears after a change");
    QVERIFY2(QFile::exists(path), "writing to the replacement file leaves the main one untouched");

    KSharedConfig::openConfig(otherPath, KConfig::SimpleConfig)->reparseConfiguration();
    auto reparsed = KSharedConfig::openConfig(otherPath, KConfig::SimpleConfig);
    std::unique_ptr<kshadowlrc::KShadowLrcSettings> roundTripped(kshadowlrc::createSettings(reparsed));
    QVERIFY2(roundTripped->margin() == 7, "the replacement file round-trips");
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
    QCoreApplication::setApplicationName(QStringLiteral("kshadowlrc-settings-test"));

    SettingsTest tc;
    return QTest::qExec(&tc, argc, argv);
}

#include "settings_test.moc"
