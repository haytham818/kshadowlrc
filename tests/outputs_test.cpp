// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 KShadowLrc Contributors
//
// Offline checks for kshadowlrc::Outputs, the output list the settings picker
// binds to.
//
// The platform plugin is offscreen: no compositor, no network. Offscreen reports
// a single unnamed screen, so what can be pinned here is the shape of the object
// - non-empty, primary first, one entry per screen, no duplicates, stable across
// two instances. A hotplug (changed() firing on screenAdded/screenRemoved/
// primaryScreenChanged) cannot be produced without a real screen list, so it is
// deliberately not faked.

#include "platform/outputs.h"

#include <QGuiApplication>
#include <QScreen>
#include <QStringList>
#include <QTest>

using namespace kshadowlrc;

class OutputsTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void primaryFirstAndDuplicateFree();
    void matchesTheConnectedScreens();
    void stableAcrossInstances();

private:
    Outputs m_outputs;
    QStringList m_names;
    QString m_primary;
};

void OutputsTest::initTestCase()
{
    m_names = m_outputs.names();
    m_primary = m_outputs.primaryName();
}

void OutputsTest::primaryFirstAndDuplicateFree()
{
    QVERIFY2(!m_names.isEmpty(), "at least one output is listed");
    // The first entry is the primary output's name; offscreen's single screen is
    // unnamed, so this is also the empty-string case.
    QVERIFY2(m_names.first() == m_primary, "the primary output is listed first");
    QVERIFY2(m_names.contains(m_primary), "the primary output is in the list");

    // No duplicates: every entry is a distinct screen.
    for (int i = 0; i < m_names.size(); ++i) {
        for (int j = i + 1; j < m_names.size(); ++j) {
            QVERIFY2(m_names.at(i) != m_names.at(j), "no output name appears twice");
        }
    }
}

void OutputsTest::matchesTheConnectedScreens()
{
    // One entry per connected screen, and the primary name is Qt's own.
    const QList<QScreen *> screens = QGuiApplication::screens();
    QVERIFY2(m_names.size() == screens.size(), "one entry per connected screen");
    QVERIFY2(QGuiApplication::primaryScreen() != nullptr, "the offscreen platform has a primary screen");
    if (QGuiApplication::primaryScreen()) {
        QVERIFY2(m_primary == QGuiApplication::primaryScreen()->name(), "primaryName() is the primary QScreen's name");
    }
}

void OutputsTest::stableAcrossInstances()
{
    // The object keeps no state between calls: a second instance agrees.
    Outputs other;
    QVERIFY2(other.names() == m_names, "two Outputs instances report the same list");
    QVERIFY2(other.primaryName() == m_primary, "two Outputs instances agree on the primary");
}

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("offscreen"));
    QGuiApplication app(argc, argv);

    OutputsTest tc;
    return QTest::qExec(&tc, argc, argv);
}

#include "outputs_test.moc"
