add_executable(ev_benchmark benchmark.cpp)
target_link_libraries(ev_benchmark PRIVATE ev_loop::ev_loop ev_loop::ev_loop_warnings ev_loop::ev_loop_options)
add_test(NAME benchmark COMMAND ev_benchmark)

add_executable(ev_benchmark_threaded benchmark_threaded.cpp)
target_link_libraries(ev_benchmark_threaded PRIVATE ev_loop::ev_loop ev_loop::ev_loop_warnings ev_loop::ev_loop_options)
add_test(NAME benchmark_threaded COMMAND ev_benchmark_threaded)