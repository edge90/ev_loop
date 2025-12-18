// NOLINTBEGIN(misc-include-cleaner)
#include "test_utils.hpp"

#include <atomic>
#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstddef>
#include <ev_loop/ev.hpp>
#include <memory>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>
// NOLINTEND(misc-include-cleaner)

namespace {
constexpr int kShortSleepMs = 10;
constexpr int kTestValue1 = 10;
constexpr int kTestValue2 = 20;
constexpr int kTestValue3 = 30;
constexpr int kTestValue4 = 42;
constexpr int kTestValue5 = 100;
constexpr int kTestValue6 = 200;
} // namespace

// =============================================================================
// Event types for GroupEventLoop tests
// =============================================================================

struct EventA
{
  int value;
};

struct EventB
{
  int value;
};

struct EventC
{
  int value;
};

// =============================================================================
// Receivers for GroupEventLoop tests (no thread_mode - grouping is external)
// =============================================================================

struct ReceiverA
{
  using receives = ev_loop::type_list<EventA>;
  using emits = ev_loop::type_list<EventB>;

  std::atomic<int> count{ 0 };

  template<typename Dispatcher> void on_event(EventA event, Dispatcher& /*dispatcher*/)
  {
    count.fetch_add(event.value, std::memory_order_relaxed);
  }
};

struct ReceiverB
{
  using receives = ev_loop::type_list<EventB>;
  using emits = ev_loop::type_list<>;

  std::atomic<int> count{ 0 };

  template<typename Dispatcher> void on_event(EventB event, Dispatcher& /*dispatcher*/)
  {
    count.fetch_add(event.value, std::memory_order_relaxed);
  }
};

struct ReceiverC
{
  using receives = ev_loop::type_list<EventC>;
  using emits = ev_loop::type_list<>;

  std::atomic<int> count{ 0 };

  template<typename Dispatcher> void on_event(EventC event, Dispatcher& /*dispatcher*/)
  {
    count.fetch_add(event.value, std::memory_order_relaxed);
  }
};

// Counter receiver - counts how many events it receives
struct CounterReceiver
{
  using receives = ev_loop::type_list<EventA>;
  using emits = ev_loop::type_list<>;

  std::atomic<int> count{ 0 };

  template<typename Dispatcher> void on_event(EventA /*event*/, Dispatcher& /*dispatcher*/)
  {
    count.fetch_add(1, std::memory_order_relaxed);
  }
};

// =============================================================================
// Type trait tests
// =============================================================================

TEST_CASE("ThreadGroup type traits", "[group_event_loop]")
{
  using Group = ev_loop::ThreadGroup<ev_loop::Spin, ReceiverA, ReceiverB>;

  SECTION("is_thread_group detects ThreadGroup")
  {
    STATIC_REQUIRE(ev_loop::detail::is_thread_group_v<Group>);
    STATIC_REQUIRE_FALSE(ev_loop::detail::is_thread_group_v<ReceiverA>);
    STATIC_REQUIRE_FALSE(ev_loop::detail::is_thread_group_v<int>);
  }

  SECTION("group_receivers_t extracts receiver list")
  {
    using Receivers = ev_loop::detail::group_receivers_t<Group>;
    STATIC_REQUIRE(std::is_same_v<Receivers, ev_loop::type_list<ReceiverA, ReceiverB>>);
  }

  SECTION("group_strategy_t extracts strategy")
  {
    using Strategy = ev_loop::detail::group_strategy_t<Group>;
    STATIC_REQUIRE(std::is_same_v<Strategy, ev_loop::Spin>);
  }
}

TEST_CASE("ThreadGroup convenience aliases", "[group_event_loop]")
{
  SECTION("SpinGroup alias")
  {
    using Group = ev_loop::SpinGroup<ReceiverA>;
    STATIC_REQUIRE(std::is_same_v<ev_loop::detail::group_strategy_t<Group>, ev_loop::Spin>);
  }

  SECTION("WaitGroup alias")
  {
    using Group = ev_loop::WaitGroup<ReceiverA>;
    STATIC_REQUIRE(std::is_same_v<ev_loop::detail::group_strategy_t<Group>, ev_loop::Wait>);
  }

  SECTION("YieldGroup alias")
  {
    using Group = ev_loop::YieldGroup<ReceiverA>;
    STATIC_REQUIRE(std::is_same_v<ev_loop::detail::group_strategy_t<Group>, ev_loop::Yield>);
  }

  SECTION("HybridGroup alias")
  {
    using Group = ev_loop::HybridGroup<ReceiverA>;
    STATIC_REQUIRE(std::is_same_v<ev_loop::detail::group_strategy_t<Group>, ev_loop::Hybrid>);
  }
}

// =============================================================================
// GroupEventLoop construction tests
// =============================================================================

TEST_CASE("GroupEventLoop construction", "[group_event_loop]")
{
  SECTION("Single group construction")
  {
    using Loop = ev_loop::GroupEventLoop<ev_loop::SpinGroup<ReceiverA>>;
    auto loop = Loop::setup().create_unique();
    REQUIRE_FALSE(loop->is_running());
    STATIC_REQUIRE(Loop::group_count == 1);
  }

  SECTION("Multiple groups construction")
  {
    using Loop = ev_loop::GroupEventLoop<ev_loop::SpinGroup<ReceiverA>, ev_loop::WaitGroup<ReceiverB>>;
    auto loop = Loop::setup().create_unique();
    REQUIRE_FALSE(loop->is_running());
    STATIC_REQUIRE(Loop::group_count == 2);
  }

  SECTION("Group with multiple receivers")
  {
    using Loop = ev_loop::GroupEventLoop<ev_loop::SpinGroup<ReceiverA, ReceiverB, ReceiverC>>;
    auto loop = Loop::setup().create_unique();
    STATIC_REQUIRE(Loop::group_count == 1);
  }
}

// =============================================================================
// GroupEventLoop receiver access tests
// =============================================================================

TEST_CASE("GroupEventLoop receiver access", "[group_event_loop]")
{
  SECTION("Access receiver in single group")
  {
    using Loop = ev_loop::GroupEventLoop<ev_loop::SpinGroup<ReceiverA>>;
    auto loop = Loop::setup().create_unique();
    auto& receiver = loop->get<ReceiverA>();
    REQUIRE(receiver.count.load() == 0);
    receiver.count.store(kTestValue4);
    REQUIRE(loop->get<ReceiverA>().count.load() == kTestValue4);
  }

  SECTION("Access receivers across multiple groups")
  {
    using Loop = ev_loop::GroupEventLoop<ev_loop::SpinGroup<ReceiverA>, ev_loop::WaitGroup<ReceiverB>>;
    auto loop = Loop::setup().create_unique();

    auto& recv_a = loop->get<ReceiverA>();
    auto& recv_b = loop->get<ReceiverB>();

    recv_a.count.store(1);
    recv_b.count.store(2);

    REQUIRE(loop->get<ReceiverA>().count.load() == 1);
    REQUIRE(loop->get<ReceiverB>().count.load() == 2);
  }
}

// =============================================================================
// GroupEventLoop start/stop tests
// =============================================================================

TEST_CASE("GroupEventLoop start and stop", "[group_event_loop]")
{
  SECTION("start sets running flag")
  {
    using Loop = ev_loop::GroupEventLoop<ev_loop::SpinGroup<CounterReceiver>>;
    auto loop = Loop::setup().create_unique();
    REQUIRE_FALSE(loop->is_running());
    loop->start();
    REQUIRE(loop->is_running());
    loop->stop();
    REQUIRE_FALSE(loop->is_running());
  }

  SECTION("stop is idempotent")
  {
    using Loop = ev_loop::GroupEventLoop<ev_loop::SpinGroup<CounterReceiver>>;
    auto loop = Loop::setup().create_unique();
    loop->start();
    loop->stop();
    loop->stop(); // Should not crash
    REQUIRE_FALSE(loop->is_running());
  }

  SECTION("destructor calls stop")
  {
    using Loop = ev_loop::GroupEventLoop<ev_loop::SpinGroup<CounterReceiver>>;
    auto loop = Loop::setup().create_unique();
    loop->start();
    REQUIRE(loop->is_running());
    loop.reset(); // Destructor should stop cleanly
  }

  SECTION("Builder::start() returns running loop")
  {
    using Loop = ev_loop::GroupEventLoop<ev_loop::SpinGroup<CounterReceiver>>;
    auto loop = Loop::setup().start_unique();
    REQUIRE(loop->is_running());
    loop->stop();
    REQUIRE_FALSE(loop->is_running());
  }
}

// =============================================================================
// GroupStorage tests
// =============================================================================

TEST_CASE("GroupStorage access by type", "[group_event_loop]")
{
  using Group = ev_loop::SpinGroup<ReceiverA, ReceiverB>;
  ev_loop::detail::GroupStorage<Group> storage;

  auto& recv_a = storage.get<ReceiverA>();
  auto& recv_b = storage.get<ReceiverB>();
  recv_a.count.store(kTestValue1);
  recv_b.count.store(kTestValue2);
  REQUIRE(storage.get<ReceiverA>().count.load() == kTestValue1);
  REQUIRE(storage.get<ReceiverB>().count.load() == kTestValue2);
}

TEST_CASE("GroupStorage access by index", "[group_event_loop]")
{
  using Group = ev_loop::SpinGroup<ReceiverA, ReceiverB>;
  ev_loop::detail::GroupStorage<Group> storage;

  storage.get_at<0>().count.store(kTestValue5);
  storage.get_at<1>().count.store(kTestValue6);
  REQUIRE(storage.get_at<0>().count.load() == kTestValue5);
  REQUIRE(storage.get_at<1>().count.load() == kTestValue6);
}

// =============================================================================
// SpscInbox tests
// =============================================================================

TEST_CASE("SpscInbox push and pop", "[inbox]")
{
  ev_loop::detail::SpscInbox<int> inbox;
  REQUIRE(inbox.empty());

  REQUIRE(inbox.push(kTestValue1));
  REQUIRE(inbox.push(kTestValue2));
  REQUIRE_FALSE(inbox.empty());
  REQUIRE(inbox.size() == 2);

  int value = 0;
  REQUIRE(inbox.try_pop(value));
  REQUIRE(value == kTestValue1);
  REQUIRE(inbox.try_pop(value));
  REQUIRE(value == kTestValue2);
  REQUIRE(inbox.empty());
  REQUIRE_FALSE(inbox.try_pop(value));
}

TEST_CASE("SpscInbox drain", "[inbox]")
{
  ev_loop::detail::SpscInbox<int> inbox;
  inbox.push(1);
  inbox.push(2);
  inbox.push(3);

  int sum = 0;
  const auto count = inbox.drain([&sum](int val) { sum += val; });
  REQUIRE(count == 3);
  REQUIRE(sum == 6);
  REQUIRE(inbox.empty());
}

// =============================================================================
// MpscInbox tests
// =============================================================================

TEST_CASE("MpscInbox push and pop", "[inbox]")
{
  ev_loop::detail::MpscInbox<int> inbox;
  REQUIRE(inbox.empty());

  REQUIRE(inbox.push(kTestValue1));
  REQUIRE(inbox.push(kTestValue2));
  REQUIRE_FALSE(inbox.empty());

  int value = 0;
  REQUIRE(inbox.try_pop(value));
  REQUIRE(value == kTestValue1);
  REQUIRE(inbox.try_pop(value));
  REQUIRE(value == kTestValue2);
  REQUIRE(inbox.empty());
  REQUIRE_FALSE(inbox.try_pop(value));
}

TEST_CASE("MpscInbox concurrent producers", "[inbox]")
{
  constexpr int kItemsPerThread = 1000;
  constexpr int kNumThreads = 4;

  ev_loop::detail::MpscInbox<int, 8192> inbox;
  std::atomic<int> items_pushed{ 0 };

  // Launch multiple producer threads
  std::vector<std::thread> producers;
  producers.reserve(kNumThreads);
  for (int thread_id = 0; thread_id < kNumThreads; ++thread_id) {
    producers.emplace_back([&inbox, &items_pushed, thread_id] {
      for (int idx = 0; idx < kItemsPerThread; ++idx) {
        while (!inbox.push((thread_id * kItemsPerThread) + idx)) { std::this_thread::yield(); }
        items_pushed.fetch_add(1, std::memory_order_relaxed);
      }
    });
  }

  // Consumer on main thread
  int items_popped = 0;
  const int total_items = kNumThreads * kItemsPerThread;

  while (items_popped < total_items) {
    int value = 0;
    if (inbox.try_pop(value)) {
      ++items_popped;
    } else {
      std::this_thread::yield();
    }
  }

  for (auto& thread : producers) { thread.join(); }

  REQUIRE(items_popped == total_items);
  REQUIRE(inbox.empty());
}

TEST_CASE("MpscInbox drain", "[inbox]")
{
  ev_loop::detail::MpscInbox<int> inbox;
  inbox.push(1);
  inbox.push(2);
  inbox.push(3);

  int sum = 0;
  const auto count = inbox.drain([&sum](int val) { sum += val; });
  REQUIRE(count == 3);
  REQUIRE(sum == 6);
  REQUIRE(inbox.empty());
}

// =============================================================================
// Queue type selection tests (compile-time SPSC vs MPSC)
// =============================================================================

namespace {
// Test events for queue selection
struct QueueTestEventA
{
};
struct QueueTestEventB
{
};

// Group1 receives A, emits B
struct QueueTestGroup1Recv
{
  using receives = ev_loop::type_list<QueueTestEventA>;
  using emits = ev_loop::type_list<QueueTestEventB>;
  template<typename D> void on_event(QueueTestEventA /*unused*/, D& /*unused*/) {}
};

// Group2 receives B, emits A
struct QueueTestGroup2Recv
{
  using receives = ev_loop::type_list<QueueTestEventB>;
  using emits = ev_loop::type_list<QueueTestEventA>;
  template<typename D> void on_event(QueueTestEventB /*unused*/, D& /*unused*/) {}
};

// Group3 receives nothing, emits B (same as Group1 - creates MPSC scenario for Group2)
struct QueueTestGroup3Recv
{
  using receives = ev_loop::type_list<QueueTestEventA>;
  using emits = ev_loop::type_list<QueueTestEventB>;
  template<typename D> void on_event(QueueTestEventA /*unused*/, D& /*unused*/) {}
};

using QueueTestGroup1 = ev_loop::SpinGroup<QueueTestGroup1Recv>;
using QueueTestGroup2 = ev_loop::SpinGroup<QueueTestGroup2Recv>;
using QueueTestGroup3 = ev_loop::SpinGroup<QueueTestGroup3Recv>;
} // namespace

TEST_CASE("Queue selection - SPSC for single producer", "[inbox]")
{
  // 2-group ping-pong: each event has exactly 1 producer
  // Group1 emits B (only producer of B for Group2)
  // Group2 emits A (only producer of A for Group1)

  using namespace ev_loop::detail;

  // EventA to Group1: only Group2 emits A → 1 producer → SPSC
  static_assert(count_event_producers_v<QueueTestGroup1, QueueTestEventA, QueueTestGroup1, QueueTestGroup2> == 1);

  // EventB to Group2: only Group1 emits B → 1 producer → SPSC
  static_assert(count_event_producers_v<QueueTestGroup2, QueueTestEventB, QueueTestGroup1, QueueTestGroup2> == 1);

  // Verify SPSC queue is selected
  static_assert(std::is_same_v<select_queue_t<QueueTestEventA, 1>, SpscInbox<QueueTestEventA>>);
  static_assert(std::is_same_v<select_queue_t<QueueTestEventB, 1>, SpscInbox<QueueTestEventB>>);

  REQUIRE(true); // Static asserts passed
}

TEST_CASE("Queue selection - MPSC for multiple producers", "[inbox]")
{
  // 3-group setup: Group1 AND Group3 both emit B to Group2
  // Group1 emits B, Group3 also emits B → 2 producers for Group2's B queue

  using namespace ev_loop::detail;

  // EventB to Group2: Group1 emits B, Group3 emits B → 2 producers → MPSC
  static_assert(
    count_event_producers_v<QueueTestGroup2, QueueTestEventB, QueueTestGroup1, QueueTestGroup2, QueueTestGroup3> == 2);

  // EventA to Group1: only Group2 emits A → 1 producer → SPSC
  static_assert(
    count_event_producers_v<QueueTestGroup1, QueueTestEventA, QueueTestGroup1, QueueTestGroup2, QueueTestGroup3> == 1);

  // Verify MPSC queue is selected for 2+ producers
  static_assert(std::is_same_v<select_queue_t<QueueTestEventB, 2>, MpscInbox<QueueTestEventB>>);
  static_assert(std::is_same_v<select_queue_t<QueueTestEventB, 3>, MpscInbox<QueueTestEventB>>);

  REQUIRE(true); // Static asserts passed
}

// =============================================================================
// GroupWorkSignal tests
// =============================================================================

TEST_CASE("GroupWorkSignal initial state", "[group_event_loop]")
{
  const ev_loop::detail::GroupWorkSignal signal;
  REQUIRE_FALSE(signal.is_stopped());
}

TEST_CASE("GroupWorkSignal notify and consume", "[group_event_loop]")
{
  ev_loop::detail::GroupWorkSignal signal;
  signal.notify_work_available();
  REQUIRE(signal.try_consume());
}

TEST_CASE("GroupWorkSignal stop wakes waiters", "[group_event_loop]")
{
  ev_loop::detail::GroupWorkSignal signal;
  std::atomic<bool> woke_up{ false };

  std::thread waiter([&] {
    const auto sig = signal.get_signal();
    (void)signal.wait_for_work(sig);
    woke_up.store(true);
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(kShortSleepMs));
  signal.stop();
  waiter.join();

  REQUIRE(woke_up.load());
  REQUIRE(signal.is_stopped());
}

TEST_CASE("GroupWorkSignal reset", "[group_event_loop]")
{
  ev_loop::detail::GroupWorkSignal signal;
  signal.stop();
  REQUIRE(signal.is_stopped());
  signal.reset();
  REQUIRE_FALSE(signal.is_stopped());
}

// =============================================================================
// Group event routing trait tests
// =============================================================================

TEST_CASE("Group handles event trait", "[group_event_loop]")
{
  using GroupA = ev_loop::SpinGroup<ReceiverA>;
  using GroupB = ev_loop::WaitGroup<ReceiverB>;

  STATIC_REQUIRE(ev_loop::detail::group_handles_event_v<GroupA, EventA>);
  STATIC_REQUIRE_FALSE(ev_loop::detail::group_handles_event_v<GroupA, EventB>);

  STATIC_REQUIRE(ev_loop::detail::group_handles_event_v<GroupB, EventB>);
  STATIC_REQUIRE_FALSE(ev_loop::detail::group_handles_event_v<GroupB, EventA>);
}

TEST_CASE("Group all events trait", "[group_event_loop]")
{
  using GroupA = ev_loop::SpinGroup<ReceiverA>;
  using Events = ev_loop::detail::group_all_events_t<GroupA>;
  STATIC_REQUIRE(ev_loop::detail::contains_v<Events, EventA>);
}

// =============================================================================
// Event routing tests
// =============================================================================

TEST_CASE("GroupEventLoop prime routes to receiver", "[group_event_loop]")
{
  using Loop = ev_loop::GroupEventLoop<ev_loop::SpinGroup<CounterReceiver>>;

  // Prime event then start
  auto loop = Loop::setup().prime(EventA{ .value = kTestValue4 }).start_unique();

  // Give time for event to be processed
  std::this_thread::sleep_for(std::chrono::milliseconds(kShortSleepMs));

  loop->stop();

  REQUIRE(loop->get<CounterReceiver>().count.load() == 1);
}

TEST_CASE("GroupEventLoop multiple primed events", "[group_event_loop]")
{
  using Loop = ev_loop::GroupEventLoop<ev_loop::SpinGroup<ReceiverA>>;

  // Prime multiple events then start
  auto loop = Loop::setup()
                .prime(EventA{ .value = kTestValue1 })
                .prime(EventA{ .value = kTestValue2 })
                .prime(EventA{ .value = kTestValue3 })
                .start_unique();

  std::this_thread::sleep_for(std::chrono::milliseconds(kShortSleepMs));

  loop->stop();

  // ReceiverA adds the event values
  REQUIRE(loop->get<ReceiverA>().count.load() == kTestValue1 + kTestValue2 + kTestValue3);
}

TEST_CASE("GroupEventLoop events route to correct group", "[group_event_loop]")
{
  using Loop = ev_loop::GroupEventLoop<ev_loop::SpinGroup<ReceiverA>, ev_loop::SpinGroup<ReceiverB>>;

  // EventA should go to ReceiverA, EventB should go to ReceiverB
  auto loop = Loop::setup().prime(EventA{ .value = kTestValue1 }).prime(EventB{ .value = kTestValue2 }).start_unique();

  std::this_thread::sleep_for(std::chrono::milliseconds(kShortSleepMs));

  loop->stop();

  REQUIRE(loop->get<ReceiverA>().count.load() == kTestValue1);
  REQUIRE(loop->get<ReceiverB>().count.load() == kTestValue2);
}

// Receiver that emits to another group
struct EmitterReceiver
{
  using receives = ev_loop::type_list<EventA>;
  using emits = ev_loop::type_list<EventB>;

  std::atomic<int> received{ 0 };

  template<typename Dispatcher> void on_event(EventA event, Dispatcher& dispatcher)
  {
    received.fetch_add(1, std::memory_order_relaxed);
    // Forward the value to EventB
    dispatcher.emit(EventB{ .value = event.value * 2 });
  }
};

TEST_CASE("GroupEventLoop inter-group emission", "[group_event_loop]")
{
  using Loop = ev_loop::GroupEventLoop<ev_loop::SpinGroup<EmitterReceiver>, ev_loop::SpinGroup<ReceiverB>>;

  // Prime EventA to EmitterReceiver, which will emit EventB to ReceiverB
  auto loop = Loop::setup().prime(EventA{ .value = kTestValue1 }).start_unique();

  std::this_thread::sleep_for(std::chrono::milliseconds(kShortSleepMs));

  loop->stop();

  REQUIRE(loop->get<EmitterReceiver>().received.load() == 1);
  REQUIRE(loop->get<ReceiverB>().count.load() == kTestValue1 * 2);
}

// Receiver that emits to same group
struct SameGroupEmitter
{
  using receives = ev_loop::type_list<EventA>;
  using emits = ev_loop::type_list<EventA>;

  std::atomic<int> received{ 0 };

  template<typename Dispatcher> void on_event(EventA event, Dispatcher& dispatcher)
  {
    received.fetch_add(1, std::memory_order_relaxed);
    // Only emit if value > 1 to prevent infinite loop
    if (event.value > 1) { dispatcher.emit(EventA{ .value = event.value - 1 }); }
  }
};

TEST_CASE("GroupEventLoop intra-group emission", "[group_event_loop]")
{
  using Loop = ev_loop::GroupEventLoop<ev_loop::SpinGroup<SameGroupEmitter>>;

  // Prime EventA with value 3, should trigger chain: 3 -> 2 -> 1
  auto loop = Loop::setup().prime(EventA{ .value = 3 }).start_unique();

  std::this_thread::sleep_for(std::chrono::milliseconds(kShortSleepMs));

  loop->stop();

  // Receiver should have been called 3 times (value=3, value=2, value=1)
  REQUIRE(loop->get<SameGroupEmitter>().received.load() == 3);
}

// Receiver that emits EventB (for testing intra-group routing to different receiver)
struct IntraGroupProducer
{
  using receives = ev_loop::type_list<EventA>;
  using emits = ev_loop::type_list<EventB>;

  std::atomic<int> received{ 0 };

  template<typename Dispatcher> void on_event(EventA event, Dispatcher& dispatcher)
  {
    received.fetch_add(1, std::memory_order_relaxed);
    dispatcher.emit(EventB{ .value = event.value * 3 });
  }
};

TEST_CASE("GroupEventLoop intra-group routing between different receivers", "[group_event_loop]")
{
  // Both receivers in same group: Producer receives EventA, emits EventB to Consumer
  using Loop = ev_loop::GroupEventLoop<ev_loop::SpinGroup<IntraGroupProducer, ReceiverB>>;

  // Prime EventA to Producer, which emits EventB to ReceiverB (same group)
  auto loop = Loop::setup().prime(EventA{ .value = kTestValue1 }).start_unique();

  std::this_thread::sleep_for(std::chrono::milliseconds(kShortSleepMs));

  loop->stop();

  REQUIRE(loop->get<IntraGroupProducer>().received.load() == 1);
  REQUIRE(loop->get<ReceiverB>().count.load() == kTestValue1 * 3);
}

// Second EventB receiver for multi-group broadcast test
struct ReceiverB2
{
  using receives = ev_loop::type_list<EventB>;
  using emits = ev_loop::type_list<>;

  std::atomic<int> count{ 0 };

  template<typename Dispatcher> void on_event(EventB event, Dispatcher& /*dispatcher*/)
  {
    count.fetch_add(event.value, std::memory_order_relaxed);
  }
};

TEST_CASE("GroupEventLoop broadcasts to all groups handling event", "[group_event_loop]")
{
  // EmitterReceiver emits EventB, which should go to BOTH ReceiverB and ReceiverB2
  using Loop = ev_loop::
    GroupEventLoop<ev_loop::SpinGroup<EmitterReceiver>, ev_loop::SpinGroup<ReceiverB>, ev_loop::SpinGroup<ReceiverB2>>;

  // Prime EventA -> EmitterReceiver emits EventB -> should go to both group 1 and group 2
  auto loop = Loop::setup().prime(EventA{ .value = kTestValue1 }).start_unique();

  std::this_thread::sleep_for(std::chrono::milliseconds(kShortSleepMs));

  loop->stop();

  REQUIRE(loop->get<EmitterReceiver>().received.load() == 1);
  // Both receivers should get the same EventB
  REQUIRE(loop->get<ReceiverB>().count.load() == kTestValue1 * 2);
  REQUIRE(loop->get<ReceiverB2>().count.load() == kTestValue1 * 2);
}

// =============================================================================
// Copy/Move optimization tests
// =============================================================================

// TrackedEvent is an alias for TrackedInt from test_utils.hpp
using TrackedEvent = TrackedInt;

// CRTP base for receivers that handle TrackedEvent
template<typename Derived> class TrackedReceiverBase
{
  friend Derived;
  TrackedReceiverBase() = default;

public:
  using receives = ev_loop::type_list<TrackedEvent>;
  using emits = ev_loop::type_list<>;

  std::atomic<int> received{ 0 };

  // NOLINTNEXTLINE(performance-unnecessary-value-param)
  template<typename Dispatcher> void on_event(TrackedEvent /*event*/, Dispatcher& /*dispatcher*/)
  {
    received.fetch_add(1, std::memory_order_relaxed);
  }
};

// Receivers for single-group test (3 receivers)
struct TrackedReceiver1 : TrackedReceiverBase<TrackedReceiver1>
{
};
struct TrackedReceiver2 : TrackedReceiverBase<TrackedReceiver2>
{
};
struct TrackedReceiver3 : TrackedReceiverBase<TrackedReceiver3>
{
};

// Receivers for multi-group test (Group 1: 3 receivers)
struct TrackedReceiverG1R1 : TrackedReceiverBase<TrackedReceiverG1R1>
{
};
struct TrackedReceiverG1R2 : TrackedReceiverBase<TrackedReceiverG1R2>
{
};
struct TrackedReceiverG1R3 : TrackedReceiverBase<TrackedReceiverG1R3>
{
};

// Receivers for multi-group test (Group 2: 3 receivers)
struct TrackedReceiverG2R1 : TrackedReceiverBase<TrackedReceiverG2R1>
{
};
struct TrackedReceiverG2R2 : TrackedReceiverBase<TrackedReceiverG2R2>
{
};
struct TrackedReceiverG2R3 : TrackedReceiverBase<TrackedReceiverG2R3>
{
};

TEST_CASE("GroupEventLoop copy/move optimization - single group with 3 receivers", "[group_event_loop][copy_move]")
{
  // Single group with 3 receivers wanting the same event
  // Expected: 2 copies, 1 move for intra-group dispatch (copy to N-1, move to last)
  using Loop = ev_loop::GroupEventLoop<ev_loop::SpinGroup<TrackedReceiver1, TrackedReceiver2, TrackedReceiver3>>;

  auto counter = std::make_shared<TrackingCounter>();

  // Prime and start
  auto loop = Loop::setup().prime(TrackedEvent{ counter, kTestValue4 }).start_unique();

  std::this_thread::sleep_for(std::chrono::milliseconds(kShortSleepMs));
  loop->stop();

  // All 3 receivers should have received the event
  REQUIRE(loop->get<TrackedReceiver1>().received.load() == 1);
  REQUIRE(loop->get<TrackedReceiver2>().received.load() == 1);
  REQUIRE(loop->get<TrackedReceiver3>().received.load() == 1);

  // Copy/move counts:
  // - Prime to queue: 1 copy (into queue)
  // - Intra-group dispatch to 3 receivers: 2 copies (to receivers 1,2), 1 move (to receiver 3)
  // Total: 3 copies, 1 move
  INFO("Copy count: " << counter->copy_count.load());
  INFO("Move count: " << counter->move_count.load());

  // The intra-group dispatch should use copy-to-N-1, move-to-last
  // Note: there's also 1 copy to push into the queue
  REQUIRE(counter->copy_count.load() >= 2); // At least 2 copies for intra-group
  REQUIRE(counter->move_count.load() >= 1); // At least 1 move for last receiver
}

TEST_CASE("GroupEventLoop copy/move optimization - two groups with 1 receiver each", "[group_event_loop][copy_move]")
{
  // Simple case: 2 groups, 1 receiver each
  using Loop =
    ev_loop::GroupEventLoop<ev_loop::SpinGroup<TrackedReceiverG1R1>, ev_loop::SpinGroup<TrackedReceiverG2R1>>;

  auto counter = std::make_shared<TrackingCounter>();

  auto loop = Loop::setup().prime(TrackedEvent{ counter, kTestValue4 }).start_unique();

  std::this_thread::sleep_for(std::chrono::milliseconds(kShortSleepMs));
  loop->stop();

  INFO("G1R1 received: " << loop->get<TrackedReceiverG1R1>().received.load());
  INFO("G2R1 received: " << loop->get<TrackedReceiverG2R1>().received.load());
  REQUIRE(loop->get<TrackedReceiverG1R1>().received.load() == 1);
  REQUIRE(loop->get<TrackedReceiverG2R1>().received.load() == 1);

  INFO("Copy count: " << counter->copy_count.load());
  INFO("Move count: " << counter->move_count.load());
}

TEST_CASE("GroupEventLoop copy/move optimization - two groups with 3 receivers each", "[group_event_loop][copy_move]")
{
  // Two groups, each with 3 receivers wanting the same event
  // Expected inter-group: 1 copy (to N-1 groups), 1 move (to last group)
  // Expected intra-group per group: 2 copies, 1 move
  using Loop =
    ev_loop::GroupEventLoop<ev_loop::SpinGroup<TrackedReceiverG1R1, TrackedReceiverG1R2, TrackedReceiverG1R3>,
      ev_loop::SpinGroup<TrackedReceiverG2R1, TrackedReceiverG2R2, TrackedReceiverG2R3>>;

  auto counter = std::make_shared<TrackingCounter>();

  // Prime and start - event should go to both groups
  auto loop = Loop::setup().prime(TrackedEvent{ counter, kTestValue4 }).start_unique();

  std::this_thread::sleep_for(std::chrono::milliseconds(kShortSleepMs));
  loop->stop();

  // All 6 receivers should have received the event
  REQUIRE(loop->get<TrackedReceiverG1R1>().received.load() == 1);
  REQUIRE(loop->get<TrackedReceiverG1R2>().received.load() == 1);
  REQUIRE(loop->get<TrackedReceiverG1R3>().received.load() == 1);
  REQUIRE(loop->get<TrackedReceiverG2R1>().received.load() == 1);
  REQUIRE(loop->get<TrackedReceiverG2R2>().received.load() == 1);
  REQUIRE(loop->get<TrackedReceiverG2R3>().received.load() == 1);

  // Copy/move counts:
  // - Prime routes to 2 groups: 1 copy (to group 1 queue), 1 move (to group 2 queue)
  // - Group 1 intra-dispatch to 3 receivers: 2 copies, 1 move
  // - Group 2 intra-dispatch to 3 receivers: 2 copies, 1 move
  // Total: 5 copies, 3 moves (theoretical minimum)
  INFO("Copy count: " << counter->copy_count.load());
  INFO("Move count: " << counter->move_count.load());

  // Verify move optimization is working: should have moves, not all copies
  REQUIRE(counter->move_count.load() >= 2); // At least 2 moves (one per group for last receiver)
}
