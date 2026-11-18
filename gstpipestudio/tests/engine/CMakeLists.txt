# Engine tests: real GStreamer, label "engine". Each test makes its own fixtures (make_fixtures.sh) in a temp folder.
foreach(name json inspector update property run import nvidia backend)
    add_executable(${name}_test ${name}_test.cpp)
    target_link_libraries(${name}_test PRIVATE gps_gstengine)
    target_include_directories(${name}_test PRIVATE ${GPS_TEST_DIR} ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_definitions(${name}_test PRIVATE GPS_FIXTURE_SCRIPT="${GPS_FIXTURES}/make_fixtures.sh")
    add_test(NAME engine.${name} COMMAND ${name}_test)
    set_tests_properties(engine.${name} PROPERTIES LABELS engine TIMEOUT 120)
endforeach()

set_tests_properties(engine.nvidia PROPERTIES SKIP_RETURN_CODE 77)

# Observe temporary allocations without retaining them; --wrap is an ELF linker option.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_executable(temporary_end_test temporary_end_test.cpp)
    target_link_libraries(temporary_end_test PRIVATE gps_gstengine)
    target_include_directories(temporary_end_test PRIVATE ${GPS_TEST_DIR})
    target_link_options(temporary_end_test PRIVATE
        "LINKER:--wrap=gst_element_factory_make" "LINKER:--wrap=gst_bin_new")
    foreach(factory queue typefind fakesink)
        add_test(NAME engine.temporary_end_missing_${factory} COMMAND temporary_end_test ${factory})
        set_tests_properties(engine.temporary_end_missing_${factory} PROPERTIES LABELS engine TIMEOUT 10)
    endforeach()
endif()
