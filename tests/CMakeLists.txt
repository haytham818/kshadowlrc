# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 KShadowLrc Contributors
#
# Verification harness. Every tests/*.cpp becomes its own Qt Test executable
# linked against kshadowlrc_lib and registered with CTest by ECM, so a lane can
# verify its own logic and nothing else.
#
# Conventions for a test file (see lrcparser_test.cpp for the plain case):
#   - a QObject test class whose private slots are the test functions, and
#     #include "<file>.moc" as the last line;
#   - QVERIFY/QCOMPARE for the checks, never a home-made macro; a private
#     helper that checks can use QVERIFY2(cond, what) so the label survives;
#   - QTEST_GUILESS_MAIN(Class) when nothing needs a platform plugin and no
#     environment has to be in place before the application object exists;
#   - otherwise a hand-written main that does what the old one did and ends in
#     QTest::qExec(&object, argc, argv). This is required for a test that needs
#     a QGuiApplication/QApplication under QT_QPA_PLATFORM=offscreen, and for a
#     test that must point XDG_CONFIG_HOME at a QTemporaryDir before anything
#     can open a config file: QTEST_MAIN() constructs the application before
#     the first test slot runs, which is too late for both.
#   - tests must be deterministic and must not touch the network: a test that
#     needs the network is not a test, so probe connectivity or use a saved
#     fixture.

include(ECMAddTests)

file(GLOB KSHADOWLRC_TEST_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)

ecm_add_tests(${KSHADOWLRC_TEST_SOURCES}
    LINK_LIBRARIES kshadowlrc_lib Qt6::Test
    TEST_NAMES_VAR KSHADOWLRC_TEST_NAMES
)

# The MPRIS tests re-exec themselves under dbus-run-session and print
# "SKIP: ..." when it is not installed. Report that as a skip rather than as a
# pass: the suite must not claim to have verified something it never ran.
set_tests_properties(${KSHADOWLRC_TEST_NAMES} PROPERTIES
    SKIP_REGULAR_EXPRESSION "SKIP: "
)
