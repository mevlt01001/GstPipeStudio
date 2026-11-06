foreach(tool gps-inspect gps-update gps-run gps-monitor gps-import)
    add_executable(${tool} ${tool}.cpp)
    target_link_libraries(${tool} PRIVATE gps_gstengine)
    install(TARGETS ${tool} RUNTIME DESTINATION ${GPS_PRIVATE_DIR}/bin)   # next to gstpipestudio: the GUI looks there
endforeach()
