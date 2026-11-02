# RuneLite's cache library and the jars it runs on, for the oracle
# (oracle/RuneLiteOracle.java): each jar is fetched, pinned by release and
# checksum, into the build tree's _deps. Sets OSRS_CACHE_RUNELITE_CLASSPATH
# to a classpath naming them all.
include_guard(GLOBAL)

set(_runelite_version "1.13.5")
set(_runelite_root "${CMAKE_BINARY_DIR}/_deps/runelite-cache-${_runelite_version}")
set(_maven "https://repo1.maven.org/maven2")
# url|sha256: the version RuneLite's pom names for each dependency.
set(_runelite_jars
    "https://repo.runelite.net/net/runelite/cache/${_runelite_version}/cache-${_runelite_version}.jar|c55d293c108800f892e3ca71e3f96da968325aa7014a4b155f3e748e695c93c9"
    "${_maven}/org/antlr/antlr4-runtime/4.13.1/antlr4-runtime-4.13.1.jar|54665d2838cc66458343468efc539e454fc95b46a8a04b13c6ac43fc9be63505"
    "${_maven}/com/google/guava/guava/23.2-jre/guava-23.2-jre.jar|5be9a7d05ba0ccd74708bc8018ae412255f85843c0b92302e9b9befa6ed52564"
    "${_maven}/org/slf4j/slf4j-api/1.7.25/slf4j-api-1.7.25.jar|18c4a0095d5c1da6b817592e767bb23d29dd2f560ad74df75ff3961dbde25b79"
    "${_maven}/org/slf4j/slf4j-simple/1.7.25/slf4j-simple-1.7.25.jar|0966e86fffa5be52d3d9e7b89dd674d98a03eed0a454fbaf7c1bd9493bd9d874"
    "${_maven}/org/apache/commons/commons-compress/1.10/commons-compress-1.10.jar|807c95293e41e8159477442077da6d0962a7f486d4b980be61f60a8db9cb290f"
    "${_maven}/com/google/code/gson/gson/2.8.5/gson-2.8.5.jar|233a0149fc365c9f6edbd683cfe266b19bdc773be98eabdaf6b3c924b48e7d81"
    "${_maven}/commons-cli/commons-cli/1.3.1/commons-cli-1.3.1.jar|3a2f057041aa6a8813f5b59b695f726c5e85014a703d208d7e1689098e92d8c0"
    "${_maven}/net/java/dev/jna/jna/5.9.0/jna-5.9.0.jar|eafcc780b445434d3c5ae7fa2fb6665de1a7560d537d2c408a8e80cd14d27161")

foreach(_entry IN LISTS _runelite_jars)
    string(REPLACE "|" ";" _entry "${_entry}")
    list(GET _entry 0 _url)
    list(GET _entry 1 _sha256)
    get_filename_component(_jar "${_url}" NAME)
    if(NOT EXISTS "${_runelite_root}/${_jar}")
        file(DOWNLOAD "${_url}" "${_runelite_root}/${_jar}.download"
            EXPECTED_HASH SHA256=${_sha256}
            TLS_VERIFY ON
            STATUS _status)
        list(GET _status 0 _code)
        if(NOT _code EQUAL 0)
            file(REMOVE "${_runelite_root}/${_jar}.download")
            message(FATAL_ERROR "Downloading ${_url} failed: ${_status}")
        endif()
        file(RENAME "${_runelite_root}/${_jar}.download" "${_runelite_root}/${_jar}")
    endif()
endforeach()

# Java expands a classpath entry ending in * to every jar in that directory.
set(OSRS_CACHE_RUNELITE_CLASSPATH "${_runelite_root}/*")
