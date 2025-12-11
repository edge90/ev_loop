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

add_executable(tests
    tests.cpp
    test_tagged_event.cpp
    test_queue.cpp
    test_event_loop.cpp
    test_threaded.cpp
    test_move_optimization.cpp
    test_builder.cpp
)
target_link_libraries(
  tests
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

# automatically discover tests that are defined in catch based test files you can modify the unittests. Set TEST_PREFIX
# to whatever you want, or use different for different binaries
catch_discover_tests(
  tests
  TEST_PREFIX
  "unittests."
  REPORTER
  XML
  OUTPUT_DIR
  .
  OUTPUT_PREFIX
  "unittests."
  OUTPUT_SUFFIX
  .xml)

# Add a file containing a set of constexpr tests
add_executable(constexpr_tests constexpr_tests.cpp)
target_link_libraries(
  constexpr_tests
  PRIVATE ev_loop::ev_loop_warnings
          ev_loop::ev_loop_options
          ev_loop::ev_loop
          Catch2::Catch2WithMain)

catch_discover_tests(
  constexpr_tests
  TEST_PREFIX
  "constexpr."
  REPORTER
  XML
  OUTPUT_DIR
  .
  OUTPUT_PREFIX
  "constexpr."
  OUTPUT_SUFFIX
  .xml)

# Disable the constexpr portion of the test, and build again this allows us to have an executable that we can debug when
# things go wrong with the constexpr testing
add_executable(relaxed_constexpr_tests constexpr_tests.cpp)
target_link_libraries(
  relaxed_constexpr_tests
  PRIVATE ev_loop::ev_loop_warnings
          ev_loop::ev_loop_options
          ev_loop::ev_loop
          Catch2::Catch2WithMain)
target_compile_definitions(relaxed_constexpr_tests PRIVATE -DCATCH_CONFIG_RUNTIME_STATIC_REQUIRE)

catch_discover_tests(
  relaxed_constexpr_tests
  TEST_PREFIX
  "relaxed_constexpr."
  REPORTER
  XML
  OUTPUT_DIR
  .
  OUTPUT_PREFIX
  "relaxed_constexpr."
  OUTPUT_SUFFIX
  .xml)

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
