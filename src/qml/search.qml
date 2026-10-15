// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 KShadowLrc Contributors

// The results delegate calls back into the root's helpers, so the component must
// be bound to the context it is declared in.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/*!
 * Search window for KShadowLrc.
 *
 * The tray's "Search lyrics…" entry opens this when the automatic match picked
 * the wrong lyric or found none: you search for the song yourself, look at what
 * each source offered, and pin the one you want to this track. From then on that
 * lyric is what the overlay uses for that song, offline included, until you
 * forget it again.
 *
 * The host hands it two objects as context properties, the way it hands the
 * settings window its settings object: `lyricsController` (what is playing, and
 * the pin and forget invokables) and `lyricSearch` (the search itself). A bare
 * engine - which is what the QML test loads - has neither, and `typeof` does not
 * throw for an identifier that was never declared, so both are reached only
 * through the guarded properties below and the window stays quiet without them.
 *
 * The one piece of local state is `searched`. An empty result list means "you
 * have not asked yet" until it does not, and the window would otherwise greet
 * you with "Nothing found." before it had looked.
 *
 * i18n() is callable here because the host installs a translator as the engine's
 * root context object; importing org.kde.kirigami on its own does not define it.
 */
Kirigami.ApplicationWindow {
    id: root

    title: i18n("Search lyrics")
    width: 620
    height: 620
    minimumWidth: 460
    minimumHeight: 360
    // The host never calls show(); the window opens itself.
    visible: true

    // ---- The host's objects -------------------------------------------------

    readonly property bool hasLyricController: typeof lyricsController !== "undefined"
    readonly property bool hasLyricSearch: typeof lyricSearch !== "undefined"

    // "title artist" of what is playing, and empty when nothing is. It decides
    // both what the top of the window says and whether there is a track to pin a
    // result to at all.
    readonly property string trackQuery: root.hasLyricController ? lyricsController.trackQuery : ""
    readonly property bool nothingPlaying: root.trackQuery.length === 0

    // Non-empty once the playing track carries a pinned lyric; it names where
    // that lyric came from, e.g. "NetEase — 光の中へ / ヨルシカ".
    readonly property string pinnedDescription: root.hasLyricController ? lyricsController.pinnedDescription : ""
    readonly property bool hasPinnedLyric: root.pinnedDescription.length > 0

    readonly property var results: root.hasLyricSearch ? lyricSearch.results : []
    readonly property bool busy: root.hasLyricSearch ? lyricSearch.busy : false
    readonly property string searchError: root.hasLyricSearch ? lyricSearch.error : ""

    // What the last pin or forget attempt came to, in the controller's own
    // words: a click that cannot be honoured says so rather than doing nothing.
    // Success is not announced here - the pin appearing at the top is the
    // success signal.
    readonly property string pinStatus: root.hasLyricController ? lyricsController.pinStatus : ""

    // Set the first time a search is asked for in this window.
    property bool searched: false

    // The rows the list draws. One function, so the mapping of a provider result
    // to the fields the delegate shows can be pinned by the test without
    // reaching into delegate objects.
    readonly property var rows: root.buildRows()

    // The top line: what is playing, or plainly that nothing is.
    readonly property string trackLine: root.nothingPlaying ? i18n("Nothing is playing.") : root.trackQuery

    // The results area says one thing at a time. Busy may sit above rows that
    // have already arrived and the diagnostic may sit under them, so neither
    // hides the list itself.
    readonly property bool showBusy: root.busy
    readonly property bool showPinStatus: root.pinStatus.length > 0
    readonly property bool showError: !root.busy && root.searchError.length > 0
    readonly property bool showNothingFound: !root.busy && !root.showError && root.rows.length === 0 && root.searched
    readonly property bool showStartHint: !root.busy && !root.showError && root.rows.length === 0 && !root.searched

    // ---- Actions ------------------------------------------------------------

    // Enter in the field and the Search button both land here. An empty query
    // searches for nothing, so it is not sent.
    function runSearch() {
        if (!root.hasLyricSearch) {
            return
        }
        const query = queryField.text.trim()
        if (query.length === 0) {
            return
        }
        root.searched = true
        lyricSearch.search(query)
    }

    // Pin the result at `index` of the provider list to the track that is
    // playing; the controller fetches its lyric and applies it.
    function pinResult(index) {
        if (!root.hasLyricController) {
            return
        }
        lyricsController.pinSearchResult(index)
    }

    // Drop the pin, and the automatic resolution takes over again.
    function forgetPinnedLyric() {
        if (!root.hasLyricController) {
            return
        }
        lyricsController.forgetPinnedLyric()
    }

    // ---- The result rows ----------------------------------------------------

    // A field of a provider result, as text. A key the provider left out arrives
    // as undefined, which must not become the word "undefined".
    function text(value) {
        return value === undefined || value === null ? "" : String(value)
    }

    function milliseconds(value) {
        return value === undefined || value === null ? 0 : Number(value)
    }

    // "3:45", or empty when the provider gave no length.
    function lengthText(lengthMs) {
        const seconds = Math.floor(root.milliseconds(lengthMs) / 1000)
        if (seconds <= 0) {
            return ""
        }
        const minutes = Math.floor(seconds / 60)
        const rest = seconds % 60
        return minutes + ":" + (rest < 10 ? "0" + rest : rest)
    }

    // Why a row cannot be pinned, or empty when it can. A row that cannot be
    // pinned offers that sentence in place of its button, so it never has a
    // control that would quietly do nothing.
    function pinHint(id) {
        if (root.nothingPlaying) {
            // No track, so no pin: the row says the same thing the top of the
            // window does, from the one place this file words it.
            return root.trackLine
        }
        if (id.length === 0) {
            return i18n("This result has no song id, so its lyric cannot be fetched.")
        }
        return ""
    }

    function buildRows() {
        const results = root.results
        const rows = []
        let previousProvider = ""
        for (let i = 0; i < results.length; ++i) {
            const result = results[i]
            const provider = root.text(result.provider)
            const id = root.text(result.id)
            const singer = root.text(result.singer)
            const album = root.text(result.album)
            const providerLabel = root.text(result.providerLabel)
            const hint = root.pinHint(id)
            rows.push({
                index: i,
                provider: provider,
                providerLabel: providerLabel.length > 0 ? providerLabel : provider,
                id: id,
                title: root.text(result.title),
                singer: singer,
                album: album,
                lengthText: root.lengthText(result.lengthMs),
                details: [singer, album].filter(part => part.length > 0).join(" · "),
                // The sources arrive one after another, so a change of provider
                // is where one group ends and the next begins.
                groupStart: i > 0 && provider !== previousProvider,
                pinnable: hint.length === 0,
                pinHint: hint
            })
            previousProvider = provider
        }
        return rows
    }

    // ---- The window ---------------------------------------------------------

    pageStack.initialPage: Kirigami.ScrollablePage {
        id: searchPage

        title: i18n("Search lyrics")

        ColumnLayout {
            spacing: Kirigami.Units.largeSpacing

            // What is playing, and the pin on it when there is one. The track is
            // read through the controller, and this window never writes it: the
            // song stays whatever the player says it is.
            ColumnLayout {
                Layout.fillWidth: true
                spacing: Math.round(Kirigami.Units.smallSpacing / 2)

                QQC2.Label {
                    objectName: "trackLabel"
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    text: root.trackLine
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Kirigami.Units.smallSpacing
                    visible: root.hasPinnedLyric

                    QQC2.Label {
                        objectName: "pinnedLabel"
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        text: i18n("Pinned for this song: %1", root.pinnedDescription)
                    }

                    QQC2.Button {
                        Layout.alignment: Qt.AlignVCenter
                        text: i18n("Forget pinned lyric")
                        onClicked: root.forgetPinnedLyric()
                    }
                }
            }

            // ---- The search -------------------------------------------------

            RowLayout {
                Layout.fillWidth: true
                spacing: Kirigami.Units.smallSpacing

                QQC2.TextField {
                    id: queryField

                    objectName: "queryField"
                    Layout.fillWidth: true
                    // Prefilled with what is playing, so the common case is one
                    // keypress away. Typing in the field takes the binding over,
                    // so a track change never overwrites a query being written.
                    text: root.trackQuery
                    placeholderText: i18n("Song title and artist")
                    onAccepted: root.runSearch()
                }

                QQC2.Button {
                    objectName: "searchButton"
                    Layout.alignment: Qt.AlignVCenter
                    text: i18n("Search")
                    enabled: queryField.text.trim().length > 0
                    onClicked: root.runSearch()
                }
            }

            // ---- What the search is doing, then the results ------------------

            QQC2.Label {
                objectName: "busyLabel"
                Layout.fillWidth: true
                visible: root.showBusy
                text: i18n("Searching…")
                font: Kirigami.Theme.smallFont
                opacity: 0.7
            }

            // A pin or forget that could not be honoured says so here. Like the
            // search failure below it is diagnostic, so it is small and dimmed
            // and never reads as the headline; both strings are worded by the
            // C++ side, so this window keeps no copy of them.
            QQC2.Label {
                objectName: "pinStatusLabel"
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                visible: root.showPinStatus
                text: root.pinStatus
                font: Kirigami.Theme.smallFont
                opacity: 0.7
            }

            // The failure string is diagnostic, not the answer: small and
            // dimmed, so it never reads as the headline, exactly as the settings
            // window treats the same string.
            QQC2.Label {
                objectName: "errorLabel"
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                visible: root.showError
                text: root.searchError
                font: Kirigami.Theme.smallFont
                opacity: 0.7
            }

            QQC2.Label {
                objectName: "nothingFoundLabel"
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                visible: root.showNothingFound
                text: i18n("Nothing found.")
            }

            QQC2.Label {
                objectName: "startHintLabel"
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                visible: root.showStartHint
                text: i18n("Search the song, then pick the lyric to use.")
                font: Kirigami.Theme.smallFont
                opacity: 0.7
            }

            Repeater {
                model: root.rows

                delegate: ColumnLayout {
                    id: resultRow

                    required property var modelData

                    Layout.fillWidth: true
                    spacing: 0

                    // A rule marks where one source's results end and the next
                    // begins; every row carries its own source label as well.
                    Kirigami.Separator {
                        Layout.fillWidth: true
                        Layout.topMargin: Kirigami.Units.smallSpacing
                        visible: resultRow.modelData.groupStart
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        Layout.topMargin: Kirigami.Units.smallSpacing
                        spacing: Kirigami.Units.smallSpacing

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 0

                            QQC2.Label {
                                Layout.fillWidth: true
                                elide: Text.ElideRight
                                text: resultRow.modelData.title
                            }

                            QQC2.Label {
                                Layout.fillWidth: true
                                elide: Text.ElideRight
                                visible: text.length > 0
                                text: resultRow.modelData.details
                                font: Kirigami.Theme.smallFont
                                opacity: 0.7
                            }

                            RowLayout {
                                spacing: Kirigami.Units.smallSpacing

                                QQC2.Label {
                                    text: resultRow.modelData.providerLabel
                                    font: Kirigami.Theme.smallFont
                                    opacity: 0.7
                                }

                                QQC2.Label {
                                    visible: text.length > 0
                                    text: resultRow.modelData.lengthText
                                    font: Kirigami.Theme.smallFont
                                    opacity: 0.7
                                }
                            }
                        }

                        // The way to pin this row, or the reason there is none.
                        QQC2.Button {
                            Layout.alignment: Qt.AlignVCenter
                            visible: resultRow.modelData.pinnable
                            text: i18n("Use this")
                            onClicked: root.pinResult(resultRow.modelData.index)
                        }

                        QQC2.Label {
                            Layout.alignment: Qt.AlignVCenter
                            Layout.preferredWidth: Kirigami.Units.gridUnit * 12
                            wrapMode: Text.WordWrap
                            visible: !resultRow.modelData.pinnable
                            text: resultRow.modelData.pinHint
                            font: Kirigami.Theme.smallFont
                            opacity: 0.7
                        }
                    }
                }
            }
        }
    }
}
