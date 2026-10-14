// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 KShadowLrc Contributors
//
// Offline test for the settings window (src/qml/settings.qml).
//
// It loads the file through a real QQmlApplicationEngine and gives it exactly
// what the host gives it: the `settings` context property and a
// KLocalizedQmlContext translator, so i18n() resolves. It checks that the file
// compiles, instantiates, shows itself and reports nothing to
// QQmlApplicationEngine::warnings. It also pins the numeric values the two enum
// combos bind to, because QML talks to the KConfigXT-generated enums purely by
// their int value.
//
// XDG_CONFIG_HOME and XDG_CONFIG_DIRS point at a private temporary directory
// before the first KSharedConfig is opened, so the developer's real
// kshadowlrcrc can be neither read nor written (the settings object saves on
// every write). The platform plugin is offscreen: no network, no GUI.

#include "config/settings.h"

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlError>
#include <QQuickStyle>
#include <QTemporaryDir>
#include <QUrl>

#include <KLocalizedQmlContext>
#include <KLocalizedString>

#include <cstdio>
#include <memory>

// The QML lives in a resource compiled into the static kshadowlrc_lib; without
// an explicit initialiser the linker drops it and qrc:/qml/settings.qml cannot
// be resolved. Q_INIT_RESOURCE must run in the global namespace, so this helper
// sits outside any namespace, exactly as src/app/application.cpp does it.
static void initResources()
{
    Q_INIT_RESOURCE(qml);
}

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
    qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("offscreen"));

    QGuiApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("settings_qml_test"));
    KLocalizedString::setApplicationDomain(QByteArrayLiteral("kshadowlrc"));

    // As src/app/application.cpp: set only a default, an explicit style wins.
    if (QQuickStyle::name().isEmpty()) {
        QQuickStyle::setStyle(QStringLiteral("org.kde.desktop"));
    }

    using Settings = kshadowlrc::KShadowLrcSettings;

    // settings.qml binds the combo boxes by index to these ints, because
    // kconfig_compiler emits old-style nested enums without Q_ENUM. If a
    // <choices> block in the kcfg is ever reordered, these checks fail and point
    // at the file that has to be updated with it.
    check(static_cast<int>(Settings::EnumEdge::Top) == 0, "EnumEdge::Top is 0");
    check(static_cast<int>(Settings::EnumEdge::Bottom) == 1, "EnumEdge::Bottom is 1");
    check(static_cast<int>(Settings::EnumEdge::COUNT) == 2, "EnumEdge has exactly 2 values");
    check(static_cast<int>(Settings::EnumDisplayMode::Original) == 0, "EnumDisplayMode::Original is 0");
    check(static_cast<int>(Settings::EnumDisplayMode::Translation) == 1, "EnumDisplayMode::Translation is 1");
    check(static_cast<int>(Settings::EnumDisplayMode::Both) == 2, "EnumDisplayMode::Both is 2");
    check(static_cast<int>(Settings::EnumDisplayMode::COUNT) == 3, "EnumDisplayMode has exactly 3 values");

    initResources();

    std::unique_ptr<Settings> settings(kshadowlrc::createSettings());

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("settings"), settings.get());

    // i18n() is not a global defined by importing org.kde.kirigami: it is a
    // method of the translator installed as the engine's root context object.
    // Give the engine the same one the host does, so the window here is loaded
    // through the same context it gets in production.
    KLocalizedQmlContext *context = KLocalization::setupLocalizedContext(&engine);
    context->setTranslationDomain(QString::fromUtf8(KLocalizedString::applicationDomain()));

    QList<QQmlError> reported;
    QObject::connect(&engine, &QQmlApplicationEngine::warnings, [&reported](const QList<QQmlError> &warnings) {
        reported += warnings;
    });

    engine.load(QUrl(QStringLiteral("qrc:/qml/settings.qml")));

    check(!engine.rootObjects().isEmpty(), "settings.qml creates a root object");
    check(reported.isEmpty(), "settings.qml reports no QML errors or warnings");
    for (const QQmlError &error : reported) {
        std::fprintf(stderr, "QML: %s\n", qPrintable(error.toString()));
    }

    if (!engine.rootObjects().isEmpty()) {
        QObject *window = engine.rootObjects().constFirst();
        // An empty title would mean i18n() never resolved, in which case every
        // label in the window is blank too.
        check(!window->property("title").toString().isEmpty(), "the title resolved through i18n()");
        check(window->property("visible").toBool(), "the root window shows itself");

        // The form really built: one control per key in the kcfg. The class names
        // of QML-defined controls carry a Qt-version-dependent suffix, so match
        // the C++ base type instead. The combo boxes and spin boxes each carry an
        // internal text field too, hence the lower bound on text fields.
        int spinBoxes = 0;
        int comboBoxes = 0;
        int switches = 0;
        int textFields = 0;
        const auto children = window->findChildren<QObject *>();
        for (QObject *object : children) {
            spinBoxes += object->inherits("QQuickSpinBox") ? 1 : 0;
            comboBoxes += object->inherits("QQuickComboBox") ? 1 : 0;
            switches += object->inherits("QQuickSwitch") ? 1 : 0;
            textFields += object->inherits("QQuickTextField") ? 1 : 0;
        }
        check(spinBoxes == 5, "the form has a spin box for each of the 5 integer keys");
        check(comboBoxes == 2, "the form has a combo box for each of the 2 enum keys");
        check(switches == 4, "the form has a switch for each of the 4 boolean keys");
        check(textFields >= 4, "the form has a text field for each of the 4 string keys");

        // The ignored-players editor is the one hand-rolled control: drive its
        // helpers and read the StringList property back, which also proves a
        // whole list survives the JS-array round trip.
        const QString extraPlayer = QStringLiteral("org.mpris.MediaPlayer2.settings-qml-test");
        check(QMetaObject::invokeMethod(window, "addIgnoredPlayer", Q_ARG(QVariant, extraPlayer)), "addIgnoredPlayer is callable from QML");
        check(settings->ignoredPlayers().contains(extraPlayer), "addIgnoredPlayer appends the entry");
        const int extraIndex = settings->ignoredPlayers().indexOf(extraPlayer);
        check(QMetaObject::invokeMethod(window, "removeIgnoredPlayer", Q_ARG(QVariant, extraIndex)), "removeIgnoredPlayer is callable from QML");
        check(!settings->ignoredPlayers().contains(extraPlayer), "removeIgnoredPlayer drops the entry");
        check(settings->ignoredPlayers().length() == 2, "the untouched defaults survive adding and removing");
    }

    if (g_failures == 0) {
        std::printf("settings_qml_test: settings.qml loads, i18n resolves, the enum values and the window state all match\n");
        return 0;
    }
    std::fprintf(stderr, "settings_qml_test: %d check(s) failed\n", g_failures);
    return 1;
}
