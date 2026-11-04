cmake_minimum_required(VERSION 3.16)
project(gstpipestudio VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
add_compile_options(-Wall -Wextra)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

set(GPS_ROOT ${CMAKE_CURRENT_SOURCE_DIR})
enable_testing()

# Installing (cmake --install, or the .deb with -DCMAKE_INSTALL_PREFIX=/usr): the programs and the Qt next to them live in
# one private folder, GPS_PRIVATE_DIR, so that the qt.conf there belongs to us alone; bin/ only gets links to them.
# The plugin goes to the plugin folder of GStreamer, the desktop file and the icons to share/.
include(GNUInstallDirs)
set(GPS_PRIVATE_DIR lib/gstpipestudio)

# Each directory owns its CMakeLists.txt; a directory without one is skipped.
# GPS_DIRS lets one developer build only what they work on while others are mid-edit:
#   -DGPS_DIRS="core/plan;app;tests" -DGPS_TEST_DIRS="unit;gui"
set(GPS_DIRS "core/plan;core/engine;tools;plugin;app;packaging;tests" CACHE STRING "Directories to build")
set(GPS_TEST_DIRS "unit;engine;cli;plugin;gui;sweep;deepstream" CACHE STRING "Test groups to build")
foreach(dir ${GPS_DIRS})
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${dir}/CMakeLists.txt)
        add_subdirectory(${dir})
    endif()
endforeach()
