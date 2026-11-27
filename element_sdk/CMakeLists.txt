cmake_minimum_required(VERSION 3.16)
project(gps_element_sdk LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(GPS_ELEM_SANITIZERS "Enable ASan and UBSan for SDK and tests" OFF)
set(standalone OFF)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(standalone ON)
endif()
option(GPS_ELEM_WITH_DEEPSTREAM "Enable NVIDIA DeepStream NVMM/NvDsMeta support" ${standalone})
option(GPS_ELEM_BUILD_TESTS "Build SDK examples and tests" ${standalone})
if(NOT DEFINED GPS_ELEM_INSTALL_DIR)
    set(GPS_ELEM_INSTALL_DIR lib/gps-element-sdk CACHE STRING "SDK installation directory")
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(GPS_ELEM_GST REQUIRED IMPORTED_TARGET
    gstreamer-1.0
    gstreamer-base-1.0
    gstreamer-video-1.0
    gstreamer-audio-1.0
    gstreamer-allocators-1.0
)

set(DS_ROOT "/opt/nvidia/deepstream/deepstream")
set(GPS_ELEM_DS_FOUND FALSE)
if(GPS_ELEM_WITH_DEEPSTREAM)
    if(EXISTS "${DS_ROOT}/sources/includes/nvbufsurface.h" AND
       EXISTS "${DS_ROOT}/sources/includes/gstnvdsmeta.h")
        find_library(NVBUFSURFACE_LIB nvbufsurface PATHS "${DS_ROOT}/lib" NO_DEFAULT_PATH)
        find_library(NVDSGST_META_LIB nvdsgst_meta PATHS "${DS_ROOT}/lib" NO_DEFAULT_PATH)
        find_library(NVDS_META_LIB nvds_meta PATHS "${DS_ROOT}/lib" NO_DEFAULT_PATH)
        if(NVBUFSURFACE_LIB AND NVDSGST_META_LIB AND NVDS_META_LIB)
            set(GPS_ELEM_DS_FOUND TRUE)
            message(STATUS "DeepStream SDK found at ${DS_ROOT}")
        endif()
    endif()
endif()

set(GPS_ELEM_SOURCES
    src/caps.cpp
    src/buffer.cpp
    src/video.cpp
    src/audio.cpp
    src/byte_stream.cpp
    src/allocation.cpp
    src/deepstream.cpp
    src/property.cpp
    src/port.cpp
    src/driver.cpp
    src/barrier.cpp
    src/clock.cpp
    src/element.cpp
)

function(gps_elem_configure_lib target)
    target_include_directories(${target} PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${GPS_ELEM_INSTALL_DIR}/include>
    )
    target_link_libraries(${target} PUBLIC PkgConfig::GPS_ELEM_GST)
    target_compile_options(${target} PRIVATE -Wall -Wextra -Werror)
    if(GPS_ELEM_DS_FOUND)
        target_compile_definitions(${target} PUBLIC GPS_ELEM_WITH_DEEPSTREAM=1)
        target_include_directories(${target} PUBLIC "${DS_ROOT}/sources/includes")
        target_link_libraries(${target} PUBLIC
            ${NVBUFSURFACE_LIB}
            ${NVDSGST_META_LIB}
            ${NVDS_META_LIB}
        )
    endif()
endfunction()

add_library(gps_element_sdk_plugin STATIC ${GPS_ELEM_SOURCES})
gps_elem_configure_lib(gps_element_sdk_plugin)

if(GPS_ELEM_SANITIZERS)
    add_library(gps_element_sdk STATIC ${GPS_ELEM_SOURCES})
    gps_elem_configure_lib(gps_element_sdk)
    target_compile_options(gps_element_sdk PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(gps_element_sdk PUBLIC -fsanitize=address,undefined)

else()
    add_library(gps_element_sdk ALIAS gps_element_sdk_plugin)
endif()

include(CMakePackageConfigHelpers)
set_target_properties(gps_element_sdk_plugin PROPERTIES EXPORT_NAME Plugin)
install(TARGETS gps_element_sdk_plugin EXPORT GpsElementSdkTargets ARCHIVE DESTINATION "${GPS_ELEM_INSTALL_DIR}/lib")
install(DIRECTORY include/gps_elem DESTINATION "${GPS_ELEM_INSTALL_DIR}/include")
install(EXPORT GpsElementSdkTargets NAMESPACE GpsElementSdk:: DESTINATION "${GPS_ELEM_INSTALL_DIR}/cmake")
configure_package_config_file(cmake/GpsElementSdkConfig.cmake.in GpsElementSdkConfig.cmake
    INSTALL_DESTINATION "${GPS_ELEM_INSTALL_DIR}/cmake")
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/GpsElementSdkConfig.cmake" DESTINATION "${GPS_ELEM_INSTALL_DIR}/cmake")
configure_package_config_file(cmake/GpsElementSdkConfig.cmake.in "${CMAKE_CURRENT_BINARY_DIR}/cmake/GpsElementSdkConfig.cmake"
    INSTALL_DESTINATION "${CMAKE_CURRENT_BINARY_DIR}/cmake")
export(TARGETS gps_element_sdk_plugin NAMESPACE GpsElementSdk:: FILE "${CMAKE_CURRENT_BINARY_DIR}/cmake/GpsElementSdkTargets.cmake")

if(NOT GPS_ELEM_BUILD_TESTS)
    return()
endif()

# Standalone .so GStreamer plugins
add_library(gstsimplethreshold MODULE examples/simplethreshold.cpp)
set_target_properties(gstsimplethreshold PROPERTIES PREFIX "lib" OUTPUT_NAME "gstsimplethreshold")
target_link_libraries(gstsimplethreshold PRIVATE gps_element_sdk_plugin)
target_compile_options(gstsimplethreshold PRIVATE -Wall -Wextra -Werror)

add_library(gstreticleoverlay MODULE examples/reticleoverlay.cpp)
set_target_properties(gstreticleoverlay PROPERTIES PREFIX "lib" OUTPUT_NAME "gstreticleoverlay")
target_link_libraries(gstreticleoverlay PRIVATE gps_element_sdk_plugin)
target_compile_options(gstreticleoverlay PRIVATE -Wall -Wextra -Werror)

add_library(gstmevouri MODULE examples/mevouri.cpp)
set_target_properties(gstmevouri PROPERTIES PREFIX "lib" OUTPUT_NAME "gstmevouri")
target_link_libraries(gstmevouri PRIVATE gps_element_sdk_plugin)
target_compile_options(gstmevouri PRIVATE -Wall -Wextra -Werror)

# Tests
enable_testing()

function(gps_elem_add_test name src)
    add_executable(${name} ${src})
    target_link_libraries(${name} PRIVATE gps_element_sdk)
    target_compile_options(${name} PRIVATE -Wall -Wextra -Werror)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

gps_elem_add_test(test_scenario1_source tests/test_scenario1_source.cpp)
gps_elem_add_test(test_scenario2_sink tests/test_scenario2_sink.cpp)
gps_elem_add_test(test_scenario3_videofilter tests/test_scenario3_videofilter.cpp)
gps_elem_add_test(test_scenario4_splitter tests/test_scenario4_splitter.cpp)
gps_elem_add_test(test_scenario5_mailbox tests/test_scenario5_mailbox.cpp)
gps_elem_add_test(test_scenario6_barrier tests/test_scenario6_barrier.cpp)
gps_elem_add_test(test_scenario7_general_guard tests/test_scenario7_general_guard.cpp)
gps_elem_add_test(test_scenario8_audio tests/test_scenario8_audio.cpp)
gps_elem_add_test(test_scenario9_bytestream_batch tests/test_scenario9_bytestream_batch.cpp)
gps_elem_add_test(test_scenario10_meta_pool_dmabuf tests/test_scenario10_meta_pool_dmabuf.cpp)
gps_elem_add_test(test_scenario11_pull_seek_uri_clock tests/test_scenario11_pull_seek_uri_clock.cpp)
gps_elem_add_test(test_scenario12_deepstream_nvmm tests/test_scenario12_deepstream_nvmm.cpp)
gps_elem_add_test(test_adversarial tests/test_adversarial.cpp)

# Plugin verification via gst-inspect-1.0 & gst-launch-1.0
add_test(
    NAME test_plugin_gst_inspect
    COMMAND gst-inspect-1.0 --gst-plugin-path=$<TARGET_FILE_DIR:gstsimplethreshold> simplethreshold
)
add_test(
    NAME test_plugin_reticle_inspect
    COMMAND gst-inspect-1.0 --gst-plugin-path=$<TARGET_FILE_DIR:gstsimplethreshold> reticleoverlay
)
add_test(
    NAME test_plugin_mevo_inspect
    COMMAND gst-inspect-1.0 --gst-plugin-path=$<TARGET_FILE_DIR:gstsimplethreshold> mevouri
)
add_test(
    NAME test_plugin_gst_launch
    COMMAND gst-launch-1.0 --gst-plugin-path=$<TARGET_FILE_DIR:gstsimplethreshold>
        videotestsrc num-buffers=5 ! video/x-raw,format=GRAY8,width=64,height=64 !
        simplethreshold threshold=100 ! fakesink
)
add_test(
    NAME test_plugin_mevo_launch
    COMMAND gst-launch-1.0 --gst-plugin-path=$<TARGET_FILE_DIR:gstsimplethreshold>
        mevo://hello ! fakesink
)
