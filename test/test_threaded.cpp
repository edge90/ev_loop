#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstddef>
#include <ev_loop/ev.hpp>
#include <mutex>
#include <string>
#include <thread>
#include <type_traits>
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
} // namespace

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
  using thread_mode = ev_loop::OwnThread;

  std::atomic<int> received_count{ 0 };
  std::atomic<int> last_value{ 0 };

  template<typename Dispatcher> void on_event(PongEvent event, Dispatcher& dispatcher)
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
  using thread_mode = ev_loop::OwnThread;

  std::atomic<int> received_count{ 0 };

  template<typename Dispatcher> void on_event(PingEvent event, Dispatcher& dispatcher)
  {
    received_count.fetch_add(1, std::memory_order_relaxed);
    dispatcher.emit(PongEvent{ event.value + 1 });
  }
};

struct ThreadedStringReceiver
{
  using receives = ev_loop::type_list<StringEvent>;
  using thread_mode = ev_loop::OwnThread;

  std::atomic<int> count{ 0 };
  std::mutex mutex;
  std::vector<std::string> received;

  template<typename Dispatcher> void on_event(StringEvent event, Dispatcher& /*dispatcher*/)
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

  auto& receiver = loop.get<ThreadedStringReceiver>();
  REQUIRE(receiver.received.size() == static_cast<std::size_t>(kEventCount));
}

// =============================================================================
// Mixed threading receivers
// =============================================================================

struct SameThreadCounter
{
  using receives = ev_loop::type_list<MixedEvent>;
  using thread_mode = ev_loop::SameThread;

  int count = 0;
  int sum = 0;

  template<typename Dispatcher> void on_event(MixedEvent event, Dispatcher& /*dispatcher*/)
  {
    ++count;
    sum += event.value;
  }
};

struct OwnThreadCounter
{
  using receives = ev_loop::type_list<MixedEvent>;
  using thread_mode = ev_loop::OwnThread;

  std::atomic<int> count{ 0 };
  std::atomic<int> sum{ 0 };

  template<typename Dispatcher> void on_event(MixedEvent event, Dispatcher& /*dispatcher*/)
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
  using thread_mode = ev_loop::SameThread;

  int received_count = 0;
  int last_value = 0;

  template<typename Dispatcher> void on_event(CrossPong event, Dispatcher& dispatcher)
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
  using thread_mode = ev_loop::OwnThread;

  std::atomic<int> received_count{ 0 };

  template<typename Dispatcher> void on_event(CrossPing event, Dispatcher& dispatcher)
  {
    received_count.fetch_add(1, std::memory_order_relaxed);
    dispatcher.emit(CrossPong{ event.value + 1 });
  }
};

struct CrossD_OwnThread_Starter
{
  using receives = ev_loop::type_list<CrossPong>;
  using emits = ev_loop::type_list<CrossPing>;
  using thread_mode = ev_loop::OwnThread;

  std::atomic<int> received_count{ 0 };
  std::atomic<int> last_value{ 0 };

  template<typename Dispatcher> void on_event(CrossPong event, Dispatcher& dispatcher)
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
  using thread_mode = ev_loop::SameThread;

  int received_count = 0;

  template<typename Dispatcher> void on_event(CrossPing event, Dispatcher& dispatcher)
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

  // Interleave polling (for SameThread) with waiting (for OwnThread completion)
  ev_loop::Spin strategy{ loop };
  auto is_done = [&] { return loop.get<CrossD_OwnThread_Starter>().received_count >= kPingPongExpectedCount; };
  while (!loop.get<CrossD_OwnThread_Starter>().wait_for(is_done, std::chrono::milliseconds(1))) {
    while (strategy.poll()) {}
  }

  loop.stop();

  REQUIRE(loop.get<CrossA_SameThread_Relay>().received_count == kPingPongExpectedCount);
  REQUIRE(loop.get<CrossD_OwnThread_Starter>().received_count == kPingPongExpectedCount);
}

TEST_CASE("EventLoop cross thread blocking strategies", "[event_loop][cross_thread]")
{
  ev_loop::EventLoop<CrossA_SameThread_Relay, CrossD_OwnThread_Starter> loop;
  loop.start();

  loop.emit(CrossPing{ 0 });

  std::thread strategy_thread;

  SECTION("Wait strategy")
  {
    strategy_thread = std::thread([&loop] {
      ev_loop::Wait strategy{ loop };
      strategy.run();
    });
  }

  SECTION("Hybrid strategy")
  {
    strategy_thread = std::thread([&loop] {
      ev_loop::Hybrid strategy{ loop, kHybridSpinCount };
      strategy.run();
    });
  }

  while (loop.get<CrossD_OwnThread_Starter>().received_count < kPingPongExpectedCount) {
    std::this_thread::sleep_for(std::chrono::milliseconds(kPollDelayMs));
  }

  loop.stop();
  strategy_thread.join();

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
  using thread_mode = ev_loop::OwnThread;

  std::atomic<int> count{ 0 };
  std::atomic<int> sum{ 0 };

  template<typename Dispatcher> void on_event(ExternalThreadEvent event, Dispatcher& /*dispatcher*/)
  {
    count.fetch_add(1, std::memory_order_relaxed);
    sum.fetch_add(event.value, std::memory_order_relaxed);
  }
};

// External emitter declaration - specifies which events can be emitted externally
struct TestExternalEmitter
{
  using emits = ev_loop::type_list<ExternalThreadEvent>;
};

TEST_CASE("ExternalEmitter from another thread", "[event_loop][external_emitter]")
{
  // Use SharedEventLoopPtr when external emitters are needed
  ev_loop::SharedEventLoopPtr<ExternalThreadReceiver, TestExternalEmitter> loop;
  loop.start();

  auto emitter = loop.get_external_emitter<TestExternalEmitter>();

  // Emit events from a separate thread
  std::thread producer([&emitter] {
    for (int i = 1; i <= kEventCount; ++i) { emitter.emit(ExternalThreadEvent{ i }); }
  });

  producer.join();

  // Wait for OwnThread receiver to process all events
  while (loop.get<ExternalThreadReceiver>().count < kEventCount) {
    std::this_thread::sleep_for(std::chrono::milliseconds(kPollDelayMs));
  }

  loop.stop();

  // Sum of 1..100 = 5050
  constexpr int kExpectedSum = kEventCount * (kEventCount + 1) / 2;
  REQUIRE(loop.get<ExternalThreadReceiver>().count == kEventCount);
  REQUIRE(loop.get<ExternalThreadReceiver>().sum == kExpectedSum);
}

TEST_CASE("ExternalEmitter safe after SharedEventLoopPtr destruction", "[event_loop][external_emitter]")
{
  auto emitter = []() {
    ev_loop::SharedEventLoopPtr<ExternalThreadReceiver, TestExternalEmitter> loop;
    loop.start();
    auto ext_emitter = loop.get_external_emitter<TestExternalEmitter>();
    // Emitter should be valid while loop exists
    REQUIRE(ext_emitter.is_valid());
    REQUIRE(ext_emitter.emit(ExternalThreadEvent{ 1 }) == true);
    // Wait for event to be processed
    while (loop.get<ExternalThreadReceiver>().count < 1) {
      std::this_thread::sleep_for(std::chrono::milliseconds(kPollDelayMs));
    }
    loop.stop();
    return ext_emitter;
  }();

  // Emitter should be invalid after loop is destroyed
  REQUIRE(!emitter.is_valid());
  // emit() should return false (not crash) when loop is gone
  REQUIRE(emitter.emit(ExternalThreadEvent{ 2 }) == false);
}

// =============================================================================
// Multi-producer tests (verifies MPSC queue is selected and works)
// =============================================================================

struct MultiProdEvent
{
  int value;
  int source;
};

// Two OwnThread receivers both emit to a third OwnThread receiver
struct ProducerA_OwnThread
{
  using receives = ev_loop::type_list<PingEvent>;
  using emits = ev_loop::type_list<MultiProdEvent>;
  using thread_mode = ev_loop::OwnThread;

  std::atomic<int> count{ 0 };

  template<typename Dispatcher> void on_event(PingEvent event, Dispatcher& dispatcher)
  {
    count.fetch_add(1, std::memory_order_relaxed);
    dispatcher.emit(MultiProdEvent{ .value = event.value, .source = 1 });
  }
};

struct ProducerB_OwnThread
{
  using receives = ev_loop::type_list<PongEvent>;
  using emits = ev_loop::type_list<MultiProdEvent>;
  using thread_mode = ev_loop::OwnThread;

  std::atomic<int> count{ 0 };

  template<typename Dispatcher> void on_event(PongEvent event, Dispatcher& dispatcher)
  {
    count.fetch_add(1, std::memory_order_relaxed);
    dispatcher.emit(MultiProdEvent{ .value = event.value, .source = 2 });
  }
};

struct MultiConsumer_OwnThread
{
  using receives = ev_loop::type_list<MultiProdEvent>;
  using thread_mode = ev_loop::OwnThread;

  std::atomic<int> count{ 0 };
  std::atomic<int> from_a{ 0 };
  std::atomic<int> from_b{ 0 };

  template<typename Dispatcher> void on_event(MultiProdEvent event, Dispatcher& /*dispatcher*/)
  {
    count.fetch_add(1, std::memory_order_relaxed);
    if (event.source == 1) {
      from_a.fetch_add(1, std::memory_order_relaxed);
    } else {
      from_b.fetch_add(1, std::memory_order_relaxed);
    }
  }
};

// SameThread producer for mixed-producer tests
struct SameThreadProducer
{
  using receives = ev_loop::type_list<PingEvent>;
  using emits = ev_loop::type_list<MultiProdEvent>;
  using thread_mode = ev_loop::SameThread;

  int count = 0;

  template<typename Dispatcher> void on_event(PingEvent event, Dispatcher& dispatcher)
  {
    ++count;
    dispatcher.emit(MultiProdEvent{ .value = event.value, .source = 1 });
  }
};

// OwnThread producer for mixed-producer tests
struct OwnThreadProducer
{
  using receives = ev_loop::type_list<PongEvent>;
  using emits = ev_loop::type_list<MultiProdEvent>;
  using thread_mode = ev_loop::OwnThread;

  std::atomic<int> count{ 0 };

  template<typename Dispatcher> void on_event(PongEvent event, Dispatcher& dispatcher)
  {
    count.fetch_add(1, std::memory_order_relaxed);
    dispatcher.emit(MultiProdEvent{ .value = event.value, .source = 2 });
  }
};

TEST_CASE("Two OwnThread producers to OwnThread consumer", "[event_loop][multi_producer]")
{
  constexpr int kEventsPerProducer = 50;

  using Loop = ev_loop::EventLoop<ProducerA_OwnThread, ProducerB_OwnThread, MultiConsumer_OwnThread>;
  using ConsumerTaggedEvent =
    ev_loop::detail::to_tagged_event_t<ev_loop::detail::get_receives_t<MultiConsumer_OwnThread>>;

  // Verify MPSC queue is selected (producer_count > 1)
  static_assert(
    Loop::producer_count_for<MultiConsumer_OwnThread> == 2, "MultiConsumer should have 2 OwnThread producers");
  static_assert(
    std::is_same_v<Loop::queue_type_for<MultiConsumer_OwnThread>, ev_loop::detail::mpsc::Queue<ConsumerTaggedEvent>>,
    "Multi-producer should use mpsc::Queue");

  Loop loop;
  loop.start();

  // Send events to both producers
  for (int idx = 0; idx < kEventsPerProducer; ++idx) {
    loop.emit(PingEvent{ idx });
    loop.emit(PongEvent{ idx });
  }

  // Wait for all events to be processed
  while (loop.get<MultiConsumer_OwnThread>().count.load() < kEventsPerProducer * 2) {
    std::this_thread::sleep_for(std::chrono::milliseconds(kPollDelayMs));
  }

  loop.stop();

  REQUIRE(loop.get<ProducerA_OwnThread>().count.load() == kEventsPerProducer);
  REQUIRE(loop.get<ProducerB_OwnThread>().count.load() == kEventsPerProducer);
  REQUIRE(loop.get<MultiConsumer_OwnThread>().count.load() == kEventsPerProducer * 2);
  REQUIRE(loop.get<MultiConsumer_OwnThread>().from_a.load() == kEventsPerProducer);
  REQUIRE(loop.get<MultiConsumer_OwnThread>().from_b.load() == kEventsPerProducer);
}

TEST_CASE("SameThread + OwnThread producers to OwnThread consumer", "[event_loop][multi_producer]")
{
  constexpr int kEventsPerProducer = 50;

  using Loop = ev_loop::EventLoop<SameThreadProducer, OwnThreadProducer, MultiConsumer_OwnThread>;
  using ConsumerTaggedEvent =
    ev_loop::detail::to_tagged_event_t<ev_loop::detail::get_receives_t<MultiConsumer_OwnThread>>;

  // Verify MPSC queue is selected (1 SameThread + 1 OwnThread = 2 producers)
  static_assert(Loop::producer_count_for<MultiConsumer_OwnThread> == 2,
    "MultiConsumer should have 2 producers (SameThread + OwnThread)");
  static_assert(
    std::is_same_v<Loop::queue_type_for<MultiConsumer_OwnThread>, ev_loop::detail::mpsc::Queue<ConsumerTaggedEvent>>,
    "Multi-producer should use mpsc::Queue");

  Loop loop;
  loop.start();

  // Send events - PingEvent goes to SameThread, PongEvent goes to OwnThread
  for (int idx = 0; idx < kEventsPerProducer; ++idx) {
    loop.emit(PingEvent{ idx });
    loop.emit(PongEvent{ idx });
  }

  // Process SameThread events
  while (ev_loop::Spin{ loop }.poll()) {}

  // Wait for OwnThread events to be processed
  while (loop.get<MultiConsumer_OwnThread>().count.load() < kEventsPerProducer * 2) {
    std::this_thread::sleep_for(std::chrono::milliseconds(kPollDelayMs));
  }

  loop.stop();

  REQUIRE(loop.get<SameThreadProducer>().count == kEventsPerProducer);
  REQUIRE(loop.get<OwnThreadProducer>().count.load() == kEventsPerProducer);
  REQUIRE(loop.get<MultiConsumer_OwnThread>().count.load() == kEventsPerProducer * 2);
  REQUIRE(loop.get<MultiConsumer_OwnThread>().from_a.load() == kEventsPerProducer);
  REQUIRE(loop.get<MultiConsumer_OwnThread>().from_b.load() == kEventsPerProducer);
}

TEST_CASE("Single producer selects SPSC queue", "[event_loop][multi_producer]")
{
  // Pure OwnThread ping-pong: each receiver has exactly 1 OwnThread producer
  using PingPongLoop = ev_loop::EventLoop<ThreadedPingReceiver, ThreadedPongReceiver>;
  using PingTaggedEvent = ev_loop::detail::to_tagged_event_t<ev_loop::detail::get_receives_t<ThreadedPingReceiver>>;
  using PongTaggedEvent = ev_loop::detail::to_tagged_event_t<ev_loop::detail::get_receives_t<ThreadedPongReceiver>>;

  static_assert(
    PingPongLoop::producer_count_for<ThreadedPingReceiver> == 1, "ThreadedPingReceiver should have 1 producer");
  static_assert(
    PingPongLoop::producer_count_for<ThreadedPongReceiver> == 1, "ThreadedPongReceiver should have 1 producer");
  static_assert(
    std::is_same_v<PingPongLoop::queue_type_for<ThreadedPingReceiver>, ev_loop::detail::spsc::Queue<PingTaggedEvent>>,
    "Single-producer should use spsc::Queue");
  static_assert(
    std::is_same_v<PingPongLoop::queue_type_for<ThreadedPongReceiver>, ev_loop::detail::spsc::Queue<PongTaggedEvent>>,
    "Single-producer should use spsc::Queue");

  // SameThread -> OwnThread: OwnThread has 1 producer (event loop thread)
  using MixedLoop = ev_loop::EventLoop<SameThreadProducer, MultiConsumer_OwnThread>;
  using ConsumerTaggedEvent =
    ev_loop::detail::to_tagged_event_t<ev_loop::detail::get_receives_t<MultiConsumer_OwnThread>>;

  static_assert(MixedLoop::producer_count_for<MultiConsumer_OwnThread> == 1,
    "MultiConsumer with only SameThread producer should have 1 producer");
  static_assert(std::is_same_v<MixedLoop::queue_type_for<MultiConsumer_OwnThread>,
                  ev_loop::detail::spsc::Queue<ConsumerTaggedEvent>>,
    "Single-producer should use spsc::Queue");
}

// External emitter for producer count test
struct ExternalMultiProdEmitter
{
  using emits = ev_loop::type_list<MultiProdEvent>;
};

TEST_CASE("External emitter counts as producer", "[event_loop][multi_producer]")
{
  // Without external emitter: single producer (SameThread) -> SPSC
  using LoopWithoutExternal = ev_loop::EventLoop<SameThreadProducer, MultiConsumer_OwnThread>;
  using TaggedEvent = ev_loop::detail::to_tagged_event_t<ev_loop::detail::get_receives_t<MultiConsumer_OwnThread>>;

  static_assert(LoopWithoutExternal::producer_count_for<MultiConsumer_OwnThread> == 1,
    "Without external emitter should have 1 producer");
  static_assert(std::is_same_v<LoopWithoutExternal::queue_type_for<MultiConsumer_OwnThread>,
                  ev_loop::detail::spsc::Queue<TaggedEvent>>,
    "Single-producer should use spsc::Queue");

  // With external emitter: 2 producers (SameThread + External) -> MPSC
  using LoopWithExternal = ev_loop::EventLoop<SameThreadProducer, MultiConsumer_OwnThread, ExternalMultiProdEmitter>;

  static_assert(LoopWithExternal::producer_count_for<MultiConsumer_OwnThread> == 2,
    "With external emitter should have 2 producers");
  static_assert(std::is_same_v<LoopWithExternal::queue_type_for<MultiConsumer_OwnThread>,
                  ev_loop::detail::mpsc::Queue<TaggedEvent>>,
    "Multi-producer (with external) should use mpsc::Queue");
}
