include(GenerateExportHeader)

add_library(ev_loop INTERFACE) 
add_library(ev_loop::ev_loop ALIAS ev_loop)

target_link_libraries(ev_loop INTERFACE ev_loop_options ev_loop_warnings)

target_include_directories(ev_loop ${WARNING_GUARD} INTERFACE $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
                                                              $<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/include>)

target_compile_features(ev_loop INTERFACE cxx_std_23)

set_target_properties(
  ev_loop
  PROPERTIES VERSION ${PROJECT_VERSION}
             CXX_VISIBILITY_PRESET hidden
             VISIBILITY_INLINES_HIDDEN YES)

#generate_export_header(ev_loop EXPORT_FILE_NAME ${PROJECT_BINARY_DIR}/include/ev_loop/ev_loop_export.hpp)

if(NOT BUILD_SHARED_LIBS)
  target_compile_definitions(ev_loop INTERFACE ev_loop_STATIC_DEFINE)
endif()
