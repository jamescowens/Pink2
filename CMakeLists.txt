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

project(Pinkcoin
    VERSION ${PINK2_VERSION}
    DESCRIPTION "Pinkcoin, a proof-of-stake cryptocurrency"
    LANGUAGES C CXX ASM
)


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


# Tests
# =====

enable_testing()

if(ENABLE_TESTS)
    find_package(Threads REQUIRED)
    add_executable(bdb48_smoke build-aux/cmake/tests/bdb48_smoke.cpp)
    target_link_libraries(bdb48_smoke PRIVATE ${LIBBDB_CXX} Threads::Threads)
    add_test(NAME bdb48_smoke COMMAND bdb48_smoke)
endif()
