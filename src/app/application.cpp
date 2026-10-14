// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 KShadowLrc Contributors

#include "app/application.h"

#include "config/settings.h"
#include "kshadowlrc.h"
#include "lyrics/controller.h"
#include "platform/overlaywindow.h"
#include "player/playertracker.h"

#include <KAboutData>
#include <KLocalizedString>

#if HAVE_KDBUSADDONS
#include <KDBusService>
#endif

#include <QIcon>
#include <QLoggingCategory>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickView>

Q_LOGGING_CATEGORY(lcApp, "kshadowlrc.app")

// Resources compiled into the static library must be explicitly initialised.
// Must be called from the global namespace, not inside kshadowlrc.
static void initResources()
{
    Q_INIT_RESOURCE(qml);
}

namespace kshadowlrc
{

Application::Application(int &argc, char **argv)
    : QApplication(argc, argv)
{
}

Application::~Application() = default;

KAboutData Application::makeAboutData()
{
    KAboutData aboutData(QStringLiteral("kshadowlrc"),
                         i18n("KShadowLrc"),
                         QStringLiteral(KSHADOWLRC_VERSION_STRING),
                         i18n("Floating desktop lyrics overlay for KDE Plasma 6"),
                         KAboutLicense::GPL_V3);
    aboutData.setOrganizationDomain(QByteArrayLiteral("io.github"));
    aboutData.setHomepage(QStringLiteral("https://github.com/kshadowlrc/kshadowlrc"));
    aboutData.setBugAddress(QByteArrayLiteral("https://github.com/kshadowlrc/kshadowlrc/issues"));
    aboutData.setDesktopFileName(QStringLiteral("io.github.kshadowlrc"));
    return aboutData;
}

/*! Maps a settings edge enum to the overlay's; only Top and Bottom exist. */
static OverlayWindow::Edge overlayEdge(int edge)
{
    return edge == KShadowLrcSettings::EnumEdge::Top ? OverlayWindow::Edge::Top : OverlayWindow::Edge::Bottom;
}

void Application::applyOverlaySettings(const QString &commandLineOutput)
{
    m_overlay->setEdge(overlayEdge(m_settings->edge()));
    m_overlay->setMargin(m_settings->margin());
    m_overlay->setClickThrough(m_settings->clickThrough());

    // Command line beats the file: a --output given here wins now, and keeps
    // winning if the file value is changed later.
    m_outputNameFromCommandLine = !commandLineOutput.isEmpty();
    m_overlay->setOutputName(m_outputNameFromCommandLine ? commandLineOutput : m_settings->outputName());

    connect(m_settings, &KShadowLrcSettings::edgeChanged, this, [this] {
        m_overlay->setEdge(overlayEdge(m_settings->edge()));
        qCInfo(lcApp) << "settings changed: edge ->" << m_settings->edge();
    });
    connect(m_settings, &KShadowLrcSettings::marginChanged, this, [this] {
        m_overlay->setMargin(m_settings->margin());
        qCInfo(lcApp) << "settings changed: margin ->" << m_settings->margin();
    });
    connect(m_settings, &KShadowLrcSettings::clickThroughChanged, this, [this] {
        m_overlay->setClickThrough(m_settings->clickThrough());
        qCInfo(lcApp) << "settings changed: clickThrough ->" << m_settings->clickThrough();
    });
    connect(m_settings, &KShadowLrcSettings::outputNameChanged, this, [this] {
        if (m_outputNameFromCommandLine) {
            qCInfo(lcApp) << "--output overrides outputName; ignoring file change to" << m_settings->outputName();
            return;
        }
        m_overlay->setOutputName(m_settings->outputName());
        qCInfo(lcApp) << "settings changed: outputName ->" << m_settings->outputName();
    });

    qCInfo(lcApp) << "overlay: edge=" << static_cast<int>(m_overlay->edge()) << "margin=" << m_overlay->margin() << "clickThrough=" << m_overlay->clickThrough()
                  << "output=" << (m_outputNameFromCommandLine ? commandLineOutput : m_settings->outputName())
                  << (m_outputNameFromCommandLine ? "(from command line)" : "(from settings)");
}

void Application::applyPlaybackSettings()
{
    m_playerTracker->setSyncIntervalMs(m_settings->syncIntervalMs());
    connect(m_settings, &KShadowLrcSettings::syncIntervalMsChanged, this, [this] {
        m_playerTracker->setSyncIntervalMs(m_settings->syncIntervalMs());
        qCInfo(lcApp) << "settings changed: syncIntervalMs ->" << m_settings->syncIntervalMs();
    });

    qCInfo(lcApp) << "playback: syncIntervalMs=" << m_settings->syncIntervalMs() << "lyricOffsetMs=" << m_settings->lyricOffsetMs()
                  << "displayMode=" << m_settings->displayMode() << "visibleLines=" << m_settings->visibleLines() << "netease=" << m_settings->neteaseEnabled()
                  << "lrclib=" << m_settings->lrclibEnabled();
}

int Application::run(const KAboutData &aboutData, const QString &outputName, bool demo)
{
    // Qt Quick Controls must use the Plasma style so Kirigami theming (fonts,
    // units) resolves. Only set a default: an explicit QT_QUICK_CONTROLS_STYLE
    // wins.
    if (QQuickStyle::name().isEmpty()) {
        QQuickStyle::setStyle(QStringLiteral("org.kde.desktop"));
    }

    // Application metadata; also sets the desktop file name used by D-Bus.
    KAboutData::setApplicationData(aboutData);
    setWindowIcon(QIcon::fromTheme(QStringLiteral("io.github.kshadowlrc")));
    // Never quit just because the (single) window is closed while remapping.
    setQuitOnLastWindowClosed(false);

#if HAVE_KDBUSADDONS
    // Single instance: a second launch hands its activation to this process.
    KDBusService service(KDBusService::Unique);
    connect(&service, &KDBusService::activateRequested, this, [](const QStringList &args, const QString &) {
        qCInfo(lcApp) << "Another instance attempted to start, ignoring. args:" << args;
    });
#else
    qCWarning(lcApp) << "KF6DBusAddons was not found at build time; the single-instance guard is disabled";
#endif

    initResources();

    // Opt into the layer-shell platform plugin. Equivalent to the deprecated
    // LayerShellQt::Shell::useLayerShell(); it only sets this variable and must
    // happen before the first QWindow is created.
    qputenv("QT_WAYLAND_SHELL_INTEGRATION", "layer-shell");

    // Settings are read once here and owned by the application. The factory
    // wires every changed signal to save(), so a later assignment persists.
    m_settings = createSettings(this);

    // The lyric bridge. Settings govern which providers it uses, its display
    // mode and its offset; the tracker supplies the playhead.
    m_lyricsController = std::make_unique<LyricsController>();
    m_lyricsController->setSettings(m_settings);

    m_playerTracker = std::make_unique<PlayerTracker>();
    m_lyricsController->setPlayerTracker(m_playerTracker.get());
    applyPlaybackSettings();

    // The overlay is constructed and configured before it is shown: the layer
    // surface binds to its output, edge and margin when its platform window is
    // created, so apply everything first.
    m_overlay = std::make_unique<OverlayWindow>();
    applyOverlaySettings(outputName);

    // Expose the native objects to QML. Set before setSource() so the root
    // object can bind to and call them from Component.onCompleted. The settings
    // object is handed over whole so QML bindings stay live rather than copied.
    QQmlContext *context = m_overlay->rootContext();
    context->setContextProperty(QStringLiteral("overlayWindow"), m_overlay.get());
    context->setContextProperty(QStringLiteral("lyricsController"), m_lyricsController.get());
    context->setContextProperty(QStringLiteral("settings"), m_settings);

    m_overlay->setSource(QUrl(QStringLiteral("qrc:/qml/main.qml")));
    if (m_overlay->status() == QQuickView::Error) {
        const auto errors = m_overlay->errors();
        for (const auto &error : errors) {
            qCCritical(lcApp) << "Overlay QML error:" << error.toString();
        }
    }

    // Mapping the surface created its platform window, so the shell integration
    // has already been chosen. Unset the variable so child processes (a future
    // "launch app" action) do not inherit it.
    m_overlay->show();
    qunsetenv("QT_WAYLAND_SHELL_INTEGRATION");

    // The demo runs only when asked for; otherwise the overlay waits for a real
    // player. The tracker connects asynchronously, and a player connecting
    // cancels the demo on its own (LyricsController::onPlayerConnected).
    if (demo) {
        m_lyricsController->setTestMode();
    }
    qCInfo(lcApp) << "starting player tracker";
    m_playerTracker->start();

    return exec();
}

} // namespace kshadowlrc
