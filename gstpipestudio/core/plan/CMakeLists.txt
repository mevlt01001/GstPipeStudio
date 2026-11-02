add_library(gps_plan STATIC plan.cpp conf.cpp capstext.cpp)
target_include_directories(gps_plan PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
set_target_properties(gps_plan PROPERTIES POSITION_INDEPENDENT_CODE ON)
