// Benchmark for GroupEventLoop performance
#include <atomic>
#include <chrono>
#include <cstddef>
#include <ev_loop/ev.hpp>
#include <print>
#include <thread>
#include <tuple>
#include <vector>

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
// External emitter benchmark types
// =============================================================================

// External input definitions - each defines what events can be emitted
struct ExternalInputs1
{
  using emits = ev_loop::type_list<Ping>;
};
struct ExternalInputs2
{
  using emits = ev_loop::type_list<Ping>;
};
struct ExternalInputs3
{
  using emits = ev_loop::type_list<Ping>;
};
struct ExternalInputs4
{
  using emits = ev_loop::type_list<Ping>;
};

// Collector that receives external Ping events
struct ExternalCollector
{
  using receives = ev_loop::type_list<Ping>;
  using emits = ev_loop::type_list<>;
  std::atomic<int> count{ 0 };

  template<typename Dispatcher> void on_event(Ping /*event*/, Dispatcher& /*dispatcher*/)
  {
    count.fetch_add(1, std::memory_order_relaxed);
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

// =============================================================================
// Benchmark functions
// =============================================================================

void bench_single_thread(int iterations)
{
  using namespace std::chrono;
  std::println("=== Single-Thread (Manual Poll) ===\n");

  // SpinGroup - manual polling
  {
    using Loop = ev_loop::GroupEventLoop<ev_loop::SpinGroup<ReceiverA, ReceiverB>>;
    auto loop = Loop::setup().prime(Ping{ 0 }).create_unique();

    const auto started = steady_clock::now();
    for (int i = 0; i < iterations; ++i) { std::ignore = loop->poll_group<0>(); }
    const auto elapsed = steady_clock::now() - started;

    const auto eps = events_per_second(iterations, elapsed);
    std::println("SpinGroup:   {:>12} events/sec  ({:>3} ns/event)", eps, ns_per_event(iterations, elapsed));
  }

  // YieldGroup - manual polling
  {
    using Loop = ev_loop::GroupEventLoop<ev_loop::YieldGroup<ReceiverA, ReceiverB>>;
    auto loop = Loop::setup().prime(Ping{ 0 }).create_unique();

    const auto started = steady_clock::now();
    for (int i = 0; i < iterations; ++i) { std::ignore = loop->poll_group<0>(); }
    const auto elapsed = steady_clock::now() - started;

    const auto eps = events_per_second(iterations, elapsed);
    std::println("YieldGroup:  {:>12} events/sec  ({:>3} ns/event)", eps, ns_per_event(iterations, elapsed));
  }
}

void bench_multi_thread()
{
  using namespace std::chrono;
  std::println("\n=== Multi-Thread (2 Groups, 2 Threads) ===\n");

  constexpr int kMultiIterations = 1'000'000;

  // 2 SpinGroups - each on own thread
  {
    using Loop = ev_loop::GroupEventLoop<ev_loop::SpinGroup<GroupA>, ev_loop::SpinGroup<GroupB>>;
    auto loop = Loop::setup().prime(Ping{ 0 }).create_unique();
    loop->start();

    const auto started = steady_clock::now();

    while (loop->get<GroupA>().count.load(std::memory_order_relaxed) < kMultiIterations) { std::this_thread::yield(); }
    const auto elapsed = steady_clock::now() - started;

    const int total_events = loop->get<GroupA>().count.load() + loop->get<GroupB>().count.load();
    const auto eps = events_per_second(total_events, elapsed);
    std::println("SpinGroup x2:  {:>12} events/sec  ({:>3} ns/event)", eps, ns_per_event(total_events, elapsed));

    loop->stop();
  }

  // 2 WaitGroups - each on own thread
  {
    using Loop = ev_loop::GroupEventLoop<ev_loop::WaitGroup<GroupA>, ev_loop::WaitGroup<GroupB>>;
    auto loop = Loop::setup().prime(Ping{ 0 }).create_unique();
    loop->start();

    const auto started = steady_clock::now();

    while (loop->get<GroupA>().count.load(std::memory_order_relaxed) < kMultiIterations) { std::this_thread::yield(); }
    const auto elapsed = steady_clock::now() - started;

    const int total_events = loop->get<GroupA>().count.load() + loop->get<GroupB>().count.load();
    const auto eps = events_per_second(total_events, elapsed);
    std::println("WaitGroup x2:  {:>12} events/sec  ({:>3} ns/event)", eps, ns_per_event(total_events, elapsed));

    loop->stop();
  }
}

void bench_ping_pong()
{
  using namespace std::chrono;
  std::println("\n=== Ping-Pong (Until {} Events) ===\n", kPingPongLimit * 2);

  // Single group ping-pong (intra-group dispatch)
  {
    using Loop = ev_loop::GroupEventLoop<ev_loop::SpinGroup<PingPongA, PingPongB>>;
    auto loop = Loop::setup().prime(Ping{ 0 }).create_unique();

    const auto started = steady_clock::now();

    while (loop->get<PingPongA>().count < kPingPongLimit) { std::ignore = loop->poll_group<0>(); }
    const auto elapsed = steady_clock::now() - started;

    const int total = loop->get<PingPongA>().count + loop->get<PingPongB>().count;
    const auto eps = events_per_second(total, elapsed);
    std::println("1 Group (intra-group):  {:>12} events/sec  ({:>3} ns/event)", eps, ns_per_event(total, elapsed));
  }

  // Two groups ping-pong (inter-group dispatch via queues) - SPSC
  {
    using Loop = ev_loop::GroupEventLoop<ev_loop::SpinGroup<PingPongGrpA>, ev_loop::SpinGroup<PingPongGrpB>>;
    auto loop = Loop::setup().prime(Ping{ 0 }).create_unique();
    loop->start();

    const auto started = steady_clock::now();

    while (loop->get<PingPongGrpA>().count.load(std::memory_order_relaxed) < kPingPongLimit) {
      std::this_thread::yield();
    }
    const auto elapsed = steady_clock::now() - started;

    const int total = loop->get<PingPongGrpA>().count.load() + loop->get<PingPongGrpB>().count.load();
    const auto eps = events_per_second(total, elapsed);
    std::println("2 Groups SPSC:          {:>12} events/sec  ({:>3} ns/event)", eps, ns_per_event(total, elapsed));

    loop->stop();
  }

  // Three groups: 2 producers → 1 collector (MPSC queue auto-selected)
  {
    using Loop = ev_loop::GroupEventLoop<ev_loop::SpinGroup<MpscProducer1>,
      ev_loop::SpinGroup<MpscProducer2>,
      ev_loop::SpinGroup<MpscCollector>>;
    auto loop = Loop::setup().prime(MpscAck{ 0 }).create_unique();
    loop->start();

    const auto started = steady_clock::now();

    while (loop->get<MpscCollector>().count.load(std::memory_order_relaxed) < kPingPongLimit) {
      std::this_thread::yield();
    }
    const auto elapsed = steady_clock::now() - started;

    const int total = loop->get<MpscProducer1>().count.load() + loop->get<MpscProducer2>().count.load()
                      + loop->get<MpscCollector>().count.load();
    const auto eps = events_per_second(total, elapsed);
    std::println("3 Groups MPSC (2→1):    {:>12} events/sec  ({:>3} ns/event)", eps, ns_per_event(total, elapsed));

    loop->stop();
  }

  // Five groups: 4 producers → 1 collector (MPSC queue auto-selected)
  {
    using Loop = ev_loop::GroupEventLoop<ev_loop::SpinGroup<MpscProducer1>,
      ev_loop::SpinGroup<MpscProducer2>,
      ev_loop::SpinGroup<MpscProducer3>,
      ev_loop::SpinGroup<MpscProducer4>,
      ev_loop::SpinGroup<MpscCollector>>;
    auto loop = Loop::setup().prime(MpscAck{ 0 }).create_unique();
    loop->start();

    const auto started = steady_clock::now();

    while (loop->get<MpscCollector>().count.load(std::memory_order_relaxed) < kPingPongLimit) {
      std::this_thread::yield();
    }
    const auto elapsed = steady_clock::now() - started;

    const int total = loop->get<MpscProducer1>().count.load() + loop->get<MpscProducer2>().count.load()
                      + loop->get<MpscProducer3>().count.load() + loop->get<MpscProducer4>().count.load()
                      + loop->get<MpscCollector>().count.load();
    const auto eps = events_per_second(total, elapsed);
    std::println("5 Groups MPSC (4→1):    {:>12} events/sec  ({:>3} ns/event)", eps, ns_per_event(total, elapsed));

    loop->stop();
  }
}

void bench_external_1_thread()
{
  using namespace std::chrono;

  using Loop = ev_loop::GroupEventLoop<ev_loop::SpinGroup<ExternalCollector>, ev_loop::ExternalGroup<ExternalInputs1>>;
  auto loop = Loop::setup().create_unique();
  auto emitter = loop->get_external_emitter<ExternalInputs1>();

  constexpr int kExternalEvents = 100'000;

  // External thread emits events with retry on queue full
  std::thread external_thread([&emitter]() {
    for (int i = 0; i < kExternalEvents; ++i) {
      while (!emitter.emit(Ping{ i })) { std::this_thread::yield(); }
    }
  });

  const auto started = steady_clock::now();

  // Main thread polls external inbox
  while (loop->get<ExternalCollector>().count.load(std::memory_order_relaxed) < kExternalEvents) {
    loop->poll_external();
    loop->poll_group<0>();
  }
  const auto elapsed = steady_clock::now() - started;

  external_thread.join();

  const int total = loop->get<ExternalCollector>().count.load();
  const auto eps = events_per_second(total, elapsed);
  std::println("1 External Thread:      {:>12} events/sec  ({:>3} ns/event)", eps, ns_per_event(total, elapsed));
}

void bench_external_4_threads_1_group()
{
  using namespace std::chrono;

  using Loop = ev_loop::GroupEventLoop<ev_loop::SpinGroup<ExternalCollector>, ev_loop::ExternalGroup<ExternalInputs1>>;
  auto loop = Loop::setup().create_unique();
  auto emitter = loop->get_external_emitter<ExternalInputs1>();

  constexpr int kEventsPerThread = 100'000;
  constexpr int kNumThreads = 4;
  constexpr int kTotalEvents = kEventsPerThread * kNumThreads;

  std::vector<std::thread> threads;
  threads.reserve(kNumThreads);

  const auto started = steady_clock::now();

  for (int idx = 0; idx < kNumThreads; ++idx) {
    threads.emplace_back([emitter]() {
      for (int i = 0; i < kEventsPerThread; ++i) {
        while (!emitter.emit(Ping{ i })) { std::this_thread::yield(); }
      }
    });
  }

  while (loop->get<ExternalCollector>().count.load(std::memory_order_relaxed) < kTotalEvents) {
    loop->poll_external();
    loop->poll_group<0>();
  }
  const auto elapsed = steady_clock::now() - started;

  for (auto& thread : threads) { thread.join(); }

  const int total = loop->get<ExternalCollector>().count.load();
  const auto eps = events_per_second(total, elapsed);
  std::println("4 Ext (1 group):        {:>12} events/sec  ({:>3} ns/event)", eps, ns_per_event(total, elapsed));
}

void bench_external_4_threads_2_groups()
{
  using namespace std::chrono;

  using Loop = ev_loop::GroupEventLoop<ev_loop::SpinGroup<ExternalCollector>,
    ev_loop::ExternalGroup<ExternalInputs1>,
    ev_loop::ExternalGroup<ExternalInputs2>>;
  auto loop = Loop::setup().create_unique();
  auto emitter1 = loop->get_external_emitter<ExternalInputs1>();
  auto emitter2 = loop->get_external_emitter<ExternalInputs2>();

  constexpr int kEventsPerThread = 100'000;
  constexpr int kThreadsPerGroup = 2;
  constexpr int kTotalEvents = kEventsPerThread * kThreadsPerGroup * 2;

  std::vector<std::thread> threads;
  threads.reserve(static_cast<std::size_t>(kThreadsPerGroup) * 2);

  const auto started = steady_clock::now();

  // 2 threads for ExternalGroup 1
  for (int idx = 0; idx < kThreadsPerGroup; ++idx) {
    threads.emplace_back([emitter1]() {
      for (int i = 0; i < kEventsPerThread; ++i) {
        while (!emitter1.emit(Ping{ i })) { std::this_thread::yield(); }
      }
    });
  }

  // 2 threads for ExternalGroup 2
  for (int idx = 0; idx < kThreadsPerGroup; ++idx) {
    threads.emplace_back([emitter2]() {
      for (int i = 0; i < kEventsPerThread; ++i) {
        while (!emitter2.emit(Ping{ i })) { std::this_thread::yield(); }
      }
    });
  }

  while (loop->get<ExternalCollector>().count.load(std::memory_order_relaxed) < kTotalEvents) {
    loop->poll_all_external();
    loop->poll_group<0>();
  }
  const auto elapsed = steady_clock::now() - started;

  for (auto& thread : threads) { thread.join(); }

  const int total = loop->get<ExternalCollector>().count.load();
  const auto eps = events_per_second(total, elapsed);
  std::println("4 Ext (2 groups):       {:>12} events/sec  ({:>3} ns/event)", eps, ns_per_event(total, elapsed));
}

void bench_external_4_threads_4_groups()
{
  using namespace std::chrono;

  using Loop = ev_loop::GroupEventLoop<ev_loop::SpinGroup<ExternalCollector>,
    ev_loop::ExternalGroup<ExternalInputs1>,
    ev_loop::ExternalGroup<ExternalInputs2>,
    ev_loop::ExternalGroup<ExternalInputs3>,
    ev_loop::ExternalGroup<ExternalInputs4>>;
  auto loop = Loop::setup().create_unique();
  auto emitter1 = loop->get_external_emitter<ExternalInputs1>();
  auto emitter2 = loop->get_external_emitter<ExternalInputs2>();
  auto emitter3 = loop->get_external_emitter<ExternalInputs3>();
  auto emitter4 = loop->get_external_emitter<ExternalInputs4>();

  constexpr int kEventsPerThread = 100'000;
  constexpr int kTotalEvents = kEventsPerThread * 4;

  const auto started = steady_clock::now();

  std::thread thread1([emitter1]() {
    for (int i = 0; i < kEventsPerThread; ++i) {
      while (!emitter1.emit(Ping{ i })) { std::this_thread::yield(); }
    }
  });
  std::thread thread2([emitter2]() {
    for (int i = 0; i < kEventsPerThread; ++i) {
      while (!emitter2.emit(Ping{ i })) { std::this_thread::yield(); }
    }
  });
  std::thread thread3([emitter3]() {
    for (int i = 0; i < kEventsPerThread; ++i) {
      while (!emitter3.emit(Ping{ i })) { std::this_thread::yield(); }
    }
  });
  std::thread thread4([emitter4]() {
    for (int i = 0; i < kEventsPerThread; ++i) {
      while (!emitter4.emit(Ping{ i })) { std::this_thread::yield(); }
    }
  });

  while (loop->get<ExternalCollector>().count.load(std::memory_order_relaxed) < kTotalEvents) {
    loop->poll_all_external();
    loop->poll_group<0>();
  }
  const auto elapsed = steady_clock::now() - started;

  thread1.join();
  thread2.join();
  thread3.join();
  thread4.join();

  const int total = loop->get<ExternalCollector>().count.load();
  const auto eps = events_per_second(total, elapsed);
  std::println("4 Ext (4 groups):       {:>12} events/sec  ({:>3} ns/event)", eps, ns_per_event(total, elapsed));
}

void bench_external()
{
  std::println("\n=== External -> Internal ===\n");

  bench_external_1_thread();
  bench_external_4_threads_1_group();
  bench_external_4_threads_2_groups();
  bench_external_4_threads_4_groups();
}

} // namespace

// =============================================================================
// Main
// =============================================================================

// NOLINTNEXTLINE(bugprone-exception-escape)
int main()
{
  constexpr int kIterations = 10'000'000;

  std::println("=== GroupEventLoop Benchmark ===");
  std::println("Poll iterations: {}", kIterations);
  std::println("Ping-pong limit: {}\n", kPingPongLimit);

  bench_single_thread(kIterations);
  bench_multi_thread();
  bench_ping_pong();
  bench_external();

  std::println("\nDone.");
  return 0;
}
