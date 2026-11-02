# doctest, the single-header test framework for osrs_cache_tests, unless the
# including project already has a doctest::doctest. Only the header is
# fetched, pinned by release and checksum, into the build tree's _deps.
include_guard(GLOBAL)

set(_doctest_version "2.4.12")
set(_doctest_sha256 "94029a7d32da24a56249658147dbd2b33ff0b9ed665295cbbaf19aafff5b0ced")
set(_doctest_root "${CMAKE_BINARY_DIR}/_deps/doctest-${_doctest_version}")
set(_doctest_header "${_doctest_root}/doctest/doctest.h")

if(NOT EXISTS "${_doctest_header}")
    set(_doctest_partial "${_doctest_header}.download")
    file(DOWNLOAD
        "https://raw.githubusercontent.com/doctest/doctest/v${_doctest_version}/doctest/doctest.h"
        "${_doctest_partial}"
        EXPECTED_HASH SHA256=${_doctest_sha256}
        TLS_VERIFY ON
        STATUS _doctest_status)
    list(GET _doctest_status 0 _doctest_code)
    if(NOT _doctest_code EQUAL 0)
        file(REMOVE "${_doctest_partial}")
        message(FATAL_ERROR "Downloading doctest ${_doctest_version} failed: ${_doctest_status}")
    endif()
    file(RENAME "${_doctest_partial}" "${_doctest_header}")
endif()

if(NOT TARGET doctest::doctest)
    add_library(osrs_cache_doctest INTERFACE)
    target_include_directories(osrs_cache_doctest INTERFACE "${_doctest_root}")
    # Include the standard headers instead of forward-declaring std types,
    # which current MSVC rejects (warning C5285).
    target_compile_definitions(osrs_cache_doctest INTERFACE DOCTEST_CONFIG_USE_STD_HEADERS)
    add_library(doctest::doctest ALIAS osrs_cache_doctest)
endif()
