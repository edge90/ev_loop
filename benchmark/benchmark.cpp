#include <chrono>
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
// Same-thread receiver: Logger
// Runs on the event loop thread, receives via queue dispatch
// =============================================================================

struct A
{
  using receives = ev_loop::type_list<Pong>;
  using emits = ev_loop::type_list<Ping>;
  // cppcheck-suppress unusedStructMember
  static constexpr ev_loop::ThreadMode thread_mode = ev_loop::ThreadMode::SameThread;

  // cppcheck-suppress functionStatic ; on_event must be member function for ev library
  template<typename Dispatcher> void on_event(Pong event, Dispatcher &dispatcher)
  {
    dispatcher.emit(Ping{ event.value + 1 });
  }
};

// =============================================================================
// Same-thread receiver: Controller
// Coordinates the system, emits events to start processing
// =============================================================================

struct B
{
  using receives = ev_loop::type_list<Ping>;
  using emits = ev_loop::type_list<Pong>;
  // cppcheck-suppress unusedStructMember
  static constexpr ev_loop::ThreadMode thread_mode = ev_loop::ThreadMode::SameThread;

  // cppcheck-suppress functionStatic ; on_event must be member function for ev library
  template<typename Dispatcher> void on_event(Ping event, Dispatcher &dispatcher)
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
  auto seconds = std::chrono::duration_cast<seconds_double>(elapsed).count();
  return static_cast<long long>(static_cast<double>(event_count) / seconds);
}

}// namespace

// NOLINTNEXTLINE(bugprone-exception-escape) - std::println may throw but we accept that in benchmarks
int main()
{
  ev_loop::EventLoop<A, B> loop;

  loop.start();

  loop.emit(Ping{ 0 });

  using namespace std::chrono;

  constexpr int kIterations = 10'000'000;

  ev_loop::Spin strategy{ loop };
  auto started = steady_clock::now();
  for (int i = 0; i < kIterations; ++i) { std::ignore = strategy.poll(); }
  auto elapsed = steady_clock::now() - started;

  std::println("poll(): {} us ({} events/sec)",
    duration_cast<microseconds>(elapsed).count(),
    events_per_second(kIterations, elapsed));

  loop.stop();

  return 0;
}
