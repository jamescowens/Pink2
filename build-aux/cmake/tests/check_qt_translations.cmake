# Fails unless every translation that src/qt/bitcoin.qrc embeds is generated
# (from its .ts) and is what the GUI's resource file compiles in.
#
# Inputs: -DQRC_SOURCE=<src/qt/bitcoin.qrc> -DQRC_PROCESSED=<the resource file
# Pinkcoin-Qt compiles> -DRCC=<rcc> -DTS_DIR=<src/qt/locale> -DQM_DIR=<build dir
# holding the .qm> -DEXPECTED_COUNT=39

cmake_minimum_required(VERSION 3.16)

foreach(_v QRC_SOURCE QRC_PROCESSED RCC TS_DIR QM_DIR EXPECTED_COUNT)
    if(NOT DEFINED ${_v})
        message(FATAL_ERROR "qt_translations: ${_v} not given")
    endif()
endforeach()
foreach(_f "${QRC_PROCESSED}" "${RCC}")
    if(NOT EXISTS "${_f}")
        message(FATAL_ERROR "qt_translations: ${_f} does not exist (no GUI target built?)")
    endif()
endforeach()

# The .qm files the source qrc names.
file(STRINGS "${QRC_SOURCE}" _lines REGEX "locale/bitcoin_[A-Za-z_]+\\.qm")
set(_expected "")
foreach(_l IN LISTS _lines)
    string(REGEX MATCH "locale/(bitcoin_[A-Za-z_]+\\.qm)" _m "${_l}")
    list(APPEND _expected "${CMAKE_MATCH_1}")
endforeach()
list(LENGTH _expected _n)
if(NOT _n EQUAL EXPECTED_COUNT)
    message(FATAL_ERROR "qt_translations: ${QRC_SOURCE} names ${_n} .qm files, expected ${EXPECTED_COUNT}")
endif()

# What rcc embeds, for the resource file Pinkcoin-Qt compiles.
execute_process(COMMAND "${RCC}" --list "${QRC_PROCESSED}"
    OUTPUT_VARIABLE _listing ERROR_VARIABLE _err RESULT_VARIABLE _rc)
if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "qt_translations: rcc --list failed: ${_err}")
endif()
string(REPLACE "\n" ";" _listing "${_listing}")

set(_missing "")
foreach(_qm IN LISTS _expected)
    string(REGEX REPLACE "\\.qm$" ".ts" _ts "${_qm}")
    if(NOT EXISTS "${TS_DIR}/${_ts}")
        list(APPEND _missing "${_ts} (source)")
    endif()
    if(NOT EXISTS "${QM_DIR}/${_qm}")
        list(APPEND _missing "${_qm} (not generated)")
        continue()
    endif()
    file(SIZE "${QM_DIR}/${_qm}" _size)
    if(_size LESS 16)
        list(APPEND _missing "${_qm} (empty)")
    endif()
    if(NOT "${QM_DIR}/${_qm}" IN_LIST _listing)
        list(APPEND _missing "${_qm} (not embedded)")
    endif()
endforeach()

# Nothing from the source tree's locale directory may be embedded.
set(_stray "")
foreach(_e IN LISTS _listing)
    if(_e MATCHES "\\.qm$" AND NOT _e MATCHES "^${QM_DIR}/")
        list(APPEND _stray "${_e}")
    endif()
endforeach()

if(_missing OR _stray)
    message(FATAL_ERROR "qt_translations: missing: ${_missing}; stray: ${_stray}")
endif()
message(STATUS "qt_translations: ${_n} .qm files, each generated from its .ts and embedded")
