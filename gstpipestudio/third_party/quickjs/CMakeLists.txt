# QuickJS 2024-01-13 (MIT, Fabrice Bellard and Charlie Gordon): a small JavaScript engine. The GUI runs the ELK layout
# (third_party/elkjs) in it. Vendored unchanged; only the engine is built, not its std/os modules or tools.
enable_language(C)
find_package(Threads REQUIRED)
file(STRINGS ${CMAKE_CURRENT_SOURCE_DIR}/VERSION QJS_VERSION)
add_library(gps_quickjs STATIC quickjs.c libregexp.c libunicode.c cutils.c libbf.c)
target_compile_definitions(gps_quickjs PRIVATE _GNU_SOURCE CONFIG_BIGNUM CONFIG_VERSION="${QJS_VERSION}")  # as its Makefile builds it
target_compile_options(gps_quickjs PRIVATE -w -fwrapv -O2)   # vendored: its warnings are not ours; optimised in every build type
target_include_directories(gps_quickjs SYSTEM PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(gps_quickjs PUBLIC m Threads::Threads)
set_target_properties(gps_quickjs PROPERTIES POSITION_INDEPENDENT_CODE ON)
