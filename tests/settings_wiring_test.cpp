// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 KShadowLrc Contributors
//
// Offline checks for the settings -> controller / tracker wiring:
//   - a settings value drives the controller's display mode and upcoming-line
//     count;
//   - disabling a provider is respected by the engine;
//   - the lyric offset round-trips between tracker and settings without
//     oscillating (each direction emits exactly once per change).
//
// XDG_CONFIG_HOME is pointed at a temporary directory before the first config
// is opened, so the developer's real kshadowlrcrc is never touched. No network,
// no GUI.

#include "config/settings.h"
#include "lyrics/controller.h"
#include "lyrics/lyricengine.h"
#include "player/playertracker.h"

#include <QCoreApplication>
#include <QTemporaryDir>
#include <QTest>

#include <cstdio>
#include <memory>

using namespace kshadowlrc;

namespace
{

/*! Names of the providers currently handed to the engine. */
QStringList providerNames(LyricsController &controller)
{
    QStringList names;
    const QList<LyricProvider *> providers = controller.engine()->providers();
    for (const LyricProvider *provider : providers) {
        names.append(provider->name());
    }
    return names;
}

} // namespace

class SettingsWiringTest : public QObject
{
    Q_OBJECT

public:
    SettingsWiringTest();

private Q_SLOTS:
    void defaultsReadFromTheEmptyConfig();
    void displayModeDrivesTheController();
    void visibleLinesDriveTheUpcomingCount();
    void providerSwitchesAreRespected();
    void offsetRoundTripsExactlyOnce();
    void rewritingTheSameOffsetEmitsNothing();

private:
    std::unique_ptr<KShadowLrcSettings> m_settings;
    LyricsController m_controller;
    PlayerTracker m_tracker;
    int m_trackerOffsetEmits = 0;
    int m_settingsOffsetEmits = 0;
};

SettingsWiringTest::SettingsWiringTest()
    : m_settings(createSettings())
{
    m_controller.setPlayerTracker(&m_tracker);
    m_controller.setSettings(m_settings.get());

    QObject::connect(&m_tracker, &PlayerTracker::lyricOffsetChanged, this, [this] {
        ++m_trackerOffsetEmits;
    });
    QObject::connect(m_settings.get(), &KShadowLrcSettings::lyricOffsetMsChanged, this, [this] {
        ++m_settingsOffsetEmits;
    });
}

void SettingsWiringTest::defaultsReadFromTheEmptyConfig()
{
    // --- defaults read from the (empty) config file -------------------------
    QVERIFY(m_controller.displayMode() == 2); // Both
    QVERIFY(providerNames(m_controller) == QStringList({QStringLiteral("netease"), QStringLiteral("lrclib")}));
    // The placement defaults reproduce the old single-edge look: bottom edge,
    // 64 px above it, horizontally centred.
    QVERIFY(m_settings->verticalAnchor() == KShadowLrcSettings::EnumVerticalAnchor::Bottom);
    QVERIFY(m_settings->verticalMargin() == 64);
    QVERIFY(m_settings->horizontalAnchor() == KShadowLrcSettings::EnumHorizontalAnchor::Center);
    QVERIFY(m_settings->horizontalMargin() == 0);
    QVERIFY(m_settings->lyricOffsetMs() == 0);
    QVERIFY(m_trackerOffsetEmits == 0); // wiring startup does not emit
    QVERIFY(m_settingsOffsetEmits == 0);
}

void SettingsWiringTest::displayModeDrivesTheController()
{
    // --- displayMode drives the controller ----------------------------------
    m_settings->setDisplayMode(KShadowLrcSettings::EnumDisplayMode::Original);
    QVERIFY(m_controller.displayMode() == 0);
    m_settings->setDisplayMode(KShadowLrcSettings::EnumDisplayMode::Translation);
    QVERIFY(m_controller.displayMode() == 1);
    m_settings->setDisplayMode(KShadowLrcSettings::EnumDisplayMode::Both);
    QVERIFY(m_controller.displayMode() == 2);
}

void SettingsWiringTest::visibleLinesDriveTheUpcomingCount()
{
    // --- visibleLines drives the upcoming-line count ------------------------
    m_controller.setTestMode(); // 5 demo lines; the current line is line 0
    m_settings->setVisibleLines(3);
    QVERIFY(m_controller.upcomingLines().size() == 3);
    m_settings->setVisibleLines(1);
    QVERIFY(m_controller.upcomingLines().size() == 1);
    m_settings->setVisibleLines(10); // clamped; only 4 lines remain after line 0
    QVERIFY(m_controller.upcomingLines().size() == 4);
}

void SettingsWiringTest::providerSwitchesAreRespected()
{
    // --- provider switches are respected ------------------------------------
    m_settings->setNeteaseEnabled(false);
    QVERIFY(providerNames(m_controller) == QStringList({QStringLiteral("lrclib")}));
    m_settings->setLrclibEnabled(false);
    QVERIFY(providerNames(m_controller).isEmpty());
    m_settings->setNeteaseEnabled(true);
    QVERIFY(providerNames(m_controller) == QStringList({QStringLiteral("netease")}));
    m_settings->setLrclibEnabled(true);
    QVERIFY(providerNames(m_controller) == QStringList({QStringLiteral("netease"), QStringLiteral("lrclib")}));
}

void SettingsWiringTest::offsetRoundTripsExactlyOnce()
{
    // --- offset round-trip, each direction exactly once ---------------------
    m_tracker.setLyricOffsetMs(500);
    QCoreApplication::processEvents();
    QVERIFY(m_trackerOffsetEmits == 1);
    QVERIFY(m_settingsOffsetEmits == 1);
    QVERIFY(m_settings->lyricOffsetMs() == 500);
    QVERIFY(m_controller.engine()->lyricOffsetMs() == 500);

    m_settings->setLyricOffsetMs(700);
    QCoreApplication::processEvents();
    QVERIFY(m_trackerOffsetEmits == 2);
    QVERIFY(m_settingsOffsetEmits == 2);
    QVERIFY(m_tracker.lyricOffsetMs() == 700);
    QVERIFY(m_controller.engine()->lyricOffsetMs() == 700);
}

void SettingsWiringTest::rewritingTheSameOffsetEmitsNothing()
{
    // Re-writing the same value must not emit anything: that is the proof the
    // two-way connection cannot spin.
    m_tracker.setLyricOffsetMs(700);
    m_settings->setLyricOffsetMs(700);
    QCoreApplication::processEvents();
    QVERIFY(m_trackerOffsetEmits == 2);
    QVERIFY(m_settingsOffsetEmits == 2);
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
    QCoreApplication::setApplicationName(QStringLiteral("kshadowlrc-settings-wiring-test"));

    SettingsWiringTest tc;
    return QTest::qExec(&tc, argc, argv);
}

#include "settings_wiring_test.moc"
