# GUI-only builds may already have added Application through their dependency.
if(TARGET gps_application)
    return()
endif()

find_package(Qt6 REQUIRED COMPONENTS Gui)
set(CMAKE_AUTOMOC ON)
add_subdirectory(${GPS_ROOT}/third_party/quickjs ${CMAKE_BINARY_DIR}/third_party/quickjs)

add_library(gps_application STATIC
    include/documentsession.h src/documentsession.cpp
    include/documenttiming.h src/documenttiming.cpp
    model/include/buildinfo.h
    model/include/canvasmetrics.h
    backend/include/apppaths.h backend/src/apppaths.cpp
    backend/include/saveresult.h
    backend/include/savetarget.h backend/src/savetarget.cpp
    backend/include/projectfile.h backend/src/projectfile.cpp
    backend/include/versionhistory.h backend/src/versionhistory.cpp
    backend/include/componentprovider.h backend/src/componentprovider.cpp
    backend/include/pluginpaths.h backend/src/pluginpaths.cpp
    backend/include/pluginlibrary.h
    backend/include/templatelibrary.h backend/src/templatelibrary.cpp
    backend/include/settings.h backend/src/settings.cpp
    backend/include/viewpreferences.h backend/src/viewpreferences.cpp
    backend/include/environmentservice.h backend/src/environmentservice.cpp
    backend/include/backupservice.h backend/src/backupservice.cpp
    backend/include/appservices.h
    backend/include/executionbackend.h backend/src/executionbackend.cpp
    backend/include/factorycatalog.h backend/src/factorycatalog.cpp
    backend/include/helperclient.h backend/src/helperclient.cpp
    backend/include/slowwatch.h backend/src/slowwatch.cpp
    backend/include/startupcheck.h backend/src/startupcheck.cpp
    backend/include/supportreport.h backend/src/supportreport.cpp
    model/include/elementedit.h
    model/include/notice.h
    model/include/capslabel.h model/src/capslabel.cpp
    model/include/contract.h model/src/contract.cpp
    model/include/elementview.h
    controller/include/controller.h controller/src/controller.cpp
    controller/include/editsession.h controller/src/editsession.cpp
    controller/include/validationsession.h controller/src/validationsession.cpp
    controller/include/runsession.h controller/src/runsession.cpp
    controller/include/templateinstances.h controller/src/templateinstances.cpp
    controller/include/documentviews.h controller/src/documentviews.cpp
    controller/include/canvaslayout.h controller/src/canvaslayout.cpp
    controller/include/outline.h controller/src/outline.cpp
    controller/include/documentbackup.h controller/src/documentbackup.cpp
    layout/include/layoutgraph.h
    layout/include/elkengine.h layout/src/elkengine.cpp
    layout/include/layoutrunner.h layout/src/layoutrunner.cpp)
target_include_directories(gps_application PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/backend/include
    ${CMAKE_CURRENT_SOURCE_DIR}/model/include
    ${CMAKE_CURRENT_SOURCE_DIR}/controller/include
    ${CMAKE_CURRENT_SOURCE_DIR}/layout/include)
target_link_libraries(gps_application PUBLIC Qt6::Gui gps_pipeline PRIVATE gps_quickjs)

add_library(GstPipeStudio::Application ALIAS gps_application)

set(GPS_BUILD_CHANNEL "Development" CACHE STRING "Application identity: Development or Release")
set_property(CACHE GPS_BUILD_CHANNEL PROPERTY STRINGS Development Release)
if(NOT GPS_BUILD_CHANNEL MATCHES "^(Development|Release)$")
    message(FATAL_ERROR "GPS_BUILD_CHANNEL must be Development or Release")
endif()
set(GPS_BUILD_REVISION "" CACHE STRING "Commit identity supplied by CI or container build")
get_filename_component(GPS_REPOSITORY "${GPS_ROOT}/.." REALPATH)
set(GPS_REVISION "${GPS_BUILD_REVISION}")
if(NOT GPS_REVISION)
    execute_process(COMMAND git -c safe.directory=${GPS_REPOSITORY} rev-parse --short HEAD
        WORKING_DIRECTORY ${GPS_ROOT} OUTPUT_VARIABLE GPS_REVISION OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
endif()
if(NOT GPS_REVISION)
    set(GPS_REVISION "unknown")
endif()
target_compile_definitions(gps_application PUBLIC GPS_APP_VERSION="${PROJECT_VERSION}" GPS_APP_REVISION="${GPS_REVISION}" GPS_APP_CHANNEL="${GPS_BUILD_CHANNEL}")
# elkjs 0.10.0 (EPL-2.0), unchanged: the ELK layout the layout::ElkEngine runs in QuickJS.
qt_add_resources(gps_application elk PREFIX "/layout" BASE "${GPS_ROOT}/third_party/elkjs" FILES "${GPS_ROOT}/third_party/elkjs/elk.bundled.js")
