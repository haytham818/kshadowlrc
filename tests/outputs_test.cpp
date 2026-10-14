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

#include <cstdio>

using namespace kshadowlrc;

namespace
{

int g_failures = 0;

void check(bool condition, const char *what)
{
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++g_failures;
    }
}

} // namespace

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("offscreen"));
    QGuiApplication app(argc, argv);

    Outputs outputs;
    const QStringList names = outputs.names();
    const QString primary = outputs.primaryName();

    check(!names.isEmpty(), "at least one output is listed");
    // The first entry is the primary output's name; offscreen's single screen is
    // unnamed, so this is also the empty-string case.
    check(names.first() == primary, "the primary output is listed first");
    check(names.contains(primary), "the primary output is in the list");

    // No duplicates: every entry is a distinct screen.
    for (int i = 0; i < names.size(); ++i) {
        for (int j = i + 1; j < names.size(); ++j) {
            check(names.at(i) != names.at(j), "no output name appears twice");
        }
    }

    // One entry per connected screen, and the primary name is Qt's own.
    const QList<QScreen *> screens = QGuiApplication::screens();
    check(names.size() == screens.size(), "one entry per connected screen");
    check(QGuiApplication::primaryScreen() != nullptr, "the offscreen platform has a primary screen");
    if (QGuiApplication::primaryScreen()) {
        check(primary == QGuiApplication::primaryScreen()->name(), "primaryName() is the primary QScreen's name");
    }

    // The object keeps no state between calls: a second instance agrees.
    Outputs other;
    check(other.names() == names, "two Outputs instances report the same list");
    check(other.primaryName() == primary, "two Outputs instances agree on the primary");

    if (g_failures == 0) {
        std::printf("outputs_test: primary-first, duplicate-free list, one entry per screen, agreed across instances\n");
        return 0;
    }
    std::fprintf(stderr, "outputs_test: %d check(s) failed\n", g_failures);
    return 1;
}
