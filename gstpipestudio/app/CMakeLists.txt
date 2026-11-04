# The GUI links gps_plan and Qt only. It never sees GStreamer: the helpers (gps-inspect, gps-update, gps-run) do.
find_package(Qt6 REQUIRED COMPONENTS Widgets)
set(CMAKE_AUTOMOC ON)

add_library(gps_app STATIC
    canvasscene.cpp canvasview.cpp capsfieldeditor.cpp capsparse.cpp capspage.cpp confirmdialog.cpp controller.cpp drawer.cpp
    elementinfo.cpp elementitem.cpp factorydelegate.cpp flows.cpp helperclient.cpp linkitem.cpp logpanel.cpp
    mainwindow.cpp paditem.cpp pickerpage.cpp propertyeditor.cpp propertypage.cpp setuppage.cpp slowwatch.cpp spinner.cpp
    startupcheck.cpp supportreport.cpp theme.cpp updatepanel.cpp updateresult.cpp validationindicator.cpp)
target_include_directories(gps_app PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(gps_app PUBLIC Qt6::Widgets gps_plan)

add_executable(gstpipestudio main.cpp)
target_link_libraries(gstpipestudio PRIVATE gps_app)
install(TARGETS gstpipestudio RUNTIME DESTINATION ${GPS_PRIVATE_DIR}/bin)

# The .deb carries its own Qt: the libraries and the plugins (xcb for a window, offscreen for tests) of the Qt it was built with,
# found through the RPATH and a qt.conf next to the program. Off for a plain install on a machine that has Qt.
option(GPS_BUNDLE_QT "Install the Qt libraries and plugins next to the app (for the .deb)" OFF)
if(GPS_BUNDLE_QT)
    get_filename_component(GPS_QT_PREFIX "${Qt6_DIR}/../../.." ABSOLUTE)   # Qt6_DIR is <prefix>/lib/cmake/Qt6
    install(DIRECTORY ${GPS_QT_PREFIX}/lib/ DESTINATION ${GPS_PRIVATE_DIR}/lib FILES_MATCHING PATTERN "libQt6*.so.*"
            PATTERN cmake EXCLUDE PATTERN pkgconfig EXCLUDE PATTERN metatypes EXCLUDE)   # the files and links of the libraries only
    install(DIRECTORY ${GPS_QT_PREFIX}/plugins/ DESTINATION ${GPS_PRIVATE_DIR}/plugins)
    file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/qt.conf "[Paths]\nPrefix = ..\nPlugins = plugins\nLibraries = lib\n")
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/qt.conf DESTINATION ${GPS_PRIVATE_DIR}/bin)
    # RPATH, not RUNPATH: it also serves the libraries that Qt loads by itself (the plugins, and what they need)
    set_target_properties(gstpipestudio PROPERTIES INSTALL_RPATH "$ORIGIN/../lib")
    target_link_options(gstpipestudio PRIVATE LINKER:--disable-new-dtags)
endif()
