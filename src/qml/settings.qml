// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 KShadowLrc Contributors

// The ignored-players repeater's delegate calls back into the root's helpers,
// so the component must be bound to the context it is declared in.
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
 * The keys are grouped exactly as in src/config/kshadowlrcsettings.kcfg:
 * General, Playback and Providers.
 *
 * The two enum combos bind their currentIndex straight to the int the KConfigXT
 * compiler generates, because kconfig_compiler emits old-style nested enums with
 * no Q_ENUM and does not register the class as a QML type - QML cannot name
 * KShadowLrcSettings.EnumEdge.Top. Their models are ordered to match the kcfg
 * <choices> blocks and must be kept in step with them.
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

    // The ignored-players list is a StringList property: read it, edit the copy,
    // write the whole list back so the object persists it.
    function addIgnoredPlayer(name) {
        const trimmed = name.trim()
        if (trimmed.length === 0) {
            return
        }
        const players = settings.ignoredPlayers.slice()
        if (players.indexOf(trimmed) !== -1) {
            return
        }
        players.push(trimmed)
        settings.ignoredPlayers = players
    }

    function removeIgnoredPlayer(playerIndex) {
        const players = settings.ignoredPlayers.slice()
        if (playerIndex < 0 || playerIndex >= players.length) {
            return
        }
        players.splice(playerIndex, 1)
        settings.ignoredPlayers = players
    }

    function submitNewPlayer() {
        root.addIgnoredPlayer(newPlayerField.text)
        newPlayerField.clear()
    }

    pageStack.initialPage: Kirigami.ScrollablePage {
        id: settingsPage

        title: i18n("Settings")

        Kirigami.FormLayout {
            id: form

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

            QQC2.ComboBox {
                id: edgeField
                Kirigami.FormData.label: i18n("Screen edge:")
                // The model order must match the <choices> order in
                // src/config/kshadowlrcsettings.kcfg exactly: 0 = Top, 1 = Bottom.
                model: [i18n("Top"), i18n("Bottom")]
                currentIndex: settings.edge
                onActivated: settings.edge = currentIndex
            }

            QQC2.SpinBox {
                id: marginField
                Kirigami.FormData.label: i18n("Edge margin:")
                from: 0
                to: 5000
                value: settings.margin
                textFromValue: (value) => i18n("%1 px", value)
                onValueModified: settings.margin = value
            }

            QQC2.Switch {
                id: clickThroughField
                Kirigami.FormData.label: i18n("Click-through:")
                checked: settings.clickThrough
                onToggled: settings.clickThrough = checked
            }

            QQC2.TextField {
                id: outputNameField
                Kirigami.FormData.label: i18n("Output:")
                placeholderText: i18n("Primary output")
                text: settings.outputName
                onEditingFinished: settings.outputName = text
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
                Layout.preferredWidth: Kirigami.Units.gridUnit * 20
                spacing: Kirigami.Units.smallSpacing

                QQC2.Label {
                    Layout.fillWidth: true
                    visible: settings.ignoredPlayers.length === 0
                    font: Kirigami.Theme.smallFont
                    opacity: 0.7
                    text: i18n("No players are ignored.")
                }

                Repeater {
                    model: settings.ignoredPlayers

                    delegate: RowLayout {
                        id: ignoredRow
                        required property string modelData
                        required property int index
                        Layout.fillWidth: true
                        spacing: Kirigami.Units.smallSpacing

                        QQC2.Label {
                            Layout.fillWidth: true
                            text: ignoredRow.modelData
                            elide: Text.ElideMiddle
                        }

                        QQC2.Button {
                            text: i18n("Remove")
                            onClicked: root.removeIgnoredPlayer(ignoredRow.index)
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Kirigami.Units.smallSpacing

                    QQC2.TextField {
                        id: newPlayerField
                        Layout.fillWidth: true
                        placeholderText: i18n("org.mpris.MediaPlayer2.example")
                        onAccepted: root.submitNewPlayer()
                    }

                    QQC2.Button {
                        text: i18n("Add")
                        enabled: newPlayerField.text.trim().length > 0
                        onClicked: root.submitNewPlayer()
                    }
                }
            }

            // ---- Providers --------------------------------------------------

            Kirigami.Separator {
                Kirigami.FormData.isSection: true
                Kirigami.FormData.label: i18n("Providers")
            }

            QQC2.Switch {
                id: neteaseField
                Kirigami.FormData.label: i18n("NetEase:")
                checked: settings.neteaseEnabled
                onToggled: settings.neteaseEnabled = checked
            }

            QQC2.Switch {
                id: lrclibField
                Kirigami.FormData.label: i18n("LRCLIB:")
                checked: settings.lrclibEnabled
                onToggled: settings.lrclibEnabled = checked
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
