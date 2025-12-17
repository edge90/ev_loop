// Benchmark comparing old EventLoop (SameThread) vs new GroupEventLoop
#include <atomic>
#include <chrono>
#include <cstddef>
#include <ev_loop/ev.hpp>
#include <print>
#include <thread>
#include <tuple>

// =============================================================================
// Event types
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
// Old-style receivers (with thread_mode for EventLoop)
// =============================================================================

struct OldA
{
  using receives = ev_loop::type_list<Pong>;
  using emits = ev_loop::type_list<Ping>;
  // cppcheck-suppress unusedStructMember
  using thread_mode = ev_loop::SameThread;

  // cppcheck-suppress functionStatic
  template<typename Dispatcher> void on_event(Pong event, Dispatcher& dispatcher)
  {
    dispatcher.emit(Ping{ event.value + 1 });
  }
};

struct OldB
{
  using receives = ev_loop::type_list<Ping>;
  using emits = ev_loop::type_list<Pong>;
  // cppcheck-suppress unusedStructMember
  using thread_mode = ev_loop::SameThread;

  // cppcheck-suppress functionStatic
  template<typename Dispatcher> void on_event(Ping event, Dispatcher& dispatcher)
  {
    dispatcher.emit(Pong{ event.value + 1 });
  }
};

// =============================================================================
// New-style receivers (no thread_mode - grouping is external)
// =============================================================================

struct NewA
{
  using receives = ev_loop::type_list<Pong>;
  using emits = ev_loop::type_list<Ping>;

  // cppcheck-suppress functionStatic
  template<typename Dispatcher> void on_event(Pong event, Dispatcher& dispatcher)
  {
    dispatcher.emit(Ping{ event.value + 1 });
  }
};

struct NewB
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
// Multi-thread receivers for old EventLoop (OwnThread)
// =============================================================================

struct OwnA
{
  using receives = ev_loop::type_list<Pong>;
  using emits = ev_loop::type_list<Ping>;
  // cppcheck-suppress unusedStructMember
  using thread_mode = ev_loop::OwnThread;
  std::atomic<int> count{ 0 };

  template<typename Dispatcher> void on_event(Pong event, Dispatcher& dispatcher)
  {
    count.fetch_add(1, std::memory_order_relaxed);
    dispatcher.emit(Ping{ event.value + 1 });
  }
};

struct OwnB
{
  using receives = ev_loop::type_list<Ping>;
  using emits = ev_loop::type_list<Pong>;
  // cppcheck-suppress unusedStructMember
  using thread_mode = ev_loop::OwnThread;
  std::atomic<int> count{ 0 };

  template<typename Dispatcher> void on_event(Ping event, Dispatcher& dispatcher)
  {
    count.fetch_add(1, std::memory_order_relaxed);
    dispatcher.emit(Pong{ event.value + 1 });
  }
};

// =============================================================================
// Multi-thread receivers for new GroupEventLoop (no thread_mode)
// =============================================================================

struct GroupA
{
  using receives = ev_loop::type_list<Pong>;
  using emits = ev_loop::type_list<Ping>;
  std::atomic<int> count{ 0 };

  template<typename Dispatcher> void on_event(Pong event, Dispatcher& dispatcher)
  {
    count.fetch_add(1, std::memory_order_relaxed);
    dispatcher.emit(Ping{ event.value + 1 });
  }
};

struct GroupB
{
  using receives = ev_loop::type_list<Ping>;
  using emits = ev_loop::type_list<Pong>;
  std::atomic<int> count{ 0 };

  template<typename Dispatcher> void on_event(Ping event, Dispatcher& dispatcher)
  {
    count.fetch_add(1, std::memory_order_relaxed);
    dispatcher.emit(Pong{ event.value + 1 });
  }
};

// =============================================================================
// Ping-Pong receivers (terminating after N iterations)
// =============================================================================

inline constexpr int kPingPongLimit = 1'000'000;

// Old-style (SameThread) ping-pong
struct PingPongOldA
{
  using receives = ev_loop::type_list<Pong>;
  using emits = ev_loop::type_list<Ping>;
  using thread_mode = ev_loop::SameThread;
  int count = 0;

  template<typename Dispatcher> void on_event(Pong event, Dispatcher& dispatcher)
  {
    ++count;
    if (event.value < kPingPongLimit) { dispatcher.emit(Ping{ event.value + 1 }); }
  }
};

struct PingPongOldB
{
  using receives = ev_loop::type_list<Ping>;
  using emits = ev_loop::type_list<Pong>;
  using thread_mode = ev_loop::SameThread;
  int count = 0;

  template<typename Dispatcher> void on_event(Ping event, Dispatcher& dispatcher)
  {
    ++count;
    dispatcher.emit(Pong{ event.value });
  }
};

// New-style (no thread_mode) ping-pong for single group
struct PingPongNewA
{
  using receives = ev_loop::type_list<Pong>;
  using emits = ev_loop::type_list<Ping>;
  int count = 0;

  template<typename Dispatcher> void on_event(Pong event, Dispatcher& dispatcher)
  {
    ++count;
    if (event.value < kPingPongLimit) { dispatcher.emit(Ping{ event.value + 1 }); }
  }
};

struct PingPongNewB
{
  using receives = ev_loop::type_list<Ping>;
  using emits = ev_loop::type_list<Pong>;
  int count = 0;

  template<typename Dispatcher> void on_event(Ping event, Dispatcher& dispatcher)
  {
    ++count;
    dispatcher.emit(Pong{ event.value });
  }
};

// Old-style (OwnThread) ping-pong for multi-thread
struct PingPongOwnA
{
  using receives = ev_loop::type_list<Pong>;
  using emits = ev_loop::type_list<Ping>;
  using thread_mode = ev_loop::OwnThread;
  std::atomic<int> count{ 0 };

  template<typename Dispatcher> void on_event(Pong event, Dispatcher& dispatcher)
  {
    count.fetch_add(1, std::memory_order_relaxed);
    if (event.value < kPingPongLimit) { dispatcher.emit(Ping{ event.value + 1 }); }
  }
};

struct PingPongOwnB
{
  using receives = ev_loop::type_list<Ping>;
  using emits = ev_loop::type_list<Pong>;
  using thread_mode = ev_loop::OwnThread;
  std::atomic<int> count{ 0 };

  template<typename Dispatcher> void on_event(Ping event, Dispatcher& dispatcher)
  {
    count.fetch_add(1, std::memory_order_relaxed);
    dispatcher.emit(Pong{ event.value });
  }
};

// New-style ping-pong for multi-group (2 groups, 2 threads)
struct PingPongGrpA
{
  using receives = ev_loop::type_list<Pong>;
  using emits = ev_loop::type_list<Ping>;
  std::atomic<int> count{ 0 };

  template<typename Dispatcher> void on_event(Pong event, Dispatcher& dispatcher)
  {
    count.fetch_add(1, std::memory_order_relaxed);
    if (event.value < kPingPongLimit) { dispatcher.emit(Ping{ event.value + 1 }); }
  }
};

struct PingPongGrpB
{
  using receives = ev_loop::type_list<Ping>;
  using emits = ev_loop::type_list<Pong>;
  std::atomic<int> count{ 0 };

  template<typename Dispatcher> void on_event(Ping event, Dispatcher& dispatcher)
  {
    count.fetch_add(1, std::memory_order_relaxed);
    dispatcher.emit(Pong{ event.value });
  }
};

// =============================================================================
// Helpers
// =============================================================================

namespace {

template<typename Count, typename Duration> auto events_per_second(Count event_count, Duration elapsed) -> long long
{
  using seconds_double = std::chrono::duration<double>;
  const auto seconds = std::chrono::duration_cast<seconds_double>(elapsed).count();
  return static_cast<long long>(static_cast<double>(event_count) / seconds);
}

template<typename Count, typename Duration> auto ns_per_event(Count event_count, Duration elapsed) -> long long
{
  const auto total_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count();
  return total_ns / static_cast<long long>(event_count);
}

} // namespace

// =============================================================================
// Main
// =============================================================================

// NOLINTNEXTLINE(bugprone-exception-escape)
int main()
{
  using namespace std::chrono;

  constexpr int kIterations = 10'000'000;

  std::println("=== EventLoop vs GroupEventLoop Comparison ===");
  std::println("Iterations: {}\n", kIterations);

  // ---------------------------------------------------------------------
  // Old EventLoop with Spin strategy
  // ---------------------------------------------------------------------
  long long old_spin_eps = 0;
  {
    ev_loop::EventLoop<OldA, OldB> loop;
    loop.start();
    loop.emit(Ping{ 0 });

    ev_loop::SpinRunner strategy{ loop };
    const auto started = steady_clock::now();
    for (int i = 0; i < kIterations; ++i) { std::ignore = strategy.poll(); }
    const auto elapsed = steady_clock::now() - started;

    old_spin_eps = events_per_second(kIterations, elapsed);
    std::println(
      "Old EventLoop (Spin):       {:>10} events/sec  ({} ns/event)", old_spin_eps, ns_per_event(kIterations, elapsed));

    loop.stop();
  }

  // ---------------------------------------------------------------------
  // New GroupEventLoop with Spin strategy (single group)
  // Manual polling from main thread (no background threads)
  // ---------------------------------------------------------------------
  long long new_spin_eps = 0;
  {
    ev_loop::GroupEventLoop<ev_loop::SpinGroup<NewA, NewB>> loop;
    // Don't call start() - we poll manually from main thread
    loop.emit(Ping{ 0 });

    const auto started = steady_clock::now();
    for (int i = 0; i < kIterations; ++i) { std::ignore = loop.poll_group<0>(); }
    const auto elapsed = steady_clock::now() - started;

    new_spin_eps = events_per_second(kIterations, elapsed);
    std::println(
      "New GroupEventLoop (Spin):  {:>10} events/sec  ({} ns/event)", new_spin_eps, ns_per_event(kIterations, elapsed));
    // No stop() needed since we didn't start()
  }

  // ---------------------------------------------------------------------
  // Old EventLoop with Yield strategy
  // ---------------------------------------------------------------------
  long long old_yield_eps = 0;
  {
    ev_loop::EventLoop<OldA, OldB> loop;
    loop.start();
    loop.emit(Ping{ 0 });

    ev_loop::YieldRunner strategy{ loop };
    const auto started = steady_clock::now();
    for (int i = 0; i < kIterations; ++i) { std::ignore = strategy.poll(); }
    const auto elapsed = steady_clock::now() - started;

    old_yield_eps = events_per_second(kIterations, elapsed);
    std::println("Old EventLoop (Yield):      {:>10} events/sec  ({} ns/event)",
      old_yield_eps,
      ns_per_event(kIterations, elapsed));

    loop.stop();
  }

  // ---------------------------------------------------------------------
  // New GroupEventLoop with Yield strategy (single group)
  // Manual polling from main thread (no background threads)
  // ---------------------------------------------------------------------
  long long new_yield_eps = 0;
  {
    ev_loop::GroupEventLoop<ev_loop::YieldGroup<NewA, NewB>> loop;
    // Don't call start() - we poll manually from main thread
    loop.emit(Ping{ 0 });

    const auto started = steady_clock::now();
    for (int i = 0; i < kIterations; ++i) { std::ignore = loop.poll_group<0>(); }
    const auto elapsed = steady_clock::now() - started;

    new_yield_eps = events_per_second(kIterations, elapsed);
    std::println("New GroupEventLoop (Yield): {:>10} events/sec  ({} ns/event)",
      new_yield_eps,
      ns_per_event(kIterations, elapsed));
    // No stop() needed since we didn't start()
  }

  // ---------------------------------------------------------------------
  // Single-thread Summary
  // ---------------------------------------------------------------------
  std::println("\n=== Single-Thread Summary ===");

  const auto spin_ratio = static_cast<double>(new_spin_eps) / static_cast<double>(old_spin_eps);
  const auto yield_ratio = static_cast<double>(new_yield_eps) / static_cast<double>(old_yield_eps);

  std::println("Spin:  New/Old = {:.2f}x {}", spin_ratio, spin_ratio >= 1.0 ? "(faster)" : "(slower)");
  std::println("Yield: New/Old = {:.2f}x {}", yield_ratio, yield_ratio >= 1.0 ? "(faster)" : "(slower)");

  // =====================================================================
  // Multi-thread benchmarks
  // =====================================================================
  std::println("\n=== Multi-Thread Comparison (2 threads) ===");

  constexpr int kMultiIterations = 1'000'000;
  std::println("Iterations: {}\n", kMultiIterations);

  // ---------------------------------------------------------------------
  // Old EventLoop with OwnThread receivers (2 threads ping-ponging)
  // ---------------------------------------------------------------------
  long long old_multi_eps = 0;
  {
    ev_loop::EventLoop<OwnA, OwnB> loop;
    loop.start();

    const auto started = steady_clock::now();
    loop.emit(Ping{ 0 });

    // Wait for enough iterations
    while (loop.get<OwnA>().count.load(std::memory_order_relaxed) < kMultiIterations) { std::this_thread::yield(); }
    const auto elapsed = steady_clock::now() - started;

    const int total_events = loop.get<OwnA>().count.load() + loop.get<OwnB>().count.load();
    old_multi_eps = events_per_second(total_events, elapsed);
    std::println("Old EventLoop (OwnThread):  {:>10} events/sec  ({} ns/event)",
      old_multi_eps,
      ns_per_event(total_events, elapsed));

    loop.stop();
  }

  // ---------------------------------------------------------------------
  // New GroupEventLoop with 2 groups (each on own thread)
  // ---------------------------------------------------------------------
  long long new_multi_eps = 0;
  {
    ev_loop::GroupEventLoop<ev_loop::SpinGroup<GroupA>, ev_loop::SpinGroup<GroupB>> loop;
    loop.start();

    const auto started = steady_clock::now();
    loop.emit(Ping{ 0 });

    // Wait for enough iterations
    while (loop.get<GroupA>().count.load(std::memory_order_relaxed) < kMultiIterations) { std::this_thread::yield(); }
    const auto elapsed = steady_clock::now() - started;

    const int total_events = loop.get<GroupA>().count.load() + loop.get<GroupB>().count.load();
    new_multi_eps = events_per_second(total_events, elapsed);
    std::println("New GroupEventLoop (2 grp): {:>10} events/sec  ({} ns/event)",
      new_multi_eps,
      ns_per_event(total_events, elapsed));

    loop.stop();
  }

  // ---------------------------------------------------------------------
  // Multi-thread Summary
  // ---------------------------------------------------------------------
  std::println("\n=== Multi-Thread Summary ===");

  const auto multi_ratio = static_cast<double>(new_multi_eps) / static_cast<double>(old_multi_eps);
  std::println("2 threads: New/Old = {:.2f}x {}", multi_ratio, multi_ratio >= 1.0 ? "(faster)" : "(slower)");

  // =====================================================================
  // Ping-Pong benchmarks (true back-and-forth)
  // =====================================================================
  std::println("\n=== Ping-Pong Comparison ===");
  std::println("Iterations: {}\n", kPingPongLimit);

  // ---------------------------------------------------------------------
  // Old EventLoop (SameThread) - ping-pong within single thread
  // ---------------------------------------------------------------------
  long long old_st_pingpong_eps = 0;
  {
    ev_loop::EventLoop<PingPongOldA, PingPongOldB> loop;
    loop.start();

    ev_loop::SpinRunner strategy{ loop };
    const auto started = steady_clock::now();
    loop.emit(Ping{ 0 });

    // Poll until done (A stops emitting when count reaches limit)
    while (loop.get<PingPongOldA>().count < kPingPongLimit) { std::ignore = strategy.poll(); }
    const auto elapsed = steady_clock::now() - started;

    const int total = loop.get<PingPongOldA>().count + loop.get<PingPongOldB>().count;
    old_st_pingpong_eps = events_per_second(total, elapsed);
    std::println("Old EventLoop (SameThread): {:>10} events/sec  ({} ns/event)",
      old_st_pingpong_eps,
      ns_per_event(total, elapsed));

    loop.stop();
  }

  // ---------------------------------------------------------------------
  // New GroupEventLoop (single group) - ping-pong within single thread
  // ---------------------------------------------------------------------
  long long new_sg_pingpong_eps = 0;
  {
    ev_loop::GroupEventLoop<ev_loop::SpinGroup<PingPongNewA, PingPongNewB>> loop;

    const auto started = steady_clock::now();
    loop.emit(Ping{ 0 });

    // Poll until done (each poll processes one event from queue)
    while (loop.get<PingPongNewA>().count < kPingPongLimit) { std::ignore = loop.poll_group<0>(); }
    const auto elapsed = steady_clock::now() - started;

    const int total = loop.get<PingPongNewA>().count + loop.get<PingPongNewB>().count;
    new_sg_pingpong_eps = events_per_second(total, elapsed);
    std::println("New GroupEventLoop (1 grp): {:>10} events/sec  ({} ns/event)",
      new_sg_pingpong_eps,
      ns_per_event(total, elapsed));
  }

  // ---------------------------------------------------------------------
  // Single-thread Ping-Pong Summary
  // ---------------------------------------------------------------------
  std::println("\n--- Single-Thread Ping-Pong ---");
  const auto st_pingpong_ratio = static_cast<double>(new_sg_pingpong_eps) / static_cast<double>(old_st_pingpong_eps);
  std::println("New/Old = {:.2f}x {}", st_pingpong_ratio, st_pingpong_ratio >= 1.0 ? "(faster)" : "(slower)");

  // ---------------------------------------------------------------------
  // Old EventLoop (OwnThread) - ping-pong between two threads
  // ---------------------------------------------------------------------
  long long old_mt_pingpong_eps = 0;
  {
    ev_loop::EventLoop<PingPongOwnA, PingPongOwnB> loop;
    loop.start();

    const auto started = steady_clock::now();
    loop.emit(Ping{ 0 });

    while (loop.get<PingPongOwnA>().count.load(std::memory_order_relaxed) < kPingPongLimit) {
      std::this_thread::yield();
    }
    const auto elapsed = steady_clock::now() - started;

    const int total = loop.get<PingPongOwnA>().count.load() + loop.get<PingPongOwnB>().count.load();
    old_mt_pingpong_eps = events_per_second(total, elapsed);
    std::println("Old EventLoop (OwnThread):  {:>10} events/sec  ({} ns/event)",
      old_mt_pingpong_eps,
      ns_per_event(total, elapsed));

    loop.stop();
  }

  // ---------------------------------------------------------------------
  // New GroupEventLoop (2 groups) - ping-pong between two threads
  // ---------------------------------------------------------------------
  long long new_mg_pingpong_eps = 0;
  {
    ev_loop::GroupEventLoop<ev_loop::SpinGroup<PingPongGrpA>, ev_loop::SpinGroup<PingPongGrpB>> loop;
    loop.start();

    const auto started = steady_clock::now();
    loop.emit(Ping{ 0 });

    while (loop.get<PingPongGrpA>().count.load(std::memory_order_relaxed) < kPingPongLimit) {
      std::this_thread::yield();
    }
    const auto elapsed = steady_clock::now() - started;

    const int total = loop.get<PingPongGrpA>().count.load() + loop.get<PingPongGrpB>().count.load();
    new_mg_pingpong_eps = events_per_second(total, elapsed);
    std::println("New GroupEventLoop (2 grp): {:>10} events/sec  ({} ns/event)",
      new_mg_pingpong_eps,
      ns_per_event(total, elapsed));

    loop.stop();
  }

  // ---------------------------------------------------------------------
  // Multi-thread Ping-Pong Summary
  // ---------------------------------------------------------------------
  std::println("\n--- Multi-Thread Ping-Pong ---");
  const auto mt_pingpong_ratio = static_cast<double>(new_mg_pingpong_eps) / static_cast<double>(old_mt_pingpong_eps);
  std::println("New/Old = {:.2f}x {}", mt_pingpong_ratio, mt_pingpong_ratio >= 1.0 ? "(faster)" : "(slower)");

  return 0;
}
