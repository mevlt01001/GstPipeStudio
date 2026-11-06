# Integration tests: the real GUI (offscreen) against the real gps-inspect / gps-update / gps-run and real GStreamer.
# Needs the tools in the same build (GPS_DIRS with GstEngine and tools); skipped otherwise.
find_package(Qt6 REQUIRED COMPONENTS Test Widgets)
set(CMAKE_AUTOMOC ON)

if(TARGET gps-update AND TARGET gps-inspect AND TARGET gps-run)
    add_executable(integration_test integration_test.cpp)
    target_link_libraries(integration_test PRIVATE gps_gui Qt6::Test)
    target_include_directories(integration_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
    target_compile_definitions(integration_test PRIVATE
        GPS_SHOT_DIR="${CMAKE_BINARY_DIR}/report/screenshots"
        GPS_REPORT_DIR="${CMAKE_BINARY_DIR}/report"
        GPS_FIXTURE_SCRIPT="${GPS_FIXTURES}/make_fixtures.sh")
    add_dependencies(integration_test gps-update gps-inspect gps-run)
    add_test(NAME gui.integration_test COMMAND integration_test)
    set_tests_properties(gui.integration_test PROPERTIES LABELS "integration;gui" TIMEOUT 600
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;PYTHONDONTWRITEBYTECODE=1;GPS_BIN_DIR=$<TARGET_FILE_DIR:gps-update>;QT_QPA_FONTDIR=/usr/share/fonts/truetype/dejavu")
else()
    message(STATUS "integration tests skipped: gps-update / gps-inspect / gps-run are not in this build")
endif()
