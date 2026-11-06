# GUI tests: Qt Test, offscreen, against the fake helpers in fake/ (python scripts with the JSON of SPEC 7).
find_package(Qt6 REQUIRED COMPONENTS Test Widgets)
set(CMAKE_AUTOMOC ON)

set(GPS_FAKE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/fake)
set(GPS_SHOT_DIR ${CMAKE_BINARY_DIR}/report/screenshots)

function(gps_gui_test name)
    add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE gps_gui Qt6::Test)
    target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_definitions(${name} PRIVATE GPS_SHOT_DIR="${GPS_SHOT_DIR}")
    add_test(NAME gui.${name} COMMAND ${name})
    set_tests_properties(gui.${name} PROPERTIES LABELS gui TIMEOUT 300
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;PYTHONDONTWRITEBYTECODE=1;GPS_BIN_DIR=${GPS_FAKE_DIR};QT_QPA_FONTDIR=/usr/share/fonts/truetype/dejavu")
endfunction()

gps_gui_test(controller_test)
gps_gui_test(widgets_test)
gps_gui_test(screens_test)
gps_gui_test(ux_test)
gps_gui_test(support_test)
gps_gui_test(real_tools_test)  # needs the real helpers: it gets their folder from this build
if(TARGET gps-update AND TARGET gps-inspect AND TARGET gps-run AND TARGET gps-import)
    add_dependencies(real_tools_test gps-update gps-inspect gps-run gps-import)
    set_property(TEST gui.real_tools_test APPEND PROPERTY ENVIRONMENT "GPS_REAL_BIN_DIR=$<TARGET_FILE_DIR:gps-update>")
endif()

# Maintainer-only capture of a real, hardware-free feature walkthrough. Not a CI test.
if(TARGET gps-monitor)
    add_executable(demo_capture demo_capture.cpp)
    target_link_libraries(demo_capture PRIVATE gps_gui Qt6::Test)
    target_include_directories(demo_capture PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_definitions(demo_capture PRIVATE GPS_SHOT_DIR="${GPS_SHOT_DIR}")
    add_dependencies(demo_capture gps-inspect gps-update gps-monitor)
endif()

# Real tools + real GStreamer (needs GstEngine and tools in the build)
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/integration/CMakeLists.txt)
    add_subdirectory(integration)
endif()
