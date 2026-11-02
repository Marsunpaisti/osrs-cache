# zlib, for the gzip groups of the game cache (src/store.cpp). The release
# tarball is fetched, pinned by release and checksum, into the build tree's
# _deps, and only its inflate sources are compiled.
include_guard(GLOBAL)

set(_zlib_version "1.3.1")
set(_zlib_sha256 "9a93b2b7dfdac77ceba5a558a580e74667dd6fede4585b91eefb60f03b72df23")
set(_zlib_root "${CMAKE_BINARY_DIR}/_deps/zlib-${_zlib_version}")

if(NOT EXISTS "${_zlib_root}/inflate.c")
    set(_zlib_archive "${CMAKE_BINARY_DIR}/_deps/zlib-${_zlib_version}.tar.gz")
    file(DOWNLOAD
        "https://github.com/madler/zlib/releases/download/v${_zlib_version}/zlib-${_zlib_version}.tar.gz"
        "${_zlib_archive}"
        EXPECTED_HASH SHA256=${_zlib_sha256}
        TLS_VERIFY ON
        STATUS _zlib_status)
    list(GET _zlib_status 0 _zlib_code)
    if(NOT _zlib_code EQUAL 0)
        file(REMOVE "${_zlib_archive}")
        message(FATAL_ERROR "Downloading zlib ${_zlib_version} failed: ${_zlib_status}")
    endif()
    file(ARCHIVE_EXTRACT INPUT "${_zlib_archive}" DESTINATION "${CMAKE_BINARY_DIR}/_deps")
    file(REMOVE "${_zlib_archive}")
endif()

if(NOT TARGET osrs_cache_zlib)
    add_library(osrs_cache_zlib STATIC
        "${_zlib_root}/adler32.c"
        "${_zlib_root}/crc32.c"
        "${_zlib_root}/inffast.c"
        "${_zlib_root}/inflate.c"
        "${_zlib_root}/inftrees.c"
        "${_zlib_root}/zutil.c")
    target_include_directories(osrs_cache_zlib PUBLIC "${_zlib_root}")
endif()
