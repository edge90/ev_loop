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
    test_group_event_loop.cpp
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
    test_type_list_constexpr.cpp
    test_group_event_loop_constexpr.cpp
)

# Constexpr tests - STATIC_REQUIRE runs as compile-time static_assert
add_test_executable(constexpr_tests "constexpr" ${CONSTEXPR_TEST_SOURCES})

# Relaxed constexpr tests - STATIC_REQUIRE runs as runtime REQUIRE (for debugging)
add_test_executable(relaxed_tests "relaxed" ${CONSTEXPR_TEST_SOURCES})
target_compile_definitions(relaxed_tests PRIVATE CATCH_CONFIG_RUNTIME_STATIC_REQUIRE)

# Compile-failure test: verifies that tag_type_t rejects N >= uint32_t max
add_executable(test_tag_type_overflow_fail EXCLUDE_FROM_ALL test_tag_type_overflow_fail.cpp)
target_link_libraries(test_tag_type_overflow_fail PRIVATE ev_loop::ev_loop)
add_test(NAME tag_type_overflow_should_fail
         COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target test_tag_type_overflow_fail)
set_tests_properties(tag_type_overflow_should_fail PROPERTIES WILL_FAIL TRUE)

# Compile-failure test: verifies that duplicate ExternalGroup types are rejected
add_executable(test_duplicate_external_group_fail EXCLUDE_FROM_ALL test_duplicate_external_group_fail.cpp)
target_link_libraries(test_duplicate_external_group_fail PRIVATE ev_loop::ev_loop)
add_test(NAME duplicate_external_group_should_fail
         COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target test_duplicate_external_group_fail)
set_tests_properties(duplicate_external_group_should_fail PROPERTIES WILL_FAIL TRUE)

# Compile-failure test: verifies that GroupEventLoop requires at least one group
add_executable(test_no_groups_fail EXCLUDE_FROM_ALL test_no_groups_fail.cpp)
target_link_libraries(test_no_groups_fail PRIVATE ev_loop::ev_loop)
add_test(NAME no_groups_should_fail
         COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target test_no_groups_fail)
set_tests_properties(no_groups_should_fail PROPERTIES WILL_FAIL TRUE)

# Compile-failure test: verifies that GroupEventLoop requires at least one ThreadGroup
add_executable(test_only_external_group_fail EXCLUDE_FROM_ALL test_only_external_group_fail.cpp)
target_link_libraries(test_only_external_group_fail PRIVATE ev_loop::ev_loop)
add_test(NAME only_external_group_should_fail
         COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target test_only_external_group_fail)
set_tests_properties(only_external_group_should_fail PROPERTIES WILL_FAIL TRUE)

# Compile-failure test: verifies that GroupEventLoop rejects invalid group types
add_executable(test_invalid_group_type_fail EXCLUDE_FROM_ALL test_invalid_group_type_fail.cpp)
target_link_libraries(test_invalid_group_type_fail PRIVATE ev_loop::ev_loop)
add_test(NAME invalid_group_type_should_fail
         COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target test_invalid_group_type_fail)
set_tests_properties(invalid_group_type_should_fail PROPERTIES WILL_FAIL TRUE)

# Compile-failure test: verifies that get_external_emitter rejects unknown ExternalGroup types
add_executable(test_external_group_not_found_fail EXCLUDE_FROM_ALL test_external_group_not_found_fail.cpp)
target_link_libraries(test_external_group_not_found_fail PRIVATE ev_loop::ev_loop)
add_test(NAME external_group_not_found_should_fail
         COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target test_external_group_not_found_fail)
set_tests_properties(external_group_not_found_should_fail PROPERTIES WILL_FAIL TRUE)

# Compile-failure test: verifies that get<Receiver>() rejects unknown receivers
add_executable(test_receiver_not_found_fail EXCLUDE_FROM_ALL test_receiver_not_found_fail.cpp)
target_link_libraries(test_receiver_not_found_fail PRIVATE ev_loop::ev_loop)
add_test(NAME receiver_not_found_should_fail
         COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target test_receiver_not_found_fail)
set_tests_properties(receiver_not_found_should_fail PROPERTIES WILL_FAIL TRUE)

# Compile-failure test: verifies that InboxBase requires power-of-2 capacity
add_executable(test_inbox_capacity_fail EXCLUDE_FROM_ALL test_inbox_capacity_fail.cpp)
target_link_libraries(test_inbox_capacity_fail PRIVATE ev_loop::ev_loop)
add_test(NAME inbox_capacity_should_fail
         COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target test_inbox_capacity_fail)
set_tests_properties(inbox_capacity_should_fail PROPERTIES WILL_FAIL TRUE)

# Compile-failure test: verifies that ExternalGroup<T> requires T to define emits
add_executable(test_external_group_no_emits_fail EXCLUDE_FROM_ALL test_external_group_no_emits_fail.cpp)
target_link_libraries(test_external_group_no_emits_fail PRIVATE ev_loop::ev_loop)
add_test(NAME external_group_no_emits_should_fail
         COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target test_external_group_no_emits_fail)
set_tests_properties(external_group_no_emits_should_fail PROPERTIES WILL_FAIL TRUE)
