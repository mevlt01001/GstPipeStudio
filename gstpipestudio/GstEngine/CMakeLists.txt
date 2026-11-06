find_package(PkgConfig REQUIRED)
pkg_check_modules(GST REQUIRED IMPORTED_TARGET GLOBAL gstreamer-1.0>=1.16 gstreamer-base-1.0>=1.16)

add_library(gps_gstengine STATIC
    src/json.cpp
    src/inspector.cpp
    src/gst_element_creator.cpp
    src/gst_engine.cpp
    src/builder.cpp
    src/session.cpp)
target_include_directories(gps_gstengine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(gps_gstengine PUBLIC gps_pipeline PkgConfig::GST)
set_target_properties(gps_gstengine PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_library(GstPipeStudio::GstEngine ALIAS gps_gstengine)
