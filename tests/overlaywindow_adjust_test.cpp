// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 KShadowLrc Contributors
//
// Offline checks for OverlayWindow's adjusting mode.
//
// The window is constructed with no compositor (the offscreen platform), so
// LayerShellQt::Window::get() returns null and m_layerWindow is null: the
// assertions are all against the window's own members and getters -- the input
// region, the requested keyboard interactivity and the placement -- never
// against anything a compositor would do. XDG_CONFIG_HOME points at a private
// temporary directory so the settings writes under test cannot reach the
// developer's real kshadowlrcrc.
//
// The offscreen cursor cannot move, so a drag through the public API is always
// motionless here: that is what the "a motionless click emits nothing" checks
// rely on, and the live anchors a real drag would apply are applied directly
// through setPlacement() where a committed edit is under test.

#include "config/settings.h"
#include "platform/overlaywindow.h"

#include <QGuiApplication>
#include <QTemporaryDir>
#include <QTest>

#include <cstdio>
#include <memory>

using namespace kshadowlrc;

namespace
{

using Settings = KShadowLrcSettings;

} // namespace

class OverlayWindowAdjustTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void enteringOverridesClickThroughAndKeyboard();
    void leavingRestoresTheConfiguredState();
    void adjustingKeepsTheTextBoxAndItsPlacement();
    void cancelledAdjustingWritesNoSettings();
    void motionlessClickEmitsNothing();
    void releasedDragWritesThePlacementOnce();
};

void OverlayWindowAdjustTest::enteringOverridesClickThroughAndKeyboard()
{
    // 1. A click-through overlay takes full pointer input while adjusting and
    //    asks for the keyboard on demand so Esc can arrive.
    OverlayWindow overlay;
    QVERIFY(overlay.clickThrough());
    QCOMPARE(overlay.inputRegion(), QRegion(0, 0, 1, 1));
    QCOMPARE(overlay.keyboardInteractivity(), LayerShellQt::Window::KeyboardInteractivityNone);
    QVERIFY(!overlay.isAdjusting());

    overlay.beginAdjusting();

    QVERIFY(overlay.isAdjusting());
    QCOMPARE(overlay.inputRegion(), QRegion()); // full input region
    QCOMPARE(overlay.keyboardInteractivity(), LayerShellQt::Window::KeyboardInteractivityOnDemand);
    QCOMPARE(overlay.adjustPadding(), 24);
}

void OverlayWindowAdjustTest::leavingRestoresTheConfiguredState()
{
    // 2. Leaving restores exactly the configured click-through and None keyboard,
    //    whether the exit committed or was cancelled.
    for (const bool commit : {true, false}) {
        OverlayWindow overlay;
        overlay.beginAdjusting();
        QCOMPARE(overlay.inputRegion(), QRegion());

        overlay.endAdjusting(commit);

        QVERIFY(!overlay.isAdjusting());
        QCOMPARE(overlay.inputRegion(), QRegion(0, 0, 1, 1));
        QCOMPARE(overlay.keyboardInteractivity(), LayerShellQt::Window::KeyboardInteractivityNone);
        QCOMPARE(overlay.adjustingFontSize(), 0);
    }

    // The configured click-through of false is restored too, not just the default.
    OverlayWindow nonClickThrough;
    nonClickThrough.setClickThrough(false);
    QCOMPARE(nonClickThrough.inputRegion(), QRegion());
    nonClickThrough.beginAdjusting();
    nonClickThrough.endAdjusting(true);
    QCOMPARE(nonClickThrough.inputRegion(), QRegion());
}

void OverlayWindowAdjustTest::adjustingKeepsTheTextBoxAndItsPlacement()
{
    // 3. The placement continuously describes the text box: adjusting pads the
    //    surface but never touches the stored margins, and an empty overlay gets
    //    the minimum box to grab.
    OverlayWindow overlay;
    overlay.setPlacement({VerticalAnchor::Bottom, 64, HorizontalAnchor::Center, 0});
    overlay.setContentSize(400, 100);
    QCOMPARE(overlay.adjustmentBox(), QSize(400, 100));

    overlay.beginAdjusting();
    QCOMPARE(overlay.placement(), (Placement{VerticalAnchor::Bottom, 64, HorizontalAnchor::Center, 0}));
    QCOMPARE(overlay.adjustmentBox(), QSize(400, 100));

    // Nothing playing: the content collapses, so the adjusting box holds its
    // minimum.
    overlay.setContentSize(0, 0);
    QCOMPARE(overlay.adjustmentBox(), QSize(240, 80));

    // Leaving drops back to the raw content size.
    overlay.endAdjusting(true);
    QCOMPARE(overlay.adjustmentBox(), QSize(1, 1));
}

void OverlayWindowAdjustTest::cancelledAdjustingWritesNoSettings()
{
    // 4. A drag the user cancels with Esc must not reach the settings file, even
    //    though it moved the overlay live while it was in flight.
    std::unique_ptr<Settings> settings(kshadowlrc::createSettings());
    const int originalFontSize = settings->fontSize();

    OverlayWindow overlay;
    QObject::connect(&overlay,
                     &OverlayWindow::placementEdited,
                     settings.get(),
                     [&settings](int verticalAnchor, int verticalMargin, int horizontalAnchor, int horizontalMargin) {
                         settings->setVerticalAnchor(verticalAnchor);
                         settings->setVerticalMargin(verticalMargin);
                         settings->setHorizontalAnchor(horizontalAnchor);
                         settings->setHorizontalMargin(horizontalMargin);
                     });
    QObject::connect(&overlay, &OverlayWindow::fontSizeEdited, settings.get(), [&settings](int points) {
        settings->setFontSize(points);
    });

    overlay.beginAdjusting();
    overlay.beginDrag(false);
    overlay.setPlacement({VerticalAnchor::Top, 200, HorizontalAnchor::Right, 40});
    overlay.endAdjusting(false);

    QCOMPARE(settings->verticalAnchor(), Settings::EnumVerticalAnchor::Bottom);
    QCOMPARE(settings->verticalMargin(), 64);
    QCOMPARE(settings->horizontalAnchor(), Settings::EnumHorizontalAnchor::Center);
    QCOMPARE(settings->horizontalMargin(), 0);
    QCOMPARE(settings->fontSize(), originalFontSize);
}

void OverlayWindowAdjustTest::motionlessClickEmitsNothing()
{
    // 5. Pressing and releasing without moving is not an edit: it must report
    //    nothing, so no identical value is written and no "edited" log line is
    //    produced for a click the user did not turn into a drag.
    OverlayWindow overlay;
    int placementEdits = 0;
    int fontEdits = 0;
    QObject::connect(&overlay, &OverlayWindow::placementEdited, &overlay, [&placementEdits] {
        ++placementEdits;
    });
    QObject::connect(&overlay, &OverlayWindow::fontSizeEdited, &overlay, [&fontEdits] {
        ++fontEdits;
    });

    overlay.setFontSize(40);
    overlay.beginAdjusting();

    // Position gesture: down and up with the cursor where it was.
    overlay.setContentSize(400, 100);
    overlay.beginDrag(false);
    overlay.updateDrag();
    overlay.endDrag();
    QCOMPARE(placementEdits, 0);

    // Font gesture: same, and the live value still shows while the pointer is
    // down even though nothing is written.
    overlay.beginDrag(true);
    overlay.updateDrag();
    QCOMPARE(overlay.adjustingFontSize(), 40);
    QCOMPARE(fontEdits, 0);
    overlay.endDrag();
    QCOMPARE(fontEdits, 0);

    // A committed exit still clears the temporary size.
    overlay.endAdjusting(true);
    QCOMPARE(overlay.adjustingFontSize(), 0);
}

void OverlayWindowAdjustTest::releasedDragWritesThePlacementOnce()
{
    // 6. The release is the one moment a changed placement is reported, and the
    //    values are the text box's margins -- the adjusting padding does not leak
    //    in. The offscreen cursor cannot move, so the live anchors a real drag
    //    would have applied are applied directly, exactly as updateDrag() does.
    std::unique_ptr<Settings> settings(kshadowlrc::createSettings());

    OverlayWindow overlay;
    int edits = 0;
    QObject::connect(&overlay,
                     &OverlayWindow::placementEdited,
                     settings.get(),
                     [&settings, &edits](int verticalAnchor, int verticalMargin, int horizontalAnchor, int horizontalMargin) {
                         ++edits;
                         settings->setVerticalAnchor(verticalAnchor);
                         settings->setVerticalMargin(verticalMargin);
                         settings->setHorizontalAnchor(horizontalAnchor);
                         settings->setHorizontalMargin(horizontalMargin);
                     });

    overlay.beginAdjusting();
    overlay.beginDrag(false);
    overlay.setPlacement({VerticalAnchor::Top, 200, HorizontalAnchor::Right, 40});
    overlay.endDrag();

    QCOMPARE(edits, 1);
    QCOMPARE(settings->verticalAnchor(), Settings::EnumVerticalAnchor::Top);
    QCOMPARE(settings->verticalMargin(), 200);
    QCOMPARE(settings->horizontalAnchor(), Settings::EnumHorizontalAnchor::Right);
    QCOMPARE(settings->horizontalMargin(), 40);
}

int main(int argc, char **argv)
{
    QTemporaryDir tempDir;
    if (!tempDir.isValid()) {
        std::fprintf(stderr, "FAIL: could not create a temporary config directory\n");
        return 1;
    }
    // Must happen before anything can open a config file.
    qputenv("XDG_CONFIG_HOME", tempDir.path().toUtf8());
    qputenv("XDG_CONFIG_DIRS", tempDir.path().toUtf8());
    qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("offscreen"));

    QGuiApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("overlaywindow-adjust-test"));

    OverlayWindowAdjustTest tc;
    return QTest::qExec(&tc, argc, argv);
}

#include "overlaywindow_adjust_test.moc"
