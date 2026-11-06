# DeepStream tests: real DeepStream pipelines on the GPU, label "deepstream" (the nvinfer one also "slow").
# Plans live in plans/; the python driver runs each through gps-update and gps-run (and the plugin).
if(TARGET gps-update AND TARGET gps-run AND TARGET gps-inspect AND TARGET gstgpsconf)
    find_program(GPS_PYTHON python3)
    if(GPS_PYTHON)
        foreach(group fast infer expose)
            add_test(NAME deepstream.${group}
                     COMMAND ${GPS_PYTHON} ${CMAKE_CURRENT_SOURCE_DIR}/deepstream_test.py
                             $<TARGET_FILE_DIR:gps-update> $<TARGET_FILE_DIR:gstgpsconf>
                             ${CMAKE_CURRENT_SOURCE_DIR}/plans ${CMAKE_BINARY_DIR}/deepstream-report ${group})
            set_tests_properties(deepstream.${group} PROPERTIES LABELS deepstream SKIP_RETURN_CODE 77 TIMEOUT 600)
        endforeach()
        set_tests_properties(deepstream.infer PROPERTIES LABELS "deepstream;slow" TIMEOUT 900)
    endif()
endif()

# CapsChoice text == the caps in the plan files (the GUI writes what the tests read).
if(TARGET gps_pipeline)
    add_executable(deepstream_caps_test caps_test.cpp)
    target_link_libraries(deepstream_caps_test PRIVATE gps_pipeline)
    target_include_directories(deepstream_caps_test PRIVATE ${GPS_TEST_DIR})
    target_compile_definitions(deepstream_caps_test PRIVATE GPS_DS_PLANS="${CMAKE_CURRENT_SOURCE_DIR}/plans")
    add_test(NAME deepstream.caps COMMAND deepstream_caps_test)
    set_tests_properties(deepstream.caps PROPERTIES LABELS deepstream TIMEOUT 60)
endif()
