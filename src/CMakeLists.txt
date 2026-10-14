# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 KShadowLrc Contributors

qt_add_resources(KSHADOWLRC_QML_RESOURCES qml/qml.qrc)

# Typed settings. kconfig_compiler turns config/kshadowlrcsettings.kcfg into
# kshadowlrcsettings.{h,cpp} in the build tree, declaring
# kshadowlrc::KShadowLrcSettings. GENERATE_MOC lets AUTOMOC see the generated
# header, which is required because the class declares Q_OBJECT signals.
# The .kcfg/.kcfgc are deliberately not named "kshadowlrc": the generated header
# is added to the include path as kshadowlrc.h and would then shadow src/kshadowlrc.h.
kconfig_add_kcfg_files(KSHADOWLRC_KCFG_SRCS GENERATE_MOC
    config/kshadowlrcsettings.kcfgc
)

# Sources are gathered per directory with GLOB_RECURSE + CONFIGURE_DEPENDS: a
# later lane can drop a new .cpp into app/, platform/, lyrics/ or player/ and
# have it compiled without ever editing this file.
#
# lyrics/ and player/ are globbed for .cpp only: their frozen headers
# (lyrictypes.h, provider.h, playertracker.h) declare Q_OBJECT classes whose
# implementations are a later lane's job, so AUTOMOC must not generate
# metaobjects for them yet. When e.g. provider.cpp lands, AUTOMOC picks up
# provider.h by its matching base name automatically.
file(GLOB_RECURSE KSHADOWLRC_APP_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/app/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/app/*.h"
)
file(GLOB_RECURSE KSHADOWLRC_PLATFORM_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/platform/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/platform/*.h"
)
file(GLOB_RECURSE KSHADOWLRC_LYRICS_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/lyrics/*.cpp"
)
file(GLOB_RECURSE KSHADOWLRC_PLAYER_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/player/*.cpp"
)
file(GLOB_RECURSE KSHADOWLRC_CONFIG_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/config/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/config/*.h"
)
# The desktop-integration surface: the tray icon and the global shortcuts.
file(GLOB_RECURSE KSHADOWLRC_SYSTEM_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/system/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/system/*.h"
)

add_library(kshadowlrc_lib STATIC
    kshadowlrc.h
    ${KSHADOWLRC_APP_SOURCES}
    ${KSHADOWLRC_PLATFORM_SOURCES}
    ${KSHADOWLRC_LYRICS_SOURCES}
    ${KSHADOWLRC_PLAYER_SOURCES}
    ${KSHADOWLRC_CONFIG_SOURCES}
    ${KSHADOWLRC_SYSTEM_SOURCES}
    ${KSHADOWLRC_KCFG_SRCS}
    ${KSHADOWLRC_QML_RESOURCES}
)

target_link_libraries(kshadowlrc_lib PUBLIC
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::DBus
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::WaylandClient
    KF6::CoreAddons
    KF6::I18n
    KF6::ConfigCore
    KF6::ConfigGui
    KF6::WindowSystem
    KF6::Kirigami
    KF6::GlobalAccel
    KF6::StatusNotifierItem
    LayerShellQt::Interface
)
# qpa/qplatformnativeinterface.h (raw wl_surface access for the input region).
target_link_libraries(kshadowlrc_lib PRIVATE Qt6::GuiPrivate)

if(KF6DBusAddons_FOUND)
    target_link_libraries(kshadowlrc_lib PUBLIC KF6::DBusAddons)
    target_compile_definitions(kshadowlrc_lib PRIVATE HAVE_KDBUSADDONS=1)
endif()

target_include_directories(kshadowlrc_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}
    # The frozen headers include their siblings flatly ("lyrictypes.h"), so the
    # lyric/player directories are on the include path.
    ${CMAKE_CURRENT_SOURCE_DIR}/lyrics
    ${CMAKE_CURRENT_SOURCE_DIR}/player
)

add_executable(kshadowlrc main.cpp)
target_link_libraries(kshadowlrc PRIVATE kshadowlrc_lib)

install(TARGETS kshadowlrc ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})

# Application identity and packaging. The three files are named after
# KSHADOWLRC_APP_ID (see the top-level CMakeLists.txt): the desktop file's base
# name must be the app id for the portal to resolve it, the AppStream component
# id must match the desktop file, and Icon= names the icon installed below.
install(FILES "${KSHADOWLRC_APP_ID}.desktop"
    DESTINATION ${KDE_INSTALL_APPDIR}
)
install(FILES "${KSHADOWLRC_APP_ID}.metainfo.xml"
    DESTINATION ${KDE_INSTALL_METAINFODIR}
)
# NOTE: the metainfo deliberately carries no <url type="homepage"> yet, because
# the project has no published URL to point at. appstreamcli validate therefore
# reports exactly one warning (url-homepage-missing) and exits non-zero, which
# makes ECM's appstreamtest fail once the project has been installed (it skips
# itself while there is no install manifest). Supplying the real homepage URL
# is all it takes to turn that test green again.
install(FILES "${KSHADOWLRC_APP_ID}.svg"
    DESTINATION ${KDE_INSTALL_ICONDIR}/hicolor/scalable/apps
)
