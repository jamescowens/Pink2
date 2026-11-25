# Fails unless the static Qt plugins Pinkcoin-Qt imports are the ones qmake
# would import for the same Qt modules, which is what 2.4.0.0's static qmake
# builds linked.
#
# Runs the Qt's own qmake on a probe project with QT += <modules> and compares
# the Q_IMPORT_PLUGIN lines of the <target>_plugin_import.cpp it writes with
# those of the file CMake generated (src/qt/CMakeLists.txt, "Static Qt
# plugins").
#
# Inputs: -DQMAKE=<qmake> -DQT_MODULES=<qmake module names, space-separated>
# -DIMPORT_FILE=<pinkcoin-qt_plugin_import.cpp> -DWORK_DIR=<scratch dir>

cmake_minimum_required(VERSION 3.16)

foreach(_v QMAKE QT_MODULES IMPORT_FILE WORK_DIR)
    if(NOT DEFINED ${_v})
        message(FATAL_ERROR "qt_static_plugins: ${_v} not given")
    endif()
endforeach()
foreach(_f "${QMAKE}" "${IMPORT_FILE}")
    if(NOT EXISTS "${_f}")
        message(FATAL_ERROR "qt_static_plugins: ${_f} does not exist")
    endif()
endforeach()

# The sorted class names of a plugin import file.
function(_plugin_classes file out)
    file(STRINGS "${file}" _lines REGEX "^Q_IMPORT_PLUGIN\\(")
    set(_classes "")
    foreach(_l IN LISTS _lines)
        string(REGEX MATCH "^Q_IMPORT_PLUGIN\\(([A-Za-z0-9_]+)\\)" _m "${_l}")
        list(APPEND _classes "${CMAKE_MATCH_1}")
    endforeach()
    list(SORT _classes)
    set(${out} "${_classes}" PARENT_SCOPE)
endfunction()

file(REMOVE_RECURSE "${WORK_DIR}")
file(MAKE_DIRECTORY "${WORK_DIR}")
file(WRITE "${WORK_DIR}/probe.pro"
    "TEMPLATE = app\nTARGET = probe\nCONFIG += thread\nQT += ${QT_MODULES}\nSOURCES += main.cpp\n")
file(WRITE "${WORK_DIR}/main.cpp" "int main() { return 0; }\n")
execute_process(COMMAND "${QMAKE}" probe.pro
    WORKING_DIRECTORY "${WORK_DIR}"
    OUTPUT_VARIABLE _out ERROR_VARIABLE _err RESULT_VARIABLE _rc)
if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "qt_static_plugins: qmake failed (${_rc}): ${_out}${_err}")
endif()
if(NOT EXISTS "${WORK_DIR}/probe_plugin_import.cpp")
    message(FATAL_ERROR "qt_static_plugins: qmake wrote no probe_plugin_import.cpp "
        "(is this Qt static?): ${_out}${_err}")
endif()

_plugin_classes("${WORK_DIR}/probe_plugin_import.cpp" _qmake)
_plugin_classes("${IMPORT_FILE}" _cmake)
message(STATUS "qmake (QT += ${QT_MODULES}): ${_qmake}")
message(STATUS "CMake: ${_cmake}")
if(NOT _qmake)
    message(FATAL_ERROR "qt_static_plugins: qmake imports no plugins")
endif()
if(NOT _qmake STREQUAL _cmake)
    message(FATAL_ERROR "qt_static_plugins: the plugin lists differ")
endif()
