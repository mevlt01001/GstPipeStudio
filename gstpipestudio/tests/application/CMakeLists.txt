# Application unit tests: links ONLY gps_application + Qt6::Test (no Qt6::Widgets / gps_gui).
if(TARGET gps_application)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    set(CMAKE_AUTOMOC ON)
    add_executable(application_test application_test.cpp)
    target_link_libraries(application_test PRIVATE gps_application Qt6::Test)
    add_test(NAME application.unit COMMAND application_test)
    set_tests_properties(application.unit PROPERTIES LABELS application TIMEOUT 60)
endif()
