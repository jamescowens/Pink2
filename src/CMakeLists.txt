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

# The libraries every program links, as variables so that on macOS the daemon can
# take static archives of them instead (below). Elsewhere they are used as they
# are.
set(PINK2_LIBS_MINIUPNPC MiniUPnPc::MiniUPnPc)
set(PINK2_LIBS_BOOST Boost::filesystem Boost::program_options Boost::thread Boost::chrono)
set(PINK2_LIBS_OPENSSL OpenSSL::SSL OpenSSL::Crypto)

# macOS: a pink2d without Homebrew
# --------------------------------
#
# The release ships the macOS pink2d on its own, outside the app bundle
# (Pinkcoin-macOS-<arch>-<version>-pink2d.tar.xz), where nothing relocates its
# libraries. So it links the static archives of Boost, OpenSSL and miniupnpc,
# which Homebrew installs beside the dylibs, and loads only the OS's libraries,
# as 2.4.0.0's did (makefile.osx STATIC=1). Pinkcoin-Qt and test_pink2 keep the
# dylibs; macdeployqtplus bundles the GUI's.
#
# The libraries reach every program through pink2_core_defs, so the choice is
# made per consumer: each item below becomes $<IF:static,twin,shared>, where
# "static" is the consuming target's PINK2_STATIC_DEPS property (in a usage
# requirement, $<TARGET_PROPERTY:prop> reads the target being linked). Only
# pink2d sets it.
if(APPLE AND ENABLE_DAEMON)
    # pink2_static_twin(<out-var> <link item>): for a shared imported library, an
    # imported target for the .a beside its .dylib, with the same compile usage
    # requirements and its link interface mapped the same way (Boost::thread
    # brings Boost::atomic, chrono, container and date_time). Anything else
    # (Boost::headers, the static-only Boost::exception, Threads::Threads, flags)
    # comes back unchanged.
    function(pink2_static_twin out item)
        set(${out} "${item}" PARENT_SCOPE)
        string(REGEX REPLACE "^\\$<LINK_ONLY:(.+)>$" "\\1" _target "${item}")
        if(NOT TARGET "${_target}")
            return()
        endif()
        get_target_property(_aliased "${_target}" ALIASED_TARGET)
        if(_aliased)
            set(_target "${_aliased}")
        endif()
        string(REPLACE "::" "_" _twin "Pink2Static::${_target}")
        if(TARGET "${_twin}")
            set(${out} "${_twin}" PARENT_SCOPE)
            return()
        endif()
        get_target_property(_type "${_target}" TYPE)
        if(NOT _type MATCHES "^(SHARED|UNKNOWN)_LIBRARY$")
            return()
        endif()
        set(_location "")
        get_target_property(_configs "${_target}" IMPORTED_CONFIGURATIONS)
        if(NOT _configs)
            set(_configs "")
        endif()
        foreach(_suffix _RELEASE _NOCONFIG "" ${_configs})
            if(_suffix AND NOT _suffix MATCHES "^_")
                string(TOUPPER "_${_suffix}" _suffix)
            endif()
            get_target_property(_location "${_target}" IMPORTED_LOCATION${_suffix})
            if(_location)
                break()
            endif()
        endforeach()
        if(NOT _location MATCHES "\\.dylib$")
            return()
        endif()
        get_filename_component(_name "${_location}" NAME)
        string(REGEX REPLACE "(\\.[0-9]+)*\\.dylib$" ".a" _name "${_name}")
        get_filename_component(_dir "${_location}" DIRECTORY)
        get_filename_component(_real_dir "${_location}" REALPATH)
        get_filename_component(_real_dir "${_real_dir}" DIRECTORY)
        set(_archive "")
        foreach(_d "${_dir}" "${_real_dir}")
            if(EXISTS "${_d}/${_name}")
                set(_archive "${_d}/${_name}")
                break()
            endif()
        endforeach()
        if(NOT _archive)
            message(FATAL_ERROR "pink2d links static archives on macOS, since it ships "
                "outside the app bundle, and there is no ${_name} beside ${_location} "
                "(for ${_target}).")
        endif()
        add_library("${_twin}" STATIC IMPORTED)
        set_target_properties("${_twin}" PROPERTIES IMPORTED_LOCATION "${_archive}")
        foreach(_prop INTERFACE_INCLUDE_DIRECTORIES INTERFACE_SYSTEM_INCLUDE_DIRECTORIES
                      INTERFACE_COMPILE_DEFINITIONS INTERFACE_COMPILE_OPTIONS INTERFACE_LINK_OPTIONS)
            get_target_property(_value "${_target}" ${_prop})
            if(_value)
                set_property(TARGET "${_twin}" PROPERTY ${_prop} "${_value}")
            endif()
        endforeach()
        get_target_property(_deps "${_target}" INTERFACE_LINK_LIBRARIES)
        set(_twin_deps "")
        if(_deps)
            foreach(_dep IN LISTS _deps)
                pink2_static_twin(_dep_twin "${_dep}")
                list(APPEND _twin_deps "${_dep_twin}")
            endforeach()
        endif()
        set_property(TARGET "${_twin}" PROPERTY INTERFACE_LINK_LIBRARIES "${_twin_deps}")
        message(STATUS "pink2d: ${_target} -> ${_archive}")
        set(${out} "${_twin}" PARENT_SCOPE)
    endfunction()

    set(_pink2_static "$<BOOL:$<TARGET_PROPERTY:PINK2_STATIC_DEPS>>")
    set(_pink2_lib_vars PINK2_LIBS_BOOST PINK2_LIBS_OPENSSL)
    if(ENABLE_UPNP)
        list(APPEND _pink2_lib_vars PINK2_LIBS_MINIUPNPC)
    endif()
    foreach(_var IN LISTS _pink2_lib_vars)
        set(_items "")
        foreach(_lib IN LISTS ${_var})
            pink2_static_twin(_twin "${_lib}")
            list(APPEND _items "$<IF:${_pink2_static},${_twin},${_lib}>")
        endforeach()
        set(${_var} "${_items}")
    endforeach()
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
    target_link_libraries(pink2_core_defs INTERFACE ${PINK2_LIBS_MINIUPNPC})
endif()

target_link_libraries(pink2_core_defs INTERFACE
    ${LIBBDB_CXX}
    Boost::headers ${PINK2_LIBS_BOOST}
    ${PINK2_LIBS_OPENSSL}
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
    if(APPLE)
        # Static Boost, OpenSSL and miniupnpc; see "macOS: a pink2d without Homebrew".
        set_target_properties(pink2d PROPERTIES PINK2_STATIC_DEPS ON)
    endif()

    add_test(NAME pink2d_help
        COMMAND ${CMAKE_COMMAND}
            -DPINK2D=$<TARGET_FILE:pink2d>
            -DPINK2_VERSION=${PINK2_VERSION}
            -P "${CMAKE_SOURCE_DIR}/build-aux/cmake/tests/check_help.cmake")
endif()

if(ENABLE_TESTS)
    add_subdirectory(test)
endif()

# GUI
# ===

if(ENABLE_GUI)
    add_subdirectory(qt)
endif()
