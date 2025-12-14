#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstddef>
#include <ev_loop/ev.hpp>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

// =============================================================================
// Test constants
// =============================================================================

namespace {
constexpr int kPingPongLimit = 100;
constexpr int kPingPongExpectedCount = 51;
constexpr int kEventCount = 100;
constexpr int kMixedEventCount = 50;
constexpr int kPollDelayMs = 1;
constexpr int kSettleDelayMs = 10;
constexpr int kSpinDelayUs = 100;
constexpr std::size_t kHybridSpinCount = 100;
}// namespace

// =============================================================================
// Event types
// =============================================================================

struct PingEvent
{
  int value;
};
struct PongEvent
{
  int value;
};
struct StringEvent
{
  std::string data;
};
struct MixedEvent
{
  int value;
};
struct CrossPing
{
  int value;
};
struct CrossPong
{
  int value;
};

// =============================================================================
// Own thread receivers
// =============================================================================

struct ThreadedPingReceiver
{
  using receives = ev_loop::type_list<PongEvent>;
  using emits = ev_loop::type_list<PingEvent>;
  // cppcheck-suppress unusedStructMember
  static constexpr ev_loop::ThreadMode thread_mode = ev_loop::ThreadMode::OwnThread;

  std::atomic<int> received_count{ 0 };
  std::atomic<int> last_value{ 0 };

  template<typename Dispatcher> void on_event(PongEvent event, Dispatcher &dispatcher)
  {
    received_count.fetch_add(1, std::memory_order_relaxed);
    last_value.store(event.value, std::memory_order_relaxed);
    if (event.value < kPingPongLimit) { dispatcher.emit(PingEvent{ event.value + 1 }); }
  }
};

struct ThreadedPongReceiver
{
  using receives = ev_loop::type_list<PingEvent>;
  using emits = ev_loop::type_list<PongEvent>;
  // cppcheck-suppress unusedStructMember
  static constexpr ev_loop::ThreadMode thread_mode = ev_loop::ThreadMode::OwnThread;

  std::atomic<int> received_count{ 0 };

  template<typename Dispatcher> void on_event(PingEvent event, Dispatcher &dispatcher)
  {
    received_count.fetch_add(1, std::memory_order_relaxed);
    dispatcher.emit(PongEvent{ event.value + 1 });
  }
};

struct ThreadedStringReceiver
{
  using receives = ev_loop::type_list<StringEvent>;
  // cppcheck-suppress unusedStructMember
  static constexpr ev_loop::ThreadMode thread_mode = ev_loop::ThreadMode::OwnThread;

  std::atomic<int> count{ 0 };
  std::mutex mutex;
  std::vector<std::string> received;

  template<typename Dispatcher> void on_event(StringEvent event, Dispatcher & /*dispatcher*/)
  {
    {
      const std::scoped_lock lock(mutex);
      received.push_back(std::move(event.data));
    }
    count.fetch_add(1, std::memory_order_relaxed);
  }
};

// =============================================================================
// Own thread tests
// =============================================================================

TEST_CASE("EventLoop own thread ping pong", "[event_loop][own_thread]")
{
  ev_loop::EventLoop<ThreadedPingReceiver, ThreadedPongReceiver> loop;
  loop.start();

  loop.emit(PingEvent{ 0 });

  while (loop.get<ThreadedPingReceiver>().last_value < kPingPongLimit + 1) {
    std::this_thread::sleep_for(std::chrono::milliseconds(kPollDelayMs));
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(kSettleDelayMs));

  loop.stop();

  REQUIRE(loop.get<ThreadedPingReceiver>().received_count == kPingPongExpectedCount);
  REQUIRE(loop.get<ThreadedPongReceiver>().received_count == kPingPongExpectedCount);
}

TEST_CASE("EventLoop own thread string events", "[event_loop][own_thread]")
{
  ev_loop::EventLoop<ThreadedStringReceiver> loop;
  loop.start();

  for (int i = 0; i < kEventCount; ++i) { loop.emit(StringEvent{ "message_" + std::to_string(i) }); }

  while (loop.get<ThreadedStringReceiver>().count < kEventCount) {
    std::this_thread::sleep_for(std::chrono::milliseconds(kPollDelayMs));
  }

  loop.stop();

  auto &receiver = loop.get<ThreadedStringReceiver>();
  REQUIRE(receiver.received.size() == static_cast<std::size_t>(kEventCount));
}

// =============================================================================
// Mixed threading receivers
// =============================================================================

struct SameThreadCounter
{
  using receives = ev_loop::type_list<MixedEvent>;
  // cppcheck-suppress unusedStructMember
  static constexpr ev_loop::ThreadMode thread_mode = ev_loop::ThreadMode::SameThread;

  int count = 0;
  int sum = 0;

  template<typename Dispatcher> void on_event(MixedEvent event, Dispatcher & /*dispatcher*/)
  {
    ++count;
    sum += event.value;
  }
};

struct OwnThreadCounter
{
  using receives = ev_loop::type_list<MixedEvent>;
  // cppcheck-suppress unusedStructMember
  static constexpr ev_loop::ThreadMode thread_mode = ev_loop::ThreadMode::OwnThread;

  std::atomic<int> count{ 0 };
  std::atomic<int> sum{ 0 };

  template<typename Dispatcher> void on_event(MixedEvent event, Dispatcher & /*dispatcher*/)
  {
    ++count;
    sum += event.value;
  }
};

TEST_CASE("EventLoop mixed threading", "[event_loop][mixed_thread]")
{
  ev_loop::EventLoop<SameThreadCounter, OwnThreadCounter> loop;
  loop.start();

  for (int i = 0; i < kMixedEventCount; ++i) { loop.emit(MixedEvent{ i }); }

  while (ev_loop::Spin{ loop }.poll()) {}

  while (loop.get<OwnThreadCounter>().count < kMixedEventCount) {
    std::this_thread::sleep_for(std::chrono::milliseconds(kPollDelayMs));
  }

  loop.stop();

  REQUIRE(loop.get<SameThreadCounter>().count == kMixedEventCount);
  REQUIRE(loop.get<OwnThreadCounter>().count == kMixedEventCount);
}

// =============================================================================
// Cross thread receivers
// =============================================================================

struct CrossA_SameThread
{
  using receives = ev_loop::type_list<CrossPong>;
  using emits = ev_loop::type_list<CrossPing>;
  // cppcheck-suppress unusedStructMember
  static constexpr ev_loop::ThreadMode thread_mode = ev_loop::ThreadMode::SameThread;

  int received_count = 0;
  int last_value = 0;

  template<typename Dispatcher> void on_event(CrossPong event, Dispatcher &dispatcher)
  {
    ++received_count;
    last_value = event.value;
    if (event.value < kPingPongLimit) { dispatcher.emit(CrossPing{ event.value + 1 }); }
  }
};

struct CrossD_OwnThread
{
  using receives = ev_loop::type_list<CrossPing>;
  using emits = ev_loop::type_list<CrossPong>;
  // cppcheck-suppress unusedStructMember
  static constexpr ev_loop::ThreadMode thread_mode = ev_loop::ThreadMode::OwnThread;

  std::atomic<int> received_count{ 0 };

  template<typename Dispatcher> void on_event(CrossPing event, Dispatcher &dispatcher)
  {
    received_count.fetch_add(1, std::memory_order_relaxed);
    dispatcher.emit(CrossPong{ event.value + 1 });
  }
};

struct CrossD_OwnThread_Starter
{
  using receives = ev_loop::type_list<CrossPong>;
  using emits = ev_loop::type_list<CrossPing>;
  // cppcheck-suppress unusedStructMember
  static constexpr ev_loop::ThreadMode thread_mode = ev_loop::ThreadMode::OwnThread;

  std::atomic<int> received_count{ 0 };
  std::atomic<int> last_value{ 0 };

  template<typename Dispatcher> void on_event(CrossPong event, Dispatcher &dispatcher)
  {
    received_count.fetch_add(1, std::memory_order_relaxed);
    last_value.store(event.value, std::memory_order_relaxed);
    if (event.value < kPingPongLimit) { dispatcher.emit(CrossPing{ event.value + 1 }); }
  }
};

struct CrossA_SameThread_Relay
{
  using receives = ev_loop::type_list<CrossPing>;
  using emits = ev_loop::type_list<CrossPong>;
  // cppcheck-suppress unusedStructMember
  static constexpr ev_loop::ThreadMode thread_mode = ev_loop::ThreadMode::SameThread;

  int received_count = 0;

  template<typename Dispatcher> void on_event(CrossPing event, Dispatcher &dispatcher)
  {
    ++received_count;
    dispatcher.emit(CrossPong{ event.value + 1 });
  }
};

TEST_CASE("EventLoop cross thread samethread to ownthread", "[event_loop][cross_thread]")
{
  ev_loop::EventLoop<CrossA_SameThread, CrossD_OwnThread> loop;
  loop.start();

  loop.emit(CrossPing{ 0 });

  ev_loop::Spin strategy{ loop };
  while (loop.get<CrossA_SameThread>().last_value < kPingPongLimit + 1) {
    (void)strategy.poll();
    std::this_thread::sleep_for(std::chrono::microseconds(kSpinDelayUs));
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(kSettleDelayMs));
  while (strategy.poll()) {}

  loop.stop();

  REQUIRE(loop.get<CrossA_SameThread>().received_count == kPingPongExpectedCount);
  REQUIRE(loop.get<CrossD_OwnThread>().received_count == kPingPongExpectedCount);
}

TEST_CASE("EventLoop cross thread ownthread to samethread", "[event_loop][cross_thread]")
{
  ev_loop::EventLoop<CrossA_SameThread_Relay, CrossD_OwnThread_Starter> loop;
  loop.start();

  loop.emit(CrossPing{ 0 });

  ev_loop::Spin strategy{ loop };
  while (loop.get<CrossD_OwnThread_Starter>().last_value < kPingPongLimit) {
    std::ignore = strategy.poll();
    std::this_thread::sleep_for(std::chrono::microseconds(kSpinDelayUs));
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(kSettleDelayMs));
  while (strategy.poll()) {}

  loop.stop();

  REQUIRE(loop.get<CrossA_SameThread_Relay>().received_count == kPingPongExpectedCount);
  REQUIRE(loop.get<CrossD_OwnThread_Starter>().received_count == kPingPongExpectedCount);
}

TEST_CASE("EventLoop cross thread with Wait strategy", "[event_loop][wait]")
{
  ev_loop::EventLoop<CrossA_SameThread_Relay, CrossD_OwnThread_Starter> loop;
  loop.start();

  loop.emit(CrossPing{ 0 });

  // Run Wait strategy in a separate thread so we can stop the loop
  std::thread wait_thread([&loop] {
    ev_loop::Wait strategy{ loop };
    strategy.run();
  });

  // Wait for expected count instead of threshold + sleep
  while (loop.get<CrossD_OwnThread_Starter>().received_count < kPingPongExpectedCount) { std::this_thread::yield(); }

  loop.stop();
  wait_thread.join();

  REQUIRE(loop.get<CrossA_SameThread_Relay>().received_count == kPingPongExpectedCount);
  REQUIRE(loop.get<CrossD_OwnThread_Starter>().received_count == kPingPongExpectedCount);
}

TEST_CASE("EventLoop cross thread with Hybrid strategy", "[event_loop][hybrid]")
{
  ev_loop::EventLoop<CrossA_SameThread_Relay, CrossD_OwnThread_Starter> loop;
  loop.start();

  loop.emit(CrossPing{ 0 });

  // Run Hybrid strategy in a separate thread so we can stop the loop
  // (Hybrid can block in wait_pop_any after spin count is exceeded)
  std::thread hybrid_thread([&loop] {
    ev_loop::Hybrid strategy{ loop, kHybridSpinCount };
    strategy.run();
  });

  // Wait for expected count
  while (loop.get<CrossD_OwnThread_Starter>().received_count < kPingPongExpectedCount) { std::this_thread::yield(); }

  loop.stop();
  hybrid_thread.join();

  REQUIRE(loop.get<CrossA_SameThread_Relay>().received_count == kPingPongExpectedCount);
  REQUIRE(loop.get<CrossD_OwnThread_Starter>().received_count == kPingPongExpectedCount);
}

// =============================================================================
// ExternalEmitter threaded tests
// =============================================================================

struct ExternalThreadEvent
{
  int value;
};

struct ExternalThreadReceiver
{
  using receives = ev_loop::type_list<ExternalThreadEvent>;
  // cppcheck-suppress unusedStructMember
  static constexpr ev_loop::ThreadMode thread_mode = ev_loop::ThreadMode::OwnThread;

  std::atomic<int> count{ 0 };
  std::atomic<int> sum{ 0 };

  template<typename Dispatcher> void on_event(ExternalThreadEvent event, Dispatcher & /*dispatcher*/)
  {
    count.fetch_add(1, std::memory_order_relaxed);
    sum.fetch_add(event.value, std::memory_order_relaxed);
  }
};

TEST_CASE("ExternalEmitter from another thread", "[event_loop][external_emitter]")
{
  ev_loop::EventLoop<ExternalThreadReceiver> loop;
  loop.start();

  auto emitter = loop.get_external_emitter();

  // Emit events from a separate thread
  std::thread producer([&emitter] {
    for (int i = 1; i <= kEventCount; ++i) { emitter.emit(ExternalThreadEvent{ i }); }
  });

  producer.join();

  // Wait for OwnThread receiver to process all events
  while (loop.get<ExternalThreadReceiver>().count < kEventCount) { std::this_thread::yield(); }

  loop.stop();

  // Sum of 1..100 = 5050
  constexpr int kExpectedSum = kEventCount * (kEventCount + 1) / 2;
  REQUIRE(loop.get<ExternalThreadReceiver>().count == kEventCount);
  REQUIRE(loop.get<ExternalThreadReceiver>().sum == kExpectedSum);
}
