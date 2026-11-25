# LevelDB 1.15, embedded. Builds libleveldb and libmemenv the way
# `make libleveldb.a libmemenv.a` does: the source list is what
# build_detect_platform finds (db/, util/ and table/, less tests, benchmarks and
# leveldb_main.cc) plus the platform's port file, and the platform flags are
# the ones build_detect_platform writes for that OS.

set(LEVELDB_SOURCES
    db/builder.cc
    db/c.cc
    db/dbformat.cc
    db/db_impl.cc
    db/db_iter.cc
    db/filename.cc
    db/log_reader.cc
    db/log_writer.cc
    db/memtable.cc
    db/repair.cc
    db/table_cache.cc
    db/version_edit.cc
    db/version_set.cc
    db/write_batch.cc
    table/block_builder.cc
    table/block.cc
    table/filter_block.cc
    table/format.cc
    table/iterator.cc
    table/merger.cc
    table/table_builder.cc
    table/table.cc
    table/two_level_iterator.cc
    util/arena.cc
    util/bloom.cc
    util/cache.cc
    util/coding.cc
    util/comparator.cc
    util/crc32c.cc
    util/env.cc
    # Both Env implementations are always compiled, as the old build does; each
    # one guards its own body on LEVELDB_PLATFORM_WINDOWS.
    util/env_posix.cc
    util/env_win.cc
    util/filter_policy.cc
    util/hash.cc
    util/histogram.cc
    util/logging.cc
    util/options.cc
    util/status.cc
)

if(WIN32)
    list(APPEND LEVELDB_SOURCES port/port_win.cc)
else()
    list(APPEND LEVELDB_SOURCES port/port_posix.cc)
endif()

add_library(leveldb STATIC ${LEVELDB_SOURCES})
add_library(memenv STATIC helpers/memenv/memenv.cc)

foreach(_target leveldb memenv)
    target_include_directories(${_target}
        PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}/include
            ${CMAKE_CURRENT_SOURCE_DIR}/helpers
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}
    )

    # The Makefile's default OPT is "-O2 -DNDEBUG", whatever the project's own
    # build type is.
    target_compile_definitions(${_target} PRIVATE NDEBUG)

    # build_detect_platform's COMMON_FLAGS, per OS. Its -std=c++0x <cstdatomic>
    # probe fails on every compiler in use, so LEVELDB_CSTDATOMIC_PRESENT and
    # -std=c++0x are never set. It links tcmalloc if the system has it; that is
    # left out so the build does not depend on what is installed.
    if(WIN32)
        # OS_WINDOWS_CROSSCOMPILE / NATIVE_WINDOWS
        target_compile_definitions(${_target} PRIVATE
            _REENTRANT OS_WINDOWS LEVELDB_PLATFORM_WINDOWS WINVER=0x0500 __USE_MINGW_ANSI_STDIO=1)
    elseif(APPLE)
        # Darwin
        target_compile_definitions(${_target} PRIVATE OS_MACOSX LEVELDB_PLATFORM_POSIX)
    else()
        # Linux
        target_compile_definitions(${_target} PRIVATE OS_LINUX LEVELDB_PLATFORM_POSIX)
        target_compile_options(${_target} PRIVATE -pthread)
    endif()

    # MEMCMP_FLAG (and the Windows set, unconditionally): use libc's memcmp.
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR WIN32)
        target_compile_options(${_target} PRIVATE -fno-builtin-memcmp)
    endif()

    # Vendored code; the old build does not turn warnings on for it.
    target_compile_options(${_target} PRIVATE -w)
endforeach()

if(WIN32)
    target_link_libraries(leveldb PUBLIC shlwapi)
else()
    target_link_libraries(leveldb PUBLIC Threads::Threads)
endif()
target_link_libraries(memenv PUBLIC leveldb)
