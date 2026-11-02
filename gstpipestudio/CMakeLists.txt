cmake_minimum_required(VERSION 3.22)
project(gstpipestudio LANGUAGES CXX)

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

# Each directory owns its CMakeLists.txt; a directory without one is skipped.
# GPS_DIRS lets one developer build only what they work on while others are mid-edit:
#   -DGPS_DIRS="core/plan;app;tests" -DGPS_TEST_DIRS="unit;gui"
set(GPS_DIRS "core/plan;core/engine;tools;plugin;app;tests" CACHE STRING "Directories to build")
set(GPS_TEST_DIRS "unit;engine;cli;plugin;gui;sweep;deepstream" CACHE STRING "Test groups to build")
foreach(dir ${GPS_DIRS})
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${dir}/CMakeLists.txt)
        add_subdirectory(${dir})
    endif()
endforeach()
