add_executable(ev_example main.cpp)
target_link_libraries(ev_example PRIVATE ev_loop::ev_loop ev_loop::ev_loop_warnings ev_loop::ev_loop_options)