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
