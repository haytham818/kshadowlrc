// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 KShadowLrc Contributors

// The corner handle reaches the `overlayWindow` the file is loaded with from
// inside a nested component, so that component must be bound to the context it
// is declared in.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Shapes
import org.kde.kirigami as Kirigami

/*!
 * Root item of the floating lyrics overlay.
 *
 * The layer-shell surface is placed by the anchors and margins a Placement maps
 * to (see OverlayWindow::applyAnchors): a point - the text box's horizontal
 * centre and its top edge, as percentages of the screen - with the box sizing
 * itself to the lyrics on both axes and those margins re-derived from the point
 * every time the box changes size. The point is what holds still: as the lyrics
 * grow or shrink, the centre and the top edge stay where they are, so the line
 * being read never moves. This item fills that surface; the lyric column is
 * centred inside it and reports its laid-out extent back through
 * OverlayWindow::setContentSize(). With no line to show it reports an empty
 * content size, so the overlay does not claim screen space while nothing is
 * playing. There is no background plate - only the outlined text is drawn.
 *
 * All styling comes live from the `settings` context property; the number of
 * upcoming lines is already clamped by the controller's visibleLines handling.
 *
 * ---- Adjusting mode -------------------------------------------------------
 *
 * The tray can turn an adjusting mode on (`overlayWindow.adjusting`). While it
 * is on the window takes pointer input again, and this file draws a dashed frame
 * around the lyrics with a handle at each corner and one short hint: drag the
 * body to move the overlay, drag a corner to scale the lyric text, Esc to
 * finish.
 *
 * Every length that matters - where the frame sits, where the overlay lands, how
 * large the text becomes - belongs to the window and is worked out in C++ from
 * the global pointer position: the surface itself moves during a drag, so
 * surface-local coordinates would mean nothing here. The handlers below only say
 * that a drag started, moved or ended, and whether the grab was the body or a
 * handle. The size to draw while a corner is dragged arrives as
 * `overlayWindow.adjustingFontSize` (0 when nothing is being scaled), and the
 * frame is inset `overlayWindow.adjustPadding` from the surface, which is where
 * the window keeps the room around the lyrics.
 *
 * The overlay has no plate behind it, so nothing drawn here may assume a dark
 * wallpaper: the frame is a light dashed line over a soft dark one, and the
 * handles and the hint carry their own translucent backing. With adjusting off
 * the whole block is invisible and takes no pointer input, so the surface stays
 * exactly as click-through as it has always been.
 */
Item {
    id: root

    // Breathing room around the text, on top of the surface's edge margin. The
    // adjusting frame's padding is the window's own (overlayWindow.adjustPadding).
    readonly property int horizontalPadding: 24
    readonly property int verticalPadding: 10

    // Live style, driven by the settings object. Sizes are derived from the one
    // configured lyric size so a single setting scales the whole column, and the
    // translation and upcoming lines keep their ratios to it. While a corner
    // handle is being dragged, the window's live preview size stands in for the
    // configured one, which is how the text follows the pointer during a scale.
    readonly property color baseColor: settings.textColor
    readonly property string fontFamily: settings.fontFamily.length > 0 ? settings.fontFamily : Kirigami.Theme.defaultFont.family
    readonly property int primaryPointSize: overlayWindow.adjustingFontSize > 0 ? overlayWindow.adjustingFontSize : settings.fontSize
    readonly property int translationPointSize: Math.max(1, Math.round(primaryPointSize * 0.7))
    readonly property int upcomingPointSize: Math.max(1, Math.round(primaryPointSize * 0.6))
    readonly property int textStyle: settings.outlineEnabled ? Text.Outline : Text.Normal
    readonly property color outlineColor: Qt.rgba(0.0, 0.0, 0.0, 0.9)

    // The overlay answers the keyboard only while it is being adjusted: that is
    // when the window takes focus, and Esc is the way back out.
    focus: overlayWindow.adjusting

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

    // One place ends the mode, so the Esc handler and the test drive the same
    // call: what the overlay was moved and scaled to is kept.
    function finishAdjusting() {
        overlayWindow.endAdjusting(false)
    }

    Keys.onEscapePressed: (event) => {
        root.finishAdjusting()
        event.accepted = true
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

    // ---- Adjusting mode -----------------------------------------------------

    // Everything the mode draws, and every grab it takes, lives in this one item.
    // It is declared after the lyrics so it sits on top of them, and it is
    // visible only while the mode is on: with adjusting false it neither paints
    // nor answers the pointer, and the surface stays as click-through as it was.
    Item {
        id: adjustOverlay

        objectName: "adjustOverlay"
        anchors.fill: parent
        visible: overlayWindow.adjusting

        // The frame is one padding inside the surface; that is the room the
        // window keeps around the lyrics, so the frame is drawn around them.
        readonly property int frameInset: overlayWindow.adjustPadding
        readonly property int frameWidth: Math.max(1, width - frameInset * 2)
        readonly property int frameHeight: Math.max(1, height - frameInset * 2)
        // The hint shares the bottom of the surface with the two bottom handles
        // and keeps clear of them: half a grab area plus a little air.
        readonly property int handleClearance: 24

        // Dragging the body moves the overlay to follow the pointer. target is
        // null on purpose: the window moves the surface itself, so the handler
        // reports the gesture and nothing else.
        DragHandler {
            objectName: "adjustBodyDrag"
            target: null
            enabled: overlayWindow.adjusting

            onActiveChanged: {
                if (active) {
                    overlayWindow.beginDrag(false)
                } else {
                    overlayWindow.endDrag()
                }
            }

            onActiveTranslationChanged: {
                if (active) {
                    overlayWindow.updateDrag()
                }
            }
        }

        // The frame: a light dashed line drawn over a soft dark one, so the edge
        // reads over a bright wallpaper as well as a dark one - there is no plate
        // behind it to fall back on.
        Shape {
            objectName: "adjustFrame"
            anchors.fill: parent

            ShapePath {
                fillColor: "transparent"
                strokeColor: Qt.rgba(0.0, 0.0, 0.0, 0.5)
                strokeWidth: 3
                startX: adjustOverlay.frameInset
                startY: adjustOverlay.frameInset
                PathLine {
                    x: adjustOverlay.frameInset + adjustOverlay.frameWidth
                    y: adjustOverlay.frameInset
                }
                PathLine {
                    x: adjustOverlay.frameInset + adjustOverlay.frameWidth
                    y: adjustOverlay.frameInset + adjustOverlay.frameHeight
                }
                PathLine {
                    x: adjustOverlay.frameInset
                    y: adjustOverlay.frameInset + adjustOverlay.frameHeight
                }
                PathLine {
                    x: adjustOverlay.frameInset
                    y: adjustOverlay.frameInset
                }
            }

            ShapePath {
                fillColor: "transparent"
                strokeColor: Qt.rgba(1.0, 1.0, 1.0, 0.9)
                strokeWidth: 1.5
                strokeStyle: ShapePath.DashLine
                dashPattern: [6, 4]
                capStyle: ShapePath.FlatCap
                startX: adjustOverlay.frameInset
                startY: adjustOverlay.frameInset
                PathLine {
                    x: adjustOverlay.frameInset + adjustOverlay.frameWidth
                    y: adjustOverlay.frameInset
                }
                PathLine {
                    x: adjustOverlay.frameInset + adjustOverlay.frameWidth
                    y: adjustOverlay.frameInset + adjustOverlay.frameHeight
                }
                PathLine {
                    x: adjustOverlay.frameInset
                    y: adjustOverlay.frameInset + adjustOverlay.frameHeight
                }
                PathLine {
                    x: adjustOverlay.frameInset
                    y: adjustOverlay.frameInset
                }
            }
        }

        // The one short hint, in its own dark pill so it reads over anything.
        // It sits centred in the gutter under the frame. On the smallest surface
        // there is (nothing playing: a 240x80 box) the sentence is wider than the
        // frame, so it wraps and reaches up into the frame - which is empty then,
        // and the pill is drawn under the handles so it never hides one.
        Rectangle {
            id: hint

            objectName: "adjustHint"
            readonly property int hPad: 10
            readonly property int vPad: 3
            // The width the sentence may wrap into: inside the frame, and clear
            // of the corner handles.
            readonly property int wrapWidth: Math.max(1, adjustOverlay.frameWidth - adjustOverlay.handleClearance * 2 - hPad * 2)
            // One line's natural width, so a short hint in a wide box keeps a
            // snug pill and only wraps when it has to.
            readonly property int naturalWidth: Math.ceil(hintMetrics.advanceWidth)

            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            anchors.bottomMargin: Math.max(4, Math.round((adjustOverlay.frameInset - height) / 2))

            width: Math.min(naturalWidth, wrapWidth) + hPad * 2
            height: hintText.contentHeight + vPad * 2
            radius: height / 2
            color: Qt.rgba(0.0, 0.0, 0.0, 0.55)
            border.width: 1
            border.color: Qt.rgba(1.0, 1.0, 1.0, 0.3)

            TextMetrics {
                id: hintMetrics
                font: hintText.font
                text: hintText.text
            }

            Text {
                id: hintText

                objectName: "adjustHintText"
                anchors.horizontalCenter: parent.horizontalCenter
                y: hint.vPad
                width: hint.wrapWidth
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                text: i18n("drag to move · drag a corner to scale · Esc to finish")
                color: Qt.rgba(1.0, 1.0, 1.0, 0.92)
                font: Kirigami.Theme.smallFont
            }
        }

        // A corner handle. The grab area is larger than the dot so a corner is
        // easy to catch; a grab on one is what scales the text rather than moving
        // the overlay.
        component CornerHandle: Item {
            width: 28
            height: 28

            Rectangle {
                anchors.centerIn: parent
                width: 14
                height: 14
                radius: width / 2
                color: Qt.rgba(1.0, 1.0, 1.0, 0.92)
                border.width: 2
                border.color: Qt.rgba(0.0, 0.0, 0.0, 0.7)
            }

            DragHandler {
                target: null
                enabled: overlayWindow.adjusting

                onActiveChanged: {
                    if (active) {
                        overlayWindow.beginDrag(true)
                    } else {
                        overlayWindow.endDrag()
                    }
                }

                onActiveTranslationChanged: {
                    if (active) {
                        overlayWindow.updateDrag()
                    }
                }
            }
        }

        CornerHandle {
            objectName: "adjustHandleTopLeft"
            x: adjustOverlay.frameInset - width / 2
            y: adjustOverlay.frameInset - height / 2
        }

        CornerHandle {
            objectName: "adjustHandleTopRight"
            x: adjustOverlay.width - adjustOverlay.frameInset - width / 2
            y: adjustOverlay.frameInset - height / 2
        }

        CornerHandle {
            objectName: "adjustHandleBottomLeft"
            x: adjustOverlay.frameInset - width / 2
            y: adjustOverlay.height - adjustOverlay.frameInset - height / 2
        }

        CornerHandle {
            objectName: "adjustHandleBottomRight"
            x: adjustOverlay.width - adjustOverlay.frameInset - width / 2
            y: adjustOverlay.height - adjustOverlay.frameInset - height / 2
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
