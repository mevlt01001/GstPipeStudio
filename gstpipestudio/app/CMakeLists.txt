# The GUI links gps_plan and Qt only. It never sees GStreamer: the helpers (gps-inspect, gps-update, gps-run) do.
find_package(Qt6 REQUIRED COMPONENTS Widgets)
set(CMAKE_AUTOMOC ON)

add_library(gps_app STATIC
    canvasscene.cpp canvasview.cpp capsdialog.cpp capsfieldeditor.cpp capsparse.cpp confirmdialog.cpp controller.cpp
    elementinfo.cpp elementitem.cpp factorydelegate.cpp flows.cpp helperclient.cpp linkitem.cpp logpanel.cpp
    mainwindow.cpp paditem.cpp pickerdialog.cpp propertydialog.cpp propertyeditor.cpp sourcepanel.cpp theme.cpp
    updatepanel.cpp updateresult.cpp)
target_include_directories(gps_app PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(gps_app PUBLIC Qt6::Widgets gps_plan)

add_executable(gstpipestudio main.cpp)
target_link_libraries(gstpipestudio PRIVATE gps_app)
