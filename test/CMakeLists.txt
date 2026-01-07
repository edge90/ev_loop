cmake_minimum_required(VERSION 3.15...3.23)

project(CmakeConfigPackageTests LANGUAGES CXX)

# ---- Test as standalone project the exported config package ----

if(PROJECT_IS_TOP_LEVEL OR TEST_INSTALLED_VERSION)
  enable_testing()

  find_package(ev_loop CONFIG REQUIRED) # for intro, project_options, ...

  if(NOT TARGET ev_loop_options)
    message(FATAL_ERROR "Required config package not found!")
    return() # be strictly paranoid for Template Janitor github action! CK
  endif()
endif()

# ---- Dependencies ----

include(${Catch2_SOURCE_DIR}/extras/Catch.cmake)

# Unit tests (runtime only)
add_executable(tests)
target_sources(tests PRIVATE
    test_external.cpp
    test_group_event_loop.cpp
    test_inbox.cpp
    test_setup.cpp
    test_strategy.cpp
    test_utils.cpp
)
target_link_libraries(tests
  PRIVATE ev_loop::ev_loop_warnings
          ev_loop::ev_loop_options
          ev_loop::ev_loop
          Catch2::Catch2WithMain)

if(WIN32 AND BUILD_SHARED_LIBS)
  add_custom_command(
    TARGET tests
    PRE_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_RUNTIME_DLLS:tests> $<TARGET_FILE_DIR:tests>
    COMMAND_EXPAND_LISTS)
endif()

catch_discover_tests(tests
  TEST_PREFIX "unittests."
  REPORTER XML
  OUTPUT_DIR .
  OUTPUT_PREFIX "unittests."
  OUTPUT_SUFFIX .xml)

# Function to create a test executable with common configuration
function(add_test_executable TARGET_NAME TEST_PREFIX)
  add_executable(${TARGET_NAME})
  target_sources(${TARGET_NAME} PRIVATE ${ARGN})
  target_link_libraries(${TARGET_NAME}
    PRIVATE ev_loop::ev_loop_warnings
            ev_loop::ev_loop_options
            ev_loop::ev_loop
            Catch2::Catch2WithMain)
  catch_discover_tests(${TARGET_NAME}
    TEST_PREFIX "${TEST_PREFIX}."
    REPORTER XML
    OUTPUT_DIR .
    OUTPUT_PREFIX "${TEST_PREFIX}."
    OUTPUT_SUFFIX .xml)
endfunction()

# Constexpr test sources
set(CONSTEXPR_TEST_SOURCES
    test_external_constexpr.cpp
    test_group_event_loop_constexpr.cpp
    test_inbox_constexpr.cpp
    test_setup_constexpr.cpp
    test_strategy_constexpr.cpp
    test_type_list_constexpr.cpp
)

# Constexpr tests - STATIC_REQUIRE runs as compile-time static_assert
add_test_executable(constexpr_tests "constexpr" ${CONSTEXPR_TEST_SOURCES})

# Relaxed constexpr tests - STATIC_REQUIRE runs as runtime REQUIRE (for debugging)
add_test_executable(relaxed_tests "relaxed" ${CONSTEXPR_TEST_SOURCES})
target_compile_definitions(relaxed_tests PRIVATE CATCH_CONFIG_RUNTIME_STATIC_REQUIRE)

# =============================================================================
# Compile-failure tests (grouped by concept)
# =============================================================================

# Helper function for compile-failure tests
function(add_compile_fail_test TARGET_NAME)
  add_executable(${TARGET_NAME} EXCLUDE_FROM_ALL ${TARGET_NAME}.cpp)
  target_link_libraries(${TARGET_NAME} PRIVATE ev_loop::ev_loop)
  add_test(NAME ${TARGET_NAME}
           COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target ${TARGET_NAME})
  set_tests_properties(${TARGET_NAME} PROPERTIES WILL_FAIL TRUE)
endfunction()

# external
add_compile_fail_test(test_external_duplicate_fail)
add_compile_fail_test(test_external_no_emits_fail)
add_compile_fail_test(test_external_not_found_fail)

# group_event_loop
add_compile_fail_test(test_group_event_loop_invalid_type_fail)
add_compile_fail_test(test_group_event_loop_no_groups_fail)
add_compile_fail_test(test_group_event_loop_only_external_fail)
add_compile_fail_test(test_group_event_loop_receiver_not_found_fail)

# inbox
add_compile_fail_test(test_inbox_capacity_fail)

# type_list
add_compile_fail_test(test_type_list_tag_overflow_fail)
