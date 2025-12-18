// Benchmark for GroupEventLoop performance
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
// Single-group receivers (all receivers in one thread)
// =============================================================================

struct ReceiverA
{
  using receives = ev_loop::type_list<Pong>;
  using emits = ev_loop::type_list<Ping>;

  // cppcheck-suppress functionStatic
  template<typename Dispatcher> void on_event(Pong event, Dispatcher& dispatcher)
  {
    dispatcher.emit(Ping{ event.value + 1 });
  }
};

struct ReceiverB
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
// Multi-group receivers (separate threads)
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
    dispatcher.emit(Pong{ event.value });
  }
};

// =============================================================================
// Ping-Pong receivers (terminating after N iterations)
// =============================================================================

inline constexpr int kPingPongLimit = 1'000'000;

// Single-group ping-pong
struct PingPongA
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

struct PingPongB
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

// Multi-group ping-pong (2 groups, 2 threads)
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
// MPSC scenario: 2 producers emit to 1 collector (3 groups, 3 threads)
// =============================================================================

struct MpscEvent
{
  int value;
};

struct MpscAck
{
  int value;
};

// Producer 1: receives Ack, emits MpscEvent
struct MpscProducer1
{
  using receives = ev_loop::type_list<MpscAck>;
  using emits = ev_loop::type_list<MpscEvent>;
  std::atomic<int> count{ 0 };

  template<typename Dispatcher> void on_event(MpscAck event, Dispatcher& dispatcher)
  {
    count.fetch_add(1, std::memory_order_relaxed);
    if (event.value < kPingPongLimit) { dispatcher.emit(MpscEvent{ event.value + 1 }); }
  }
};

// Producer 2: receives Ack, emits MpscEvent (same as Producer1 → MPSC to Collector)
struct MpscProducer2
{
  using receives = ev_loop::type_list<MpscAck>;
  using emits = ev_loop::type_list<MpscEvent>;
  std::atomic<int> count{ 0 };

  template<typename Dispatcher> void on_event(MpscAck event, Dispatcher& dispatcher)
  {
    count.fetch_add(1, std::memory_order_relaxed);
    if (event.value < kPingPongLimit) { dispatcher.emit(MpscEvent{ event.value + 1 }); }
  }
};

// Producer 3: receives Ack, emits MpscEvent
struct MpscProducer3
{
  using receives = ev_loop::type_list<MpscAck>;
  using emits = ev_loop::type_list<MpscEvent>;
  std::atomic<int> count{ 0 };

  template<typename Dispatcher> void on_event(MpscAck event, Dispatcher& dispatcher)
  {
    count.fetch_add(1, std::memory_order_relaxed);
    if (event.value < kPingPongLimit) { dispatcher.emit(MpscEvent{ event.value + 1 }); }
  }
};

// Producer 4: receives Ack, emits MpscEvent
struct MpscProducer4
{
  using receives = ev_loop::type_list<MpscAck>;
  using emits = ev_loop::type_list<MpscEvent>;
  std::atomic<int> count{ 0 };

  template<typename Dispatcher> void on_event(MpscAck event, Dispatcher& dispatcher)
  {
    count.fetch_add(1, std::memory_order_relaxed);
    if (event.value < kPingPongLimit) { dispatcher.emit(MpscEvent{ event.value + 1 }); }
  }
};

// Collector: receives MpscEvent from all producers, emits Ack back to all
struct MpscCollector
{
  using receives = ev_loop::type_list<MpscEvent>;
  using emits = ev_loop::type_list<MpscAck>;
  std::atomic<int> count{ 0 };

  template<typename Dispatcher> void on_event(MpscEvent event, Dispatcher& dispatcher)
  {
    count.fetch_add(1, std::memory_order_relaxed);
    dispatcher.emit(MpscAck{ event.value });
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

  std::println("=== GroupEventLoop Benchmark ===");
  std::println("Poll iterations: {}", kIterations);
  std::println("Ping-pong limit: {}\n", kPingPongLimit);

  // =====================================================================
  // Single-thread benchmarks (manual polling)
  // =====================================================================
  std::println("=== Single-Thread (Manual Poll) ===\n");

  // ---------------------------------------------------------------------
  // SpinGroup - manual polling
  // ---------------------------------------------------------------------
  {
    ev_loop::GroupEventLoop<ev_loop::SpinGroup<ReceiverA, ReceiverB>> loop;
    loop.emit(Ping{ 0 });

    const auto started = steady_clock::now();
    for (int i = 0; i < kIterations; ++i) { std::ignore = loop.poll_group<0>(); }
    const auto elapsed = steady_clock::now() - started;

    const auto eps = events_per_second(kIterations, elapsed);
    std::println("SpinGroup:   {:>12} events/sec  ({:>3} ns/event)", eps, ns_per_event(kIterations, elapsed));
  }

  // ---------------------------------------------------------------------
  // YieldGroup - manual polling
  // ---------------------------------------------------------------------
  {
    ev_loop::GroupEventLoop<ev_loop::YieldGroup<ReceiverA, ReceiverB>> loop;
    loop.emit(Ping{ 0 });

    const auto started = steady_clock::now();
    for (int i = 0; i < kIterations; ++i) { std::ignore = loop.poll_group<0>(); }
    const auto elapsed = steady_clock::now() - started;

    const auto eps = events_per_second(kIterations, elapsed);
    std::println("YieldGroup:  {:>12} events/sec  ({:>3} ns/event)", eps, ns_per_event(kIterations, elapsed));
  }

  // =====================================================================
  // Multi-thread benchmarks (background threads)
  // =====================================================================
  std::println("\n=== Multi-Thread (2 Groups, 2 Threads) ===\n");

  constexpr int kMultiIterations = 1'000'000;

  // ---------------------------------------------------------------------
  // 2 SpinGroups - each on own thread
  // ---------------------------------------------------------------------
  {
    ev_loop::GroupEventLoop<ev_loop::SpinGroup<GroupA>, ev_loop::SpinGroup<GroupB>> loop;
    loop.emit(Ping{ 0 }); // Emit before start (only safe before start)

    const auto started = steady_clock::now();
    loop.start();

    while (loop.get<GroupA>().count.load(std::memory_order_relaxed) < kMultiIterations) { std::this_thread::yield(); }
    const auto elapsed = steady_clock::now() - started;

    const int total_events = loop.get<GroupA>().count.load() + loop.get<GroupB>().count.load();
    const auto eps = events_per_second(total_events, elapsed);
    std::println("SpinGroup x2:  {:>12} events/sec  ({:>3} ns/event)", eps, ns_per_event(total_events, elapsed));

    loop.stop();
  }

  // ---------------------------------------------------------------------
  // 2 WaitGroups - each on own thread
  // ---------------------------------------------------------------------
  {
    ev_loop::GroupEventLoop<ev_loop::WaitGroup<GroupA>, ev_loop::WaitGroup<GroupB>> loop;
    loop.emit(Ping{ 0 }); // Emit before start (only safe before start)

    const auto started = steady_clock::now();
    loop.start();

    while (loop.get<GroupA>().count.load(std::memory_order_relaxed) < kMultiIterations) { std::this_thread::yield(); }
    const auto elapsed = steady_clock::now() - started;

    const int total_events = loop.get<GroupA>().count.load() + loop.get<GroupB>().count.load();
    const auto eps = events_per_second(total_events, elapsed);
    std::println("WaitGroup x2:  {:>12} events/sec  ({:>3} ns/event)", eps, ns_per_event(total_events, elapsed));

    loop.stop();
  }

  // =====================================================================
  // Ping-Pong benchmarks (true back-and-forth until limit)
  // =====================================================================
  std::println("\n=== Ping-Pong (Until {} Events) ===\n", kPingPongLimit * 2);

  // ---------------------------------------------------------------------
  // Single group ping-pong (intra-group dispatch)
  // ---------------------------------------------------------------------
  {
    ev_loop::GroupEventLoop<ev_loop::SpinGroup<PingPongA, PingPongB>> loop;

    const auto started = steady_clock::now();
    loop.emit(Ping{ 0 });

    while (loop.get<PingPongA>().count < kPingPongLimit) { std::ignore = loop.poll_group<0>(); }
    const auto elapsed = steady_clock::now() - started;

    const int total = loop.get<PingPongA>().count + loop.get<PingPongB>().count;
    const auto eps = events_per_second(total, elapsed);
    std::println("1 Group (intra-group):  {:>12} events/sec  ({:>3} ns/event)", eps, ns_per_event(total, elapsed));
  }

  // ---------------------------------------------------------------------
  // Two groups ping-pong (inter-group dispatch via queues) - SPSC
  // ---------------------------------------------------------------------
  {
    ev_loop::GroupEventLoop<ev_loop::SpinGroup<PingPongGrpA>, ev_loop::SpinGroup<PingPongGrpB>> loop;
    loop.emit(Ping{ 0 }); // Emit before start (only safe before start)

    const auto started = steady_clock::now();
    loop.start();

    while (loop.get<PingPongGrpA>().count.load(std::memory_order_relaxed) < kPingPongLimit) {
      std::this_thread::yield();
    }
    const auto elapsed = steady_clock::now() - started;

    const int total = loop.get<PingPongGrpA>().count.load() + loop.get<PingPongGrpB>().count.load();
    const auto eps = events_per_second(total, elapsed);
    std::println("2 Groups SPSC:          {:>12} events/sec  ({:>3} ns/event)", eps, ns_per_event(total, elapsed));

    loop.stop();
  }

  // ---------------------------------------------------------------------
  // Three groups: 2 producers → 1 collector (MPSC queue auto-selected)
  // Producer1 and Producer2 both emit MpscEvent to Collector
  // ---------------------------------------------------------------------
  {
    using Loop = ev_loop::GroupEventLoop<ev_loop::SpinGroup<MpscProducer1>,
      ev_loop::SpinGroup<MpscProducer2>,
      ev_loop::SpinGroup<MpscCollector>>;
    Loop loop;

    // Kick off both producers BEFORE start (emit is only safe before start)
    loop.emit(MpscAck{ 0 });

    const auto started = steady_clock::now();
    loop.start();

    // Wait for collector to process enough events
    while (loop.get<MpscCollector>().count.load(std::memory_order_relaxed) < kPingPongLimit) {
      std::this_thread::yield();
    }
    const auto elapsed = steady_clock::now() - started;

    const int total = loop.get<MpscProducer1>().count.load() + loop.get<MpscProducer2>().count.load()
                      + loop.get<MpscCollector>().count.load();
    const auto eps = events_per_second(total, elapsed);
    std::println("3 Groups MPSC (2→1):    {:>12} events/sec  ({:>3} ns/event)", eps, ns_per_event(total, elapsed));

    loop.stop();
  }

  // ---------------------------------------------------------------------
  // Five groups: 4 producers → 1 collector (MPSC queue auto-selected)
  // ---------------------------------------------------------------------
  {
    using Loop = ev_loop::GroupEventLoop<ev_loop::SpinGroup<MpscProducer1>,
      ev_loop::SpinGroup<MpscProducer2>,
      ev_loop::SpinGroup<MpscProducer3>,
      ev_loop::SpinGroup<MpscProducer4>,
      ev_loop::SpinGroup<MpscCollector>>;
    Loop loop;

    // Kick off all producers BEFORE start (emit is only safe before start)
    loop.emit(MpscAck{ 0 });

    const auto started = steady_clock::now();
    loop.start();

    // Wait for collector to process enough events
    while (loop.get<MpscCollector>().count.load(std::memory_order_relaxed) < kPingPongLimit) {
      std::this_thread::yield();
    }
    const auto elapsed = steady_clock::now() - started;

    const int total = loop.get<MpscProducer1>().count.load() + loop.get<MpscProducer2>().count.load()
                      + loop.get<MpscProducer3>().count.load() + loop.get<MpscProducer4>().count.load()
                      + loop.get<MpscCollector>().count.load();
    const auto eps = events_per_second(total, elapsed);
    std::println("5 Groups MPSC (4→1):    {:>12} events/sec  ({:>3} ns/event)", eps, ns_per_event(total, elapsed));

    loop.stop();
  }

  std::println("\nDone.");
  return 0;
}
