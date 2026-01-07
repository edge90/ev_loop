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
// Receivers for multi-group benchmarks
// =============================================================================

struct ReceiverC
{
  using receives = ev_loop::type_list<Pong>;
  using emits = ev_loop::type_list<Ping>;

  std::atomic<int> counter{ 0 };

  template<typename Dispatcher> void on_event(Pong event, Dispatcher& dispatcher)
  {
    counter.fetch_add(1, std::memory_order_relaxed);
    dispatcher.emit(Ping{ event.value + 1 });
  }
};

struct ReceiverD
{
  using receives = ev_loop::type_list<Ping>;
  using emits = ev_loop::type_list<Pong>;

  std::atomic<int> counter{ 0 };

  template<typename Dispatcher> void on_event(Ping event, Dispatcher& dispatcher)
  {
    counter.fetch_add(1, std::memory_order_relaxed);
    dispatcher.emit(Pong{ event.value + 1 });
  }
};

// Receivers for mixed benchmark (both run on threads now)
struct MainThreadReceiver
{
  using receives = ev_loop::type_list<Pong>;
  using emits = ev_loop::type_list<Ping>;

  std::atomic<int> counter{ 0 };

  template<typename Dispatcher> void on_event(Pong event, Dispatcher& dispatcher)
  {
    counter.fetch_add(1, std::memory_order_relaxed);
    dispatcher.emit(Ping{ event.value + 1 });
  }
};

struct BackgroundReceiver
{
  using receives = ev_loop::type_list<Ping>;
  using emits = ev_loop::type_list<Pong>;

  std::atomic<int> counter{ 0 };

  template<typename Dispatcher> void on_event(Ping event, Dispatcher& dispatcher)
  {
    counter.fetch_add(1, std::memory_order_relaxed);
    dispatcher.emit(Pong{ event.value + 1 });
  }
};

// Receivers for background-to-main benchmark
struct MainPongReceiver
{
  using receives = ev_loop::type_list<Pong>;
  using emits = ev_loop::type_list<Ping>;
  std::atomic<int> counter{ 0 };

  template<typename Dispatcher> void on_event(Pong event, Dispatcher& dispatcher)
  {
    counter.fetch_add(1, std::memory_order_relaxed);
    dispatcher.emit(Ping{ event.value + 1 });
  }
};

struct BackgroundPingReceiver
{
  using receives = ev_loop::type_list<Ping>;
  using emits = ev_loop::type_list<Pong>;
  std::atomic<int> counter{ 0 };

  template<typename Dispatcher> void on_event(Ping event, Dispatcher& dispatcher)
  {
    counter.fetch_add(1, std::memory_order_relaxed);
    dispatcher.emit(Pong{ event.value + 1 });
  }
};

// =============================================================================
// Main
// =============================================================================

namespace {
constexpr int kTwoGroupTargetCount = 10'000'000;
constexpr int kMixedTargetCount = 1'000'000;
} // namespace

void benchmark_two_groups()
{
  std::println("=== Benchmark 1: Group 0 <-> Group 1 (2 threads) ===\n");
  using namespace std::chrono;

  // SpinGroup x2
  {
    using Loop = ev_loop::GroupEventLoop<ev_loop::SpinGroup<ReceiverC>, ev_loop::SpinGroup<ReceiverD>>;
    auto loop = Loop::setup().prime(Ping{ 0 }).create_unique();
    loop->start();

    const auto started = steady_clock::now();

    while (loop->get<ReceiverC>().counter.load(std::memory_order_relaxed) < kTwoGroupTargetCount) {
      std::this_thread::yield();
    }
    const auto elapsed = steady_clock::now() - started;

    loop->stop();
    const auto total = loop->get<ReceiverC>().counter.load() + loop->get<ReceiverD>().counter.load();
    std::println("  SpinGroup x2: {:>12} events/sec", events_per_second(total, elapsed));
  }

  // WaitGroup x2
  {
    using Loop = ev_loop::GroupEventLoop<ev_loop::WaitGroup<ReceiverC>, ev_loop::WaitGroup<ReceiverD>>;
    auto loop = Loop::setup().prime(Ping{ 0 }).create_unique();
    loop->start();

    const auto started = steady_clock::now();

    while (loop->get<ReceiverC>().counter.load(std::memory_order_relaxed) < kTwoGroupTargetCount) {
      std::this_thread::yield();
    }
    const auto elapsed = steady_clock::now() - started;

    loop->stop();
    const auto total = loop->get<ReceiverC>().counter.load() + loop->get<ReceiverD>().counter.load();
    std::println("  WaitGroup x2: {:>12} events/sec\n", events_per_second(total, elapsed));
  }
}

void benchmark_main_to_background()
{
  std::println("=== Benchmark 2: Main Thread -> Background Thread ===\n");
  using namespace std::chrono;

  // SpinGroup x2
  {
    using Loop =
      ev_loop::GroupEventLoop<ev_loop::SpinGroup<MainThreadReceiver>, ev_loop::SpinGroup<BackgroundReceiver>>;
    auto loop = Loop::setup().prime(Ping{ 0 }).create_unique();
    loop->start();

    const auto started = steady_clock::now();

    while (loop->get<MainThreadReceiver>().counter.load(std::memory_order_relaxed) < kMixedTargetCount) {
      std::this_thread::yield();
    }
    const auto elapsed = steady_clock::now() - started;

    loop->stop();
    const auto total = loop->get<MainThreadReceiver>().counter.load() + loop->get<BackgroundReceiver>().counter.load();
    std::println("  SpinGroup x2: {:>12} events/sec", events_per_second(total, elapsed));
  }

  // WaitGroup x2
  {
    using Loop =
      ev_loop::GroupEventLoop<ev_loop::WaitGroup<MainThreadReceiver>, ev_loop::WaitGroup<BackgroundReceiver>>;
    auto loop = Loop::setup().prime(Ping{ 0 }).create_unique();
    loop->start();

    const auto started = steady_clock::now();

    while (loop->get<MainThreadReceiver>().counter.load(std::memory_order_relaxed) < kMixedTargetCount) {
      std::this_thread::yield();
    }
    const auto elapsed = steady_clock::now() - started;

    loop->stop();
    const auto total = loop->get<MainThreadReceiver>().counter.load() + loop->get<BackgroundReceiver>().counter.load();
    std::println("  WaitGroup x2: {:>12} events/sec\n", events_per_second(total, elapsed));
  }
}

void benchmark_background_to_main()
{
  std::println("=== Benchmark 3: Background Thread -> Main Thread ===\n");
  using namespace std::chrono;

  // SpinGroup x2
  {
    using Loop =
      ev_loop::GroupEventLoop<ev_loop::SpinGroup<MainPongReceiver>, ev_loop::SpinGroup<BackgroundPingReceiver>>;
    auto loop = Loop::setup().prime(Pong{ 0 }).create_unique();
    loop->start();

    const auto started = steady_clock::now();

    while (loop->get<BackgroundPingReceiver>().counter.load(std::memory_order_relaxed) < kMixedTargetCount) {
      std::this_thread::yield();
    }
    const auto elapsed = steady_clock::now() - started;

    loop->stop();
    const auto total =
      loop->get<MainPongReceiver>().counter.load() + loop->get<BackgroundPingReceiver>().counter.load();
    std::println("  SpinGroup x2: {:>12} events/sec", events_per_second(total, elapsed));
  }

  // WaitGroup x2
  {
    using Loop =
      ev_loop::GroupEventLoop<ev_loop::WaitGroup<MainPongReceiver>, ev_loop::WaitGroup<BackgroundPingReceiver>>;
    auto loop = Loop::setup().prime(Pong{ 0 }).create_unique();
    loop->start();

    const auto started = steady_clock::now();

    while (loop->get<BackgroundPingReceiver>().counter.load(std::memory_order_relaxed) < kMixedTargetCount) {
      std::this_thread::yield();
    }
    const auto elapsed = steady_clock::now() - started;

    loop->stop();
    const auto total =
      loop->get<MainPongReceiver>().counter.load() + loop->get<BackgroundPingReceiver>().counter.load();
    std::println("  WaitGroup x2: {:>12} events/sec\n", events_per_second(total, elapsed));
  }
}

// NOLINTNEXTLINE(bugprone-exception-escape)
int main()
{
  std::println("=== GroupEventLoop Threaded Benchmark ===\n");
  benchmark_two_groups();
  benchmark_main_to_background();
  benchmark_background_to_main();
  return 0;
}
