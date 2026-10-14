// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 KShadowLrc Contributors

#include "app/application.h"

#include "config/settings.h"
#include "kshadowlrc.h"
#include "lyrics/controller.h"
#include "platform/outputs.h"
#include "platform/overlaywindow.h"
#include "player/playertracker.h"
#include "system/controlservice.h"

#include <KAboutData>
#include <KLocalizedQmlContext>
#include <KLocalizedString>

#if HAVE_KDBUSADDONS
#include <KDBusService>
#endif

#include <QIcon>
#include <QLoggingCategory>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickView>
#include <QQuickWindow>

Q_LOGGING_CATEGORY(lcApp, "kshadowlrc.app")

// Resources compiled into the static library must be explicitly initialised.
// Must be called from the global namespace, not inside kshadowlrc.
static void initResources()
{
    Q_INIT_RESOURCE(qml);
    Q_INIT_RESOURCE(icons);
}

namespace kshadowlrc
{

/*!
 * Give \p engine's QML the i18n() family.
 *
 * Importing org.kde.kirigami does not define i18n(): it is a method of the
 * translator that has to be installed as the engine's root context object. The
 * translation domain is the one main.cpp sets with
 * KLocalizedString::setApplicationDomain, read back rather than repeated here.
 */
static void installLocalization(QQmlEngine *engine)
{
    KLocalizedQmlContext *context = KLocalization::setupLocalizedContext(engine);
    context->setTranslationDomain(QString::fromUtf8(KLocalizedString::applicationDomain()));
}

Application::Application(int &argc, char **argv)
    : QApplication(argc, argv)
{
}

Application::~Application() = default;

KAboutData Application::makeAboutData()
{
    // The component name is the application id: KAboutData::setApplicationData
    // makes it QCoreApplication::applicationName, which is what the host portal
    // and KGlobalAccel identify us by. The desktop file, the AppStream
    // metainfo and the icon are all named after it.
    KAboutData aboutData(QStringLiteral(KSHADOWLRC_APP_ID),
                         i18n("KShadowLrc"),
                         QStringLiteral(KSHADOWLRC_VERSION_STRING),
                         i18n("Floating desktop lyrics overlay for KDE Plasma 6"),
                         KAboutLicense::GPL_V3);
    // Deliberately no organization domain. KDBusService builds its single-instance
    // bus name as reversed(organizationDomain) + "." + applicationName, and the
    // component name is already a reverse-DNS id, so setting one here would yield
    // the doubled "haytham818.github.io.io.github.haytham818.kshadowlrc". Leaving
    // it empty makes the bus name exactly the application id.
    aboutData.setHomepage(QStringLiteral("https://github.com/haytham818/kshadowlrc"));
    aboutData.setBugAddress(QByteArrayLiteral("https://github.com/haytham818/kshadowlrc/issues"));
    aboutData.setDesktopFileName(QStringLiteral(KSHADOWLRC_APP_ID));
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

    // The current value decides the initial output; the settings window's picker
    // is authoritative afterwards, so a change there moves the overlay even when
    // --output was passed. (A second --output from the same process is not a
    // thing.)
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

void Application::createControlService()
{
    // The tray icon and the global shortcuts need the lyric controller's offset;
    // everything else they ask for belongs to the application, so they come back
    // as signals rather than as pointers to the overlay or the window policy.
    m_controlService = std::make_unique<ControlService>(m_lyricsController.get());

    connect(m_controlService.get(), &ControlService::settingsRequested, this, &Application::openSettingsWindow);
    connect(m_controlService.get(), &ControlService::overlayVisibilityToggleRequested, this, [this] {
        const bool visible = !m_overlay->isVisible();
        m_overlay->setVisible(visible);
        m_controlService->setOverlayVisible(visible);
        qCInfo(lcApp) << "overlay" << (visible ? "shown" : "hidden");
    });
    connect(m_controlService.get(), &ControlService::quitRequested, this, [] {
        qCInfo(lcApp) << "quit requested from the tray";
        QCoreApplication::quit();
    });

    m_controlService->setOverlayVisible(m_overlay->isVisible());
}

void Application::openSettingsWindow()
{
    if (!m_settingsEngine) {
        m_settingsEngine = std::make_unique<QQmlApplicationEngine>();
        installLocalization(m_settingsEngine.get());
        // Set before load() so the window's bindings are live from the first
        // evaluation; the settings object is shared with the overlay, so a change
        // made in either place reaches the other at once.
        m_settingsEngine->rootContext()->setContextProperty(QStringLiteral("settings"), m_settings);
        // The window's status line says what the lyrics are doing, which is the
        // same answer the tray gives: both read this controller.
        m_settingsEngine->rootContext()->setContextProperty(QStringLiteral("lyricsController"), m_lyricsController.get());
        // The output picker can only show what is connected, so it reads the live
        // output list. Parented to the application, so it outlives the engine.
        m_settingsEngine->rootContext()->setContextProperty(QStringLiteral("outputs"), new Outputs(this));

        connect(m_settingsEngine.get(), &QQmlApplicationEngine::warnings, this, [](const QList<QQmlError> &warnings) {
            for (const QQmlError &warning : warnings) {
                qCWarning(lcApp) << "Settings window QML:" << warning.toString();
            }
        });

        m_settingsEngine->load(QUrl(QStringLiteral("qrc:/qml/settings.qml")));

        if (m_settingsEngine->rootObjects().isEmpty()) {
            qCCritical(lcApp) << "the settings window could not be loaded";
            m_settingsEngine.reset();
            return;
        }
        qCInfo(lcApp) << "settings window loaded";
    }

    auto *window = qobject_cast<QQuickWindow *>(m_settingsEngine->rootObjects().constFirst());
    if (window) {
        window->show();
        window->raise();
        window->requestActivate();
        qCInfo(lcApp) << "settings window shown";
    }
}

int Application::run(const KAboutData &aboutData, const QString &outputName, bool demo, bool openSettings)
{
    // Qt Quick Controls must use the Plasma style so Kirigami theming (fonts,
    // units) resolves. Only set a default: an explicit QT_QUICK_CONTROLS_STYLE
    // wins.
    if (QQuickStyle::name().isEmpty()) {
        QQuickStyle::setStyle(QStringLiteral("org.kde.desktop"));
    }

    // Application metadata; also sets the desktop file name used by D-Bus.
    KAboutData::setApplicationData(aboutData);
    setWindowIcon(QIcon::fromTheme(QStringLiteral(KSHADOWLRC_APP_ID)));
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

    // The overlay's layer-shell identity comes from LayerShellQt::Window::get()
    // inside its constructor (on the platform surface being created), so that has
    // to run before this QWindow is first mapped. Everything the surface binds -
    // the output, anchors and margins - is applied before show() below too.
    m_overlay = std::make_unique<OverlayWindow>();
    applyOverlaySettings(outputName);

    // Expose the native objects to QML. Set before setSource() so the root
    // object can bind to and call them from Component.onCompleted. The settings
    // object is handed over whole so QML bindings stay live rather than copied.
    installLocalization(m_overlay->engine());
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

    m_overlay->show();

    // The settings window and the tray icon are ordinary toplevels: they are never
    // passed through LayerShellQt, so only the overlay above is a layer surface.
    createControlService();
    if (openSettings) {
        openSettingsWindow();
    }

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
