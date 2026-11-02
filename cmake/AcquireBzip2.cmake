# bzip2, for the bzip2 groups of the game cache (src/store.cpp). The release
# tarball is fetched, pinned by release and checksum, into the build tree's _deps.
include_guard(GLOBAL)

set(_bzip2_version "1.0.8")
set(_bzip2_sha256 "ab5a03176ee106d3f0fa90e381da478ddae405918153cca248e682cd0c4a2269")
set(_bzip2_root "${CMAKE_BINARY_DIR}/_deps/bzip2-${_bzip2_version}")

if(NOT EXISTS "${_bzip2_root}/bzlib.c")
    set(_bzip2_archive "${CMAKE_BINARY_DIR}/_deps/bzip2-${_bzip2_version}.tar.gz")
    file(DOWNLOAD
        "https://sourceware.org/pub/bzip2/bzip2-${_bzip2_version}.tar.gz"
        "${_bzip2_archive}"
        EXPECTED_HASH SHA256=${_bzip2_sha256}
        TLS_VERIFY ON
        STATUS _bzip2_status)
    list(GET _bzip2_status 0 _bzip2_code)
    if(NOT _bzip2_code EQUAL 0)
        file(REMOVE "${_bzip2_archive}")
        message(FATAL_ERROR "Downloading bzip2 ${_bzip2_version} failed: ${_bzip2_status}")
    endif()
    file(ARCHIVE_EXTRACT INPUT "${_bzip2_archive}" DESTINATION "${CMAKE_BINARY_DIR}/_deps")
    file(REMOVE "${_bzip2_archive}")
endif()

if(NOT TARGET osrs_cache_bzip2)
    # bzlib.c holds the compressor too, so its sources come along.
    add_library(osrs_cache_bzip2 STATIC
        "${_bzip2_root}/blocksort.c"
        "${_bzip2_root}/bzlib.c"
        "${_bzip2_root}/compress.c"
        "${_bzip2_root}/crctable.c"
        "${_bzip2_root}/decompress.c"
        "${_bzip2_root}/huffman.c"
        "${_bzip2_root}/randtable.c")
    target_include_directories(osrs_cache_bzip2 PUBLIC "${_bzip2_root}")
endif()
