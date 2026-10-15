// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 KShadowLrc Contributors

// The ignored-players repeaters' delegates call back into the root's helpers,
// so the components must be bound to the context they are declared in.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Dialogs
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/*!
 * Settings window for KShadowLrc.
 *
 * Every control reads and writes the one `settings` context property, a
 * kshadowlrc::KShadowLrcSettings whose mutators are wired to save(): a change
 * made here is written to kshadowlrcrc at once and reaches the running overlay
 * through the object's changed signals. Nothing is buffered locally, so there is
 * no OK or Apply button and the window can be closed at any time.
 *
 * The keys are grouped exactly as in src/config/kshadowlrcsettings.kcfg
 * (General, Playback, Providers); the section headings spell those groups out
 * in words rather than in the kcfg's names.
 *
 * Two things here are about the lyrics rather than a preference. The row at the
 * very top says what the lyrics are doing right now, read from the
 * `lyricsController` context property the overlay uses; it stays hidden when the
 * host gave the window no controller. Its two lines come from that controller's
 * `lyricStatus` and `lastError` properties, composed in C++ from
 * src/lyrics/lyricstatus.h -- the one home for the wording, which the tray tool
 * tip also goes through -- so this window keeps no copy of the sentences. The
 * last section orders the lyric sources: three rows over
 * root.resolvedSourceOrder() with up/down buttons, which write values only back
 * into settings.sourceOrder. The rows are written out one by one rather than
 * built by a Repeater, because a Repeater's delegates are not part of the
 * window's object tree and the window's test counts the controls it finds there.
 *
 * The ignored-players section is built from the `playerTracker` context property
 * the host injects, which reports every MPRIS player it can see: the bus name,
 * the player's own label, and the ignore entry that catches it, if any. Each row
 * is one player and writes only back into settings.ignoredPlayers: switching a
 * player on appends its full bus name, switching it off drops that name, and a
 * player caught by a wildcard or prefix rule is shown without a switch at all,
 * because there is no entry for that player alone to turn off. Entries that
 * answer to no running player stay listed underneath - a hand-written rule with
 * a Remove button, a player that has stopped with a switch - so both can be
 * dropped. Opening the window asks the tracker for a fresh scan, or the list
 * would be whatever it saw up to ten seconds ago.
 *
 * The enum combo (which lyric line to show) binds its currentIndex straight to
 * the int the KConfigXT compiler generates, because kconfig_compiler emits
 * old-style nested enums with no Q_ENUM and does not register the class as a QML
 * type - QML cannot name KShadowLrcSettings.EnumDisplayMode.Both. Its model is
 * ordered to match the kcfg <choices> block and must be kept in step with it.
 *
 * The output picker is the one control with no plain file-backed value to edit:
 * it lists the connected outputs from the `outputs` context property (a
 * kshadowlrc::Outputs) and writes the chosen name into settings.outputName on
 * activation only. Committing from a currentIndex change instead would rewrite
 * the config file every time a hotplug rebuilds the model.
 *
 * i18n() is callable here because the host installs a translator
 * (KLocalization::setupLocalizedContext, or a KLocalizedContext) as the engine's
 * root context object; importing org.kde.kirigami on its own does not define it.
 */
Kirigami.ApplicationWindow {
    id: root

    title: i18n("KShadowLrc Settings")
    width: 620
    height: 680
    minimumWidth: 460
    minimumHeight: 360
    // The host never calls show(); the window opens itself.
    visible: true

    // A colour the settings' textColor accepts: exactly #rrggbb.
    function isHexColor(value) {
        return /^#[0-9a-fA-F]{6}$/.test(value)
    }

    // One 0..1 colour channel as a two-digit hex byte.
    function hexByte(value) {
        const byte = Math.round(Math.max(0, Math.min(1, value)) * 255).toString(16)
        return byte.length === 1 ? "0" + byte : byte
    }

    function colorToHex(color) {
        return "#" + root.hexByte(color.r) + root.hexByte(color.g) + root.hexByte(color.b)
    }

    // The picker's entries: "follow the primary output" first, naming the output
    // that is primary right now, then one entry per connected output. A name
    // that is configured but not connected stays in the list, so the row keeps
    // showing what the file actually says.
    function outputModel() {
        const names = outputs.names
        const primary = outputs.primaryName
        const entries = [{
            value: "",
            label: primary.length > 0 ? i18n("Primary output (%1)", primary) : i18n("Primary output")
        }]
        for (let i = 0; i < names.length; ++i) {
            entries.push({ value: names[i], label: names[i] })
        }
        if (root.configuredOutputMissing()) {
            entries.push({ value: settings.outputName, label: i18n("%1 (not connected)", settings.outputName) })
        }
        return entries
    }

    // True when the configured output is a name no connected screen has: the
    // overlay then falls back to the primary output, and the row says so.
    function configuredOutputMissing() {
        return settings.outputName.length > 0 && outputs.names.indexOf(settings.outputName) === -1
    }

    // ---- Ignored players ----------------------------------------------------

    // The host hands the window the player tracker, as the `playerTracker`
    // context property. A bare engine (the QML test) has none, so the section
    // then lists the saved entries alone rather than reporting a warning.
    readonly property bool hasPlayerTracker: typeof playerTracker !== "undefined"

    // What the tracker can see right now, in its own order (by label). Reading
    // the property here is what keeps the rows live: the tracker notifies
    // knownPlayersChanged whenever it re-scans.
    readonly property var knownPlayers: root.hasPlayerTracker ? playerTracker.knownPlayers : []

    // The saved entries no visible player answers to: a player that is not
    // running right now, or a rule written by hand. Listed under the running
    // players so either can be taken off the list.
    function storedIgnoredPlayers() {
        const running = root.knownPlayers.map(entry => entry.name)
        return settings.ignoredPlayers.filter(entry => running.indexOf(entry) === -1)
    }

    // A saved entry with a * or ? is a glob over the whole bus name, as the
    // kcfg entry says; anything else names one player.
    function isPlayerPattern(entry) {
        return entry.indexOf("*") !== -1 || entry.indexOf("?") !== -1
    }

    // The ignored-players list is a StringList property: read it, edit the copy,
    // write the whole list back so the object persists it. Every control in the
    // section goes through here, so the file holds exactly the entries that were
    // chosen and nothing else. A player is always written as its full bus name,
    // which is what an ignore entry matches.
    function setPlayerIgnored(name, ignored) {
        const players = settings.ignoredPlayers.slice()
        const index = players.indexOf(name)
        if (ignored) {
            if (index !== -1) {
                return
            }
            players.push(name)
        } else if (index !== -1) {
            players.splice(index, 1)
        } else {
            return
        }
        settings.ignoredPlayers = players
    }

    // A just-opened window must not list what was playing a scan ago; the
    // tracker re-scans on a timer, so ask it for one now.
    Component.onCompleted: {
        if (root.hasPlayerTracker) {
            playerTracker.refreshKnownPlayers()
        }
    }

    // ---- Lyric sources ------------------------------------------------------

    // The three lyric sources, in the order they are tried by default. The
    // value is what the engine and the settings file use; `local` is a .lrc
    // file next to the song or lyrics saved inside the song file itself.
    readonly property var lyricSources: [
        {
            value: "netease",
            label: i18n("NetEase"),
            hint: i18n("Online. Synced lyrics, with a translation when one is available.")
        },
        {
            value: "lrclib",
            label: i18n("LRCLIB"),
            hint: i18n("Online. Synced lyrics in a single language.")
        },
        {
            value: "local",
            label: i18n("Local file"),
            hint: i18n("A .lrc file next to the song, or lyrics saved inside the song file.")
        }
    ]

    // The source order as the engine reads it: unknown entries dropped, entries
    // missing from the setting appended in the default order. The list shows
    // this, so a hand-edited config file still lines up with three rows.
    function resolvedSourceOrder() {
        const known = root.lyricSources.map(source => source.value)
        const ordered = []
        const configured = settings.sourceOrder
        for (let i = 0; i < configured.length; ++i) {
            if (known.indexOf(configured[i]) !== -1 && ordered.indexOf(configured[i]) === -1) {
                ordered.push(configured[i])
            }
        }
        for (let i = 0; i < known.length; ++i) {
            if (ordered.indexOf(known[i]) === -1) {
                ordered.push(known[i])
            }
        }
        return ordered
    }

    function sourceLabel(value) {
        const source = root.lyricSources.find(entry => entry.value === value)
        return source ? source.label : value
    }

    // The source the search-order list shows in one of its three rows, so a row
    // can be written out statically and still follow the configured order.
    function sourceAt(index) {
        const order = root.resolvedSourceOrder()
        return index >= 0 && index < order.length ? order[index] : ""
    }

    function sourceHint(value) {
        const source = root.lyricSources.find(entry => entry.value === value)
        return source ? source.hint : ""
    }

    // The two online sources can be switched off; the local file source has no
    // switch, so this is always true for it.
    function sourceEnabled(value) {
        if (value === "netease") {
            return settings.neteaseEnabled
        }
        if (value === "lrclib") {
            return settings.lrclibEnabled
        }
        return true
    }

    function setSourceEnabled(value, enabled) {
        if (value === "netease") {
            settings.neteaseEnabled = enabled
        } else if (value === "lrclib") {
            settings.lrclibEnabled = enabled
        }
    }

    // Move one source up (-1) or down (+1) and write the whole list back, values
    // only, in display order.
    function moveSource(index, delta) {
        const order = root.resolvedSourceOrder()
        const target = index + delta
        if (index < 0 || index >= order.length || target < 0 || target >= order.length) {
            return
        }
        const moved = order.splice(index, 1)[0]
        order.splice(target, 0, moved)
        settings.sourceOrder = order
    }

    // ---- Lyric status -------------------------------------------------------

    // The host hands the window the lyric controller the overlay uses, as the
    // `lyricsController` context property. A bare engine (the QML test) has
    // none, so the row hides itself rather than naming something that is not
    // there; typeof does not throw for an identifier that was never declared.
    readonly property bool hasLyricController: typeof lyricsController !== "undefined"

    // What the lyrics are doing, in words, straight from the controller. Both
    // strings are composed in C++ from src/lyrics/lyricstatus.h, so this window
    // keeps no copy of the sentences: lyricStatus is the sentence for the
    // current state, and lyricError is the diagnostic it failed with. The engine
    // clears the diagnostic on success, so no state check is needed for it.
    readonly property string lyricStatus: root.hasLyricController ? lyricsController.lyricStatus : ""
    readonly property string lyricError: root.hasLyricController ? lyricsController.lastError : ""

    pageStack.initialPage: Kirigami.ScrollablePage {
        id: settingsPage

        title: i18n("Settings")

        Kirigami.FormLayout {
            id: form

            // ---- Lyric status -----------------------------------------------

            // What the lyrics are doing right now, at the top because it says
            // something about this moment rather than a preference. The words
            // come from the controller and never from this file. A failure adds
            // the diagnostic underneath, small and dimmed, so it reads as an
            // aside rather than as part of the sentence.
            ColumnLayout {
                Kirigami.FormData.label: i18n("Lyric status:")
                Layout.preferredWidth: Kirigami.Units.gridUnit * 20
                spacing: Math.round(Kirigami.Units.smallSpacing / 2)
                // Hidden only when the host gave the window no controller, which
                // is what a bare QML load does.
                visible: root.hasLyricController

                QQC2.Label {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    text: root.lyricStatus
                }

                QQC2.Label {
                    Layout.fillWidth: true
                    // Only when there is one; a successful resolution leaves the
                    // diagnostic empty.
                    visible: text.length > 0
                    wrapMode: Text.WordWrap
                    font: Kirigami.Theme.smallFont
                    opacity: 0.7
                    text: root.lyricError
                }
            }

            // ---- General ----------------------------------------------------

            Kirigami.Separator {
                Kirigami.FormData.isSection: true
                Kirigami.FormData.label: i18n("General")
            }

            QQC2.TextField {
                id: fontFamilyField
                Kirigami.FormData.label: i18n("Font family:")
                placeholderText: i18n("Default font")
                text: settings.fontFamily
                // Committed on Enter or focus loss, not per keystroke: every
                // assignment writes the config file.
                onEditingFinished: settings.fontFamily = text
            }

            QQC2.SpinBox {
                id: fontSizeField
                Kirigami.FormData.label: i18n("Font size:")
                from: 1
                to: 999
                value: settings.fontSize
                textFromValue: (value) => i18n("%1 pt", value)
                onValueModified: settings.fontSize = value
            }

            RowLayout {
                Kirigami.FormData.label: i18n("Text color:")
                spacing: Kirigami.Units.smallSpacing

                // Swatch: shows the typed colour once it is valid, otherwise the
                // last one that was committed.
                Rectangle {
                    Layout.preferredWidth: Kirigami.Units.gridUnit * 1.5
                    Layout.preferredHeight: Kirigami.Units.gridUnit * 1.5
                    Layout.alignment: Qt.AlignVCenter
                    radius: Math.round(Kirigami.Units.smallSpacing * 0.75)
                    color: root.isHexColor(colorField.text) ? colorField.text : settings.textColor
                    border.width: 1
                    border.color: Kirigami.Theme.textColor
                }

                QQC2.TextField {
                    id: colorField
                    Layout.preferredWidth: Kirigami.Units.gridUnit * 6
                    placeholderText: i18n("#rrggbb")
                    text: settings.textColor
                    // Only a complete #rrggbb is written; a half-typed value is
                    // left alone rather than stored.
                    onTextEdited: {
                        if (root.isHexColor(text)) {
                            settings.textColor = text
                        }
                    }
                }

                QQC2.Button {
                    text: i18n("Choose…")
                    onClicked: {
                        colorDialog.selectedColor = root.isHexColor(colorField.text) ? colorField.text : settings.textColor
                        colorDialog.open()
                    }
                }
            }

            QQC2.Switch {
                id: outlineField
                Kirigami.FormData.label: i18n("Text outline:")
                checked: settings.outlineEnabled
                onToggled: settings.outlineEnabled = checked
            }

            QQC2.ComboBox {
                id: displayModeField
                Kirigami.FormData.label: i18n("Lyrics to show:")
                // The model order must match the <choices> order in
                // src/config/kshadowlrcsettings.kcfg exactly:
                // 0 = Original, 1 = Translation, 2 = Both.
                model: [i18n("Original"), i18n("Translation"), i18n("Both")]
                currentIndex: settings.displayMode
                onActivated: settings.displayMode = currentIndex
            }

            QQC2.SpinBox {
                id: visibleLinesField
                Kirigami.FormData.label: i18n("Upcoming lines:")
                from: 0
                to: 10
                value: settings.visibleLines
                onValueModified: settings.visibleLines = value
            }

            // Where the overlay sits: a point, not a place between two edges. The
            // text box's horizontal centre and its top edge are percentages of
            // the screen, and the layer-shell margins are re-derived from that
            // point every time the lyrics change size, so a longer or shorter
            // line never moves the text being read.
            //
            // QQC2.SpinBox is integer-only in this Qt - DoubleSpinBox is only
            // exported from 6.11 and the project builds against 6.8 - so each
            // control counts tenths of a percent: 0..1000 is 0.0..100.0 %, and
            // the text reads as a percentage with one decimal. The wheel, the
            // arrow keys and the step buttons all move one tenth (0.1 %) at a
            // time, and the value can also be typed. The validator has to accept
            // the " %" the text carries, or the field would refuse its own
            // displayed value the moment a key is pressed.
            ColumnLayout {
                Kirigami.FormData.label: i18n("Horizontal position:")
                Layout.preferredWidth: Kirigami.Units.gridUnit * 15
                spacing: Math.round(Kirigami.Units.smallSpacing / 2)

                QQC2.SpinBox {
                    id: horizontalPositionField

                    objectName: "horizontalPositionField"
                    Layout.fillWidth: true
                    editable: true
                    from: 0
                    to: 1000
                    stepSize: 1
                    value: Math.round(settings.horizontalPosition * 10)
                    textFromValue: (value) => i18n("%1 %", (value / 10).toFixed(1))
                    // A typed edit becomes the value only on commit. An empty
                    // field or a bare "." passes the validator but parses to
                    // NaN, and assigning that to the int value would silently
                    // drop it to 0 - the screen edge. Keep the value already
                    // shown then, and clamp the rest to the control's range.
                    valueFromText: (text) => {
                        const tenths = Math.round(parseFloat(text.replace(",", ".")) * 10)
                        return isNaN(tenths) ? value : Math.max(0, Math.min(1000, tenths))
                    }
                    validator: RegularExpressionValidator {
                        regularExpression: /^\s*\d{0,3}([.,]\d*)?\s*%?$/
                    }
                    onValueModified: settings.horizontalPosition = value / 10

                    // The overlay can be dragged while this window is open, which
                    // rewrites the setting and would break the value binding
                    // above; follow it explicitly, as the offset row does.
                    Connections {
                        target: settings
                        function onHorizontalPositionChanged() {
                            horizontalPositionField.value = Math.round(settings.horizontalPosition * 10)
                        }
                    }
                }

                QQC2.Label {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    font: Kirigami.Theme.smallFont
                    opacity: 0.7
                    text: i18n("The centre of the lyric text, as a percentage of the screen width.")
                }
            }

            ColumnLayout {
                Kirigami.FormData.label: i18n("Vertical position:")
                Layout.preferredWidth: Kirigami.Units.gridUnit * 15
                spacing: Math.round(Kirigami.Units.smallSpacing / 2)

                QQC2.SpinBox {
                    id: verticalPositionField

                    objectName: "verticalPositionField"
                    Layout.fillWidth: true
                    editable: true
                    from: 0
                    to: 1000
                    stepSize: 1
                    value: Math.round(settings.verticalPosition * 10)
                    textFromValue: (value) => i18n("%1 %", (value / 10).toFixed(1))
                    // As above: a commit of NaN (empty field, bare ".") must
                    // not become 0 %, and the tenths are clamped to the range.
                    valueFromText: (text) => {
                        const tenths = Math.round(parseFloat(text.replace(",", ".")) * 10)
                        return isNaN(tenths) ? value : Math.max(0, Math.min(1000, tenths))
                    }
                    validator: RegularExpressionValidator {
                        regularExpression: /^\s*\d{0,3}([.,]\d*)?\s*%?$/
                    }
                    onValueModified: settings.verticalPosition = value / 10

                    // As above: a drag in adjusting mode rewrites the setting.
                    Connections {
                        target: settings
                        function onVerticalPositionChanged() {
                            verticalPositionField.value = Math.round(settings.verticalPosition * 10)
                        }
                    }
                }

                QQC2.Label {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    font: Kirigami.Theme.smallFont
                    opacity: 0.7
                    text: i18n("The top edge of the lyric text, as a percentage of the screen height; the text grows downward from here, so the line being read stays put.")
                }
            }

            QQC2.Switch {
                id: clickThroughField
                Kirigami.FormData.label: i18n("Click-through:")
                checked: settings.clickThrough
                onToggled: settings.clickThrough = checked
            }

            ColumnLayout {
                Kirigami.FormData.label: i18n("Output:")
                Layout.preferredWidth: Kirigami.Units.gridUnit * 15
                spacing: Math.round(Kirigami.Units.smallSpacing / 2)

                QQC2.ComboBox {
                    id: outputField
                    Layout.fillWidth: true
                    // Rebuilt whenever a screen comes or goes: outputModel()
                    // reads outputs.names and outputs.primaryName, both of which
                    // notify through outputs.changed.
                    model: root.outputModel()
                    // The entries are { value, label }: the value is what goes
                    // into settings.outputName, "" meaning "follow the primary".
                    valueRole: "value"
                    textRole: "label"

                    // currentIndex is deliberately not a binding: picking an
                    // entry makes the control assign it itself, which drops the
                    // binding, and after that a hotplug has to re-resolve the row
                    // from the setting rather than leave the index sitting on
                    // whichever entry happened to move into that slot.
                    function selectConfiguredOutput() {
                        currentIndex = indexOfValue(settings.outputName)
                    }

                    Component.onCompleted: outputField.selectConfiguredOutput()

                    // A pick is the only thing that writes the setting: committing
                    // from a currentIndex change would rewrite the config file
                    // every time a hotplug rebuilds the model.
                    onActivated: settings.outputName = currentValue

                    Connections {
                        target: settings
                        function onOutputNameChanged() {
                            outputField.selectConfiguredOutput()
                        }
                    }

                    Connections {
                        target: outputs
                        function onChanged() {
                            outputField.selectConfiguredOutput()
                        }
                    }
                }

                QQC2.Label {
                    Layout.fillWidth: true
                    // Only while the configured name really is unconnected.
                    visible: root.configuredOutputMissing()
                    wrapMode: Text.WordWrap
                    font: Kirigami.Theme.smallFont
                    opacity: 0.7
                    text: i18n("That output is not connected; the overlay is shown on the primary output.")
                }
            }

            // ---- Playback ---------------------------------------------------

            Kirigami.Separator {
                Kirigami.FormData.isSection: true
                Kirigami.FormData.label: i18n("Playback")
            }

            ColumnLayout {
                Kirigami.FormData.label: i18n("Lyric offset:")
                Layout.preferredWidth: Kirigami.Units.gridUnit * 15
                spacing: Math.round(Kirigami.Units.smallSpacing / 2)

                QQC2.SpinBox {
                    id: offsetField
                    Layout.fillWidth: true
                    // Same bounds as <min>/<max> for lyricOffsetMs in
                    // src/config/kshadowlrcsettings.kcfg; they are also what the
                    // global-shortcut nudge clamps to, so a value picked here is
                    // never silently trimmed later.
                    from: -10000
                    to: 10000
                    stepSize: 50
                    value: settings.lyricOffsetMs
                    textFromValue: (value) => i18n("%1 ms", value)
                    onValueModified: settings.lyricOffsetMs = value

                    // The Meta+Alt nudge shortcuts write the offset while this
                    // window is open, which would break the value binding above;
                    // follow the setting explicitly.
                    Connections {
                        target: settings
                        function onLyricOffsetMsChanged() {
                            offsetField.value = settings.lyricOffsetMs
                        }
                    }
                }

                QQC2.Label {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    font: Kirigami.Theme.smallFont
                    opacity: 0.7
                    text: i18n("A positive value shifts the lyrics earlier.")
                }
            }

            QQC2.SpinBox {
                id: syncIntervalField
                Kirigami.FormData.label: i18n("Sync interval:")
                from: 1
                to: 60000
                stepSize: 100
                value: settings.syncIntervalMs
                textFromValue: (value) => i18n("%1 ms", value)
                onValueModified: settings.syncIntervalMs = value
            }

            ColumnLayout {
                Kirigami.FormData.label: i18n("Ignored players:")
                Layout.preferredWidth: Kirigami.Units.gridUnit * 24
                spacing: Kirigami.Units.smallSpacing

                // Nothing to list at all: say so instead of leaving a heading
                // with nothing under it.
                QQC2.Label {
                    objectName: "noPlayersLabel"
                    Layout.fillWidth: true
                    visible: root.knownPlayers.length === 0 && settings.ignoredPlayers.length === 0
                    wrapMode: Text.WordWrap
                    font: Kirigami.Theme.smallFont
                    opacity: 0.7
                    text: i18n("No MPRIS player is running right now.")
                }

                // Every player the tracker can see, ignored or not, sorted by
                // label in C++. One row per player, and the switch is the whole
                // edit: on means the overlay ignores that player.
                Repeater {
                    id: runningPlayerRepeater
                    objectName: "runningPlayerRepeater"
                    model: root.knownPlayers

                    delegate: RowLayout {
                        id: runningPlayerRow
                        objectName: "runningPlayerRow"
                        required property var modelData
                        Layout.fillWidth: true
                        spacing: Kirigami.Units.smallSpacing

                        // `rule` is the ignore entry that catches this player,
                        // empty when none does. Only an entry naming this player
                        // and nothing else can be turned off without changing
                        // what happens to other players.
                        readonly property bool ignoredByRule: runningPlayerRow.modelData.rule.length > 0
                        readonly property bool ignoredByName: runningPlayerRow.modelData.rule === runningPlayerRow.modelData.name

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 0

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: Kirigami.Units.smallSpacing

                                QQC2.Label {
                                    Layout.fillWidth: true
                                    text: runningPlayerRow.modelData.label
                                    elide: Text.ElideRight
                                }

                                // Which player is feeding the overlay this
                                // moment. Small and dim, an aside to the name
                                // rather than a badge of its own.
                                QQC2.Label {
                                    objectName: "runningPlayerActive"
                                    Layout.alignment: Qt.AlignVCenter
                                    visible: runningPlayerRow.modelData.active
                                    text: i18n("Following")
                                    font: Kirigami.Theme.smallFont
                                    opacity: 0.7
                                }
                            }

                            // The bus name under the label, so what is listed
                            // here can be checked against a hand-written entry
                            // in the settings file.
                            QQC2.Label {
                                Layout.fillWidth: true
                                text: runningPlayerRow.modelData.name
                                elide: Text.ElideMiddle
                                font: Kirigami.Theme.smallFont
                                opacity: 0.7
                            }

                            // Caught by a wildcard, a prefix or a bus-name
                            // segment: there is no entry for this player alone, so
                            // a switch could only do nothing, or stop ignoring
                            // others as well. Say which rule catches it instead,
                            // and leave the row without a switch.
                            QQC2.Label {
                                objectName: "runningPlayerRule"
                                Layout.fillWidth: true
                                visible: runningPlayerRow.ignoredByRule && !runningPlayerRow.ignoredByName
                                text: i18n("Ignored by the rule “%1”", runningPlayerRow.modelData.rule)
                                elide: Text.ElideMiddle
                                font: Kirigami.Theme.smallFont
                                opacity: 0.7
                            }
                        }

                        // Only where a switch can mean something: off (ignored)
                        // and on (not ignored) both map to one entry.
                        Loader {
                            Layout.alignment: Qt.AlignVCenter
                            active: !runningPlayerRow.ignoredByRule || runningPlayerRow.ignoredByName
                            sourceComponent: Component {
                                QQC2.Switch {
                                    objectName: "runningPlayerSwitch"
                                    // A label-free switch still has to say what it
                                    // does: its own text would be read out as the
                                    // switch's label and shown next to it.
                                    Accessible.name: i18n("Ignore %1", runningPlayerRow.modelData.label)
                                    checked: runningPlayerRow.ignoredByName
                                    onToggled: root.setPlayerIgnored(runningPlayerRow.modelData.name, checked)

                                    // Flipping the switch assigns checked itself,
                                    // which drops the binding above; a row the
                                    // tracker later re-uses for another player, or
                                    // an entry edited elsewhere, is followed
                                    // explicitly instead, as the output row does.
                                    Connections {
                                        target: runningPlayerRow

                                        function onIgnoredByNameChanged() {
                                            checked = runningPlayerRow.ignoredByName
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                // Entries no running player answers to. A hand-written rule can
                // only be removed; a player that has stopped keeps its switch, so
                // ignoring it can be undone without starting it again.
                Repeater {
                    id: storedPlayerRepeater
                    objectName: "storedPlayerRepeater"
                    model: root.storedIgnoredPlayers()

                    delegate: RowLayout {
                        id: storedPlayerRow
                        objectName: "storedPlayerRow"
                        required property string modelData
                        Layout.fillWidth: true
                        spacing: Kirigami.Units.smallSpacing

                        readonly property bool isPattern: root.isPlayerPattern(storedPlayerRow.modelData)

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 0

                            QQC2.Label {
                                Layout.fillWidth: true
                                text: storedPlayerRow.modelData
                                elide: Text.ElideMiddle
                            }

                            QQC2.Label {
                                visible: !storedPlayerRow.isPattern
                                text: i18n("Not running")
                                font: Kirigami.Theme.smallFont
                                opacity: 0.7
                            }
                        }

                        Loader {
                            Layout.alignment: Qt.AlignVCenter
                            active: storedPlayerRow.isPattern
                            sourceComponent: Component {
                                QQC2.Button {
                                    objectName: "storedPlayerRemove"
                                    Accessible.name: i18n("Remove the rule %1", storedPlayerRow.modelData)
                                    text: i18n("Remove")
                                    onClicked: root.setPlayerIgnored(storedPlayerRow.modelData, false)
                                }
                            }
                        }

                        Loader {
                            Layout.alignment: Qt.AlignVCenter
                            active: !storedPlayerRow.isPattern
                            sourceComponent: Component {
                                QQC2.Switch {
                                    objectName: "storedPlayerSwitch"
                                    Accessible.name: i18n("Ignore %1", storedPlayerRow.modelData)
                                    checked: true
                                    onToggled: root.setPlayerIgnored(storedPlayerRow.modelData, false)
                                }
                            }
                        }
                    }
                }

                QQC2.Label {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    font: Kirigami.Theme.smallFont
                    opacity: 0.7
                    text: i18n("A switched-on player is skipped by the overlay. To ignore one that is not in the list, add its bus name or a pattern to the settings file by hand.")
                }
            }

            // ---- Lyric sources ----------------------------------------------

            Kirigami.Separator {
                Kirigami.FormData.isSection: true
                Kirigami.FormData.label: i18n("Lyric sources")
            }

            ColumnLayout {
                Kirigami.FormData.label: i18n("Search order:")
                Layout.preferredWidth: Kirigami.Units.gridUnit * 24
                spacing: Kirigami.Units.smallSpacing

                // The three rows are written out one at a time rather than built
                // by a Repeater: the settings window's test counts the controls
                // it finds in the window's object tree, and a Repeater's
                // delegates are not part of it. Each row reads its source from
                // root.sourceAt(index), so the order still comes from
                // settings.sourceOrder alone. The shared comment on the buttons
                // and the switch sits by the first row.

                // Row 0: the source tried first.
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Kirigami.Units.smallSpacing

                    Loader {
                        Layout.alignment: Qt.AlignVCenter
                        active: root.sourceAt(0) !== "local"
                        sourceComponent: Component {
                            QQC2.Switch {
                                checked: root.sourceEnabled(root.sourceAt(0))
                                onToggled: root.setSourceEnabled(root.sourceAt(0), checked)
                            }
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 0

                        QQC2.Label {
                            Layout.fillWidth: true
                            text: root.sourceLabel(root.sourceAt(0))
                        }

                        // One line saying what the source is, so the order is a
                        // choice between things a person recognises.
                        QQC2.Label {
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            font: Kirigami.Theme.smallFont
                            opacity: 0.7
                            text: root.sourceHint(root.sourceAt(0))
                        }
                    }

                    // Buttons, not drag and drop: three rows, and the keys have
                    // to work here as well as the pointer. The first row cannot
                    // move up and the last cannot move down. Only the two online
                    // sources carry a switch; the local file source has none, so
                    // moving it last is how you stop it being used.
                    QQC2.ToolButton {
                        Layout.alignment: Qt.AlignVCenter
                        icon.name: "go-up"
                        display: QQC2.AbstractButton.IconOnly
                        text: i18n("Move up")
                        enabled: false
                        onClicked: root.moveSource(0, -1)

                        QQC2.ToolTip.text: text
                        QQC2.ToolTip.visible: hovered
                    }

                    QQC2.ToolButton {
                        Layout.alignment: Qt.AlignVCenter
                        icon.name: "go-down"
                        display: QQC2.AbstractButton.IconOnly
                        text: i18n("Move down")
                        enabled: root.sourceAt(1).length > 0
                        onClicked: root.moveSource(0, 1)

                        QQC2.ToolTip.text: text
                        QQC2.ToolTip.visible: hovered
                    }
                }

                // Row 1: the source tried second.
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Kirigami.Units.smallSpacing

                    Loader {
                        Layout.alignment: Qt.AlignVCenter
                        active: root.sourceAt(1) !== "local"
                        sourceComponent: Component {
                            QQC2.Switch {
                                checked: root.sourceEnabled(root.sourceAt(1))
                                onToggled: root.setSourceEnabled(root.sourceAt(1), checked)
                            }
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 0

                        QQC2.Label {
                            Layout.fillWidth: true
                            text: root.sourceLabel(root.sourceAt(1))
                        }

                        QQC2.Label {
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            font: Kirigami.Theme.smallFont
                            opacity: 0.7
                            text: root.sourceHint(root.sourceAt(1))
                        }
                    }

                    QQC2.ToolButton {
                        Layout.alignment: Qt.AlignVCenter
                        icon.name: "go-up"
                        display: QQC2.AbstractButton.IconOnly
                        text: i18n("Move up")
                        enabled: root.sourceAt(1).length > 0
                        onClicked: root.moveSource(1, -1)

                        QQC2.ToolTip.text: text
                        QQC2.ToolTip.visible: hovered
                    }

                    QQC2.ToolButton {
                        Layout.alignment: Qt.AlignVCenter
                        icon.name: "go-down"
                        display: QQC2.AbstractButton.IconOnly
                        text: i18n("Move down")
                        enabled: root.sourceAt(2).length > 0
                        onClicked: root.moveSource(1, 1)

                        QQC2.ToolTip.text: text
                        QQC2.ToolTip.visible: hovered
                    }
                }

                // Row 2: the source tried last.
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Kirigami.Units.smallSpacing

                    Loader {
                        Layout.alignment: Qt.AlignVCenter
                        active: root.sourceAt(2) !== "local"
                        sourceComponent: Component {
                            QQC2.Switch {
                                checked: root.sourceEnabled(root.sourceAt(2))
                                onToggled: root.setSourceEnabled(root.sourceAt(2), checked)
                            }
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 0

                        QQC2.Label {
                            Layout.fillWidth: true
                            text: root.sourceLabel(root.sourceAt(2))
                        }

                        QQC2.Label {
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            font: Kirigami.Theme.smallFont
                            opacity: 0.7
                            text: root.sourceHint(root.sourceAt(2))
                        }
                    }

                    QQC2.ToolButton {
                        Layout.alignment: Qt.AlignVCenter
                        icon.name: "go-up"
                        display: QQC2.AbstractButton.IconOnly
                        text: i18n("Move up")
                        enabled: root.sourceAt(2).length > 0
                        onClicked: root.moveSource(2, -1)

                        QQC2.ToolTip.text: text
                        QQC2.ToolTip.visible: hovered
                    }

                    QQC2.ToolButton {
                        Layout.alignment: Qt.AlignVCenter
                        icon.name: "go-down"
                        display: QQC2.AbstractButton.IconOnly
                        text: i18n("Move down")
                        enabled: false
                        onClicked: root.moveSource(2, 1)

                        QQC2.ToolTip.text: text
                        QQC2.ToolTip.visible: hovered
                    }
                }

                QQC2.Label {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    font: Kirigami.Theme.smallFont
                    opacity: 0.7
                    text: i18n("Sources are tried from top to bottom; the first one with lyrics for the song is used.")
                }
            }
        }
    }

    ColorDialog {
        id: colorDialog
        title: i18n("Choose the lyric text color")
        selectedColor: settings.textColor
        onAccepted: {
            settings.textColor = root.colorToHex(selectedColor)
            colorField.text = settings.textColor
        }
    }
}
