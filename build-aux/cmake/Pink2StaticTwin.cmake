# pink2_static_twin(<out-var> <link item>)
#
# The static counterpart of a shared imported library, for the macOS pink2d,
# which ships outside the app bundle and so must not load Homebrew's dylibs (see
# "macOS: a pink2d without Homebrew" in src/CMakeLists.txt).
#
# For a SHARED or UNKNOWN imported library whose location is a .dylib, this
# creates Pink2Static_<name> (each :: in <name> becomes _), an imported target
# for the .a beside that dylib (or beside its real path). It has the same
# compile usage requirements, and its link interface is mapped the same way
# (Boost::thread brings Boost::atomic, chrono, container and date_time).
# <out-var> is set to its name.
#
# Anything else comes back unchanged: Boost::headers, the static-only
# Boost::exception, Threads::Threads, flags, an SDK .tbd stub, or an UNKNOWN
# library that is already an archive.
#
# Never falls back to the dylib. A shared library without an IMPORTED_LOCATION,
# or without an archive beside it, stops the configure: otherwise pink2d would
# quietly link the dylib.
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
    if(NOT _location)
        message(FATAL_ERROR "pink2d links static archives on macOS, since it ships "
            "outside the app bundle, and ${_target} (${_type}) has no IMPORTED_LOCATION "
            "to find its archive beside.")
    endif()
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
