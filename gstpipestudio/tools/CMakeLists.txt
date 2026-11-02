foreach(tool gps-inspect gps-update gps-run)
    add_executable(${tool} ${tool}.cpp)
    target_link_libraries(${tool} PRIVATE gps_engine)
endforeach()
