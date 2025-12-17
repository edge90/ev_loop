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
    const Loop loop;
    REQUIRE_FALSE(loop.is_running());
    STATIC_REQUIRE(Loop::group_count == 1);
  }

  SECTION("Multiple groups construction")
  {
    using Loop = ev_loop::GroupEventLoop<ev_loop::SpinGroup<ReceiverA>, ev_loop::WaitGroup<ReceiverB>>;
    const Loop loop;
    REQUIRE_FALSE(loop.is_running());
    STATIC_REQUIRE(Loop::group_count == 2);
  }

  SECTION("Group with multiple receivers")
  {
    using Loop = ev_loop::GroupEventLoop<ev_loop::SpinGroup<ReceiverA, ReceiverB, ReceiverC>>;
    const Loop loop;
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
    ev_loop::GroupEventLoop<ev_loop::SpinGroup<ReceiverA>> loop;
    auto& receiver = loop.get<ReceiverA>();
    REQUIRE(receiver.count.load() == 0);
    receiver.count.store(kTestValue4);
    REQUIRE(loop.get<ReceiverA>().count.load() == kTestValue4);
  }

  SECTION("Access receivers across multiple groups")
  {
    ev_loop::GroupEventLoop<ev_loop::SpinGroup<ReceiverA>, ev_loop::WaitGroup<ReceiverB>> loop;

    auto& recv_a = loop.get<ReceiverA>();
    auto& recv_b = loop.get<ReceiverB>();

    recv_a.count.store(1);
    recv_b.count.store(2);

    REQUIRE(loop.get<ReceiverA>().count.load() == 1);
    REQUIRE(loop.get<ReceiverB>().count.load() == 2);
  }
}

// =============================================================================
// GroupEventLoop start/stop tests
// =============================================================================

TEST_CASE("GroupEventLoop start and stop", "[group_event_loop]")
{
  SECTION("start sets running flag")
  {
    ev_loop::GroupEventLoop<ev_loop::SpinGroup<CounterReceiver>> loop;
    REQUIRE_FALSE(loop.is_running());
    loop.start();
    REQUIRE(loop.is_running());
    loop.stop();
    REQUIRE_FALSE(loop.is_running());
  }

  SECTION("stop is idempotent")
  {
    ev_loop::GroupEventLoop<ev_loop::SpinGroup<CounterReceiver>> loop;
    loop.start();
    loop.stop();
    loop.stop(); // Should not crash
    REQUIRE_FALSE(loop.is_running());
  }

  SECTION("destructor calls stop")
  {
    auto loop = std::make_unique<ev_loop::GroupEventLoop<ev_loop::SpinGroup<CounterReceiver>>>();
    loop->start();
    REQUIRE(loop->is_running());
    loop.reset(); // Destructor should stop cleanly
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
    (void)signal.wait_for_work();
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

TEST_CASE("GroupEventLoop external emit routes to receiver", "[group_event_loop]")
{
  ev_loop::GroupEventLoop<ev_loop::SpinGroup<CounterReceiver>> loop;
  loop.start();

  // Give the thread time to start
  std::this_thread::sleep_for(std::chrono::milliseconds(kShortSleepMs));

  // Emit event externally
  loop.emit(EventA{ .value = kTestValue4 });

  // Give time for event to be processed
  std::this_thread::sleep_for(std::chrono::milliseconds(kShortSleepMs));

  loop.stop();

  REQUIRE(loop.get<CounterReceiver>().count.load() == 1);
}

TEST_CASE("GroupEventLoop multiple external emits", "[group_event_loop]")
{
  ev_loop::GroupEventLoop<ev_loop::SpinGroup<ReceiverA>> loop;
  loop.start();

  std::this_thread::sleep_for(std::chrono::milliseconds(kShortSleepMs));

  loop.emit(EventA{ .value = kTestValue1 });
  loop.emit(EventA{ .value = kTestValue2 });
  loop.emit(EventA{ .value = kTestValue3 });

  std::this_thread::sleep_for(std::chrono::milliseconds(kShortSleepMs));

  loop.stop();

  // ReceiverA adds the event values
  REQUIRE(loop.get<ReceiverA>().count.load() == kTestValue1 + kTestValue2 + kTestValue3);
}

TEST_CASE("GroupEventLoop events route to correct group", "[group_event_loop]")
{
  using Loop = ev_loop::GroupEventLoop<ev_loop::SpinGroup<ReceiverA>, ev_loop::SpinGroup<ReceiverB>>;
  Loop loop;
  loop.start();

  std::this_thread::sleep_for(std::chrono::milliseconds(kShortSleepMs));

  // EventA should go to ReceiverA, EventB should go to ReceiverB
  loop.emit(EventA{ .value = kTestValue1 });
  loop.emit(EventB{ .value = kTestValue2 });

  std::this_thread::sleep_for(std::chrono::milliseconds(kShortSleepMs));

  loop.stop();

  REQUIRE(loop.get<ReceiverA>().count.load() == kTestValue1);
  REQUIRE(loop.get<ReceiverB>().count.load() == kTestValue2);
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
  Loop loop;
  loop.start();

  std::this_thread::sleep_for(std::chrono::milliseconds(kShortSleepMs));

  // Emit EventA to EmitterReceiver, which will emit EventB to ReceiverB
  loop.emit(EventA{ .value = kTestValue1 });

  std::this_thread::sleep_for(std::chrono::milliseconds(kShortSleepMs));

  loop.stop();

  REQUIRE(loop.get<EmitterReceiver>().received.load() == 1);
  REQUIRE(loop.get<ReceiverB>().count.load() == kTestValue1 * 2);
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
  Loop loop;
  loop.start();

  std::this_thread::sleep_for(std::chrono::milliseconds(kShortSleepMs));

  // Emit EventA with value 3, should trigger chain: 3 -> 2 -> 1
  loop.emit(EventA{ .value = 3 });

  std::this_thread::sleep_for(std::chrono::milliseconds(kShortSleepMs));

  loop.stop();

  // Receiver should have been called 3 times (value=3, value=2, value=1)
  REQUIRE(loop.get<SameGroupEmitter>().received.load() == 3);
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
  Loop loop;
  loop.start();

  std::this_thread::sleep_for(std::chrono::milliseconds(kShortSleepMs));

  // Emit EventA to Producer, which emits EventB to ReceiverB (same group)
  loop.emit(EventA{ .value = kTestValue1 });

  std::this_thread::sleep_for(std::chrono::milliseconds(kShortSleepMs));

  loop.stop();

  REQUIRE(loop.get<IntraGroupProducer>().received.load() == 1);
  REQUIRE(loop.get<ReceiverB>().count.load() == kTestValue1 * 3);
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
  Loop loop;
  loop.start();

  std::this_thread::sleep_for(std::chrono::milliseconds(kShortSleepMs));

  // Emit EventA -> EmitterReceiver emits EventB -> should go to both group 1 and group 2
  loop.emit(EventA{ .value = kTestValue1 });

  std::this_thread::sleep_for(std::chrono::milliseconds(kShortSleepMs));

  loop.stop();

  REQUIRE(loop.get<EmitterReceiver>().received.load() == 1);
  // Both receivers should get the same EventB
  REQUIRE(loop.get<ReceiverB>().count.load() == kTestValue1 * 2);
  REQUIRE(loop.get<ReceiverB2>().count.load() == kTestValue1 * 2);
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
  Loop loop;

  auto counter = std::make_shared<TrackingCounter>();

  loop.start();
  std::this_thread::sleep_for(std::chrono::milliseconds(kShortSleepMs));

  // Emit a TrackedEvent
  loop.emit(TrackedEvent{ counter, kTestValue4 });

  std::this_thread::sleep_for(std::chrono::milliseconds(kShortSleepMs));
  loop.stop();

  // All 3 receivers should have received the event
  REQUIRE(loop.get<TrackedReceiver1>().received.load() == 1);
  REQUIRE(loop.get<TrackedReceiver2>().received.load() == 1);
  REQUIRE(loop.get<TrackedReceiver3>().received.load() == 1);

  // Copy/move counts:
  // - External emit to queue: 1 copy (into queue)
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
  Loop loop;

  auto counter = std::make_shared<TrackingCounter>();

  loop.start();
  std::this_thread::sleep_for(std::chrono::milliseconds(kShortSleepMs));

  loop.emit(TrackedEvent{ counter, kTestValue4 });

  std::this_thread::sleep_for(std::chrono::milliseconds(kShortSleepMs));
  loop.stop();

  INFO("G1R1 received: " << loop.get<TrackedReceiverG1R1>().received.load());
  INFO("G2R1 received: " << loop.get<TrackedReceiverG2R1>().received.load());
  REQUIRE(loop.get<TrackedReceiverG1R1>().received.load() == 1);
  REQUIRE(loop.get<TrackedReceiverG2R1>().received.load() == 1);

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
  Loop loop;

  auto counter = std::make_shared<TrackingCounter>();

  loop.start();
  std::this_thread::sleep_for(std::chrono::milliseconds(kShortSleepMs));

  // Emit a TrackedEvent - should go to both groups
  loop.emit(TrackedEvent{ counter, kTestValue4 });

  std::this_thread::sleep_for(std::chrono::milliseconds(kShortSleepMs));
  loop.stop();

  // All 6 receivers should have received the event
  REQUIRE(loop.get<TrackedReceiverG1R1>().received.load() == 1);
  REQUIRE(loop.get<TrackedReceiverG1R2>().received.load() == 1);
  REQUIRE(loop.get<TrackedReceiverG1R3>().received.load() == 1);
  REQUIRE(loop.get<TrackedReceiverG2R1>().received.load() == 1);
  REQUIRE(loop.get<TrackedReceiverG2R2>().received.load() == 1);
  REQUIRE(loop.get<TrackedReceiverG2R3>().received.load() == 1);

  // Copy/move counts:
  // - External emit routes to 2 groups: 1 copy (to group 1 queue), 1 move (to group 2 queue)
  // - Group 1 intra-dispatch to 3 receivers: 2 copies, 1 move
  // - Group 2 intra-dispatch to 3 receivers: 2 copies, 1 move
  // Total: 5 copies, 3 moves (theoretical minimum)
  INFO("Copy count: " << counter->copy_count.load());
  INFO("Move count: " << counter->move_count.load());

  // Verify move optimization is working: should have moves, not all copies
  REQUIRE(counter->move_count.load() >= 2); // At least 2 moves (one per group for last receiver)
}
