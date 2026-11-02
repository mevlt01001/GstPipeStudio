# Plugin tests: libgstgpsconf.so through gst-inspect-1.0 and gst-launch-1.0, label "plugin".
if(TARGET gstgpsconf)
    find_program(GPS_PYTHON python3)
    if(GPS_PYTHON)
        add_test(NAME plugin.gst
                 COMMAND ${GPS_PYTHON} ${CMAKE_CURRENT_SOURCE_DIR}/plugin_test.py
                         $<TARGET_FILE_DIR:gstgpsconf> ${GPS_FIXTURES}/make_fixtures.sh)
        set_tests_properties(plugin.gst PROPERTIES LABELS plugin TIMEOUT 300)
    endif()
endif()
