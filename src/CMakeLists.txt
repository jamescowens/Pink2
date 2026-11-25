# The daemon, pink2d, and the core library everything else links.

# Every executable goes into <build>/bin.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")


# Sources
# =======

# The objects makefile.unix links into pink2d and test_pink2, less init.cpp and
# noui.cpp: those two belong to the daemon alone, and the GUI has its own
# versions. Absolute, so that src/qt and src/test can use the list as it is.
# Set here, ahead of every add_subdirectory.
set(PINK2_CORE_SOURCES
    alert.cpp
    version.cpp
    checkpoints.cpp
    netbase.cpp
    addrman.cpp
    crypter.cpp
    key.cpp
    db.cpp
    keystore.cpp
    miner.cpp
    main.cpp
    net.cpp
    ntp.cpp
    protocol.cpp
    bitcoinrpc.cpp
    rpcdump.cpp
    rpcnet.cpp
    rpcmining.cpp
    rpcwallet.cpp
    rpcblockchain.cpp
    rpcrawtransaction.cpp
    rpcsmessage.cpp
    script.cpp
    sync.cpp
    util.cpp
    wallet.cpp
    walletdb.cpp
    kernel.cpp
    stakeproofs.cpp
    pbkdf2.cpp
    scrypt.cpp
    stakedb.cpp
    smessage.cpp
    stealth.cpp
    txdb-leveldb.cpp
)

# scrypt-*.S: makefile.unix assembles all three, makefile.osx and
# makefile.linux-mingw the two x86 ones. Each file is guarded on its own
# architecture (and on OPTIMIZED_SALSA, which no makefile defines).
set(PINK2_ASM_SOURCES scrypt-x86.S scrypt-x86_64.S)
if(NOT APPLE AND NOT WIN32)
    list(APPEND PINK2_ASM_SOURCES scrypt-arm.S)
endif()

list(TRANSFORM PINK2_CORE_SOURCES PREPEND "${CMAKE_CURRENT_SOURCE_DIR}/")
list(TRANSFORM PINK2_ASM_SOURCES PREPEND "${CMAKE_CURRENT_SOURCE_DIR}/")
list(APPEND PINK2_CORE_SOURCES ${PINK2_ASM_SOURCES})


# Build information
# =================

# share/genbuild.sh writes obj/build.h, which version.cpp includes with
# HAVE_BUILD_INFO. It is run on every build and rewrites the file only when the
# commit changes, as the makefile's FORCE rule does. A tree with no .git, such
# as a `git archive`, gets "No build information available", and version.cpp then
# takes the commit from its export-subst lines. GIT_CEILING_DIRECTORIES keeps git
# from finding some repository the tree happens to be unpacked inside.
find_program(SH_EXE NAMES sh bash REQUIRED)
get_filename_component(PINK2_SOURCE_PARENT "${CMAKE_SOURCE_DIR}" DIRECTORY)
set(PINK2_BUILD_INFO_DIR "${CMAKE_BINARY_DIR}/obj")
add_custom_target(pink2_build_info
    COMMAND ${CMAKE_COMMAND} -E make_directory "${PINK2_BUILD_INFO_DIR}"
    COMMAND ${CMAKE_COMMAND} -E env "GIT_CEILING_DIRECTORIES=${PINK2_SOURCE_PARENT}"
            ${SH_EXE} "${CMAKE_SOURCE_DIR}/share/genbuild.sh" "${PINK2_BUILD_INFO_DIR}/build.h"
    BYPRODUCTS "${PINK2_BUILD_INFO_DIR}/build.h"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    COMMENT "Generating build info"
    VERBATIM
)


# Shared compile settings
# =======================

# The defines and include paths of the makefiles, carried on an INTERFACE target
# so that the GUI can compile the same sources with QT_GUI added.
add_library(pink2_core_defs INTERFACE)

target_include_directories(pink2_core_defs INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${PINK2_BUILD_INFO_DIR}"
)

target_compile_definitions(pink2_core_defs INTERFACE
    BOOST_SPIRIT_THREADSAFE
    HAVE_BUILD_INFO
)

if(WIN32)
    # makefile.linux-mingw: -D_MT -DWIN32 -D_WINDOWS -DBOOST_THREAD_USE_LIB;
    # pinkcoin-qt.pro adds BOOST_THREAD_PROVIDES_GENERIC_SHARED_MUTEX_ON_WIN.
    # No IS_ARCH_64: smessage.cpp would then print 64-bit values with "l", wrong
    # on LLP64.
    target_compile_definitions(pink2_core_defs INTERFACE
        _MT WIN32 _WINDOWS BOOST_THREAD_USE_LIB BOOST_THREAD_PROVIDES_GENERIC_SHARED_MUTEX_ON_WIN)
    target_compile_options(pink2_core_defs INTERFACE -mthreads)
else()
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        # makefile.unix passes it unconditionally; pinkcoin-qt.pro on macOS.
        target_compile_definitions(pink2_core_defs INTERFACE IS_ARCH_64)
    endif()
    if(APPLE)
        # makefile.osx
        target_compile_definitions(pink2_core_defs INTERFACE MAC_OSX MSG_NOSIGNAL=0)
    endif()
endif()

# makefile.unix's xCXXFLAGS (-O2 and -std=c++11 are set at the top level).
target_compile_options(pink2_core_defs INTERFACE
    -Wall -Wextra -Wno-ignored-qualifiers -Wformat -Wformat-security -Wno-unused-parameter)
if(CMAKE_SIZEOF_VOID_P EQUAL 4 AND CMAKE_SYSTEM_PROCESSOR MATCHES "^i[3-6]86$")
    target_compile_options(pink2_core_defs INTERFACE -msse2)  # EXT_OPTIONS for i686
endif()

if(ENABLE_UPNP)
    # makefile.unix's USE_UPNP: 1 when UPnP is on at startup, 0 when it is
    # compiled in and off by default. ENABLE_UPNP=OFF defines nothing.
    if(DEFAULT_UPNP)
        target_compile_definitions(pink2_core_defs INTERFACE USE_UPNP=1)
    else()
        target_compile_definitions(pink2_core_defs INTERFACE USE_UPNP=0)
    endif()
    if(WIN32)
        # STATICLIB: makefile.linux-mingw. MINIUPNP_STATICLIB is the name
        # miniupnpc 2.x reads for the same thing.
        target_compile_definitions(pink2_core_defs INTERFACE STATICLIB MINIUPNP_STATICLIB)
    endif()
    target_link_libraries(pink2_core_defs INTERFACE MiniUPnPc::MiniUPnPc)
endif()

target_link_libraries(pink2_core_defs INTERFACE
    ${LIBBDB_CXX}
    Boost::headers Boost::filesystem Boost::program_options Boost::thread Boost::chrono
    OpenSSL::SSL OpenSSL::Crypto
    ZLIB::ZLIB
    Threads::Threads
)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    # -l rt -l dl, as makefile.unix
    find_library(PINK2_RT_LIBRARY rt)
    target_link_libraries(pink2_core_defs INTERFACE ${PINK2_RT_LIBRARY} ${CMAKE_DL_LIBS})
endif()
if(WIN32)
    target_link_libraries(pink2_core_defs INTERFACE ws2_32 mswsock shlwapi advapi32)
endif()


# leveldb and memenv
# ==================

add_subdirectory(leveldb)

# txdb-leveldb.cpp includes <leveldb/...> and <memenv.h>.
target_link_libraries(pink2_core_defs INTERFACE memenv leveldb)


# Core library
# ============

# Without QT_GUI, as the daemon is built.
add_library(pink2_core STATIC ${PINK2_CORE_SOURCES})
target_link_libraries(pink2_core PUBLIC pink2_core_defs)
add_dependencies(pink2_core pink2_build_info)


# Daemon
# ======

if(ENABLE_DAEMON)
    add_executable(pink2d init.cpp noui.cpp)
    target_link_libraries(pink2d PRIVATE pink2_core ${PINK2_RUNTIME_LIBS})
    set_target_properties(pink2d PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

    add_test(NAME pink2d_help
        COMMAND ${CMAKE_COMMAND}
            -DPINK2D=$<TARGET_FILE:pink2d>
            -DPINK2_VERSION=${PINK2_VERSION}
            -P "${CMAKE_SOURCE_DIR}/build-aux/cmake/tests/check_help.cmake")
endif()
