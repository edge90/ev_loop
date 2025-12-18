#include <chrono>
#include <cstddef>
#include <ev_loop/ev.hpp>
#include <print>
#include <tuple>

// =============================================================================
// Define event types
// =============================================================================

struct Ping
{
  int value;
};

struct Pong
{
  int value;
};

// =============================================================================
// Receivers for single-group benchmarks
// =============================================================================

struct A
{
  using receives = ev_loop::type_list<Pong>;
  using emits = ev_loop::type_list<Ping>;

  // cppcheck-suppress functionStatic
  template<typename Dispatcher> void on_event(Pong event, Dispatcher& dispatcher)
  {
    dispatcher.emit(Ping{ event.value + 1 });
  }
};

struct B
{
  using receives = ev_loop::type_list<Ping>;
  using emits = ev_loop::type_list<Pong>;

  // cppcheck-suppress functionStatic
  template<typename Dispatcher> void on_event(Ping event, Dispatcher& dispatcher)
  {
    dispatcher.emit(Pong{ event.value + 1 });
  }
};

// =============================================================================
// Main
// =============================================================================

namespace {

template<typename Count, typename Duration> auto events_per_second(Count event_count, Duration elapsed) -> long long
{
  using seconds_double = std::chrono::duration<double>;
  const auto seconds = std::chrono::duration_cast<seconds_double>(elapsed).count();
  return static_cast<long long>(static_cast<double>(event_count) / seconds);
}

} // namespace

// NOLINTNEXTLINE(bugprone-exception-escape)
int main()
{
  using namespace std::chrono;

  constexpr int kIterations = 10'000'000;

  std::println("=== GroupEventLoop Strategy Benchmark ===");
  std::println("Iterations: {}\n", kIterations);

  // SpinGroup benchmark
  {
    ev_loop::GroupEventLoop<ev_loop::SpinGroup<A, B>> loop;
    loop.emit(Ping{ 0 });

    const auto started = steady_clock::now();
    for (int i = 0; i < kIterations; ++i) { std::ignore = loop.poll_group<0>(); }
    const auto elapsed = steady_clock::now() - started;

    std::println("SpinGroup:   {:>8} us  ({:>12} events/sec)",
      duration_cast<microseconds>(elapsed).count(),
      events_per_second(kIterations, elapsed));
  }

  // YieldGroup benchmark
  {
    ev_loop::GroupEventLoop<ev_loop::YieldGroup<A, B>> loop;
    loop.emit(Ping{ 0 });

    const auto started = steady_clock::now();
    for (int i = 0; i < kIterations; ++i) { std::ignore = loop.poll_group<0>(); }
    const auto elapsed = steady_clock::now() - started;

    std::println("YieldGroup:  {:>8} us  ({:>12} events/sec)",
      duration_cast<microseconds>(elapsed).count(),
      events_per_second(kIterations, elapsed));
  }

  // HybridGroup benchmark
  {
    ev_loop::GroupEventLoop<ev_loop::HybridGroup<A, B>> loop;
    loop.emit(Ping{ 0 });

    const auto started = steady_clock::now();
    for (int i = 0; i < kIterations; ++i) { std::ignore = loop.poll_group<0>(); }
    const auto elapsed = steady_clock::now() - started;

    std::println("HybridGroup: {:>8} us  ({:>12} events/sec)",
      duration_cast<microseconds>(elapsed).count(),
      events_per_second(kIterations, elapsed));
  }

  // WaitGroup benchmark
  {
    ev_loop::GroupEventLoop<ev_loop::WaitGroup<A, B>> loop;
    loop.emit(Ping{ 0 });

    const auto started = steady_clock::now();
    for (int i = 0; i < kIterations; ++i) { std::ignore = loop.poll_group<0>(); }
    const auto elapsed = steady_clock::now() - started;

    std::println("WaitGroup:   {:>8} us  ({:>12} events/sec)",
      duration_cast<microseconds>(elapsed).count(),
      events_per_second(kIterations, elapsed));
  }

  return 0;
}
