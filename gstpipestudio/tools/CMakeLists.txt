foreach(tool gps-inspect gps-update gps-run)
    add_executable(${tool} ${tool}.cpp)
    target_link_libraries(${tool} PRIVATE gps_engine)
    install(TARGETS ${tool} RUNTIME DESTINATION ${GPS_PRIVATE_DIR}/bin)   # next to gstpipestudio: the GUI looks there
endforeach()
