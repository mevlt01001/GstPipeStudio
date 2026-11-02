if(NOT TARGET gps_engine)
    message(STATUS "plugin: gps_engine is not part of this build, skipped")
    return()
endif()

add_library(gstgpsconf MODULE gstgpsconf.cpp)
target_link_libraries(gstgpsconf PRIVATE gps_engine)
set_target_properties(gstgpsconf PROPERTIES LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib/gstreamer-1.0)
