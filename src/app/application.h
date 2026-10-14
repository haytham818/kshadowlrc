// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 KShadowLrc Contributors

#pragma once

#include <KAboutData>

#include <QApplication>

#include <memory>

namespace kshadowlrc
{

class KShadowLrcSettings;
class LyricsController;
class OverlayWindow;
class PlayerTracker;

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
     * Sets up metadata, the single-instance guard, the layer-shell platform
     * integration and the overlay, then runs the event loop. Returns the exit
     * code. When \p demo is set the built-in demo lyric runs instead of the
     * desktop's media players. \p outputName is the \c --output value and wins
     * over the settings file when non-empty.
     */
    int run(const KAboutData &aboutData, const QString &outputName = QString(), bool demo = false);

private:
    /*! Apply the overlay-owned settings and follow them at runtime. */
    void applyOverlaySettings(const QString &commandLineOutput);
    /*! Apply the playback-owned settings and follow them at runtime. */
    void applyPlaybackSettings();

    std::unique_ptr<OverlayWindow> m_overlay;
    std::unique_ptr<LyricsController> m_lyricsController;
    std::unique_ptr<PlayerTracker> m_playerTracker;
    KShadowLrcSettings *m_settings = nullptr;
    /*! True when --output was given: the command line then beats the file. */
    bool m_outputNameFromCommandLine = false;
};

} // namespace kshadowlrc
