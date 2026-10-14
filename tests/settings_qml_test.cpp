// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 KShadowLrc Contributors
//
// Offline test for the settings window (src/qml/settings.qml).
//
// It loads the file through a real QQmlApplicationEngine and gives it exactly
// what the host gives it: the `settings` context property, an `outputs` object
// (a test-local stand-in for kshadowlrc::Outputs, so the screens are the test's
// choice) and a KLocalizedQmlContext translator, so i18n() resolves. It checks
// that the file compiles, instantiates, shows itself and reports nothing to
// QQmlApplicationEngine::warnings. It also pins the numeric values the two enum
// combos bind to, because QML talks to the KConfigXT-generated enums purely by
// their int value, and the entries the output picker offers for a fixed pair of
// screens.
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
#include <QStringList>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

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

// The fallback hint under the output picker, as written in src/qml/settings.qml.
// The label carries no objectName, so it is looked up by text; keep the two in
// step if the wording ever changes.
const char kOutputFallbackHint[] = "That output is not connected; the overlay is shown on the primary output.";

/*!
 * Stand-in for kshadowlrc::Outputs, the object the host injects as the `outputs`
 * context property. It has the same shape - a list of names, the primary name,
 * and the one changed() signal both properties notify through - so the picker
 * can be driven against screens the test picks, without depending on the real
 * class or on the machine's actual outputs.
 */
class StubOutputs : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QStringList names READ names NOTIFY changed)
    Q_PROPERTY(QString primaryName READ primaryName NOTIFY changed)

public:
    using QObject::QObject;

    QStringList names() const
    {
        return m_names;
    }

    QString primaryName() const
    {
        return m_primaryName;
    }

    // What the real object does on screenAdded/screenRemoved/primaryScreenChanged.
    void setScreens(const QStringList &names, const QString &primaryName)
    {
        m_names = names;
        m_primaryName = primaryName;
        Q_EMIT changed();
    }

Q_SIGNALS:
    void changed();

private:
    QStringList m_names;
    QString m_primaryName;
};

} // namespace

using Settings = kshadowlrc::KShadowLrcSettings;

class SettingsQmlTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void enumValues();
    void loadsWithoutWarnings();
    void windowState();
    void outputPicker();
    void ignoredPlayersEditor();

private:
    std::unique_ptr<Settings> m_settings;
    QQmlApplicationEngine m_engine;
    StubOutputs m_outputs;
    QList<QQmlError> m_reported;
    QObject *m_window = nullptr;
};

void SettingsQmlTest::initTestCase()
{
    KLocalizedString::setApplicationDomain(QByteArrayLiteral("kshadowlrc"));

    // As src/app/application.cpp: set only a default, an explicit style wins.
    if (QQuickStyle::name().isEmpty()) {
        QQuickStyle::setStyle(QStringLiteral("org.kde.desktop"));
    }

    initResources();

    m_settings.reset(kshadowlrc::createSettings());

    m_engine.rootContext()->setContextProperty(QStringLiteral("settings"), m_settings.get());

    // What the host injects for the output picker: two connected outputs, with
    // HDMI-A-1 the primary one. The picker is bound to this object, so an
    // undefined `outputs` would make the window report a QML warning below.
    m_outputs.setScreens(QStringList{QStringLiteral("HDMI-A-1"), QStringLiteral("DP-1")}, QStringLiteral("HDMI-A-1"));
    m_engine.rootContext()->setContextProperty(QStringLiteral("outputs"), &m_outputs);

    // The window is loaded with an output already configured, so the picker has
    // to open on that entry; the picker checks below reset this setting.
    m_settings->setOutputName(QStringLiteral("DP-1"));

    // i18n() is not a global defined by importing org.kde.kirigami: it is a
    // method of the translator installed as the engine's root context object.
    // Give the engine the same one the host does, so the window here is loaded
    // through the same context it gets in production.
    KLocalizedQmlContext *context = KLocalization::setupLocalizedContext(&m_engine);
    context->setTranslationDomain(QString::fromUtf8(KLocalizedString::applicationDomain()));

    QObject::connect(&m_engine, &QQmlApplicationEngine::warnings, [this](const QList<QQmlError> &warnings) {
        m_reported += warnings;
    });

    m_engine.load(QUrl(QStringLiteral("qrc:/qml/settings.qml")));

    if (!m_engine.rootObjects().isEmpty()) {
        m_window = m_engine.rootObjects().constFirst();
    }
}

void SettingsQmlTest::enumValues()
{
    // settings.qml binds the combo boxes by index to these ints, because
    // kconfig_compiler emits old-style nested enums without Q_ENUM. If a
    // <choices> block in the kcfg is ever reordered, these checks fail and point
    // at the file that has to be updated with it.
    QCOMPARE(static_cast<int>(Settings::EnumEdge::Top), 0);
    QCOMPARE(static_cast<int>(Settings::EnumEdge::Bottom), 1);
    QCOMPARE(static_cast<int>(Settings::EnumEdge::COUNT), 2);
    QCOMPARE(static_cast<int>(Settings::EnumDisplayMode::Original), 0);
    QCOMPARE(static_cast<int>(Settings::EnumDisplayMode::Translation), 1);
    QCOMPARE(static_cast<int>(Settings::EnumDisplayMode::Both), 2);
    QCOMPARE(static_cast<int>(Settings::EnumDisplayMode::COUNT), 3);
}

void SettingsQmlTest::loadsWithoutWarnings()
{
    for (const QQmlError &error : m_reported) {
        std::fprintf(stderr, "QML: %s\n", qPrintable(error.toString()));
    }
    QVERIFY2(!m_engine.rootObjects().isEmpty(), "settings.qml creates a root object");
    QVERIFY2(m_reported.isEmpty(), "settings.qml reports no QML errors or warnings");
}

void SettingsQmlTest::windowState()
{
    QVERIFY2(m_window != nullptr, "settings.qml creates a root object");

    // An empty title would mean i18n() never resolved, in which case every
    // label in the window is blank too.
    QVERIFY2(!m_window->property("title").toString().isEmpty(), "the title resolved through i18n()");
    QVERIFY2(m_window->property("visible").toBool(), "the root window shows itself");

    // The form really built: one control per key in the kcfg. The class names
    // of QML-defined controls carry a Qt-version-dependent suffix, so match
    // the C++ base type instead. The combo boxes carry an internal text field
    // too, hence the lower bound on text fields.
    int spinBoxes = 0;
    int comboBoxes = 0;
    int switches = 0;
    int textFields = 0;
    const auto children = m_window->findChildren<QObject *>();
    for (QObject *object : children) {
        spinBoxes += object->inherits("QQuickSpinBox") ? 1 : 0;
        comboBoxes += object->inherits("QQuickComboBox") ? 1 : 0;
        switches += object->inherits("QQuickSwitch") ? 1 : 0;
        textFields += object->inherits("QQuickTextField") ? 1 : 0;
    }
    QVERIFY2(spinBoxes == 5, "the form has a spin box for each of the 5 integer keys");
    QVERIFY2(comboBoxes == 3, "the form has a combo box for each of the 2 enum keys and one for the output");
    QVERIFY2(switches == 4, "the form has a switch for each of the 4 boolean keys");
    QVERIFY2(textFields >= 3, "the form still has a text field for each of the free-text keys");
}

void SettingsQmlTest::outputPicker()
{
    QVERIFY(m_window != nullptr);
    const auto children = m_window->findChildren<QObject *>();

    // The output row is the only combo whose entries are value/label pairs,
    // so select it by its textRole; the two enum combos feed string models.
    QObject *outputPicker = nullptr;
    QObject *outputHint = nullptr;
    for (QObject *object : children) {
        if (object->inherits("QQuickComboBox") && object->property("textRole").toString() == QStringLiteral("label")) {
            outputPicker = object;
        }
        if (object->inherits("QQuickLabel") && object->property("text").toString() == QString::fromUtf8(kOutputFallbackHint)) {
            outputHint = object;
        }
    }
    QVERIFY2(outputPicker != nullptr, "the output row is a combo box over value/label entries");
    QVERIFY2(outputHint != nullptr, "the output row carries the fallback hint label");

    if (outputPicker && outputHint) {
        const auto entryCount = [outputPicker] {
            return outputPicker->property("model").toList().length();
        };
        const auto entryValue = [outputPicker](int index) {
            return outputPicker->property("model").toList().value(index).toMap().value(QStringLiteral("value")).toString();
        };
        const auto entryLabel = [outputPicker](int index) {
            return outputPicker->property("model").toList().value(index).toMap().value(QStringLiteral("label")).toString();
        };
        const auto currentIndex = [outputPicker] {
            return outputPicker->property("currentIndex").toInt();
        };
        const auto hintVisible = [outputHint] {
            return outputHint->property("visible").toBool();
        };

        // The file already names DP-1 (set before the load above): the picker
        // opens on that entry, not on the first one.
        QVERIFY2(currentIndex() == 2, "the picker opens on the output the file names");

        // An empty setting means "follow the primary output": it selects the
        // entry naming the output that is primary right now, and every
        // connected output follows under its own QScreen name.
        m_settings->setOutputName(QString());
        QVERIFY2(entryCount() == 3, "the picker offers the primary entry and one entry per connected output");
        QVERIFY2(entryValue(0) == QString(), "the primary entry is the empty name, which means follow the primary output");
        QVERIFY2(entryLabel(0) == QStringLiteral("Primary output (HDMI-A-1)"), "the primary entry names the output that is primary");
        QVERIFY2(entryValue(1) == QStringLiteral("HDMI-A-1") && entryValue(2) == QStringLiteral("DP-1"),
                 "each connected output appears under its QScreen name");
        QVERIFY2(currentIndex() == 0, "an empty setting selects the primary entry");
        QVERIFY2(!hintVisible(), "the fallback hint stays hidden while the configured output is connected");

        // The row follows the setting while the window is open, too.
        m_settings->setOutputName(QStringLiteral("DP-1"));
        QVERIFY2(currentIndex() == 2, "the configured output selects the matching entry");

        // A name the file carries but no screen answers to stays in the list,
        // marked unavailable and selected, and the row says what happens.
        m_settings->setOutputName(QStringLiteral("HDMI-A-9"));
        QVERIFY2(entryCount() == 4, "an unconnected configured output stays in the list");
        QVERIFY2(entryValue(3) == QStringLiteral("HDMI-A-9"), "the unavailable entry carries the configured name");
        QVERIFY2(entryLabel(3) == QStringLiteral("HDMI-A-9 (not connected)"), "the unavailable entry is marked as not connected");
        QVERIFY2(currentIndex() == 3, "an unconnected configured output stays selected");
        QVERIFY2(hintVisible(), "the fallback hint appears for an unconnected output");

        // The picked output disconnecting does the same, and still does not
        // rewrite the setting: only a pick commits.
        m_settings->setOutputName(QStringLiteral("DP-1"));
        m_outputs.setScreens(QStringList{QStringLiteral("HDMI-A-1"), QStringLiteral("eDP-1")}, QStringLiteral("HDMI-A-1"));
        QVERIFY2(m_settings->outputName() == QStringLiteral("DP-1"), "a hotplug does not rewrite the configured output");
        QVERIFY2(entryCount() == 4, "the output that went away becomes the unavailable entry");
        QVERIFY2(entryLabel(3) == QStringLiteral("DP-1 (not connected)"), "the output that went away is marked as not connected");
        QVERIFY2(currentIndex() == 3, "the selection stays on the output that went away");
        QVERIFY2(hintVisible(), "the fallback hint appears once the output is gone");

        // Outputs arriving around the pick must carry the selection with it:
        // the index is resolved from the value, never remembered.
        m_outputs.setScreens(QStringList{QStringLiteral("HDMI-A-1"), QStringLiteral("DP-1"), QStringLiteral("DVI-I-1"), QStringLiteral("eDP-1")},
                             QStringLiteral("HDMI-A-1"));
        QVERIFY2(m_settings->outputName() == QStringLiteral("DP-1"), "an output coming back does not rewrite it either");
        QVERIFY2(entryCount() == 5, "the output coming back is a normal entry again");
        QVERIFY2(currentIndex() == 2, "the selection follows the picked output when the list grows around it");
        QVERIFY2(!hintVisible(), "the fallback hint goes away once the output is connected again");

        // Picking an entry writes its value into the setting, exactly as the
        // control does it: set the index, then emit activated.
        outputPicker->setProperty("currentIndex", 1);
        QVERIFY2(QMetaObject::invokeMethod(outputPicker, "activated", Q_ARG(int, 1)), "the combo's activated signal is reachable");
        QVERIFY2(m_settings->outputName() == QStringLiteral("HDMI-A-1"), "activating an entry commits its value");
    }
}

void SettingsQmlTest::ignoredPlayersEditor()
{
    QVERIFY(m_window != nullptr);

    // The ignored-players editor is the one hand-rolled control: drive its
    // helpers and read the StringList property back, which also proves a
    // whole list survives the JS-array round trip.
    const QString extraPlayer = QStringLiteral("org.mpris.MediaPlayer2.settings-qml-test");
    QVERIFY2(QMetaObject::invokeMethod(m_window, "addIgnoredPlayer", Q_ARG(QVariant, extraPlayer)), "addIgnoredPlayer is callable from QML");
    QVERIFY2(m_settings->ignoredPlayers().contains(extraPlayer), "addIgnoredPlayer appends the entry");
    const int extraIndex = m_settings->ignoredPlayers().indexOf(extraPlayer);
    QVERIFY2(QMetaObject::invokeMethod(m_window, "removeIgnoredPlayer", Q_ARG(QVariant, extraIndex)), "removeIgnoredPlayer is callable from QML");
    QVERIFY2(!m_settings->ignoredPlayers().contains(extraPlayer), "removeIgnoredPlayer drops the entry");
    QVERIFY2(m_settings->ignoredPlayers().length() == 2, "the untouched defaults survive adding and removing");
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
    qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("offscreen"));

    QGuiApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("settings_qml_test"));

    SettingsQmlTest tc;
    return QTest::qExec(&tc, argc, argv);
}

#include "settings_qml_test.moc"
