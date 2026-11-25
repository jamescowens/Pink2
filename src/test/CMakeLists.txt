# Unit tests: test_pink2, as makefile.unix's test_pink2 target (every
# src/test/*.cpp, plus the daemon objects without init.o, noui.o included).

if(Boost_VERSION VERSION_LESS 1.70.0)
    find_package(Boost ${BOOST_MINIMUM_VERSION} COMPONENTS unit_test_framework REQUIRED)
else()
    find_package(Boost ${BOOST_MINIMUM_VERSION} COMPONENTS unit_test_framework CONFIG REQUIRED)
endif()
pink2_strip_boost_link_defines(Boost::unit_test_framework)

file(GLOB PINK2_TEST_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
list(SORT PINK2_TEST_SOURCES)

add_executable(test_pink2 ${PINK2_TEST_SOURCES} ${CMAKE_CURRENT_SOURCE_DIR}/../noui.cpp)
target_link_libraries(test_pink2 PRIVATE pink2_core Boost::unit_test_framework ${PINK2_RUNTIME_LIBS})
target_compile_definitions(test_pink2 PRIVATE TEST_DATA_DIR=${CMAKE_CURRENT_SOURCE_DIR}/data)
set_target_properties(test_pink2 PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

# makefile.unix defines BOOST_TEST_DYN_LINK exactly when it links Boost.Test
# shared (LMODE = dynamic).
get_target_property(_ut_type Boost::unit_test_framework TYPE)
get_target_property(_ut_loc Boost::unit_test_framework IMPORTED_LOCATION)
if(NOT _ut_loc)
    get_target_property(_ut_loc Boost::unit_test_framework IMPORTED_LOCATION_RELEASE)
endif()
if(NOT _ut_loc)
    set(_ut_loc "${Boost_UNIT_TEST_FRAMEWORK_LIBRARY}")
endif()
set(PINK2_BOOST_TEST_SHARED FALSE)
if(_ut_type STREQUAL "SHARED_LIBRARY")
    set(PINK2_BOOST_TEST_SHARED TRUE)
elseif(NOT _ut_type STREQUAL "STATIC_LIBRARY" AND _ut_loc MATCHES "\\.(so|dylib|dll|dll\\.a)(\\.[0-9.]+)?$")
    set(PINK2_BOOST_TEST_SHARED TRUE)
endif()
if(PINK2_BOOST_TEST_SHARED)
    target_compile_definitions(test_pink2 PRIVATE BOOST_TEST_DYN_LINK)
endif()
message(STATUS "Boost.Test: type ${_ut_type}, ${_ut_loc}, BOOST_TEST_DYN_LINK ${PINK2_BOOST_TEST_SHARED}")

add_test(NAME test_pink2 COMMAND test_pink2)
