set(GPS_TEST_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(GPS_FIXTURES ${CMAKE_CURRENT_SOURCE_DIR}/fixtures)

# Each test group owns its CMakeLists.txt; a group without one is skipped.
foreach(dir ${GPS_TEST_DIRS})
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${dir}/CMakeLists.txt)
        add_subdirectory(${dir})
    endif()
endforeach()
