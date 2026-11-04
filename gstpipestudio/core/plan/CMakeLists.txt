add_library(gps_plan STATIC
    src/plan.cpp
    src/pipeline.cpp
    src/conf.cpp
    src/capstext.cpp)
target_include_directories(gps_plan PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
set_target_properties(gps_plan PROPERTIES POSITION_INDEPENDENT_CODE ON)
