find_package(PkgConfig REQUIRED)
pkg_check_modules(GST REQUIRED IMPORTED_TARGET GLOBAL gstreamer-1.0 gstreamer-base-1.0)

add_library(gps_engine STATIC
    src/json.cpp
    src/inspector.cpp
    src/gst_element_creator.cpp
    src/gst_engine.cpp
    src/builder.cpp)
target_include_directories(gps_engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(gps_engine PUBLIC gps_plan PkgConfig::GST)
set_target_properties(gps_engine PROPERTIES POSITION_INDEPENDENT_CODE ON)
