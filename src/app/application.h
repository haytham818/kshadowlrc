// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 KShadowLrc Contributors

#pragma once

#include "platform/overlayplacement.h"

#include <KAboutData>

#include <QApplication>
#include <QString>

#include <memory>

class QQmlApplicationEngine;

namespace kshadowlrc
{

class KShadowLrcSettings;
class LyricsController;
class OverlayWindow;
class PlayerTracker;
class ControlService;

/*!
 * The application object.
 *
 * Owns the process lifetime and the top-level objects: the settings object, the
 * single layer-shell overlay surface and the lyric controller the overlay
 * renders.
 */
class Application : public QApplication
{
    Q_OBJECT

public:
    Application(int &argc, char **argv);
    ~Application() override;

    /*! Static metadata; callable before a QApplication exists. */
    static KAboutData makeAboutData();

    /*!
     * Sets up metadata, the single-instance guard, the overlay and the lyrics
     * pipeline behind it, then runs the event loop. Returns the exit code. When
     * \p demo is set the built-in demo lyric runs instead of the desktop's media
     * players. \p outputName is the \c --output value: it decides the initial
     * output when non-empty, and the settings window can move the overlay from
     * there on. When \p openSettings is set the settings window is opened as soon
     * as the overlay is up; it is otherwise reachable from the tray icon.
     */
    int run(const KAboutData &aboutData, const QString &outputName = QString(), bool demo = false, bool openSettings = false);

private:
    /*! Apply the overlay-owned settings and follow them at runtime. */
    void applyOverlaySettings(const QString &commandLineOutput);
    /*! The placement the four General settings currently describe. */
    Placement placementFromSettings() const;
    /*! Apply the playback-owned settings and follow them at runtime. */
    void applyPlaybackSettings();
    /*! Build the tray icon and the global shortcuts and hook them to the app. */
    void createControlService();
    /*! Show the settings window, loading it on first use. */
    void openSettingsWindow();
    /*! Show the manual lyric search window, loading it on first use. */
    void openSearchWindow();

    std::unique_ptr<OverlayWindow> m_overlay;
    std::unique_ptr<LyricsController> m_lyricsController;
    std::unique_ptr<PlayerTracker> m_playerTracker;
    std::unique_ptr<ControlService> m_controlService;
    /*! The settings window's QML engine; created the first time it is opened. */
    std::unique_ptr<QQmlApplicationEngine> m_settingsEngine;
    /*! The search window's QML engine; created the first time it is opened. */
    std::unique_ptr<QQmlApplicationEngine> m_searchEngine;
    KShadowLrcSettings *m_settings = nullptr;
    /*!
     * True when --output was given: it then decides the initial output instead of
     * the settings file. Later changes come from the settings window, which is
     * authoritative while the application runs.
     */
    bool m_outputNameFromCommandLine = false;
    /*! The --output value, kept so a cancelled drag can re-apply the settings
     *  without clobbering it. */
    QString m_commandLineOutput;
    /*! True once the settings-changed connects have been made, so re-applying
     *  the settings on a cancelled drag does not wire them twice. */
    bool m_overlaySettingsWired = false;
};

} // namespace kshadowlrc
