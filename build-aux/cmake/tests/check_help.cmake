# Runs `pink2d -?` and checks the first line of its output carries the
# version, v<PINK2_VERSION>. Usage:
#   cmake -DPINK2D=<path> -DPINK2_VERSION=<x.y.z.b> -P check_help.cmake
if(NOT PINK2D OR NOT PINK2_VERSION)
    message(FATAL_ERROR "PINK2D and PINK2_VERSION are required")
endif()
if(NOT EXISTS "${PINK2D}")
    message(FATAL_ERROR "${PINK2D} does not exist")
endif()
execute_process(COMMAND "${PINK2D}" -?
    OUTPUT_VARIABLE _out ERROR_VARIABLE _err RESULT_VARIABLE _rc)
string(REGEX MATCH "^[^\n]*" _first "${_out}")
message(STATUS "first line: ${_first}")
if(NOT _first MATCHES "v${PINK2_VERSION}")
    message(FATAL_ERROR "first line of `pink2d -?` lacks v${PINK2_VERSION} (rc=${_rc}): ${_first}${_err}")
endif()
