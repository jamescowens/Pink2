# Copyright (c) 2024 The Pinkcoin developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

#[=======================================================================[.rst:
FindMiniUPnPc
-------------

Find the miniupnpc library for UPnP support.

Imported Targets
^^^^^^^^^^^^^^^^

This module provides the following imported targets, if found:

``MiniUPnPc::MiniUPnPc``
  The miniupnpc library

Result Variables
^^^^^^^^^^^^^^^^

This module will set the following variables in your project:

``MiniUPnPc_FOUND``
  True if miniupnpc was found.
``MiniUPnPc_INCLUDE_DIRS``
  Include directories for miniupnpc.
``MiniUPnPc_LIBRARIES``
  Libraries to link against.
``MiniUPnPc_VERSION``
  Version of miniupnpc found.

#]=======================================================================]

include(FindPackageHandleStandardArgs)

# Allow user to specify location
set(MINIUPNPC_ROOT "$ENV{MINIUPNPC_ROOT}" CACHE PATH "miniupnpc installation root")

# Search paths
set(_MINIUPNPC_SEARCH_PATHS
    ${MINIUPNPC_ROOT}
    /usr/local
    /usr
    /opt/local
)

# miniupnpc's own CMake package first, where one is installed: its CMake build
# installs it, so depends has one (Homebrew's keg has none, only a .pc). It
# knows the library's file name, which the search below has to guess: on
# Windows its static archive is OUTPUT_NAME libminiupnpc, so liblibminiupnpc.a.
#
# Only the file and the include directory are taken from it, into the target
# below. Its own target carries MINIUPNP_STATICLIB on every platform, which
# makefile.unix never defines (the Windows build adds it in src/CMakeLists.txt),
# and the macOS pink2d needs an UNKNOWN target with a location to find its
# static twin by. pkg-config is not used for the same reason: its imported
# target is an INTERFACE library with no location.
#
# MINIUPNPC_USE_STATIC_LIBS is left as the caller set it. Unset, the package
# loads whichever export it has (the shared one if both), and never fails: set
# with no static export installed (openSUSE ships the shared one only), its
# miniupnpc-config.cmake includes a file that does not exist. A result of the
# wrong kind for STATIC_LIBS is not taken; the search below then decides.
if(NOT MiniUPnPc_LIBRARY OR NOT MiniUPnPc_INCLUDE_DIR)
    find_package(miniupnpc CONFIG QUIET HINTS ${MINIUPNPC_ROOT})

    set(_miniupnpc_target miniupnpc::miniupnpc)
    if(TARGET ${_miniupnpc_target})
        get_target_property(_miniupnpc_aliased ${_miniupnpc_target} ALIASED_TARGET)
        if(_miniupnpc_aliased)
            set(_miniupnpc_target ${_miniupnpc_aliased})
        endif()
        get_target_property(_miniupnpc_type ${_miniupnpc_target} TYPE)

        # The file to link: the archive, or for a shared library its import
        # library where there is one (Windows), else the library itself.
        set(_miniupnpc_location "")
        if(_miniupnpc_type MATCHES "^(STATIC|SHARED|UNKNOWN)_LIBRARY$")
            get_target_property(_miniupnpc_configs ${_miniupnpc_target} IMPORTED_CONFIGURATIONS)
            if(NOT _miniupnpc_configs)
                set(_miniupnpc_configs "")
            endif()
            foreach(_miniupnpc_config RELEASE NOCONFIG ${_miniupnpc_configs} "")
                if(_miniupnpc_config)
                    string(TOUPPER "_${_miniupnpc_config}" _miniupnpc_config)
                endif()
                foreach(_miniupnpc_property IMPORTED_IMPLIB IMPORTED_LOCATION)
                    get_target_property(_miniupnpc_location ${_miniupnpc_target}
                        ${_miniupnpc_property}${_miniupnpc_config})
                    if(_miniupnpc_location AND EXISTS "${_miniupnpc_location}")
                        break()
                    endif()
                    set(_miniupnpc_location "")
                endforeach()
                if(_miniupnpc_location)
                    break()
                endif()
            endforeach()
        endif()
        if(_miniupnpc_location AND STATIC_LIBS)
            get_filename_component(_miniupnpc_ext "${_miniupnpc_location}" LAST_EXT)
            if(NOT _miniupnpc_type STREQUAL "STATIC_LIBRARY"
               OR NOT _miniupnpc_ext STREQUAL CMAKE_STATIC_LIBRARY_SUFFIX)
                message(STATUS "miniupnpc: ${miniupnpc_DIR} offers ${_miniupnpc_location} "
                    "(${_miniupnpc_type}); STATIC_LIBS wants an archive, so searching for one")
                set(_miniupnpc_location "")
            endif()
        endif()

        # The directory that holds miniupnpc/miniupnpc.h, as src/net.cpp
        # includes it.
        get_target_property(_miniupnpc_includes ${_miniupnpc_target} INTERFACE_INCLUDE_DIRECTORIES)
        set(_miniupnpc_include "")
        foreach(_miniupnpc_dir IN LISTS _miniupnpc_includes)
            if(EXISTS "${_miniupnpc_dir}/miniupnpc/miniupnpc.h")
                set(_miniupnpc_include "${_miniupnpc_dir}")
                break()
            endif()
        endforeach()

        if(_miniupnpc_location AND _miniupnpc_include)
            set(MiniUPnPc_LIBRARY "${_miniupnpc_location}" CACHE FILEPATH "miniupnpc library" FORCE)
            set(MiniUPnPc_INCLUDE_DIR "${_miniupnpc_include}" CACHE PATH "miniupnpc include directory" FORCE)
        endif()
    endif()
endif()

# Otherwise search for the header and the library. "libminiupnpc" is the
# Windows file name of miniupnpc's own CMake build (see above).
find_path(MiniUPnPc_INCLUDE_DIR
    NAMES miniupnpc/miniupnpc.h miniupnpc.h
    HINTS ${_MINIUPNPC_SEARCH_PATHS}
    PATH_SUFFIXES include
)

find_library(MiniUPnPc_LIBRARY
    NAMES miniupnpc libminiupnpc
    HINTS ${_MINIUPNPC_SEARCH_PATHS}
    PATH_SUFFIXES lib lib64
)

# Extract version
if(MiniUPnPc_INCLUDE_DIR)
    # Try to find version in miniupnpc.h
    set(_MINIUPNPC_H "${MiniUPnPc_INCLUDE_DIR}/miniupnpc/miniupnpc.h")
    if(NOT EXISTS "${_MINIUPNPC_H}")
        set(_MINIUPNPC_H "${MiniUPnPc_INCLUDE_DIR}/miniupnpc.h")
    endif()

    if(EXISTS "${_MINIUPNPC_H}")
        file(STRINGS "${_MINIUPNPC_H}" _MINIUPNPC_VERSION_STR
            REGEX "^#define[ \t]+MINIUPNPC_VERSION[ \t]+\"[^\"]+\"")
        if(_MINIUPNPC_VERSION_STR)
            string(REGEX REPLACE ".*\"([^\"]+)\".*" "\\1" MiniUPnPc_VERSION "${_MINIUPNPC_VERSION_STR}")
        endif()

        # Also check for API version
        file(STRINGS "${_MINIUPNPC_H}" _MINIUPNPC_API_STR
            REGEX "^#define[ \t]+MINIUPNPC_API_VERSION[ \t]+[0-9]+")
        if(_MINIUPNPC_API_STR)
            string(REGEX REPLACE ".*[ \t]([0-9]+)$" "\\1" MiniUPnPc_API_VERSION "${_MINIUPNPC_API_STR}")
        endif()
    endif()
endif()

find_package_handle_standard_args(MiniUPnPc
    REQUIRED_VARS
        MiniUPnPc_LIBRARY
        MiniUPnPc_INCLUDE_DIR
    VERSION_VAR MiniUPnPc_VERSION
)

if(MiniUPnPc_FOUND)
    set(MiniUPnPc_INCLUDE_DIRS ${MiniUPnPc_INCLUDE_DIR})
    set(MiniUPnPc_LIBRARIES ${MiniUPnPc_LIBRARY})

    # Windows requires additional libraries
    if(WIN32)
        list(APPEND MiniUPnPc_LIBRARIES iphlpapi ws2_32)
    endif()

    if(NOT TARGET MiniUPnPc::MiniUPnPc)
        add_library(MiniUPnPc::MiniUPnPc UNKNOWN IMPORTED)
        set_target_properties(MiniUPnPc::MiniUPnPc PROPERTIES
            IMPORTED_LOCATION "${MiniUPnPc_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${MiniUPnPc_INCLUDE_DIR}"
        )

        # Add Windows dependencies
        if(WIN32)
            set_property(TARGET MiniUPnPc::MiniUPnPc APPEND PROPERTY
                INTERFACE_LINK_LIBRARIES iphlpapi ws2_32)
        endif()
    endif()
endif()

mark_as_advanced(MiniUPnPc_INCLUDE_DIR MiniUPnPc_LIBRARY)
