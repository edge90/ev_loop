#include <atomic>
#include <chrono>
#include <ev_loop/ev.hpp>
#include <print>
#include <thread>

namespace {

template<typename Count, typename Duration> auto events_per_second(Count event_count, Duration elapsed) -> long long
{
  using seconds_double = std::chrono::duration<double>;
  const auto seconds = std::chrono::duration_cast<seconds_double>(elapsed).count();
  return static_cast<long long>(static_cast<double>(event_count) / seconds);
}

} // namespace

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
// Benchmark 1: OwnThread C <-> OwnThread D
// =============================================================================

struct C_OwnThread
{
  using receives = ev_loop::type_list<Pong>;
  using emits = ev_loop::type_list<Ping>;
  // cppcheck-suppress unusedStructMember
  static constexpr ev_loop::ThreadMode thread_mode = ev_loop::ThreadMode::OwnThread;

  std::atomic<int>* counter = nullptr;

  template<typename Dispatcher> void on_event(Pong event, Dispatcher& dispatcher)
  {
    if (counter) { counter->fetch_add(1, std::memory_order_relaxed); }
    dispatcher.emit(Ping{ event.value + 1 });
  }
};

struct D_OwnThread
{
  using receives = ev_loop::type_list<Ping>;
  using emits = ev_loop::type_list<Pong>;
  // cppcheck-suppress unusedStructMember
  static constexpr ev_loop::ThreadMode thread_mode = ev_loop::ThreadMode::OwnThread;

  std::atomic<int>* counter = nullptr;

  template<typename Dispatcher> void on_event(Ping event, Dispatcher& dispatcher)
  {
    if (counter) { counter->fetch_add(1, std::memory_order_relaxed); }
    dispatcher.emit(Pong{ event.value + 1 });
  }
};

// =============================================================================
// Benchmark 2: SameThread A -> OwnThread D -> SameThread A
// =============================================================================

struct A_SameThread
{
  using receives = ev_loop::type_list<Pong>;
  using emits = ev_loop::type_list<Ping>;
  // cppcheck-suppress unusedStructMember
  static constexpr ev_loop::ThreadMode thread_mode = ev_loop::ThreadMode::SameThread;

  int counter = 0;
  int last_value = 0;

  template<typename Dispatcher> void on_event(Pong event, Dispatcher& dispatcher)
  {
    ++counter;
    last_value = event.value;
    dispatcher.emit(Ping{ event.value + 1 });
  }
};

struct D_OwnThread_ForMixed
{
  using receives = ev_loop::type_list<Ping>;
  using emits = ev_loop::type_list<Pong>;
  // cppcheck-suppress unusedStructMember
  static constexpr ev_loop::ThreadMode thread_mode = ev_loop::ThreadMode::OwnThread;

  std::atomic<int> counter{ 0 };

  template<typename Dispatcher> void on_event(Ping event, Dispatcher& dispatcher)
  {
    counter.fetch_add(1, std::memory_order_relaxed);
    dispatcher.emit(Pong{ event.value + 1 });
  }
};

// =============================================================================
// Benchmark 3: OwnThread D -> SameThread A -> OwnThread D
// =============================================================================

struct A_SameThread_Relay
{
  using receives = ev_loop::type_list<Pong>;
  using emits = ev_loop::type_list<Ping>;
  // cppcheck-suppress unusedStructMember
  static constexpr ev_loop::ThreadMode thread_mode = ev_loop::ThreadMode::SameThread;

  int counter = 0;

  template<typename Dispatcher> void on_event(Pong event, Dispatcher& dispatcher)
  {
    ++counter;
    dispatcher.emit(Ping{ event.value + 1 });
  }
};

struct D_OwnThread_Starter
{
  using receives = ev_loop::type_list<Ping>;
  using emits = ev_loop::type_list<Pong>;
  // cppcheck-suppress unusedStructMember
  static constexpr ev_loop::ThreadMode thread_mode = ev_loop::ThreadMode::OwnThread;

  std::atomic<int> counter{ 0 };
  std::atomic<int> last_value{ 0 };

  template<typename Dispatcher> void on_event(Ping event, Dispatcher& dispatcher)
  {
    counter.fetch_add(1, std::memory_order_relaxed);
    last_value.store(event.value, std::memory_order_relaxed);
    dispatcher.emit(Pong{ event.value + 1 });
  }
};

// =============================================================================
// Main
// =============================================================================

namespace {
constexpr int kOwnThreadTargetCount = 10'000'000;
constexpr int kMixedTargetCount = 1'000'000;
} // namespace

void benchmark_ownthread_to_ownthread()
{
  std::println("=== Benchmark 1: OwnThread C <-> OwnThread D ===");

  ev_loop::EventLoop<C_OwnThread, D_OwnThread> loop;

  std::atomic<int> counter{ 0 };
  loop.get<C_OwnThread>().counter = &counter;
  loop.get<D_OwnThread>().counter = &counter;

  loop.start();

  using namespace std::chrono;

  loop.emit(Ping{ 0 });

  const auto started = steady_clock::now();

  while (counter.load(std::memory_order_relaxed) < kOwnThreadTargetCount) { std::this_thread::yield(); }

  const auto elapsed = steady_clock::now() - started;

  loop.stop();

  const auto final_count = counter.load();
  std::println("  Events:     {}", final_count);
  std::println("  Time:       {} us", duration_cast<microseconds>(elapsed).count());
  std::println("  Throughput: {} events/sec\n", events_per_second(final_count, elapsed));
}

void benchmark_samethread_to_ownthread()
{
  std::println("=== Benchmark 2: SameThread A -> OwnThread D -> A ===");

  using Loop = ev_loop::EventLoop<A_SameThread, D_OwnThread_ForMixed>;
  using namespace std::chrono;

  // Test with Spin strategy
  {
    Loop loop;
    loop.start();
    loop.emit(Ping{ 0 });
    const auto started = steady_clock::now();

    ev_loop::Spin{ loop }.run_while([&] { return loop.get<A_SameThread>().counter < kMixedTargetCount; });

    const auto elapsed = steady_clock::now() - started;
    loop.stop();
    const auto total = loop.get<A_SameThread>().counter + loop.get<D_OwnThread_ForMixed>().counter.load();
    std::println("  Spin:   {} events/sec", events_per_second(total, elapsed));
  }

  // Test with Yield strategy
  {
    Loop loop;
    loop.start();
    loop.emit(Ping{ 0 });
    const auto started = steady_clock::now();

    ev_loop::Yield{ loop }.run_while([&] { return loop.get<A_SameThread>().counter < kMixedTargetCount; });

    const auto elapsed = steady_clock::now() - started;
    loop.stop();
    const auto total = loop.get<A_SameThread>().counter + loop.get<D_OwnThread_ForMixed>().counter.load();
    std::println("  Yield:  {} events/sec", events_per_second(total, elapsed));
  }

  // Test with Wait strategy
  {
    Loop loop;
    loop.start();
    loop.emit(Ping{ 0 });
    const auto started = steady_clock::now();

    ev_loop::Wait{ loop }.run_while([&] { return loop.get<A_SameThread>().counter < kMixedTargetCount; });

    const auto elapsed = steady_clock::now() - started;
    loop.stop();
    const auto total = loop.get<A_SameThread>().counter + loop.get<D_OwnThread_ForMixed>().counter.load();
    std::println("  Wait:   {} events/sec\n", events_per_second(total, elapsed));
  }
}

void benchmark_ownthread_to_samethread()
{
  std::println("=== Benchmark 3: OwnThread D -> SameThread A -> D ===");

  using Loop = ev_loop::EventLoop<A_SameThread_Relay, D_OwnThread_Starter>;
  using namespace std::chrono;

  // Test with Spin strategy
  {
    Loop loop;
    loop.start();
    loop.emit(Pong{ 0 });
    const auto started = steady_clock::now();

    ev_loop::Spin{ loop }.run_while(
      [&] { return loop.get<D_OwnThread_Starter>().counter.load(std::memory_order_relaxed) < kMixedTargetCount; });

    const auto elapsed = steady_clock::now() - started;
    loop.stop();
    const auto total = loop.get<A_SameThread_Relay>().counter + loop.get<D_OwnThread_Starter>().counter.load();
    std::println("  Spin:   {} events/sec", events_per_second(total, elapsed));
  }

  // Test with Yield strategy
  {
    Loop loop;
    loop.start();
    loop.emit(Pong{ 0 });
    const auto started = steady_clock::now();

    ev_loop::Yield{ loop }.run_while(
      [&] { return loop.get<D_OwnThread_Starter>().counter.load(std::memory_order_relaxed) < kMixedTargetCount; });

    const auto elapsed = steady_clock::now() - started;
    loop.stop();
    const auto total = loop.get<A_SameThread_Relay>().counter + loop.get<D_OwnThread_Starter>().counter.load();
    std::println("  Yield:  {} events/sec", events_per_second(total, elapsed));
  }

  // Test with Wait strategy
  {
    Loop loop;
    loop.start();
    loop.emit(Pong{ 0 });
    const auto started = steady_clock::now();

    ev_loop::Wait{ loop }.run_while(
      [&] { return loop.get<D_OwnThread_Starter>().counter.load(std::memory_order_relaxed) < kMixedTargetCount; });

    const auto elapsed = steady_clock::now() - started;
    loop.stop();
    const auto total = loop.get<A_SameThread_Relay>().counter + loop.get<D_OwnThread_Starter>().counter.load();
    std::println("  Wait:   {} events/sec\n", events_per_second(total, elapsed));
  }
}

// NOLINTNEXTLINE(bugprone-exception-escape) - std::println may throw but we accept that in benchmarks
int main()
{
  std::println("");
  benchmark_ownthread_to_ownthread();
  benchmark_samethread_to_ownthread();
  benchmark_ownthread_to_samethread();
  return 0;
}
