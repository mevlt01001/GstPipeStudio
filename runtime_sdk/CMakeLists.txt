cmake_minimum_required(VERSION 3.16)
project(gps_runtime_sdk VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(gps_runtime_sdk STATIC src/pipeline.cpp)
add_library(GpsRuntimeSdk::Runtime ALIAS gps_runtime_sdk)
set_target_properties(gps_runtime_sdk PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(gps_runtime_sdk PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)
target_link_libraries(gps_runtime_sdk PUBLIC ${CMAKE_DL_LIBS})
target_compile_options(gps_runtime_sdk PRIVATE -Wall -Wextra -Werror)

option(GPS_RUNTIME_BUILD_TESTS "Build runtime_sdk standalone tests" ON)
if(GPS_RUNTIME_BUILD_TESTS)
    enable_testing()
    add_executable(host_app_demo examples/host_app_demo.cpp)
    target_link_libraries(host_app_demo PRIVATE gps_runtime_sdk)

    add_library(mock_pipeline MODULE tests/mock_pipeline.c)
    set_target_properties(mock_pipeline PROPERTIES PREFIX "lib")
    target_include_directories(mock_pipeline PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

    add_executable(test_runtime_sdk tests/test_runtime_sdk.cpp)
    target_link_libraries(test_runtime_sdk PRIVATE gps_runtime_sdk)
    target_compile_definitions(test_runtime_sdk PRIVATE MOCK_PIPELINE_PATH="$<TARGET_FILE:mock_pipeline>")
    add_test(NAME runtime.abi_and_cpp_wrapper COMMAND test_runtime_sdk)
endif()

if(DEFINED GPS_RUNTIME_INSTALL_DIR)
    include(CMakePackageConfigHelpers)
    install(DIRECTORY include/gps_runtime DESTINATION ${GPS_RUNTIME_INSTALL_DIR}/include)
    install(FILES src/pipeline.cpp DESTINATION ${GPS_RUNTIME_INSTALL_DIR}/src)
    install(TARGETS gps_runtime_sdk EXPORT GpsRuntimeSdkTargets
            ARCHIVE DESTINATION ${GPS_RUNTIME_INSTALL_DIR}/lib)
    install(EXPORT GpsRuntimeSdkTargets
            FILE GpsRuntimeSdkTargets.cmake
            NAMESPACE GpsRuntimeSdk::
            DESTINATION ${GPS_RUNTIME_INSTALL_DIR}/cmake)
    configure_package_config_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/cmake/GpsRuntimeSdkConfig.cmake.in
        ${CMAKE_CURRENT_BINARY_DIR}/GpsRuntimeSdkConfig.cmake
        INSTALL_DESTINATION ${GPS_RUNTIME_INSTALL_DIR}/cmake)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/GpsRuntimeSdkConfig.cmake
            DESTINATION ${GPS_RUNTIME_INSTALL_DIR}/cmake)
endif()
