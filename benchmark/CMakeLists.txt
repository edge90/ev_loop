add_executable(ev_benchmark benchmark.cpp)
target_link_libraries(ev_benchmark PRIVATE ev_loop::ev_loop ev_loop::ev_loop_warnings ev_loop::ev_loop_options)

add_executable(ev_benchmark_threaded benchmark_threaded.cpp)
target_link_libraries(ev_benchmark_threaded PRIVATE ev_loop::ev_loop ev_loop::ev_loop_warnings ev_loop::ev_loop_options)