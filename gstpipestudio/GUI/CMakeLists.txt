# The GUI links gps_pipeline and Qt only. It never sees GStreamer: the helpers (gps-inspect, gps-update, gps-run) do.
find_package(Qt6 REQUIRED COMPONENTS Widgets)
set(CMAKE_AUTOMOC ON)

add_library(gps_gui STATIC
    backend/include/executionbackend.h backend/src/executionbackend.cpp
    backend/include/helperclient.h backend/src/helperclient.cpp
    backend/include/slowwatch.h backend/src/slowwatch.cpp
    backend/include/startupcheck.h backend/src/startupcheck.cpp
    backend/include/supportreport.h backend/src/supportreport.cpp
    model/include/capsparse.h model/src/capsparse.cpp
    model/include/elementedit.h
    model/include/elementinfo.h model/src/elementinfo.cpp
    model/include/elementview.h
    model/include/updateresult.h model/src/updateresult.cpp
    controller/include/controller.h controller/src/controller.cpp
    controller/include/flows.h controller/src/flows.cpp
    canvas/include/canvasmetrics.h
    canvas/include/canvasscene.h canvas/src/canvasscene.cpp
    canvas/include/canvasview.h canvas/src/canvasview.cpp
    canvas/include/elementitem.h canvas/src/elementitem.cpp
    canvas/include/linkitem.h canvas/src/linkitem.cpp
    canvas/include/paditem.h canvas/src/paditem.cpp
    ui/include/drawer.h ui/src/drawer.cpp
    ui/include/mainwindow.h ui/src/mainwindow.cpp
    ui/include/theme.h ui/src/theme.cpp
    ui/pages/include/capspage.h ui/pages/src/capspage.cpp
    ui/pages/include/pickerpage.h ui/pages/src/pickerpage.cpp
    ui/pages/include/propertypage.h ui/pages/src/propertypage.cpp
    ui/pages/include/setuppage.h ui/pages/src/setuppage.cpp
    ui/widgets/include/capsfieldeditor.h ui/widgets/src/capsfieldeditor.cpp
    ui/widgets/include/confirmdialog.h ui/widgets/src/confirmdialog.cpp
    ui/widgets/include/factorydelegate.h ui/widgets/src/factorydelegate.cpp
    ui/widgets/include/logpanel.h ui/widgets/src/logpanel.cpp
    ui/widgets/include/propertyeditor.h ui/widgets/src/propertyeditor.cpp
    ui/widgets/include/spinner.h ui/widgets/src/spinner.cpp
    ui/widgets/include/updatepanel.h ui/widgets/src/updatepanel.cpp
    ui/widgets/include/validationindicator.h ui/widgets/src/validationindicator.cpp)
target_include_directories(gps_gui PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/backend/include
    ${CMAKE_CURRENT_SOURCE_DIR}/model/include
    ${CMAKE_CURRENT_SOURCE_DIR}/controller/include
    ${CMAKE_CURRENT_SOURCE_DIR}/canvas/include
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/include
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/pages/include
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/widgets/include)
target_link_libraries(gps_gui PUBLIC Qt6::Widgets gps_pipeline)

add_executable(gstpipestudio ${GPS_ROOT}/main.cpp)
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
