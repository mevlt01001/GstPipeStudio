# Sweep: every element factory of the image through gps-inspect / gps-update / gps-run (SPEC 9).
# sweep.selftest checks the orchestrator with stub tools (fast).
# sweep.all is the real run (label slow, about 10 minutes): it is only a survey of the elements, so it is off by default;
# turn it on with -DGPS_SWEEP_ALL=ON.
option(GPS_SWEEP_ALL "Add the slow test sweep.all (every element factory)" OFF)
if(TARGET gps-update AND TARGET gps-inspect AND TARGET gps-run)
    find_program(GPS_PYTHON python3)
    if(GPS_PYTHON)
        set(GPS_SWEEP_JOBS 6 CACHE STRING "Parallel workers of sweep.all")
        add_test(NAME sweep.selftest
                 COMMAND ${GPS_PYTHON} ${CMAKE_CURRENT_SOURCE_DIR}/sweep_selftest.py
                         $<TARGET_FILE_DIR:gps-update> ${CMAKE_BINARY_DIR}/sweep-selftest)
        set_tests_properties(sweep.selftest PROPERTIES LABELS sweep TIMEOUT 600)
        if(GPS_SWEEP_ALL)
            add_test(NAME sweep.all
                     COMMAND ${GPS_PYTHON} ${CMAKE_CURRENT_SOURCE_DIR}/sweep.py
                             --bin-dir $<TARGET_FILE_DIR:gps-update> --work-dir ${CMAKE_BINARY_DIR}/sweep-work
                             --report-dir ${CMAKE_BINARY_DIR}/report --jobs ${GPS_SWEEP_JOBS})
            set_tests_properties(sweep.all PROPERTIES LABELS "sweep;slow" TIMEOUT 3000)
        endif()
    endif()
endif()
