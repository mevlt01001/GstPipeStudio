add_library(gps_pipeline STATIC
    src/graph.cpp
    src/pipeline.cpp
    src/actions.cpp
    src/runtime.cpp
    src/conf.cpp
    src/capstext.cpp)
target_include_directories(gps_pipeline PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
set_target_properties(gps_pipeline PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_library(GstPipeStudio::Pipeline ALIAS gps_pipeline)
