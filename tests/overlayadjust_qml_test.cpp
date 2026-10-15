// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 KShadowLrc Contributors
//
// Offline test for the overlay's adjusting mode (src/qml/main.qml).
//
// It loads the file on a bare QQmlEngine with three stand-ins - a `settings`, a
// `lyricsController` and an `overlayWindow` - so the mode can be driven without
// the sibling lane's C++ and without a screen: nothing here clicks, drags or
// looks at anything, and the only input it sends is a wheel event into an
// offscreen window of its own. main.qml is an Item, not a Window, because the
// host puts it into its own QQuickView, so it is built with a QQmlComponent
// rather than with QQmlApplicationEngine; the item is then parented into a bare
// QQuickWindow, because a WheelHandler is only ever reached through a scene.
//
// What is pinned is what a regression would break: that the frame and the four
// corners are part of the item and are drawn only while the mode is on; that no
// grab is armed while it is off, so the surface stays as click-through as it
// was; that a wheel event over the lyric block arrives at the window's
// nudgeFontSize() carrying the raw angle the wheel reported (the step, the
// limits and the leftover between notches are the window's, not QML's); that
// each handle sits on a corner one adjusting padding inside the surface; that
// the drawn lyric line takes the live preview size from overlayWindow while a
// corner is dragged and the configured size otherwise, with the translation and
// upcoming sizes keeping their ratios; and that Esc's path reaches the window's
// endAdjusting(false). The geometry is read from the items themselves, never
// from a render, and the `frameInset` property the container exposes is what the
// test compares the padding against.
//
// The platform plugin is offscreen: no network, no screen, and the one wheel
// event goes to this test's own window, inside this process.

#include <QFont>
#include <QGuiApplication>
#include <QList>
#include <QPair>
#include <QPointF>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQmlError>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSizeF>
#include <QStringList>
#include <QTest>
#include <QUrl>

#include <KLocalizedQmlContext>
#include <KLocalizedString>

#include <cstdio>

// The QML lives in a resource compiled into the static kshadowlrc_lib; without
// an explicit initialiser the linker drops it and qrc:/qml/main.qml cannot be
// resolved. Q_INIT_RESOURCE must run in the global namespace, so this helper
// sits outside any namespace, exactly as src/app/application.cpp does it.
static void initResources()
{
    Q_INIT_RESOURCE(qml);
}

namespace
{

/*!
 * Stand-in for kshadowlrc::KShadowLrcSettings, the object the host injects as
 * the `settings` context property. Only what the overlay reads: the colour, the
 * font and the outline switch.
 */
class StubSettings : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString textColor READ textColor NOTIFY textColorChanged)
    Q_PROPERTY(QString fontFamily READ fontFamily NOTIFY fontFamilyChanged)
    Q_PROPERTY(int fontSize READ fontSize NOTIFY fontSizeChanged)
    Q_PROPERTY(bool outlineEnabled READ outlineEnabled NOTIFY outlineEnabledChanged)

public:
    using QObject::QObject;

    QString textColor() const
    {
        return m_textColor;
    }

    QString fontFamily() const
    {
        return m_fontFamily;
    }

    int fontSize() const
    {
        return m_fontSize;
    }

    bool outlineEnabled() const
    {
        return m_outlineEnabled;
    }

Q_SIGNALS:
    void textColorChanged();
    void fontFamilyChanged();
    void fontSizeChanged();
    void outlineEnabledChanged();

private:
    QString m_textColor = QStringLiteral("#ffffff");
    QString m_fontFamily;
    int m_fontSize = 32;
    bool m_outlineEnabled = true;
};

/*!
 * Stand-in for kshadowlrc::LyricsController for the three properties the overlay
 * binds to. The test decides what is playing, so the empty and the non-empty
 * case can both be exercised.
 */
class StubLyricsController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString currentText READ currentText NOTIFY currentTextChanged)
    Q_PROPERTY(QString translationText READ translationText NOTIFY translationTextChanged)
    Q_PROPERTY(QStringList upcomingLines READ upcomingLines NOTIFY upcomingLinesChanged)

public:
    using QObject::QObject;

    QString currentText() const
    {
        return m_currentText;
    }

    QString translationText() const
    {
        return m_translationText;
    }

    QStringList upcomingLines() const
    {
        return m_upcomingLines;
    }

    void setCurrentText(const QString &text)
    {
        if (m_currentText == text) {
            return;
        }
        m_currentText = text;
        Q_EMIT currentTextChanged();
    }

Q_SIGNALS:
    void currentTextChanged();
    void translationTextChanged();
    void upcomingLinesChanged();

private:
    QString m_currentText;
    QString m_translationText;
    QStringList m_upcomingLines;
};

/*!
 * Stand-in for kshadowlrc::OverlayWindow for the adjusting interface: the mode,
 * the live preview size, the padding, the four drag calls and the wheel's font
 * nudge. Every call is recorded, so the test can tell a wired grab from one that
 * only exists - and the angle a wheel event carried from one the handler
 * computed itself.
 *
 * It clears the live preview when the mode goes off, exactly as the frozen
 * interface says the window does ("0 when not scaling"), so the drawn size can
 * never be left at a preview nobody is dragging.
 */
class StubOverlayWindow : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool adjusting READ adjusting WRITE setAdjusting NOTIFY adjustingChanged)
    Q_PROPERTY(int adjustingFontSize READ adjustingFontSize WRITE setAdjustingFontSize NOTIFY adjustingFontSizeChanged)
    Q_PROPERTY(int adjustPadding READ adjustPadding CONSTANT)

public:
    using QObject::QObject;

    bool adjusting() const
    {
        return m_adjusting;
    }

    int adjustingFontSize() const
    {
        return m_adjustingFontSize;
    }

    int adjustPadding() const
    {
        return 24;
    }

    void setAdjusting(bool adjusting)
    {
        if (m_adjusting == adjusting) {
            return;
        }
        m_adjusting = adjusting;
        if (!adjusting) {
            m_adjustingFontSize = 0;
        }
        Q_EMIT adjustingChanged();
    }

    void setAdjustingFontSize(int size)
    {
        if (m_adjustingFontSize == size) {
            return;
        }
        m_adjustingFontSize = size;
        Q_EMIT adjustingFontSizeChanged();
    }

    Q_INVOKABLE void setContentSize(qreal width, qreal height)
    {
        m_contentSizes.append(QSizeF(width, height));
    }

    Q_INVOKABLE void beginDrag(bool handlesFont)
    {
        m_begins.append(handlesFont);
    }

    Q_INVOKABLE void updateDrag()
    {
        ++m_updates;
    }

    Q_INVOKABLE void endDrag()
    {
        ++m_ends;
    }

    Q_INVOKABLE void endAdjusting(bool keep)
    {
        m_endsAdjusting.append(keep);
    }

    // The wheel's font nudge, as OverlayWindow::nudgeFontSize: the angle is the
    // raw event.angleDelta.y, 120 units per notch. Recorded untouched, so a
    // value the test reads back is one QML passed through and nothing else.
    Q_INVOKABLE void nudgeFontSize(int angleDelta)
    {
        m_fontNudges.append(angleDelta);
    }

    QList<QSizeF> contentSizes() const
    {
        return m_contentSizes;
    }

    QList<bool> begins() const
    {
        return m_begins;
    }

    QList<bool> endsAdjusting() const
    {
        return m_endsAdjusting;
    }

    QList<int> fontNudges() const
    {
        return m_fontNudges;
    }

Q_SIGNALS:
    void adjustingChanged();
    void adjustingFontSizeChanged();

private:
    bool m_adjusting = false;
    int m_adjustingFontSize = 0;
    QList<QSizeF> m_contentSizes;
    QList<bool> m_begins;
    int m_updates = 0;
    int m_ends = 0;
    QList<bool> m_endsAdjusting;
    QList<int> m_fontNudges;
};

} // namespace

class OverlayAdjustQmlTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void cleanupTestCase();
    void loadsWithoutWarnings();
    void reportsTheLyricExtentAsBefore();
    void drawsNothingWhileNotAdjusting();
    void drawsTheFrameAndHandlesWhileAdjusting();
    void theLyricSizeFollowsTheLivePreview();
    void theWheelReachesTheWindow();
    void escapeReachesTheWindow();

private:
    QObject *byObjectName(const QString &name) const;
    /*! Whether \p object and every ancestor of it is marked visible. */
    bool shown(QObject *object) const;
    /*! How many drag handlers of the item are enabled, or disabled. */
    int handlerCount(bool enabled) const;
    void setAdjusting(bool adjusting);

    QQmlEngine m_engine;
    QQmlComponent *m_component = nullptr;
    QObject *m_root = nullptr;
    StubSettings m_settings;
    StubLyricsController m_controller;
    StubOverlayWindow m_overlay;
    QList<QQmlError> m_reported;
    // The window the item is parented into. A wheel event is only ever delivered
    // to a handler through a scene, so the one input this file sends needs one;
    // it stays offscreen and nothing is rendered from it.
    QQuickWindow m_window;
};

QObject *OverlayAdjustQmlTest::byObjectName(const QString &name) const
{
    return m_root ? m_root->findChild<QObject *>(name) : nullptr;
}

// Visibility as it is drawn: the item's own flag and every ancestor's. The flag
// is walked rather than item->isVisible() called, so a check reads the `visible`
// the file binds (overlayWindow.adjusting) and not a window's idea of exposure.
bool OverlayAdjustQmlTest::shown(QObject *object) const
{
    if (object == nullptr) {
        return false;
    }
    for (QObject *ancestor = object; ancestor != nullptr; ancestor = ancestor->parent()) {
        const QVariant visible = ancestor->property("visible");
        if (visible.isValid() && !visible.toBool()) {
            return false;
        }
    }
    return true;
}

int OverlayAdjustQmlTest::handlerCount(bool enabled) const
{
    int count = 0;
    if (m_root == nullptr) {
        return count;
    }
    const auto children = m_root->findChildren<QObject *>();
    for (QObject *child : children) {
        if (child->inherits("QQuickDragHandler") && child->property("enabled").toBool() == enabled) {
            ++count;
        }
    }
    return count;
}

void OverlayAdjustQmlTest::setAdjusting(bool adjusting)
{
    if (!adjusting) {
        // As the window does: no live preview outside a scale.
        m_overlay.setAdjustingFontSize(0);
    }
    m_overlay.setAdjusting(adjusting);
}

void OverlayAdjustQmlTest::initTestCase()
{
    KLocalizedString::setApplicationDomain(QByteArrayLiteral("kshadowlrc"));

    // As src/app/application.cpp: set only a default, an explicit style wins.
    if (QQuickStyle::name().isEmpty()) {
        QQuickStyle::setStyle(QStringLiteral("org.kde.desktop"));
    }

    initResources();

    m_engine.rootContext()->setContextProperty(QStringLiteral("settings"), &m_settings);
    m_engine.rootContext()->setContextProperty(QStringLiteral("lyricsController"), &m_controller);
    m_engine.rootContext()->setContextProperty(QStringLiteral("overlayWindow"), &m_overlay);

    // i18n() is not a global defined by importing org.kde.kirigami: it is a
    // method of the translator installed as the engine's root context object.
    // The host installs one on the overlay's engine, so the test does too.
    KLocalizedQmlContext *context = KLocalization::setupLocalizedContext(&m_engine);
    context->setTranslationDomain(QString::fromUtf8(KLocalizedString::applicationDomain()));

    QObject::connect(&m_engine, &QQmlEngine::warnings, [this](const QList<QQmlError> &warnings) {
        m_reported += warnings;
    });

    m_component = new QQmlComponent(&m_engine, QUrl(QStringLiteral("qrc:/qml/main.qml")), &m_engine);
    m_root = m_component->create();

    // The wheel assertion drives a real event, and a handler is only ever handed
    // one through a scene: the item is put into a window whole - same tree, same
    // bindings - sized as the smallest surface the overlay ever has (240x80, the
    // window's minAdjustBox), and shown offscreen. Nothing is rendered from it.
    auto *item = qobject_cast<QQuickItem *>(m_root);
    QVERIFY2(item != nullptr, "main.qml creates an item");
    m_window.resize(240, 80);
    item->setParentItem(m_window.contentItem());
    item->setSize(QSizeF(m_window.width(), m_window.height()));
    m_window.show();
    QVERIFY2(item->isVisible(), "the item is visible in the window, so a handler can be delivered to");
}

void OverlayAdjustQmlTest::cleanupTestCase()
{
    delete m_root;
    m_root = nullptr;
}

void OverlayAdjustQmlTest::loadsWithoutWarnings()
{
    for (const QQmlError &error : m_reported) {
        std::fprintf(stderr, "QML: %s\n", qPrintable(error.toString()));
    }
    for (const QQmlError &error : m_component->errors()) {
        std::fprintf(stderr, "QML (component): %s\n", qPrintable(error.toString()));
    }
    QVERIFY2(m_root != nullptr, "main.qml creates a root object");
    QVERIFY2(m_reported.isEmpty(), "main.qml reports no QML errors or warnings");
}

void OverlayAdjustQmlTest::reportsTheLyricExtentAsBefore()
{
    QVERIFY(m_root != nullptr);
    setAdjusting(false);

    // The column reports the text's own extent, and with nothing playing it
    // reports an empty one so the surface claims no screen space - the adjusting
    // frame's padding is the window's business and never enters this size.
    m_controller.setCurrentText(QString());
    QVERIFY2(m_overlay.contentSizes().indexOf(QSizeF(0, 0)) != -1, "with nothing playing the column reports nothing");

    m_controller.setCurrentText(QStringLiteral("光の中へ"));
    QVERIFY2(!m_overlay.contentSizes().isEmpty(), "the column reports its extent");
    const QSizeF reported = m_overlay.contentSizes().last();
    QVERIFY2(reported.width() > 0 && reported.height() > 0, "the reported extent follows the lyric text");

    // The frame sits one adjusting padding inside the surface.
    QObject *overlay = byObjectName(QStringLiteral("adjustOverlay"));
    QVERIFY(overlay != nullptr);
    QCOMPARE(overlay->property("frameInset").toInt(), m_overlay.adjustPadding());
}

void OverlayAdjustQmlTest::drawsNothingWhileNotAdjusting()
{
    QVERIFY(m_root != nullptr);
    setAdjusting(false);

    QObject *overlay = byObjectName(QStringLiteral("adjustOverlay"));
    QObject *frame = byObjectName(QStringLiteral("adjustFrame"));
    QVERIFY2(overlay != nullptr && frame != nullptr, "the adjusting block is part of the item");
    QVERIFY2(!shown(overlay), "the adjusting block is not drawn while the mode is off");
    QVERIFY2(!shown(frame), "no frame while the mode is off");

    const QStringList handles{QStringLiteral("adjustHandleTopLeft"),
                              QStringLiteral("adjustHandleTopRight"),
                              QStringLiteral("adjustHandleBottomLeft"),
                              QStringLiteral("adjustHandleBottomRight")};
    for (const QString &name : handles) {
        QObject *handle = byObjectName(name);
        QVERIFY2(handle != nullptr, qPrintable(name + QStringLiteral(" is part of the item")));
        QVERIFY2(!shown(handle), qPrintable(name + QStringLiteral(" is not drawn while the mode is off")));
    }

    // Nothing may depend on the pointer being over the overlay: with the mode off
    // no grab is armed, so the surface is exactly as click-through as it was.
    QVERIFY2(handlerCount(true) == 0, "no drag handler is armed while the mode is off");
    QVERIFY2(handlerCount(false) == 5, "the body grab and the four corner grabs all exist while the mode is off");

    // The wheel is part of the block and is deaf while the mode is off: a wheel
    // event must never be listened for on a click-through surface.
    QObject *wheel = byObjectName(QStringLiteral("adjustWheel"));
    QVERIFY2(wheel != nullptr && wheel->inherits("QQuickWheelHandler"), "the adjusting block carries the wheel");
    QVERIFY2(!wheel->property("enabled").toBool(), "no wheel event is listened for while the mode is off");

    // And the lyrics keep the configured size.
    QCOMPARE(m_root->property("primaryPointSize").toInt(), m_settings.fontSize());
}

void OverlayAdjustQmlTest::drawsTheFrameAndHandlesWhileAdjusting()
{
    QVERIFY(m_root != nullptr);

    // The window's smallest surface (nothing playing): 240x80. The mode has to
    // read at that size, so the geometry is checked there.
    auto *item = qobject_cast<QQuickItem *>(m_root);
    QVERIFY(item != nullptr);
    item->setWidth(240);
    item->setHeight(80);

    setAdjusting(true);

    QObject *overlay = byObjectName(QStringLiteral("adjustOverlay"));
    QObject *frame = byObjectName(QStringLiteral("adjustFrame"));
    QVERIFY2(overlay != nullptr && frame != nullptr, "the adjusting block is part of the item");
    QVERIFY2(shown(overlay), "the adjusting block is drawn while the mode is on");
    QVERIFY2(shown(frame), "the frame is drawn while the mode is on");

    // The block fills the surface, so the frame is inset from the surface itself.
    QCOMPARE(overlay->property("width").toReal(), item->width());
    QCOMPARE(overlay->property("height").toReal(), item->height());

    // Five grabs: the body moves the overlay, each corner scales the text.
    QVERIFY2(handlerCount(true) == 5, "the body and the four corners are armed while the mode is on");

    // Each handle is centred on a corner of the frame, one adjusting padding
    // inside the surface.
    const qreal inset = m_overlay.adjustPadding();
    const QList<QPair<QString, QPointF>> corners{
        {QStringLiteral("adjustHandleTopLeft"), QPointF(inset, inset)},
        {QStringLiteral("adjustHandleTopRight"), QPointF(item->width() - inset, inset)},
        {QStringLiteral("adjustHandleBottomLeft"), QPointF(inset, item->height() - inset)},
        {QStringLiteral("adjustHandleBottomRight"), QPointF(item->width() - inset, item->height() - inset)},
    };
    for (const auto &corner : corners) {
        QObject *handle = byObjectName(corner.first);
        QVERIFY2(handle != nullptr, qPrintable(corner.first + QStringLiteral(" is part of the item")));
        QVERIFY2(shown(handle), qPrintable(corner.first + QStringLiteral(" is drawn while the mode is on")));
        const QPointF centre(handle->property("x").toReal() + handle->property("width").toReal() / 2,
                             handle->property("y").toReal() + handle->property("height").toReal() / 2);
        QCOMPARE(centre, corner.second);

        // A corner handle is a grab: exactly one armed drag on each of them, which
        // is what reaches the window as a font grab rather than a body move.
        int armed = 0;
        const auto grabs = handle->findChildren<QObject *>();
        for (QObject *grab : grabs) {
            armed += grab->inherits("QQuickDragHandler") && grab->property("enabled").toBool() ? 1 : 0;
        }
        QCOMPARE(armed, 1);
    }

    // The body grab is a drag of its own, distinct from the four corners.
    QObject *bodyGrab = byObjectName(QStringLiteral("adjustBodyDrag"));
    QVERIFY2(bodyGrab != nullptr && bodyGrab->inherits("QQuickDragHandler"), "the body carries a grab of its own");
    QVERIFY2(bodyGrab->property("enabled").toBool(), "the body grab is armed while the mode is on");
}

void OverlayAdjustQmlTest::theLyricSizeFollowsTheLivePreview()
{
    QVERIFY(m_root != nullptr);
    const int configured = m_settings.fontSize();
    QVERIFY(configured > 0);

    // Not adjusting: the configured size.
    setAdjusting(false);
    QCOMPARE(m_root->property("primaryPointSize").toInt(), configured);

    // Adjusting but not scaling: still the configured size. The window reports 0
    // for the live preview until a corner is really being dragged.
    setAdjusting(true);
    m_overlay.setAdjustingFontSize(0);
    QCOMPARE(m_root->property("primaryPointSize").toInt(), configured);

    // Scaling: the window's live preview, and the translation and upcoming lines
    // keep the ratios they have to the drawn line.
    m_overlay.setAdjustingFontSize(48);
    QCOMPARE(m_root->property("primaryPointSize").toInt(), 48);
    QCOMPARE(m_root->property("translationPointSize").toInt(), 34);
    QCOMPARE(m_root->property("upcomingPointSize").toInt(), 29);

    // The line that is really drawn takes it too, not just the property.
    m_controller.setCurrentText(QStringLiteral("光の中へ"));
    int matching = 0;
    const auto children = m_root->findChildren<QObject *>();
    for (QObject *child : children) {
        if (!child->inherits("QQuickText") || child->property("text").toString() != QStringLiteral("光の中へ")) {
            continue;
        }
        const QFont font = child->property("font").value<QFont>();
        matching += font.pointSize() == 48 ? 1 : 0;
    }
    QVERIFY2(matching == 1, "the drawn lyric line takes the live preview size while a corner is dragged");

    // The preview ends and the configured size comes back.
    m_overlay.setAdjustingFontSize(0);
    QCOMPARE(m_root->property("primaryPointSize").toInt(), configured);
    QCOMPARE(m_root->property("translationPointSize").toInt(), 22);
    QCOMPARE(m_root->property("upcomingPointSize").toInt(), 19);

    setAdjusting(false);
    QCOMPARE(m_root->property("primaryPointSize").toInt(), configured);
}

void OverlayAdjustQmlTest::theWheelReachesTheWindow()
{
    QVERIFY(m_root != nullptr);

    // The handler the block carries, found by its objectName like the handles.
    QObject *wheel = byObjectName(QStringLiteral("adjustWheel"));
    QVERIFY2(wheel != nullptr && wheel->inherits("QQuickWheelHandler"), "the adjusting block carries a wheel handler");

    // The pointer is put over the middle of the lyric block, not near the frame:
    // that is why the handler hangs on the whole adjusting overlay - a scroll
    // over the words has to reach the window, where the input region is.
    auto *overlay = qobject_cast<QQuickItem *>(byObjectName(QStringLiteral("adjustOverlay")));
    QVERIFY2(overlay != nullptr, "the adjusting block is part of the item");
    const QPointF overTheLyrics = overlay->mapToScene(QPointF(overlay->width() / 2, overlay->height() / 2));

    setAdjusting(true);
    QVERIFY2(wheel->property("enabled").toBool(), "the wheel is listened to while the mode is on");

    // One notch up and one down: 120 units is a notch in Qt's angleDelta, and the
    // angle is what the window is handed - it is not turned into a size here.
    QTest::wheelEvent(&m_window, overTheLyrics, QPoint(0, 120));
    QTest::wheelEvent(&m_window, overTheLyrics, QPoint(0, -120));
    const QList<int> aNotchEachWay{120, -120};
    QCOMPARE(m_overlay.fontNudges(), aNotchEachWay);

    // A small odd angle arrives exactly as the wheel reported it too: the step,
    // the limits and the leftover between notches are all the window's, so
    // nothing may be rounded, scaled or clamped on the way through.
    QTest::wheelEvent(&m_window, overTheLyrics, QPoint(0, 37));
    const QList<int> andOneOddOne{aNotchEachWay + QList<int>{37}};
    QCOMPARE(m_overlay.fontNudges(), andOneOddOne);

    // With the mode off the wheel goes deaf with the rest of the block, and the
    // surface takes no pointer input again.
    setAdjusting(false);
    QTest::wheelEvent(&m_window, overTheLyrics, QPoint(0, 120));
    QCOMPARE(m_overlay.fontNudges(), andOneOddOne);
}

void OverlayAdjustQmlTest::escapeReachesTheWindow()
{
    QVERIFY(m_root != nullptr);

    setAdjusting(true);
    QVERIFY2(m_root->property("focus").toBool(), "the overlay takes focus while adjusting, so the key reaches it");

    // What Keys.onEscapePressed calls, so the key's path is the one under test.
    QVERIFY2(QMetaObject::invokeMethod(m_root, "finishAdjusting"), "finishAdjusting is callable from QML");
    QCOMPARE(m_overlay.endsAdjusting(), QList<bool>{false});

    setAdjusting(false);
    QVERIFY2(!m_root->property("focus").toBool(), "the overlay lets focus go when the mode is off");
}

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("offscreen"));

    QGuiApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("overlayadjust_qml_test"));

    OverlayAdjustQmlTest tc;
    return QTest::qExec(&tc, argc, argv);
}

#include "overlayadjust_qml_test.moc"
