# CMake build for Pinkcoin (Pink2), modeled on Gridcoin's.

cmake_minimum_required(VERSION 3.21)
set(CMAKE_MODULE_PATH "${CMAKE_MODULE_PATH}" "${CMAKE_CURRENT_SOURCE_DIR}/build-aux/cmake")

# Version, from the one place the source keeps it.
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/src/clientversion.h" _client_version_lines
    REGEX "^#define CLIENT_VERSION_(MAJOR|MINOR|REVISION|BUILD)[ \t]+[0-9]+")
foreach(_part MAJOR MINOR REVISION BUILD)
    set(CLIENT_VERSION_${_part} "")
    foreach(_line IN LISTS _client_version_lines)
        if(_line MATCHES "^#define CLIENT_VERSION_${_part}[ \t]+([0-9]+)")
            set(CLIENT_VERSION_${_part} "${CMAKE_MATCH_1}")
        endif()
    endforeach()
    if(CLIENT_VERSION_${_part} STREQUAL "")
        message(FATAL_ERROR "CLIENT_VERSION_${_part} not found in src/clientversion.h")
    endif()
endforeach()
set(PINK2_VERSION "${CLIENT_VERSION_MAJOR}.${CLIENT_VERSION_MINOR}.${CLIENT_VERSION_REVISION}.${CLIENT_VERSION_BUILD}")

# The macOS floor
# ===============

# Set before project(), which is when CMake reads it.
#
# share/qt/Info.plist.cmake.in substitutes it into LSMinimumSystemVersion, so
# without a default here every build that does not pass
# -DCMAKE_OSX_DEPLOYMENT_TARGET would stamp the bundle with an empty minimum.
# The Intel CI job uses this default; the Apple Silicon job passes 15.0.
#
# FORCE is required, not tidiness. On macOS, CMake's own
# Platform/Darwin-Initialize.cmake unconditionally caches this variable (empty
# when MACOSX_DEPLOYMENT_TARGET is unset), so a plain `set(... CACHE ...)` is a
# no-op whenever an entry exists. An explicitly empty -DCMAKE_OSX_DEPLOYMENT_TARGET=
# is covered by the same FORCE. An explicit non-empty -D still wins, because then
# the guard fails.
#
# The toolchain guard keeps this out of depends cross-builds, which declare their
# own floor and bake it into their compile and link flags; applying this default
# there would put two contradictory version-min specs on the same lines.
#
# 14.0 is the floor of the Intel dependency stack. The Apple Silicon job declares
# 15.0 (-DCMAKE_OSX_DEPLOYMENT_TARGET=15.0).
#
# The default is never above the Mac doing the build: built for 14.0 on macOS 12,
# a bundle would not launch on the machine that built it. A Mac older than macOS
# 14 gets its own version (the major version from macOS 11 on, 10.x before), with
# a warning. CMAKE_HOST_APPLE is set this early but CMAKE_HOST_SYSTEM_VERSION is
# not, hence sw_vers. A MACOSX_DEPLOYMENT_TARGET in the environment is used as
# given, ahead of this default; only an explicit -D comes before it.
if(NOT CMAKE_TOOLCHAIN_FILE AND NOT CMAKE_OSX_DEPLOYMENT_TARGET)
    set(_macos_floor "14.0")
    if(NOT "$ENV{MACOSX_DEPLOYMENT_TARGET}" STREQUAL "")
        set(_macos_floor "$ENV{MACOSX_DEPLOYMENT_TARGET}")
    elseif(CMAKE_HOST_APPLE)
        execute_process(COMMAND sw_vers -productVersion
            OUTPUT_VARIABLE _macos_host_version
            OUTPUT_STRIP_TRAILING_WHITESPACE
            RESULT_VARIABLE _macos_sw_vers_status
            ERROR_QUIET)
        if(_macos_sw_vers_status EQUAL 0 AND _macos_host_version MATCHES "^(10\\.[0-9]+|[0-9]+)")
            set(_macos_host_floor "${CMAKE_MATCH_1}")
            if(NOT _macos_host_floor MATCHES "\\.")
                string(APPEND _macos_host_floor ".0")
            endif()
            if(_macos_host_floor VERSION_LESS _macos_floor)
                set(_macos_floor "${_macos_host_floor}")
                message(WARNING "This Mac runs macOS ${_macos_host_version}, so the build targets "
                    "macOS ${_macos_floor} instead of 14.0, and the app will need macOS "
                    "${_macos_floor} or newer. Releases require macOS 14 (Intel) or 15 (Apple "
                    "Silicon). Pass -DCMAKE_OSX_DEPLOYMENT_TARGET=<version>, or set "
                    "MACOSX_DEPLOYMENT_TARGET, to choose the target yourself.")
            endif()
        endif()
    endif()
    set(CMAKE_OSX_DEPLOYMENT_TARGET "${_macos_floor}" CACHE STRING "Minimum macOS version (ignored off Apple platforms)" FORCE)
    unset(_macos_floor)
    unset(_macos_host_version)
    unset(_macos_host_floor)
    unset(_macos_sw_vers_status)
endif()

project(Pinkcoin
    VERSION ${PINK2_VERSION}
    DESCRIPTION "Pinkcoin, a proof-of-stake cryptocurrency"
    LANGUAGES C CXX ASM
)

# The GUI's icon and version resource, src/qt/res/bitcoin-qt.rc (the .pro's
# RC_FILE). CMake's MinGW platform files enable RC already; this states it, at
# the top level, where enable_language() has to be.
if(WIN32)
    enable_language(RC)
endif()


# Toolchain configuration
# =======================

# The makefiles pass -std=c++11.
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# The makefiles use -O2, without -DNDEBUG (asserts stay live), where CMake's
# Release default is -O3 -DNDEBUG. Berkeley DB takes its optimisation from here.
set(CMAKE_C_FLAGS_RELEASE "-O2")
set(CMAKE_CXX_FLAGS_RELEASE "-O2")
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()


# Define options
# ==============

option(ENABLE_DAEMON    "Enable daemon" ON)
option(ENABLE_GUI       "Enable Qt-based GUI" OFF)
option(ENABLE_TESTS     "Build tests" OFF)
option(ENABLE_UPNP      "Enable UPnP port mapping support" ON)
option(DEFAULT_UPNP     "Turn UPnP on startup" OFF)
option(ENABLE_QRENCODE  "Enable generation of QR Codes for receiving payments" OFF)
option(USE_DBUS         "Enable DBus support" OFF)
option(USE_QT6          "Use Qt 6 instead of Qt 5" OFF)
option(STATIC_LIBS      "Prefer static variants of system libraries" ${WIN32})
option(STATIC_RUNTIME   "Link runtime statically" ${WIN32})
option(ENABLE_PIE       "Build position-independent executables" OFF)
option(ENABLE_HARDENING "Apply compiler and linker hardening flags" ON)
option(SYSTEM_BDB       "Find system installation of Berkeley DB CXX 4.8" OFF)

if(USE_QT6)
    message(FATAL_ERROR "Qt 6 is a separate PR")
endif()


# Berkeley DB
# ===========

# 4.8, the wallet format of every Pinkcoin release.
if(SYSTEM_BDB)
    find_package(BerkeleyDB 4.8...<4.9 COMPONENTS CXX REQUIRED)
    set(LIBBDB_CXX BerkeleyDB::CXX)
else()
    find_program(SH_EXE NAMES sh bash REQUIRED)
    find_program(MAKE_EXE NAMES gmake nmake make REQUIRED)
    add_subdirectory(src/bdb48)
    add_library(BerkeleyDB::CXX ALIAS libdb_cxx)
    set(LIBBDB_CXX libdb_cxx)
endif()


# Dependencies
# ============

include(CheckCXXCompilerFlag)
include(CheckLinkerFlag)
include(CheckPIESupported)

# Static linking must be set up before the dependencies are searched for. The
# platform's own suffix list is kept for the Qt search below.
set(PINK2_DEFAULT_FIND_LIBRARY_SUFFIXES "${CMAKE_FIND_LIBRARY_SUFFIXES}")
if(STATIC_LIBS)
    # The programs ship from the build tree. CMake's build RPATH would point
    # them at the build machine's library directories (on Linux, depends' lib,
    # for the X11 libraries the GUI links shared there and takes from the
    # system at run time).
    set(CMAKE_SKIP_BUILD_RPATH ON)
    set(CMAKE_LINK_SEARCH_START_STATIC ON)
    set(CMAKE_FIND_LIBRARY_SUFFIXES "${CMAKE_STATIC_LIBRARY_SUFFIX}")
    set(Boost_USE_STATIC_LIBS ON)
    set(OPENSSL_USE_STATIC_LIBS TRUE)
    set(ZLIB_USE_STATIC_LIBS ON)
endif()

if(STATIC_RUNTIME AND CMAKE_CXX_COMPILER_ID MATCHES "^(GNU|Clang)$")
    set(PINK2_RUNTIME_LIBS -static-libgcc -static-libstdc++)
endif()

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

# CMake 3.30 deprecates the module-mode FindBoost; it is only used for Boost
# older than 1.70.
if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.30)
    cmake_policy(SET CMP0167 OLD)
endif()
if(NOT DEFINED Boost_USE_DEBUG_RUNTIME)
    set(Boost_USE_DEBUG_RUNTIME OFF)
endif()

# The components the makefiles link. system is header-only from Boost 1.69, and
# Boost 1.90 no longer builds the library.
set(BOOST_MINIMUM_VERSION 1.66.0)
set(BOOST_COMPONENTS filesystem program_options thread chrono)
find_package(Boost ${BOOST_MINIMUM_VERSION} REQUIRED)
if(Boost_VERSION VERSION_LESS 1.69.0)
    list(APPEND BOOST_COMPONENTS system)
endif()
if(Boost_VERSION VERSION_LESS 1.70.0)
    find_package(Boost ${BOOST_MINIMUM_VERSION} COMPONENTS ${BOOST_COMPONENTS} REQUIRED)
else()
    find_package(Boost ${BOOST_MINIMUM_VERSION} COMPONENTS ${BOOST_COMPONENTS} CONFIG REQUIRED)
endif()

# Boost's CMake package puts BOOST_<LIB>_DYN_LINK and BOOST_<LIB>_NO_LIB on its
# targets. The makefiles define neither, and they only matter to the Windows
# DLL and auto-link machinery, so they are taken off to keep the define set the
# same as the old build's (contrib/devtools/compare-defines.sh checks that).
function(pink2_strip_boost_link_defines)
    set(_todo ${ARGN})
    set(_seen "")
    while(_todo)
        list(POP_FRONT _todo _t)
        if(NOT TARGET ${_t} OR ${_t} IN_LIST _seen)
            continue()
        endif()
        list(APPEND _seen ${_t})
        get_target_property(_defs ${_t} INTERFACE_COMPILE_DEFINITIONS)
        if(_defs)
            list(FILTER _defs EXCLUDE REGEX "^BOOST_[A-Z0-9_]+_(DYN_LINK|NO_LIB)$")
            set_property(TARGET ${_t} PROPERTY INTERFACE_COMPILE_DEFINITIONS "${_defs}")
        endif()
        get_target_property(_libs ${_t} INTERFACE_LINK_LIBRARIES)
        if(_libs)
            foreach(_l IN LISTS _libs)
                if(_l MATCHES "^Boost::")
                    list(APPEND _todo ${_l})
                endif()
            endforeach()
        endif()
    endwhile()
endfunction()
set(_boost_targets Boost::headers)
foreach(_c IN LISTS BOOST_COMPONENTS)
    list(APPEND _boost_targets Boost::${_c})
endforeach()
pink2_strip_boost_link_defines(${_boost_targets})

find_package(OpenSSL REQUIRED)
find_package(ZLIB REQUIRED)

if(ENABLE_UPNP)
    find_package(MiniUPnPc REQUIRED)
endif()

# Only the GUI draws QR codes.
if(ENABLE_GUI AND ENABLE_QRENCODE)
    find_package(QREncode REQUIRED)
endif()

# Qt 5, with the modules pinkcoin-qt.pro uses (widgets network svg, and dbus
# with USE_DBUS). LinguistTools provides lrelease for the translations.
if(ENABLE_GUI)
    set(PINK2_QT_COMPONENTS Widgets Network Svg LinguistTools)
    if(USE_DBUS)
        list(APPEND PINK2_QT_COMPONENTS DBus)
    endif()

    # A static Qt (depends) lists its own dependencies in .prl files, and its
    # CMake package find_library()s every -l in them: the system's libm, the
    # MinGW import libraries, and on Linux depends' X11 libraries, which depends
    # builds shared. Two settings would hide some of those, so for this search
    # only, as Bitcoin Core v29's FindQt.cmake does with the first:
    #  - the depends toolchain's CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY confines
    #    find_library() to depends ("Library not found: m"); BOTH still searches
    #    depends first;
    #  - STATIC_LIBS' static-only suffix list would skip libxcb.so and
    #    libfontconfig.so, and resolve -lm to the system's static libm.a.
    # Both are restored afterwards, so every other package is still found in
    # depends only, and statically.
    set(_qt_saved_root_mode_defined FALSE)
    if(DEFINED CMAKE_FIND_ROOT_PATH_MODE_LIBRARY)
        set(_qt_saved_root_mode_defined TRUE)
        set(_qt_saved_root_mode "${CMAKE_FIND_ROOT_PATH_MODE_LIBRARY}")
    endif()
    set(_qt_saved_suffixes "${CMAKE_FIND_LIBRARY_SUFFIXES}")
    set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY BOTH)
    set(CMAKE_FIND_LIBRARY_SUFFIXES "${PINK2_DEFAULT_FIND_LIBRARY_SUFFIXES}")

    find_package(Qt5 5.12 COMPONENTS ${PINK2_QT_COMPONENTS} REQUIRED)

    set(CMAKE_FIND_LIBRARY_SUFFIXES "${_qt_saved_suffixes}")
    if(_qt_saved_root_mode_defined)
        set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY "${_qt_saved_root_mode}")
    else()
        unset(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY)
    endif()
    unset(_qt_saved_root_mode_defined)
    unset(_qt_saved_root_mode)
    unset(_qt_saved_suffixes)
endif()


# Hardening and PIE
# =================

# makefile.unix's HARDENING is -fstack-protector-all -Wstack-protector
# -D_FORTIFY_SOURCE=2 (with the PIE flags when asked), and -Wl,-z,relro -Wl,-z,now
# at link time. This set is Gridcoin's, which contains all of those, probed before
# use; it adds the CET and branch-protection flags and noexecstack/separate-code.
#
# check_pie_supported() has to run for CMake to honour POSITION_INDEPENDENT_CODE on
# executables in both directions: ENABLE_PIE=OFF then emits -no-pie, and does not
# leave the toolchain's default.
#
# Except on Apple, where ENABLE_PIE=OFF leaves POSITION_INDEPENDENT_CODE unset, so
# CMake adds neither flag and the linker's default, PIE, stands. Apple's ld
# deprecates -no_pie but on x86_64 still honours it, which takes ASLR away from
# the executables. makefile.osx passes no PIE flag, and 2.4.0.0's macOS binaries
# are PIE.
check_pie_supported()
if(ENABLE_PIE AND NOT CMAKE_CXX_LINK_PIE_SUPPORTED)
    message(FATAL_ERROR "PIE is not supported by the current linker")
endif()
if(ENABLE_PIE OR NOT APPLE)
    set(CMAKE_POSITION_INDEPENDENT_CODE ${ENABLE_PIE})
endif()

if(ENABLE_HARDENING)
    string(FIND "${CMAKE_CXX_FLAGS}" "-fsanitize" _hardening_sanitizer_pos)
    if(_hardening_sanitizer_pos EQUAL -1)
        # _FORTIFY_SOURCE needs optimisation. A value already in
        # CMAKE_<LANG>_FLAGS is the packager's, and stands for that language.
        if(CMAKE_BUILD_TYPE OR CMAKE_CONFIGURATION_TYPES)
            set(_hardening_fortify_when "$<NOT:$<CONFIG:Debug>>")
            foreach(_hardening_lang C CXX)
                string(FIND "${CMAKE_${_hardening_lang}_FLAGS}" "_FORTIFY_SOURCE" _hardening_fortify_pos)
                if(NOT _hardening_fortify_pos EQUAL -1)
                    string(APPEND _hardening_fortify_when ",$<NOT:$<COMPILE_LANGUAGE:${_hardening_lang}>>")
                    message(STATUS "Hardening: _FORTIFY_SOURCE left as CMAKE_${_hardening_lang}_FLAGS set it")
                endif()
            endforeach()
            add_compile_options(
                "$<$<AND:${_hardening_fortify_when}>:-U_FORTIFY_SOURCE>"
                "$<$<AND:${_hardening_fortify_when}>:-D_FORTIFY_SOURCE=2>")
        else()
            message(STATUS "Hardening: _FORTIFY_SOURCE skipped (no build type, so no optimisation)")
        endif()
    else()
        message(STATUS "Hardening: _FORTIFY_SOURCE skipped (sanitizer build)")
    endif()

    set(hardening_flags -fstack-protector-all -Wstack-protector -fcf-protection=full)
    # Withheld from Mach-O as Gridcoin does: -mbranch-protection=standard breaks
    # unwinding on plain arm64, and -fstack-clash-protection is ignored there.
    if(NOT APPLE)
        list(APPEND hardening_flags -fstack-clash-protection -mbranch-protection=standard)
    endif()

    # Make a probe reject a flag the driver accepts and then ignores.
    check_cxx_compiler_flag("-Werror=unused-command-line-argument" HARDENING_HAVE_WERROR_UNUSED_ARG)
    set(_hardening_saved_required_flags "${CMAKE_REQUIRED_FLAGS}")
    if(HARDENING_HAVE_WERROR_UNUSED_ARG)
        string(APPEND CMAKE_REQUIRED_FLAGS " -Werror=unused-command-line-argument")
    endif()
    foreach(flag ${hardening_flags})
        string(MAKE_C_IDENTIFIER "HARDENING_CXX${flag}" flag_var)
        check_cxx_compiler_flag("${flag}" ${flag_var})
        if(${flag_var})
            add_compile_options("${flag}")
        else()
            message(STATUS "Hardening: ${flag} not applied (not supported on this target)")
        endif()
    endforeach()
    set(CMAKE_REQUIRED_FLAGS "${_hardening_saved_required_flags}")
    unset(_hardening_saved_required_flags)

    foreach(flag -Wl,-z,relro -Wl,-z,now -Wl,-z,noexecstack -Wl,-z,separate-code)
        string(MAKE_C_IDENTIFIER "HARDENING_LD${flag}" flag_var)
        check_linker_flag(CXX "${flag}" ${flag_var})
        if(${flag_var})
            add_link_options("${flag}")
        endif()
    endforeach()
endif()


# Sources
# =======

# Before add_subdirectory, so the tests registered there count.
enable_testing()

add_subdirectory(src)


# Tests
# =====

if(ENABLE_TESTS)
    find_package(Threads REQUIRED)
    add_executable(bdb48_smoke build-aux/cmake/tests/bdb48_smoke.cpp)
    target_link_libraries(bdb48_smoke PRIVATE ${LIBBDB_CXX} Threads::Threads)
    add_test(NAME bdb48_smoke COMMAND bdb48_smoke)
endif()
