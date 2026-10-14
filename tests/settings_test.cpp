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

#include <KConfig>
#include <KSharedConfig>

#include <cstdio>
#include <memory>

namespace
{

int g_failures = 0;

void check(bool condition, const char *what)
{
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++g_failures;
    }
}

QString configPath(const QString &dir)
{
    return dir + QStringLiteral("/kshadowlrcrc");
}

} // namespace

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

    const QString path = configPath(tempDir.path());

    // 1. A fresh, never-written config yields every default, and constructing
    //    and reading it must not create the file.
    {
        std::unique_ptr<kshadowlrc::KShadowLrcSettings> settings(kshadowlrc::createSettings());

        check(settings->fontFamily() == QString(), "fontFamily default is empty");
        check(settings->fontSize() == 32, "fontSize default is 32");
        check(settings->textColor() == QStringLiteral("#ffffff"), "textColor default is #ffffff");
        check(settings->outlineEnabled() == true, "outlineEnabled default is true");
        check(settings->displayMode() == kshadowlrc::KShadowLrcSettings::EnumDisplayMode::Both, "displayMode default is Both");
        check(settings->visibleLines() == 2, "visibleLines default is 2");
        check(settings->edge() == kshadowlrc::KShadowLrcSettings::EnumEdge::Bottom, "edge default is Bottom");
        check(settings->margin() == 64, "margin default is 64");
        check(settings->clickThrough() == true, "clickThrough default is true");
        check(settings->outputName() == QString(), "outputName default is empty");
        check(settings->lyricOffsetMs() == 0, "lyricOffsetMs default is 0");
        check(settings->syncIntervalMs() == 1000, "syncIntervalMs default is 1000");
        check(settings->neteaseEnabled() == true, "neteaseEnabled default is true");
        check(settings->lrclibEnabled() == true, "lrclibEnabled default is true");

        check(!QFile::exists(path), "no config file exists before the first write");

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

        check(edgeChanged, "edgeChanged fires on a value change");
        check(QFile::exists(path), "the config file appears after a write");
    }

    // 2. Round-trip: a brand new settings object reads back what was written.
    //    Reparse first so the values really come from the file on disk.
    KSharedConfig::openConfig(QStringLiteral("kshadowlrcrc"))->reparseConfiguration();
    {
        std::unique_ptr<kshadowlrc::KShadowLrcSettings> settings(kshadowlrc::createSettings());

        check(settings->fontSize() == 48, "fontSize round-trips (General)");
        check(settings->textColor() == QStringLiteral("#123456"), "textColor round-trips (General)");
        check(settings->edge() == kshadowlrc::KShadowLrcSettings::EnumEdge::Top, "edge round-trips (General)");
        check(settings->margin() == 12, "margin round-trips (General)");
        check(settings->lyricOffsetMs() == 250, "lyricOffsetMs round-trips (Playback)");
        check(settings->syncIntervalMs() == 500, "syncIntervalMs round-trips (Playback)");
        check(settings->neteaseEnabled() == false, "neteaseEnabled round-trips (Providers)");
        check(settings->lrclibEnabled() == false, "lrclibEnabled round-trips (Providers)");

        // Untouched values keep their defaults.
        check(settings->fontFamily() == QString(), "untouched fontFamily stays default");
        check(settings->displayMode() == kshadowlrc::KShadowLrcSettings::EnumDisplayMode::Both, "untouched displayMode stays default");
    }

    // 3. The explicit-file overload replaces the config path entirely.
    {
        const QString otherPath = tempDir.path() + QStringLiteral("/other-kshadowlrcrc");
        auto other = KSharedConfig::openConfig(otherPath, KConfig::SimpleConfig);

        std::unique_ptr<kshadowlrc::KShadowLrcSettings> settings(kshadowlrc::createSettings(other));
        check(settings->margin() == 64, "a different config file starts from defaults");
        check(!QFile::exists(otherPath), "the replacement file is not written before a change");

        settings->setMargin(7);
        check(QFile::exists(otherPath), "the replacement file appears after a change");
        check(QFile::exists(path), "writing to the replacement file leaves the main one untouched");
    }
    KSharedConfig::openConfig(tempDir.path() + QStringLiteral("/other-kshadowlrcrc"), KConfig::SimpleConfig)->reparseConfiguration();
    {
        const QString otherPath = tempDir.path() + QStringLiteral("/other-kshadowlrcrc");
        auto other = KSharedConfig::openConfig(otherPath, KConfig::SimpleConfig);
        std::unique_ptr<kshadowlrc::KShadowLrcSettings> settings(kshadowlrc::createSettings(other));
        check(settings->margin() == 7, "the replacement file round-trips");
    }

    if (g_failures == 0) {
        std::printf("settings_test: all checks passed (defaults, no write on read, round-trip, path override)\n");
        return 0;
    }
    std::fprintf(stderr, "settings_test: %d check(s) failed\n", g_failures);
    return 1;
}
