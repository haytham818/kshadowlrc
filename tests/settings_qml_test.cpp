// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 KShadowLrc Contributors
//
// Offline test for the settings window (src/qml/settings.qml).
//
// It loads the file through a real QQmlApplicationEngine and gives it exactly
// what the host gives it: the `settings` context property, an `outputs` object
// (a test-local stand-in for kshadowlrc::Outputs, so the screens are the test's
// choice), a `playerTracker` object (a stand-in for kshadowlrc::PlayerTracker,
// so the players are the test's choice too) and a KLocalizedQmlContext
// translator, so i18n() resolves. It checks that the file compiles,
// instantiates, shows itself and reports nothing to
// QQmlApplicationEngine::warnings. It also pins the numeric values the three
// enum combos bind to, because QML talks to the KConfigXT-generated enums purely
// by their int value, and the entries the output picker offers for a fixed pair
// of screens.
//
// The ignored-players list is pinned player by player: a player nobody ignores
// gets a switch that is off and writes its full bus name when flipped, a player
// an exact entry names gets a switch that is on and drops that name when
// flipped, and a player caught by a wildcard rule gets no switch but the rule
// spelled out. Entries no running player answers to are pinned too: a player
// that has stopped keeps a switch, a hand-written rule only a Remove button.
// The window asks the tracker for a fresh scan as it opens, which the stand-in
// counts.
//
// XDG_CONFIG_HOME and XDG_CONFIG_DIRS point at a private temporary directory
// before the first KSharedConfig is opened, so the developer's real
// kshadowlrcrc can be neither read nor written (the settings object saves on
// every write). The platform plugin is offscreen: no network, no GUI.

#include "config/settings.h"

#include <QGuiApplication>
#include <QJSValue>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlError>
#include <QQmlProperty>
#include <QQuickItem>
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

// The line the ignored-players section shows when there is nothing to list, as
// written in src/qml/settings.qml. Like the hint above it is looked up by
// objectName but pinned by text; keep the two in step if the wording changes.
const char kNoPlayersHint[] = "No MPRIS player is running right now.";

// The bus names and the one hand-written rule the player fixture is built from,
// written out once so a check that a switch writes "the full bus name" reads the
// same string its row was built from. The two shipped defaults live in the
// settings file's own entry (see kshadowlrcsettings.kcfg); the pattern is put on
// top of them by this test.
const QString kFirefox = QStringLiteral("org.mpris.MediaPlayer2.firefox");
const QString kPlayerctld = QStringLiteral("org.mpris.MediaPlayer2.playerctld");
const QString kMpv = QStringLiteral("org.mpris.MediaPlayer2.mpv");
const QString kVlc = QStringLiteral("org.mpris.MediaPlayer2.vlc");
const QString kMpvPattern = QStringLiteral("org.mpris.MediaPlayer2.mpv*");

// One player row's labels, as src/qml/settings.qml writes them.
const QString kFollowingHint = QStringLiteral("Following");
const QString kNotRunningHint = QStringLiteral("Not running");

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

/*!
 * Stand-in for kshadowlrc::PlayerTracker, the object the host injects as the
 * `playerTracker` context property. It has the shape the settings window reads:
 * a list of player maps, the signal that list notifies through, and the refresh
 * the window asks for as it opens. The list is whatever the test sets, which is
 * how the ignored-players rows are driven with no bus, no player and no D-Bus.
 */
class StubPlayerTracker : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList knownPlayers READ knownPlayers NOTIFY knownPlayersChanged)

public:
    using QObject::QObject;

    QVariantList knownPlayers() const
    {
        return m_players;
    }

    void setKnownPlayers(const QVariantList &players)
    {
        m_players = players;
        Q_EMIT knownPlayersChanged();
    }

    // The window calls this once as it is built; the test pins that it did.
    Q_INVOKABLE void refreshKnownPlayers()
    {
        ++m_refreshes;
    }

    int refreshes() const
    {
        return m_refreshes;
    }

Q_SIGNALS:
    void knownPlayersChanged();

private:
    QVariantList m_players;
    int m_refreshes = 0;
};

/*!
 * One entry of PlayerTracker::knownPlayers(): the bus name, the player's own
 * label, the ignore entry that catches it (empty when none does) and whether it
 * is the player being followed. `ignored` is derived from `rule`, so a fixture
 * cannot claim two different things about the same player.
 */
QVariantMap knownPlayer(const QString &name, const QString &label, const QString &rule, bool active)
{
    QVariantMap entry;
    entry.insert(QStringLiteral("name"), name);
    entry.insert(QStringLiteral("label"), label);
    entry.insert(QStringLiteral("ignored"), !rule.isEmpty());
    entry.insert(QStringLiteral("active"), active);
    entry.insert(QStringLiteral("rule"), rule);
    return entry;
}

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
    void placementRows();
    void outputPicker();
    void ignoredPlayersEditor();

private:
    std::unique_ptr<Settings> m_settings;
    QQmlApplicationEngine m_engine;
    StubOutputs m_outputs;
    StubPlayerTracker m_tracker;
    QList<QQmlError> m_reported;
    QObject *m_window = nullptr;

    // A Repeater inserts its delegates as children of its parent item, and they
    // carry no QObject parent at all, so findChildren() on the window never
    // reaches them. The rows of one repeater are read off its parent item's
    // childItems(), in model order, filtered by the objectName the delegate
    // declares.
    QList<QObject *> rowsOf(const QString &repeaterName, const QString &rowName) const;
    // The first label under `object` whose text is exactly `text`, or nullptr.
    QObject *labelWithText(QObject *object, const QString &text) const;
    // Flips a QQC2.Switch the way a click does, and reports whether the control
    // could be reached at all.
    static bool flipSwitch(QObject *control);
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

    // What the host injects for the ignored-players list: the three players the
    // section has to render, in the order the tracker sorts them by label - one
    // nobody ignores, one an exact entry names, one a pattern catches - and the
    // saved entries below, so every kind of row exists from the first load. The
    // window asks this object for a fresh scan as it opens, which it counts.
    m_tracker.setKnownPlayers(QVariantList{
        knownPlayer(kFirefox, QStringLiteral("Firefox"), kFirefox, false),
        knownPlayer(kMpv, QStringLiteral("mpv"), kMpvPattern, false),
        knownPlayer(kVlc, QStringLiteral("VLC media player"), QString(), true),
    });
    m_engine.rootContext()->setContextProperty(QStringLiteral("playerTracker"), &m_tracker);

    // The two entries the shipped settings file carries, plus one rule written
    // by hand. playerctld is not among the players above, which is what makes it
    // the "saved but not running" row.
    const QStringList shipped = m_settings->ignoredPlayers();
    QCOMPARE(shipped, QStringList({kFirefox, kPlayerctld}));
    m_settings->setIgnoredPlayers(QStringList({kFirefox, kPlayerctld, kMpvPattern}));

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

// The rows of one ignored-players repeater, in model order. A Repeater inserts
// its delegates as children of its parent item and gives them no QObject parent,
// so findChildren() on the window cannot see them; the parent item's childItems()
// can. Only the delegates are items there, so the filter is the objectName the
// delegate declares.
QList<QObject *> SettingsQmlTest::rowsOf(const QString &repeaterName, const QString &rowName) const
{
    QList<QObject *> rows;
    if (m_window == nullptr) {
        return rows;
    }
    auto *repeater = qobject_cast<QQuickItem *>(m_window->findChild<QObject *>(repeaterName));
    if (repeater == nullptr || repeater->parentItem() == nullptr) {
        return rows;
    }
    const QList<QQuickItem *> siblings = repeater->parentItem()->childItems();
    for (QQuickItem *sibling : siblings) {
        if (sibling->objectName() == rowName) {
            rows.append(sibling);
        }
    }
    return rows;
}

QObject *SettingsQmlTest::labelWithText(QObject *object, const QString &text) const
{
    if (object == nullptr) {
        return nullptr;
    }
    const auto children = object->findChildren<QObject *>();
    for (QObject *child : children) {
        if (child->inherits("QQuickLabel") && child->property("text").toString() == text) {
            return child;
        }
    }
    return nullptr;
}

bool SettingsQmlTest::flipSwitch(QObject *control)
{
    // A click moves the state and then emits toggled(), which is what the QML
    // listens to; toggle() alone only moves the state and emits nothing, so the
    // two steps are driven here the way the other controls in this test are.
    if (control == nullptr) {
        return false;
    }
    control->setProperty("checked", !control->property("checked").toBool());
    return QMetaObject::invokeMethod(control, "toggled");
}

void SettingsQmlTest::enumValues()
{
    // settings.qml binds the display-mode combo by index to this int, because
    // kconfig_compiler emits old-style nested enums without Q_ENUM. If the
    // <choices> block in the kcfg is ever reordered, this check fails and points
    // at the file that has to be updated with it.
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
    // the C++ base type instead.
    //
    // What is counted here is the window's own tree, which the ignored-player
    // rows are not part of: those are Repeater delegates, and a Repeater gives
    // its delegates no QObject parent (see rowsOf(), and ignoredPlayersEditor()
    // for the switches among them). The source rows are Loaders, whose items do
    // sit in the tree, so their two switches are counted below.
    int spinBoxes = 0;
    int comboBoxes = 0;
    int switches = 0;
    int textFields = 0;
    const auto children = m_window->findChildren<QObject *>();
    for (QObject *object : children) {
        // Both placement keys are QQC2.SpinBox too: the integer control driven in
        // tenths of a percent, not the 6.11-only DoubleSpinBox.
        spinBoxes += object->inherits("QQuickSpinBox") ? 1 : 0;
        comboBoxes += object->inherits("QQuickComboBox") ? 1 : 0;
        switches += object->inherits("QQuickSwitch") ? 1 : 0;
        textFields += object->inherits("QQuickTextField") ? 1 : 0;
    }
    QVERIFY2(spinBoxes == 6, "the form has a spin box for each of the 6 numeric keys");
    QVERIFY2(comboBoxes == 2, "the form has a combo box for the enum key and one for the output");
    QVERIFY2(switches == 4, "the form has a switch for each of the 4 boolean keys");
    // The two keys that are free text - the font family and the text colour -
    // plus the one field each of the 6 spin boxes and each of the 2 combo boxes
    // carries internally. The hand-typed player field is gone, and no ignored
    // player is edited by typing.
    QVERIFY2(textFields == 10, "the form has a text field for each of the 2 free-text keys, plus those of the spin and combo boxes");
    QVERIFY2(m_window->findChild<QObject *>(QStringLiteral("newPlayerField")) == nullptr, "the hand-typed player field is gone");
}

void SettingsQmlTest::placementRows()
{
    QVERIFY(m_window != nullptr);

    // The placement is a point: the text box's horizontal centre and its top
    // edge, each a percentage of the screen. One row each; the margins are
    // derived from that point in C++, never edited here. QQC2.SpinBox is
    // integer-only in this Qt, so each control counts tenths of a percent and
    // 375 is 37.5 %.
    QObject *horizontalPosition = m_window->findChild<QObject *>(QStringLiteral("horizontalPositionField"));
    QObject *verticalPosition = m_window->findChild<QObject *>(QStringLiteral("verticalPositionField"));
    QVERIFY2(horizontalPosition != nullptr, "the horizontal-position row is in the window");
    QVERIFY2(verticalPosition != nullptr, "the vertical-position row is in the window");

    // The rows open on what the file says, both as the tenths the control holds
    // and as the percentage it shows.
    QCOMPARE(horizontalPosition->property("value").toInt(), qRound(m_settings->horizontalPosition() * 10));
    QCOMPARE(verticalPosition->property("value").toInt(), qRound(m_settings->verticalPosition() * 10));
    QCOMPARE(horizontalPosition->property("displayText").toString(), QStringLiteral("50.0 %"));
    QCOMPARE(verticalPosition->property("displayText").toString(), QStringLiteral("80.0 %"));

    // Editing a row writes the setting, exactly as the control does it: set the
    // tenths value, then emit valueModified (which the arrow keys, the wheel and
    // the step buttons all do).
    horizontalPosition->setProperty("value", 375);
    QVERIFY2(QMetaObject::invokeMethod(horizontalPosition, "valueModified"), "the horizontal-position spin box's valueModified signal is reachable");
    QCOMPARE(m_settings->horizontalPosition(), 37.5);

    verticalPosition->setProperty("value", 625);
    QVERIFY2(QMetaObject::invokeMethod(verticalPosition, "valueModified"), "the vertical-position spin box's valueModified signal is reachable");
    QCOMPARE(m_settings->verticalPosition(), 62.5);

    // Dragging the overlay in adjusting mode writes the setting while this window
    // is open; the row follows it instead of keeping the value it last showed.
    m_settings->setHorizontalPosition(12.5);
    QCOMPARE(horizontalPosition->property("value").toInt(), 125);

    // A typed edit reaches the control through valueFromText when it is
    // committed; drive that function directly, the way the spin box does on
    // Enter, rather than going through the (GUI) text field. The function is a
    // QML JS function, so read it as a QJSValue and call it.
    const auto commitText = [](QObject *field, const QString &text) {
        const QJSValue function = QQmlProperty(field, QStringLiteral("valueFromText")).read().value<QJSValue>();
        return function.call(QJSValueList{QJSValue(text)}).toInt();
    };
    QVERIFY2(QQmlProperty(horizontalPosition, QStringLiteral("valueFromText")).read().value<QJSValue>().isCallable(),
             "the horizontal-position row exposes its valueFromText function");
    QVERIFY2(QQmlProperty(verticalPosition, QStringLiteral("valueFromText")).read().value<QJSValue>().isCallable(),
             "the vertical-position row exposes its valueFromText function");

    const auto valueFromTextHolds = [&](QObject *field) {
        // The commit runs against the value the control already shows: 12.5 %
        // on the horizontal row after the drag above, 62.5 % on the vertical
        // one after its arrow-key edit.
        const int shown = field->property("value").toInt();
        QVERIFY2(shown > 0, "the fallback is checked against a non-zero value");
        // Empty text and a bare "." pass the validator but parse to NaN; the
        // int value would take that as 0 and move the overlay to the screen
        // edge, so both have to keep what the control shows.
        QCOMPARE(commitText(field, QString()), shown);
        QCOMPARE(commitText(field, QStringLiteral(".")), shown);
        // A normal commit still parses, comma separator included ...
        QCOMPARE(commitText(field, QStringLiteral("37.5 %")), 375);
        QCOMPARE(commitText(field, QStringLiteral("37,5 %")), 375);
        // ... and an out-of-range entry clamps to the control's range.
        QCOMPARE(commitText(field, QStringLiteral("999 %")), 1000);
    };
    valueFromTextHolds(horizontalPosition);
    valueFromTextHolds(verticalPosition);

    // The old anchor and margin rows went with their keys.
    QVERIFY2(m_window->findChild<QObject *>(QStringLiteral("verticalAnchorField")) == nullptr, "the old vertical-anchor row is gone");
    QVERIFY2(m_window->findChild<QObject *>(QStringLiteral("horizontalAnchorField")) == nullptr, "the old horizontal-anchor row is gone");
    QVERIFY2(m_window->findChild<QObject *>(QStringLiteral("verticalMarginField")) == nullptr, "the old vertical-margin row is gone");
    QVERIFY2(m_window->findChild<QObject *>(QStringLiteral("horizontalMarginField")) == nullptr, "the old horizontal-margin row is gone");
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

    // The list is built from the player tracker the host injects, and the window
    // asks it for a fresh scan as it opens: the tracker re-scans on a timer, so
    // without that the window could open on what was playing a while ago.
    QCOMPARE(m_tracker.refreshes(), 1);
    QCOMPARE(m_tracker.knownPlayers().length(), 3);

    const auto runningRows = [this] {
        return rowsOf(QStringLiteral("runningPlayerRepeater"), QStringLiteral("runningPlayerRow"));
    };
    const auto storedRows = [this] {
        return rowsOf(QStringLiteral("storedPlayerRepeater"), QStringLiteral("storedPlayerRow"));
    };
    const auto labelsOf = [this](QObject *row, const QString &text) {
        return labelWithText(row, text);
    };
    const auto switchOf = [](QObject *row, const QString &name) {
        return row != nullptr ? row->findChild<QObject *>(name) : nullptr;
    };
    const auto visible = [](QObject *object) {
        return object != nullptr && object->property("visible").toBool();
    };
    // The sentence the window says for a rule that catches a player; keep it in
    // step with src/qml/settings.qml if the wording changes.
    const auto ruleText = [](const QString &rule) {
        return QStringLiteral("Ignored by the rule “%1”").arg(rule);
    };

    // ---- The players the tracker can see ------------------------------------

    // One row per player, in the order the tracker sorted them by label.
    QList<QObject *> running = runningRows();
    QVERIFY2(running.length() == 3, "one row per player the tracker reports");
    if (running.length() != 3) {
        return;
    }

    // Firefox: named by one of the shipped entries, so its switch is on.
    QObject *firefoxRow = running.at(0);
    QVERIFY2(labelsOf(firefoxRow, QStringLiteral("Firefox")) != nullptr, "the row shows the player's own label");
    QVERIFY2(labelsOf(firefoxRow, kFirefox) != nullptr, "the row shows the bus name under the label");
    QObject *firefoxSwitch = switchOf(firefoxRow, QStringLiteral("runningPlayerSwitch"));
    QVERIFY2(firefoxSwitch != nullptr, "a player named by an exact entry gets a switch");
    QVERIFY2(firefoxSwitch && firefoxSwitch->property("checked").toBool(), "the switch is on for a player that is ignored");
    QObject *firefoxRule = labelsOf(firefoxRow, ruleText(kFirefox));
    QVERIFY2(firefoxRule != nullptr, "a player row carries the rule line, shown or not");
    QVERIFY2(!visible(firefoxRule), "an exact entry is not reported as a rule catching the player");

    // mpv: caught by a pattern rather than by its own name. There is no entry
    // for that one player to turn off, so no switch is offered at all - only the
    // rule that catches it, spelled out.
    QObject *mpvRow = running.at(1);
    QVERIFY2(labelsOf(mpvRow, QStringLiteral("mpv")) != nullptr, "the row shows the player's own label");
    QVERIFY2(switchOf(mpvRow, QStringLiteral("runningPlayerSwitch")) == nullptr,
             "a player caught by a pattern gets no switch: turning one off cannot leave other players alone");
    QObject *mpvRule = labelsOf(mpvRow, ruleText(kMpvPattern));
    QVERIFY2(mpvRule != nullptr, "the row names the rule that catches the player");
    QVERIFY2(visible(mpvRule), "the rule is said out loud rather than left to the switch that is not there");

    // VLC: nobody ignores it, and it is the player being followed right now.
    QObject *vlcRow = running.at(2);
    QVERIFY2(labelsOf(vlcRow, QStringLiteral("VLC media player")) != nullptr, "the row shows the player's own label");
    QObject *vlcSwitch = switchOf(vlcRow, QStringLiteral("runningPlayerSwitch"));
    QVERIFY2(vlcSwitch != nullptr, "a player nobody ignores gets a switch");
    QVERIFY2(vlcSwitch && !vlcSwitch->property("checked").toBool(), "the switch is off for a player that is not ignored");
    QVERIFY2(visible(labelsOf(vlcRow, kFollowingHint)), "the player being followed is marked as such");
    QObject *firefoxFollowing = labelsOf(firefoxRow, kFollowingHint);
    QVERIFY2(firefoxFollowing != nullptr, "every player row can carry the followed mark");
    QVERIFY2(!visible(firefoxFollowing), "a player that is not followed carries no such mark");

    // Flipping a switch on writes the player's **full bus name** and nothing
    // else: no label, and the entries already there are untouched.
    const QStringList before = m_settings->ignoredPlayers();
    QVERIFY2(flipSwitch(vlcSwitch), "the switch's toggled signal is reachable");
    QStringList expected = before;
    expected.append(kVlc);
    QCOMPARE(m_settings->ignoredPlayers(), expected);
    QVERIFY2(!m_settings->ignoredPlayers().contains(QStringLiteral("VLC media player")), "the label is never written, the bus name is");

    // Flipping it off drops that entry again, leaving the rest alone.
    vlcSwitch = switchOf(runningRows().at(2), QStringLiteral("runningPlayerSwitch"));
    QVERIFY2(vlcSwitch != nullptr, "the row survived its own switch being flipped");
    QVERIFY2(flipSwitch(vlcSwitch), "the switch can be flipped back");
    QCOMPARE(m_settings->ignoredPlayers(), before);

    // The same for a player an exact entry names: off drops that entry.
    firefoxSwitch = switchOf(runningRows().at(0), QStringLiteral("runningPlayerSwitch"));
    QVERIFY2(firefoxSwitch != nullptr, "the ignored player's row is still there");
    QVERIFY2(flipSwitch(firefoxSwitch), "the ignored player's switch can be flipped off");
    QVERIFY2(!m_settings->ignoredPlayers().contains(kFirefox), "flipping the switch off drops the entry that named the player");
    QCOMPARE(m_settings->ignoredPlayers(), QStringList({kPlayerctld, kMpvPattern}));

    // ---- Entries no running player answers to --------------------------------

    // playerctld is in the settings file but not on the bus, and the pattern is
    // not a player at all. They come after the players, in the file's order.
    QList<QObject *> stored = storedRows();
    QVERIFY2(stored.length() == 2, "the entries no player answers to are listed under the players");
    if (stored.length() != 2) {
        return;
    }

    // A player that is not running keeps a switch, so ignoring it can be undone
    // without starting it again.
    QObject *playerctldRow = stored.at(0);
    QVERIFY2(labelsOf(playerctldRow, kPlayerctld) != nullptr, "the saved player is listed by its bus name");
    QVERIFY2(visible(labelsOf(playerctldRow, kNotRunningHint)), "the saved player is marked as not running");
    QObject *playerctldSwitch = switchOf(playerctldRow, QStringLiteral("storedPlayerSwitch"));
    QVERIFY2(playerctldSwitch != nullptr, "a saved player that is not running keeps its switch");
    QVERIFY2(playerctldSwitch && playerctldSwitch->property("checked").toBool(), "that switch is on, the entry is there");
    QVERIFY2(switchOf(playerctldRow, QStringLiteral("storedPlayerRemove")) == nullptr, "a player name is not offered a Remove button");

    // A pattern is a rule, not a player: it can only be taken off the list.
    QObject *patternRow = stored.at(1);
    QVERIFY2(labelsOf(patternRow, kMpvPattern) != nullptr, "the hand-written rule is listed as written");
    QObject *patternNotRunning = labelsOf(patternRow, kNotRunningHint);
    QVERIFY2(patternNotRunning != nullptr, "a saved row carries the not-running line, shown or not");
    QVERIFY2(!visible(patternNotRunning), "a rule is not a player and is not called one");
    QObject *patternRemove = switchOf(patternRow, QStringLiteral("storedPlayerRemove"));
    QVERIFY2(patternRemove != nullptr, "a hand-written rule gets a Remove button");
    QVERIFY2(switchOf(patternRow, QStringLiteral("storedPlayerSwitch")) == nullptr, "a hand-written rule gets no switch: it names no single player");

    // Removing the rule drops exactly that entry.
    QVERIFY2(QMetaObject::invokeMethod(patternRemove, "clicked"), "the Remove button's clicked signal is reachable");
    QCOMPARE(m_settings->ignoredPlayers(), QStringList({kPlayerctld}));
    QVERIFY2(storedRows().length() == 1, "the rule goes away with its row");

    // Switching the saved player off drops it too, so a player that was ignored
    // while it ran can be taken off the list after it has stopped.
    playerctldSwitch = switchOf(storedRows().at(0), QStringLiteral("storedPlayerSwitch"));
    QVERIFY2(playerctldSwitch != nullptr, "the saved player's row is still there");
    QVERIFY2(flipSwitch(playerctldSwitch), "the saved player's switch can be flipped off");
    QVERIFY2(m_settings->ignoredPlayers().isEmpty(), "flipping it off drops the saved entry");
    QVERIFY2(storedRows().isEmpty(), "the row goes away with the entry");

    // ---- Nothing at all -----------------------------------------------------

    QObject *emptyLabel = m_window->findChild<QObject *>(QStringLiteral("noPlayersLabel"));
    QVERIFY2(emptyLabel != nullptr, "the section carries the empty-state line");
    QVERIFY2(emptyLabel && emptyLabel->property("text").toString() == QLatin1String(kNoPlayersHint), "the empty-state line says what is missing");
    QVERIFY2(!visible(emptyLabel), "the empty state stays hidden while a player is listed");

    // No player on the bus and nothing saved: the section says so.
    m_tracker.setKnownPlayers(QVariantList());
    QVERIFY2(runningRows().isEmpty(), "no player means no player row");
    QVERIFY2(visible(emptyLabel), "with no player and no saved entry the section says so");

    // Put the fixture back for anyone reading the file's state afterwards; the
    // rows follow, which also proves the list is rebuilt from the tracker.
    m_tracker.setKnownPlayers(QVariantList{
        knownPlayer(kFirefox, QStringLiteral("Firefox"), kFirefox, false),
        knownPlayer(kMpv, QStringLiteral("mpv"), kMpvPattern, false),
        knownPlayer(kVlc, QStringLiteral("VLC media player"), QString(), true),
    });
    m_settings->setIgnoredPlayers(QStringList({kFirefox, kPlayerctld, kMpvPattern}));
    QVERIFY2(runningRows().length() == 3, "the rows follow the tracker's list");
    QVERIFY2(storedRows().length() == 2, "and so do the saved entries");
    QVERIFY2(!visible(emptyLabel), "the empty state hides again");
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
