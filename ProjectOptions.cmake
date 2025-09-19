include(cmake/SystemLink.cmake)
include(cmake/LibFuzzer.cmake)
include(CMakeDependentOption)
include(CheckCXXCompilerFlag)


include(CheckCXXSourceCompiles)


macro(ev_loop_supports_sanitizers)
  if((CMAKE_CXX_COMPILER_ID MATCHES ".*Clang.*" OR CMAKE_CXX_COMPILER_ID MATCHES ".*GNU.*") AND NOT WIN32)

    message(STATUS "Sanity checking UndefinedBehaviorSanitizer, it should be supported on this platform")
    set(TEST_PROGRAM "int main() { return 0; }")

    # Check if UndefinedBehaviorSanitizer works at link time
    set(CMAKE_REQUIRED_FLAGS "-fsanitize=undefined")
    set(CMAKE_REQUIRED_LINK_OPTIONS "-fsanitize=undefined")
    check_cxx_source_compiles("${TEST_PROGRAM}" HAS_UBSAN_LINK_SUPPORT)

    if(HAS_UBSAN_LINK_SUPPORT)
      message(STATUS "UndefinedBehaviorSanitizer is supported at both compile and link time.")
      set(SUPPORTS_UBSAN ON)
    else()
      message(WARNING "UndefinedBehaviorSanitizer is NOT supported at link time.")
      set(SUPPORTS_UBSAN OFF)
    endif()
  else()
    set(SUPPORTS_UBSAN OFF)
  endif()

  if((CMAKE_CXX_COMPILER_ID MATCHES ".*Clang.*" OR CMAKE_CXX_COMPILER_ID MATCHES ".*GNU.*") AND WIN32)
    set(SUPPORTS_ASAN OFF)
  else()
    if (NOT WIN32)
      message(STATUS "Sanity checking AddressSanitizer, it should be supported on this platform")
      set(TEST_PROGRAM "int main() { return 0; }")

      # Check if AddressSanitizer works at link time
      set(CMAKE_REQUIRED_FLAGS "-fsanitize=address")
      set(CMAKE_REQUIRED_LINK_OPTIONS "-fsanitize=address")
      check_cxx_source_compiles("${TEST_PROGRAM}" HAS_ASAN_LINK_SUPPORT)

      if(HAS_ASAN_LINK_SUPPORT)
        message(STATUS "AddressSanitizer is supported at both compile and link time.")
        set(SUPPORTS_ASAN ON)
      else()
        message(WARNING "AddressSanitizer is NOT supported at link time.")
        set(SUPPORTS_ASAN OFF)
      endif()
    else()
      set(SUPPORTS_ASAN ON)
    endif()
  endif()
endmacro()

macro(ev_loop_setup_options)
  option(ev_loop_ENABLE_HARDENING "Enable hardening" ON)
  option(ev_loop_ENABLE_COVERAGE "Enable coverage reporting" OFF)
  cmake_dependent_option(
    ev_loop_ENABLE_GLOBAL_HARDENING
    "Attempt to push hardening options to built dependencies"
    ON
    ev_loop_ENABLE_HARDENING
    OFF)

  ev_loop_supports_sanitizers()

  if(NOT PROJECT_IS_TOP_LEVEL OR ev_loop_PACKAGING_MAINTAINER_MODE)
    option(ev_loop_ENABLE_IPO "Enable IPO/LTO" OFF)
    option(ev_loop_WARNINGS_AS_ERRORS "Treat Warnings As Errors" OFF)
    option(ev_loop_ENABLE_USER_LINKER "Enable user-selected linker" OFF)
    option(ev_loop_ENABLE_SANITIZER_ADDRESS "Enable address sanitizer" OFF)
    option(ev_loop_ENABLE_SANITIZER_LEAK "Enable leak sanitizer" OFF)
    option(ev_loop_ENABLE_SANITIZER_UNDEFINED "Enable undefined sanitizer" OFF)
    option(ev_loop_ENABLE_SANITIZER_THREAD "Enable thread sanitizer" OFF)
    option(ev_loop_ENABLE_SANITIZER_MEMORY "Enable memory sanitizer" OFF)
    option(ev_loop_ENABLE_UNITY_BUILD "Enable unity builds" OFF)
    option(ev_loop_ENABLE_CLANG_TIDY "Enable clang-tidy" OFF)
    option(ev_loop_ENABLE_CPPCHECK "Enable cpp-check analysis" OFF)
    option(ev_loop_ENABLE_PCH "Enable precompiled headers" OFF)
    option(ev_loop_ENABLE_CACHE "Enable ccache" OFF)
  else()
    option(ev_loop_ENABLE_IPO "Enable IPO/LTO" ON)
    option(ev_loop_WARNINGS_AS_ERRORS "Treat Warnings As Errors" ON)
    option(ev_loop_ENABLE_USER_LINKER "Enable user-selected linker" OFF)
    option(ev_loop_ENABLE_SANITIZER_ADDRESS "Enable address sanitizer" ${SUPPORTS_ASAN})
    option(ev_loop_ENABLE_SANITIZER_LEAK "Enable leak sanitizer" OFF)
    option(ev_loop_ENABLE_SANITIZER_UNDEFINED "Enable undefined sanitizer" ${SUPPORTS_UBSAN})
    option(ev_loop_ENABLE_SANITIZER_THREAD "Enable thread sanitizer" OFF)
    option(ev_loop_ENABLE_SANITIZER_MEMORY "Enable memory sanitizer" OFF)
    option(ev_loop_ENABLE_UNITY_BUILD "Enable unity builds" OFF)
    option(ev_loop_ENABLE_CLANG_TIDY "Enable clang-tidy" ON)
    option(ev_loop_ENABLE_CPPCHECK "Enable cpp-check analysis" ON)
    option(ev_loop_ENABLE_PCH "Enable precompiled headers" OFF)
    option(ev_loop_ENABLE_CACHE "Enable ccache" ON)
  endif()

  if(NOT PROJECT_IS_TOP_LEVEL)
    mark_as_advanced(
      ev_loop_ENABLE_IPO
      ev_loop_WARNINGS_AS_ERRORS
      ev_loop_ENABLE_USER_LINKER
      ev_loop_ENABLE_SANITIZER_ADDRESS
      ev_loop_ENABLE_SANITIZER_LEAK
      ev_loop_ENABLE_SANITIZER_UNDEFINED
      ev_loop_ENABLE_SANITIZER_THREAD
      ev_loop_ENABLE_SANITIZER_MEMORY
      ev_loop_ENABLE_UNITY_BUILD
      ev_loop_ENABLE_CLANG_TIDY
      ev_loop_ENABLE_CPPCHECK
      ev_loop_ENABLE_COVERAGE
      ev_loop_ENABLE_PCH
      ev_loop_ENABLE_CACHE)
  endif()

  ev_loop_check_libfuzzer_support(LIBFUZZER_SUPPORTED)
  if(LIBFUZZER_SUPPORTED AND (ev_loop_ENABLE_SANITIZER_ADDRESS OR ev_loop_ENABLE_SANITIZER_THREAD OR ev_loop_ENABLE_SANITIZER_UNDEFINED))
    set(DEFAULT_FUZZER ON)
  else()
    set(DEFAULT_FUZZER OFF)
  endif()

  option(ev_loop_BUILD_FUZZ_TESTS "Enable fuzz testing executable" ${DEFAULT_FUZZER})

endmacro()

macro(ev_loop_global_options)
  if(ev_loop_ENABLE_IPO)
    include(cmake/InterproceduralOptimization.cmake)
    ev_loop_enable_ipo()
  endif()

  ev_loop_supports_sanitizers()

  if(ev_loop_ENABLE_HARDENING AND ev_loop_ENABLE_GLOBAL_HARDENING)
    include(cmake/Hardening.cmake)
    if(NOT SUPPORTS_UBSAN 
       OR ev_loop_ENABLE_SANITIZER_UNDEFINED
       OR ev_loop_ENABLE_SANITIZER_ADDRESS
       OR ev_loop_ENABLE_SANITIZER_THREAD
       OR ev_loop_ENABLE_SANITIZER_LEAK)
      set(ENABLE_UBSAN_MINIMAL_RUNTIME FALSE)
    else()
      set(ENABLE_UBSAN_MINIMAL_RUNTIME TRUE)
    endif()
    message("${ev_loop_ENABLE_HARDENING} ${ENABLE_UBSAN_MINIMAL_RUNTIME} ${ev_loop_ENABLE_SANITIZER_UNDEFINED}")
    ev_loop_enable_hardening(ev_loop_options ON ${ENABLE_UBSAN_MINIMAL_RUNTIME})
  endif()
endmacro()

macro(ev_loop_local_options)
  if(PROJECT_IS_TOP_LEVEL)
    include(cmake/StandardProjectSettings.cmake)
  endif()

  add_library(ev_loop_warnings INTERFACE)
  add_library(ev_loop_options INTERFACE)

  include(cmake/CompilerWarnings.cmake)
  ev_loop_set_project_warnings(
    ev_loop_warnings
    ${ev_loop_WARNINGS_AS_ERRORS}
    ""
    ""
    ""
    "")

  if(ev_loop_ENABLE_USER_LINKER)
    include(cmake/Linker.cmake)
    ev_loop_configure_linker(ev_loop_options)
  endif()

  include(cmake/Sanitizers.cmake)
  ev_loop_enable_sanitizers(
    ev_loop_options
    ${ev_loop_ENABLE_SANITIZER_ADDRESS}
    ${ev_loop_ENABLE_SANITIZER_LEAK}
    ${ev_loop_ENABLE_SANITIZER_UNDEFINED}
    ${ev_loop_ENABLE_SANITIZER_THREAD}
    ${ev_loop_ENABLE_SANITIZER_MEMORY})

  set_target_properties(ev_loop_options PROPERTIES UNITY_BUILD ${ev_loop_ENABLE_UNITY_BUILD})

  if(ev_loop_ENABLE_PCH)
    target_precompile_headers(
      ev_loop_options
      INTERFACE
      <vector>
      <string>
      <utility>)
  endif()

  if(ev_loop_ENABLE_CACHE)
    include(cmake/Cache.cmake)
    ev_loop_enable_cache()
  endif()

  include(cmake/StaticAnalyzers.cmake)
  if(ev_loop_ENABLE_CLANG_TIDY)
    ev_loop_enable_clang_tidy(ev_loop_options ${ev_loop_WARNINGS_AS_ERRORS})
  endif()

  if(ev_loop_ENABLE_CPPCHECK)
    ev_loop_enable_cppcheck(${ev_loop_WARNINGS_AS_ERRORS} "" # override cppcheck options
    )
  endif()

  if(ev_loop_ENABLE_COVERAGE)
    include(cmake/Tests.cmake)
    ev_loop_enable_coverage(ev_loop_options)
  endif()

  if(ev_loop_WARNINGS_AS_ERRORS)
    check_cxx_compiler_flag("-Wl,--fatal-warnings" LINKER_FATAL_WARNINGS)
    if(LINKER_FATAL_WARNINGS)
      # This is not working consistently, so disabling for now
      # target_link_options(ev_loop_options INTERFACE -Wl,--fatal-warnings)
    endif()
  endif()

  if(ev_loop_ENABLE_HARDENING AND NOT ev_loop_ENABLE_GLOBAL_HARDENING)
    include(cmake/Hardening.cmake)
    if(NOT SUPPORTS_UBSAN 
       OR ev_loop_ENABLE_SANITIZER_UNDEFINED
       OR ev_loop_ENABLE_SANITIZER_ADDRESS
       OR ev_loop_ENABLE_SANITIZER_THREAD
       OR ev_loop_ENABLE_SANITIZER_LEAK)
      set(ENABLE_UBSAN_MINIMAL_RUNTIME FALSE)
    else()
      set(ENABLE_UBSAN_MINIMAL_RUNTIME TRUE)
    endif()
    ev_loop_enable_hardening(ev_loop_options OFF ${ENABLE_UBSAN_MINIMAL_RUNTIME})
  endif()

endmacro()
