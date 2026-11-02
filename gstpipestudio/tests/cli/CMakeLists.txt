# CLI tests: the three helper programs through python (plain asserts + json), label "cli".
if(TARGET gps-update AND TARGET gps-inspect AND TARGET gps-run)
    find_program(GPS_PYTHON python3)
    if(GPS_PYTHON)
        foreach(name inspect update run)
            add_test(NAME cli.${name}
                     COMMAND ${GPS_PYTHON} ${CMAKE_CURRENT_SOURCE_DIR}/cli_${name}_test.py
                             $<TARGET_FILE_DIR:gps-update> ${GPS_FIXTURES}/make_fixtures.sh)
            set_tests_properties(cli.${name} PROPERTIES LABELS cli TIMEOUT 180)
        endforeach()
    endif()
endif()
