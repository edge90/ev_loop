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
    test_tagged_event.cpp
    test_queue.cpp
    test_event_loop.cpp
    test_strategies.cpp
    test_shared_event_loop_ptr.cpp
    test_external_emitter.cpp
    test_builder.cpp
    test_move_optimization.cpp
    test_threaded.cpp
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
    test_tagged_event_constexpr.cpp
    test_queue_constexpr.cpp
    test_event_loop_constexpr.cpp
    test_strategies_constexpr.cpp
    test_shared_event_loop_ptr_constexpr.cpp
    test_external_emitter_constexpr.cpp
    test_builder_constexpr.cpp
)

# Constexpr tests - STATIC_REQUIRE runs as compile-time static_assert
add_test_executable(constexpr_tests "constexpr" ${CONSTEXPR_TEST_SOURCES})

# Relaxed constexpr tests - STATIC_REQUIRE runs as runtime REQUIRE (for debugging)
add_test_executable(relaxed_tests "relaxed" ${CONSTEXPR_TEST_SOURCES})
target_compile_definitions(relaxed_tests PRIVATE CATCH_CONFIG_RUNTIME_STATIC_REQUIRE)

# Compile-failure test: verifies that duplicate receivers trigger static_assert
add_executable(test_builder_duplicate_fail EXCLUDE_FROM_ALL test_builder_duplicate_fail.cpp)
target_link_libraries(test_builder_duplicate_fail PRIVATE ev_loop::ev_loop)
add_test(NAME builder_duplicate_should_fail
         COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target test_builder_duplicate_fail)
set_tests_properties(builder_duplicate_should_fail PROPERTIES WILL_FAIL TRUE)

# Compile-failure test: verifies that tag_type_t rejects N >= uint32_t max
add_executable(test_tag_type_overflow_fail EXCLUDE_FROM_ALL test_tag_type_overflow_fail.cpp)
target_link_libraries(test_tag_type_overflow_fail PRIVATE ev_loop::ev_loop)
add_test(NAME tag_type_overflow_should_fail
         COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target test_tag_type_overflow_fail)
set_tests_properties(tag_type_overflow_should_fail PROPERTIES WILL_FAIL TRUE)
