# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 KShadowLrc Contributors
#
# Verification harness. Every tests/*.cpp becomes its own executable linked
# against kshadowlrc_lib and registered with CTest, so a lane can verify its own
# logic without a framework dependency and without touching any other file.
#
# Conventions for a test file:
#   - int main(int argc, char **argv) creating a QCoreApplication when Qt objects
#     are involved, then returning a non-zero code on failure;
#   - print a one-line summary of what it checked so `ctest -V` is readable;
#   - tests must be deterministic and must not touch the network. A test that
#     needs the network is not a test: probe connectivity, or use a saved fixture.

file(GLOB KSHADOWLRC_TEST_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)

foreach(test_source IN LISTS KSHADOWLRC_TEST_SOURCES)
    get_filename_component(test_name "${test_source}" NAME_WE)
    add_executable(${test_name} "${test_source}")
    target_link_libraries(${test_name} PRIVATE kshadowlrc_lib)
    add_test(NAME ${test_name} COMMAND ${test_name})
endforeach()
