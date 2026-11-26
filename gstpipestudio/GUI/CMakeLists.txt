# Keep older GPS_DIRS selections that list GUI without Application working.
if(NOT TARGET gps_application)
    add_subdirectory(${GPS_ROOT}/Application ${CMAKE_BINARY_DIR}/Application)
endif()

find_package(Qt6 REQUIRED COMPONENTS Widgets)
set(CMAKE_AUTOMOC ON)

add_library(gps_gui STATIC
    ui/include/flows.h ui/src/flows.cpp
    ui/include/panelslot.h ui/src/panelslot.cpp
    canvas/include/canvasscene.h canvas/src/canvasscene.cpp
    canvas/include/canvasview.h canvas/src/canvasview.cpp
    canvas/include/elementitem.h canvas/src/elementitem.cpp
    canvas/include/headerbutton.h canvas/src/headerbutton.cpp
    canvas/include/linkitem.h canvas/src/linkitem.cpp
    canvas/include/paditem.h canvas/src/paditem.cpp
    canvas/include/templateframeitem.h canvas/src/templateframeitem.cpp
    ui/include/drawer.h ui/src/drawer.cpp
    ui/include/mainwindow.h ui/src/mainwindow.cpp
    ui/include/panels.h ui/src/panels.cpp
    ui/include/document.h ui/src/document.cpp
    ui/include/documenttabs.h ui/src/documenttabs.cpp
    ui/include/commandset.h ui/src/commandset.cpp
    ui/include/contextmenus.h ui/src/contextmenus.cpp
    ui/include/prompt.h ui/src/prompt.cpp
    ui/include/saveflow.h ui/src/saveflow.cpp
    ui/include/documentregistry.h ui/src/documentregistry.cpp
    ui/widgets/include/newprojectdialog.h ui/widgets/src/newprojectdialog.cpp
    ui/include/fileoperations.h ui/src/fileoperations.cpp
    ui/explorer/include/explorerentry.h ui/explorer/src/explorerentry.cpp
    ui/explorer/include/explorersource.h ui/explorer/src/explorersource.cpp
    ui/explorer/include/explorerlook.h ui/explorer/src/explorerlook.cpp
    ui/explorer/include/explorer.h ui/explorer/src/explorer.cpp
    ui/widgets/include/tabclosebutton.h ui/widgets/src/tabclosebutton.cpp
    ui/include/theme.h ui/src/theme.cpp
    ui/pages/include/capspage.h ui/pages/src/capspage.cpp
    ui/pages/include/pickerpage.h ui/pages/src/pickerpage.cpp
    ui/pages/include/propertypage.h ui/pages/src/propertypage.cpp
    ui/pages/include/setuppage.h ui/pages/src/setuppage.cpp
    ui/pages/include/templatepage.h ui/pages/src/templatepage.cpp
    ui/pages/include/templateparamspage.h ui/pages/src/templateparamspage.cpp
    ui/widgets/include/capsfieldeditor.h ui/widgets/src/capsfieldeditor.cpp
    ui/widgets/include/factorydelegate.h ui/widgets/src/factorydelegate.cpp
    ui/widgets/include/logpanel.h ui/widgets/src/logpanel.cpp
    ui/widgets/include/wheelguard.h ui/widgets/src/wheelguard.cpp
    ui/widgets/include/propertyeditor.h ui/widgets/src/propertyeditor.cpp
    ui/widgets/include/spinner.h ui/widgets/src/spinner.cpp
    ui/widgets/include/updatepanel.h ui/widgets/src/updatepanel.cpp
    ui/widgets/include/telemetrypanel.h ui/widgets/src/telemetrypanel.cpp
    ui/widgets/include/templatepeek.h ui/widgets/src/templatepeek.cpp
    ui/widgets/include/validationindicator.h ui/widgets/src/validationindicator.cpp)
target_include_directories(gps_gui PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/canvas/include
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/include
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/explorer/include
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/pages/include
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/widgets/include)
target_link_libraries(gps_gui PUBLIC Qt6::Widgets gps_application)

add_executable(gstpipestudio ${GPS_ROOT}/main.cpp ${GPS_ROOT}/gstpipestudio.cpp)
target_link_libraries(gstpipestudio PRIVATE gps_gui)
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

add_library(GstPipeStudio::GUI ALIAS gps_gui)

qt_add_resources(gps_gui branding PREFIX "/brand" BASE "${GPS_ROOT}/assets/logo"
    FILES "${GPS_ROOT}/assets/logo/gstpipestudio.svg" "${GPS_ROOT}/assets/logo/animation.gif")
target_sources(gps_gui PRIVATE ui/widgets/src/windowchrome.cpp ui/widgets/src/aboutdialog.cpp)

target_sources(gps_gui PRIVATE canvas/src/connectionpreview.cpp)
qt_add_resources(gps_gui appicon PREFIX "/brand" BASE "${GPS_ROOT}/packaging/icons/hicolor/64x64/apps"
    FILES "${GPS_ROOT}/packaging/icons/hicolor/64x64/apps/gstpipestudio.png")
