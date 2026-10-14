# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 KShadowLrc Contributors

cmake_minimum_required(VERSION 3.22)

project(kshadowlrc VERSION 0.1.0 LANGUAGES C CXX)

set(QT_MIN_VERSION "6.8.0")
set(KF6_MIN_VERSION "6.0.0")
set(LAYER_SHELL_QT_MIN_VERSION "6.0.0")

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(ECM ${KF6_MIN_VERSION} REQUIRED NO_MODULE)
set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH})

include(KDEInstallDirs)
include(KDECMakeSettings)
include(KDECompilerSettings NO_POLICY_SCOPE)
include(KDEClangFormat)
include(KDEGitCommitHooks)
include(ECMDeprecationSettings)
include(FeatureSummary)

find_package(Qt6 ${QT_MIN_VERSION} REQUIRED COMPONENTS
    Core
    Gui
    Widgets
    DBus
    Quick
    QuickControls2
    WaylandClient
    # The test framework. Qt's own, so the harness needs no dependency of its
    # own; tests/ links Qt6::Test and ECM registers the result with CTest.
    Test
)
# Qt 6.9 moved the private targets into separate CMake packages; on older Qt the
# Qt6::GuiPrivate target already comes with the Gui component.
if(NOT TARGET Qt6::GuiPrivate)
    find_package(Qt6GuiPrivate ${Qt6Gui_VERSION} EXACT REQUIRED)
endif()

find_package(KF6 ${KF6_MIN_VERSION} REQUIRED COMPONENTS
    CoreAddons
    I18n
    Config
    WindowSystem
    # Kirigami gives the overlay and the settings window the KDE look, and
    # linking it is what makes the org.kde.kirigami QML module resolvable at
    # runtime instead of merely installed on the developer's machine.
    Kirigami
    # The system tray icon and the global shortcuts that adjust the lyric
    # offset from anywhere. Both are required: without them the overlay has no
    # way to be configured or nudged once it is running, and there is no
    # sensible degraded mode to fall back to.
    GlobalAccel
    StatusNotifierItem
)

# Optional: KDBusService backs the single-instance guard. Its devel package is
# not present everywhere, so the build stays fully functional without it and
# simply disables the guard (see HAVE_KDBUSADDONS in src/app/application.cpp).
find_package(KF6DBusAddons ${KF6_MIN_VERSION} QUIET)
add_feature_info("Single instance via D-Bus (KF6DBusAddons)" KF6DBusAddons_FOUND
    "KDBusService prevents a second copy of the overlay; install kf6-kdbusaddons-devel to enable it.")

find_package(LayerShellQt ${LAYER_SHELL_QT_MIN_VERSION} REQUIRED)

ecm_set_disabled_deprecation_versions(
    QT ${QT_MIN_VERSION}
    KF ${KF6_MIN_VERSION}
)

add_compile_definitions(KSHADOWLRC_VERSION_STRING="${PROJECT_VERSION}")

# The application identity, in one place. It is the reverse-DNS application id
# that the host portal, the tray and KGlobalAccel key off: it is the desktop
# file's base name, the AppStream component id, the icon name, and the value
# KAboutData reports as the desktop file name and the component name (from
# which QCoreApplication::applicationName is taken). The packaging files in
# src/ are named after it; everything that has to agree is either generated
# from this variable or names the same files.
set(KSHADOWLRC_APP_ID "io.github.haytham818.kshadowlrc")
add_compile_definitions(KSHADOWLRC_APP_ID="${KSHADOWLRC_APP_ID}")

add_subdirectory(src)
add_subdirectory(tests)

file(GLOB_RECURSE ALL_CLANG_FORMAT_SOURCE_FILES CONFIGURE_DEPENDS src/*.cpp src/*.h tests/*.cpp)
kde_clang_format(${ALL_CLANG_FORMAT_SOURCE_FILES})
kde_configure_git_pre_commit_hook(CHECKS CLANG_FORMAT)

feature_summary(WHAT ALL FATAL_ON_MISSING_REQUIRED_PACKAGES)
