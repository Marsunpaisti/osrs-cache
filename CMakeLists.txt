# osrs_cache: the game's cache on disk, everything RuneLite's cache library
# reads from it (configs, interfaces, scripts, maps, models, animations,
# sprites, textures, the world map, music and sound effects), plus static
# collision and skeletal animations. Pure C++20 over zlib and bzip2, which
# cmake/ fetches.
#
# Nothing in this folder may depend on anything outside it, so that it can be
# copied into another project as it is (README.md): added there with
# add_subdirectory, or configured on its own, which builds its tests too.

cmake_minimum_required(VERSION 3.24)
project(osrs_cache LANGUAGES C CXX)

include("${CMAKE_CURRENT_LIST_DIR}/cmake/AcquireZlib.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/cmake/AcquireBzip2.cmake")

add_library(osrs_cache STATIC
    src/audio.cpp
    src/collision.cpp
    src/configs.cpp
    src/definitions.cpp
    src/disassembler.cpp
    src/frames.cpp
    src/interfaces.cpp
    src/map.cpp
    src/model.cpp
    src/scripts.cpp
    src/skeletal.cpp
    src/sprites.cpp
    src/store.cpp
    src/worldmap.cpp
)
target_include_directories(osrs_cache PUBLIC include)
target_compile_features(osrs_cache PUBLIC cxx_std_20)
target_link_libraries(osrs_cache PRIVATE osrs_cache_zlib osrs_cache_bzip2)

option(OSRS_CACHE_BUILD_TESTS "Build osrs_cache_tests" ${PROJECT_IS_TOP_LEVEL})
set(OSRS_CACHE_DIR "" CACHE PATH "The copy of the game cache osrs_cache_tests reads")
if(OSRS_CACHE_BUILD_TESTS)
    enable_testing()
    include("${CMAKE_CURRENT_LIST_DIR}/cmake/AcquireDoctest.cmake")
    add_executable(osrs_cache_tests
        tests/main.cpp
        tests/audio_tests.cpp
        tests/cache_tests.cpp
        tests/configs_tests.cpp
        tests/definitions_tests.cpp
        tests/graphics_tests.cpp
        tests/interfaces_scripts_tests.cpp
        tests/storage_tests.cpp
        tests/worldmap_tests.cpp
    )
    target_link_libraries(osrs_cache_tests PRIVATE osrs_cache doctest::doctest)
    # The tests pass without running when the cache is missing.
    target_compile_definitions(osrs_cache_tests PRIVATE OSRS_CACHE_DIR="${OSRS_CACHE_DIR}")
    add_test(NAME osrs_cache_tests COMMAND osrs_cache_tests)
endif()

# The oracle: RuneLite's cache library decodes the same cache and every
# object is compared field by field (oracle/RuneLiteOracle.java). It needs
# Java 11 or later and fetches RuneLite's jars, so it's off by default.
option(OSRS_CACHE_ORACLE "Build osrs_cache_dump and the osrs_cache_oracle test" OFF)
if(OSRS_CACHE_ORACLE)
    find_package(Java 11 REQUIRED COMPONENTS Runtime)
    include("${CMAKE_CURRENT_LIST_DIR}/cmake/AcquireRuneLite.cmake")
    add_executable(osrs_cache_dump
        oracle/dump.cpp
        oracle/dump_audio.cpp
        oracle/dump_configs.cpp
        oracle/dump_definitions.cpp
        oracle/dump_graphics.cpp
        oracle/dump_interfaces.cpp
        oracle/dump_maps.cpp
        oracle/dump_scripts.cpp
        oracle/dump_storage.cpp
        oracle/dump_worldmap.cpp
    )
    target_link_libraries(osrs_cache_dump PRIVATE osrs_cache)
    enable_testing()
    add_test(NAME osrs_cache_oracle
        COMMAND "${Java_JAVA_EXECUTABLE}" -cp "${OSRS_CACHE_RUNELITE_CLASSPATH}"
                "${CMAKE_CURRENT_LIST_DIR}/oracle/RuneLiteOracle.java" "${OSRS_CACHE_DIR}" $<TARGET_FILE:osrs_cache_dump>)
    set_tests_properties(osrs_cache_oracle PROPERTIES TIMEOUT 3600)
endif()
