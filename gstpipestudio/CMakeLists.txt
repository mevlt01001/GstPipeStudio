cmake_minimum_required(VERSION 3.16)
# The version is not typed in here by hand: a release passes its own (-DGPS_VERSION=..., see .github/workflows/release.yaml).
set(GPS_VERSION 0.1.0 CACHE STRING "Version of the programs and of the package")
project(gstpipestudio VERSION ${GPS_VERSION} LANGUAGES CXX)

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

# The .deb: cpack -G DEB (scripts/make_deb.sh runs it). That script also passes the file name and the dependencies on system
# libraries, which dpkg-shlibdeps finds out from the files that were built. Qt is inside the package (GPS_BUNDLE_QT), GStreamer is not.
set(CPACK_GENERATOR DEB)
set(CPACK_PACKAGE_NAME gstpipestudio)
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_CONTACT "mevlt01001 <mevlutbasaran01@gmail.com>")
set(CPACK_PACKAGE_HOMEPAGE_URL https://github.com/mevlt01001/GstPipeStudio)
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Build GStreamer and DeepStream pipelines by clicking")
set(CPACK_PACKAGE_DESCRIPTION "GstPipeStudio builds a GStreamer or DeepStream pipeline by clicking and saves it as a .conf file.\n gps-run runs the file, and the gpsconf plugin runs it as an element inside gst-launch.")
set(CPACK_DEBIAN_PACKAGE_SECTION video)
# apt installs Recommends with the package (the plugins-good set is small); the big sets are only suggested, the user adds them
set(CPACK_DEBIAN_PACKAGE_RECOMMENDS "gstreamer1.0-plugins-good")
set(CPACK_DEBIAN_PACKAGE_SUGGESTS "gstreamer1.0-plugins-bad, gstreamer1.0-plugins-ugly, gstreamer1.0-libav")
include(CPack)
