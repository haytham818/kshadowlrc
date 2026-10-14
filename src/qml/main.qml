// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 KShadowLrc Contributors

import QtQuick
import org.kde.kirigami as Kirigami

/*!
 * Root item of the floating lyrics overlay.
 *
 * The layer-shell surface is anchored to one screen edge and stretched along it
 * (see OverlayWindow::applyAnchors). This item fills that surface; the lyric
 * column is centred inside it and reports its laid-out extent back through
 * OverlayWindow::setContentSize() so the free axis sizes itself to the lyrics.
 * With no line to show it reports an empty content size, so the overlay does not
 * claim screen space while nothing is playing. There is no background plate -
 * only the outlined text is drawn.
 *
 * All styling comes live from the `settings` context property; the number of
 * upcoming lines is already clamped by the controller's visibleLines handling.
 */
Item {
    id: root

    // Breathing room around the text, on top of the surface's edge margin.
    readonly property int horizontalPadding: 24
    readonly property int verticalPadding: 10

    // Live style, driven by the settings object. Sizes are derived from the one
    // configured lyric size so a single setting scales the whole column.
    readonly property color baseColor: settings.textColor
    readonly property string fontFamily: settings.fontFamily.length > 0 ? settings.fontFamily : Kirigami.Theme.defaultFont.family
    readonly property int primaryPointSize: settings.fontSize
    readonly property int translationPointSize: Math.max(1, Math.round(settings.fontSize * 0.7))
    readonly property int upcomingPointSize: Math.max(1, Math.round(settings.fontSize * 0.6))
    readonly property int textStyle: settings.outlineEnabled ? Text.Outline : Text.Normal
    readonly property color outlineColor: Qt.rgba(0.0, 0.0, 0.0, 0.9)

    function contentIsEmpty() {
        return lyricsController.currentText.length === 0
                && lyricsController.translationText.length === 0
                && lyricsController.upcomingLines.length === 0
    }

    function reportContentSize() {
        if (contentIsEmpty()) {
            // Nothing to show: report an empty extent so the free axis collapses
            // instead of leaving an invisible strip on screen.
            overlayWindow.setContentSize(0, 0)
            return
        }
        overlayWindow.setContentSize(lines.implicitWidth + horizontalPadding * 2,
                                     lines.implicitHeight + verticalPadding * 2)
    }

    Column {
        id: lines
        anchors.centerIn: parent
        spacing: Math.round(Kirigami.Units.smallSpacing * 0.8)

        // Re-report whenever the lyric text changes the column's extent.
        onImplicitWidthChanged: root.reportContentSize()
        onImplicitHeightChanged: root.reportContentSize()

        // Current line: large and bright, with a dark outline so it stays
        // readable over any window content behind it.
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: lyricsController.currentText
            visible: text.length > 0
            color: root.baseColor
            renderType: Text.NativeRendering
            style: root.textStyle
            styleColor: root.outlineColor
            font.family: root.fontFamily
            font.pointSize: root.primaryPointSize
            font.weight: Font.DemiBold
        }

        // Translation of the current line: smaller and slightly muted, only in
        // the "both" display mode (the engine blanks it otherwise).
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            visible: text.length > 0
            text: lyricsController.translationText
            color: Qt.rgba(root.baseColor.r, root.baseColor.g, root.baseColor.b, 0.85)
            renderType: Text.NativeRendering
            style: root.textStyle
            styleColor: root.outlineColor
            font.family: root.fontFamily
            font.pointSize: root.translationPointSize
        }

        // The upcoming lines: dimmed, to show the LRC-style stacking. The
        // controller already limits the list to the configured visibleLines.
        Repeater {
            model: lyricsController.upcomingLines

            Text {
                required property string modelData
                anchors.horizontalCenter: parent.horizontalCenter
                text: modelData
                visible: text.length > 0
                color: Qt.rgba(root.baseColor.r, root.baseColor.g, root.baseColor.b, 0.45)
                renderType: Text.NativeRendering
                style: root.textStyle
                styleColor: root.outlineColor
                font.family: root.fontFamily
                font.pointSize: root.upcomingPointSize
            }
        }
    }

    // Re-report whenever the QML-facing text changes, including the gate flipping
    // to empty when nothing is playing.
    Connections {
        target: lyricsController
        function onCurrentTextChanged() {
            root.reportContentSize()
        }
        function onTranslationTextChanged() {
            root.reportContentSize()
        }
        function onUpcomingLinesChanged() {
            root.reportContentSize()
        }
    }

    Component.onCompleted: reportContentSize()
}
