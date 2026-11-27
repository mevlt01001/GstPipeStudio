foreach(tool gps-inspect gps-update gps-run gps-monitor gps-import)
    add_executable(${tool} ${tool}.cpp)
    target_link_libraries(${tool} PRIVATE gps_gstengine)
    install(TARGETS ${tool} RUNTIME DESTINATION ${GPS_PRIVATE_DIR}/bin)   # next to gstpipestudio: the GUI looks there
endforeach()

find_package(Qt6 REQUIRED COMPONENTS Gui Network)
add_executable(gps-mcp gps-mcp.cpp)
target_link_libraries(gps-mcp PRIVATE gps_application)
install(TARGETS gps-mcp RUNTIME DESTINATION ${GPS_PRIVATE_DIR}/bin)
if(GPS_BUNDLE_QT)
    set_target_properties(gps-mcp PROPERTIES INSTALL_RPATH "$ORIGIN/../lib")
    target_link_options(gps-mcp PRIVATE LINKER:--disable-new-dtags)
endif()
