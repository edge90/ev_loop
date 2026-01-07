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
#include <utility>
// NOLINTEND(misc-include-cleaner)

namespace {
constexpr int kTestValue1 = 10;
constexpr int kTestValue2 = 20;
constexpr int kTestValue3 = 30;
constexpr int kTestValue4 = 42;
constexpr int kTestValue5 = 100;
constexpr int kTestValue6 = 200;
constexpr auto kDefaultTimeout = std::chrono::milliseconds(5000);

// Wait for a condition to become true, with timeout. Returns true if condition was met.
template<typename Pred> bool wait_for(const Pred& pred, std::chrono::milliseconds timeout = kDefaultTimeout)
{
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (!pred()) {
    if (std::chrono::steady_clock::now() > deadline) { return false; }
    std::this_thread::yield();
  }
  return true;
}

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
// GroupEventLoop construction tests
// =============================================================================

// NOLINTBEGIN(readability-function-cognitive-complexity)

TEST_CASE("GroupEventLoop construction", "[group_event_loop]")
{
  SECTION("Single group construction")
  {
    using Loop = ev_loop::GroupEventLoop<ev_loop::SpinGroup<ReceiverA>>;
    auto loop = Loop::setup().create_unique();
    REQUIRE_FALSE(loop->is_running());
  }

  SECTION("Multiple groups construction")
  {
    using Loop = ev_loop::GroupEventLoop<ev_loop::SpinGroup<ReceiverA>, ev_loop::WaitGroup<ReceiverB>>;
    auto loop = Loop::setup().create_unique();
    REQUIRE_FALSE(loop->is_running());
  }

  SECTION("Group with multiple receivers")
  {
    using Loop = ev_loop::GroupEventLoop<ev_loop::SpinGroup<ReceiverA, ReceiverB, ReceiverC>>;
    auto loop = Loop::setup().create_unique();
    REQUIRE_FALSE(loop->is_running());
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
    auto loop = Loop::setup().create_unique();
    loop->start();
    REQUIRE(loop->is_running());
    loop->stop();
    REQUIRE_FALSE(loop->is_running());
  }
}

// NOLINTEND(readability-function-cognitive-complexity)

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

// NOLINTBEGIN(readability-function-cognitive-complexity)
TEST_CASE("GroupWorkSignal", "[group_event_loop]")
{
  ev_loop::detail::GroupWorkSignal signal;

  SECTION("initial state is not stopped") { REQUIRE_FALSE(signal.is_stopped()); }

  SECTION("notify and consume")
  {
    signal.notify_work_available();
    REQUIRE(signal.try_consume());
  }

  SECTION("stop wakes waiters")
  {
    std::atomic<bool> ready_to_wait{ false };
    std::atomic<bool> woke_up{ false };

    std::thread waiter([&] {
      const auto sig = signal.get_signal();
      ready_to_wait.store(true, std::memory_order_release);
      (void)signal.wait_for_work(sig);
      woke_up.store(true);
    });

    REQUIRE(wait_for([&] { return ready_to_wait.load(std::memory_order_acquire); }));
    signal.stop();
    waiter.join();

    REQUIRE(woke_up.load());
    REQUIRE(signal.is_stopped());
  }

  SECTION("reset clears stopped state")
  {
    signal.stop();
    REQUIRE(signal.is_stopped());
    signal.reset();
    REQUIRE_FALSE(signal.is_stopped());
  }
}
// NOLINTEND(readability-function-cognitive-complexity)

// =============================================================================
// Event routing tests
// =============================================================================

TEST_CASE("GroupEventLoop prime routes to receiver", "[group_event_loop]")
{
  using Loop = ev_loop::GroupEventLoop<ev_loop::SpinGroup<CounterReceiver>>;

  // Prime event then start
  auto loop = Loop::setup().prime(EventA{ .value = kTestValue4 }).create_unique();
  loop->start();

  REQUIRE(wait_for([&] { return loop->get<CounterReceiver>().count.load() == 1; }));

  loop->stop();
}

TEST_CASE("GroupEventLoop multiple primed events", "[group_event_loop]")
{
  using Loop = ev_loop::GroupEventLoop<ev_loop::SpinGroup<ReceiverA>>;
  constexpr int kExpected = kTestValue1 + kTestValue2 + kTestValue3;

  // Prime multiple events then start
  auto loop = Loop::setup()
                .prime(EventA{ .value = kTestValue1 })
                .prime(EventA{ .value = kTestValue2 })
                .prime(EventA{ .value = kTestValue3 })
                .create_unique();
  loop->start();

  REQUIRE(wait_for([&] { return loop->get<ReceiverA>().count.load() == kExpected; }));

  loop->stop();
}

TEST_CASE("GroupEventLoop events route to correct group", "[group_event_loop]")
{
  using Loop = ev_loop::GroupEventLoop<ev_loop::SpinGroup<ReceiverA>, ev_loop::SpinGroup<ReceiverB>>;

  // EventA should go to ReceiverA, EventB should go to ReceiverB
  auto loop = Loop::setup().prime(EventA{ .value = kTestValue1 }).prime(EventB{ .value = kTestValue2 }).create_unique();
  loop->start();

  REQUIRE(wait_for([&] {
    return loop->get<ReceiverA>().count.load() == kTestValue1 && loop->get<ReceiverB>().count.load() == kTestValue2;
  }));

  loop->stop();
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
  auto loop = Loop::setup().prime(EventA{ .value = kTestValue1 }).create_unique();
  loop->start();

  REQUIRE(wait_for([&] {
    return loop->get<EmitterReceiver>().received.load() == 1 && loop->get<ReceiverB>().count.load() == kTestValue1 * 2;
  }));

  loop->stop();
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
  auto loop = Loop::setup().prime(EventA{ .value = 3 }).create_unique();
  loop->start();

  // Receiver should have been called 3 times (value=3, value=2, value=1)
  REQUIRE(wait_for([&] { return loop->get<SameGroupEmitter>().received.load() == 3; }));

  loop->stop();
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
  auto loop = Loop::setup().prime(EventA{ .value = kTestValue1 }).create_unique();
  loop->start();

  REQUIRE(wait_for([&] {
    return loop->get<IntraGroupProducer>().received.load() == 1
           && loop->get<ReceiverB>().count.load() == kTestValue1 * 3;
  }));

  loop->stop();
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
  auto loop = Loop::setup().prime(EventA{ .value = kTestValue1 }).create_unique();
  loop->start();

  REQUIRE(wait_for([&] {
    return loop->get<EmitterReceiver>().received.load() == 1 && loop->get<ReceiverB>().count.load() == kTestValue1 * 2
           && loop->get<ReceiverB2>().count.load() == kTestValue1 * 2;
  }));

  loop->stop();
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
  auto loop = Loop::setup().prime(TrackedEvent{ counter, kTestValue4 }).create_unique();
  loop->start();

  // All 3 receivers should have received the event
  REQUIRE(wait_for([&] {
    return loop->get<TrackedReceiver1>().received.load() == 1 && loop->get<TrackedReceiver2>().received.load() == 1
           && loop->get<TrackedReceiver3>().received.load() == 1;
  }));
  loop->stop();

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

  auto loop = Loop::setup().prime(TrackedEvent{ counter, kTestValue4 }).create_unique();
  loop->start();

  REQUIRE(wait_for([&] {
    return loop->get<TrackedReceiverG1R1>().received.load() == 1
           && loop->get<TrackedReceiverG2R1>().received.load() == 1;
  }));
  loop->stop();

  INFO("G1R1 received: " << loop->get<TrackedReceiverG1R1>().received.load());
  INFO("G2R1 received: " << loop->get<TrackedReceiverG2R1>().received.load());

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
  auto loop = Loop::setup().prime(TrackedEvent{ counter, kTestValue4 }).create_unique();
  loop->start();

  // All 6 receivers should have received the event
  REQUIRE(wait_for([&] {
    return loop->get<TrackedReceiverG1R1>().received.load() == 1
           && loop->get<TrackedReceiverG1R2>().received.load() == 1
           && loop->get<TrackedReceiverG1R3>().received.load() == 1
           && loop->get<TrackedReceiverG2R1>().received.load() == 1
           && loop->get<TrackedReceiverG2R2>().received.load() == 1
           && loop->get<TrackedReceiverG2R3>().received.load() == 1;
  }));
  loop->stop();

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

// =============================================================================
// run<I>() tests - run specific group on current thread
// =============================================================================

TEST_CASE("GroupEventLoop run<I>() runs group on current thread", "[group_event_loop]")
{
  using Loop = ev_loop::GroupEventLoop<ev_loop::SpinGroup<ReceiverA>, ev_loop::SpinGroup<ReceiverB>>;

  auto loop = Loop::setup().prime(EventA{ .value = kTestValue1 }).prime(EventB{ .value = kTestValue2 }).create_unique();

  // Launch run<0>() in a separate thread (since it blocks until stopped)
  std::atomic<bool> started{ false };
  std::thread runner([&loop, &started] {
    started.store(true);
    loop->run<0>(); // Run group 0 on this thread, start group 1 on its own thread
  });

  // Wait for the runner to start and events to be processed
  REQUIRE(wait_for([&] {
    return started.load() && loop->get<ReceiverA>().count.load() == kTestValue1
           && loop->get<ReceiverB>().count.load() == kTestValue2;
  }));

  loop->stop();
  runner.join();
}

// =============================================================================
// run_while() tests - conditional running with predicate
// =============================================================================

TEST_CASE("GroupEventLoop run_while() stops when predicate returns false", "[group_event_loop]")
{
  using Loop = ev_loop::GroupEventLoop<ev_loop::SpinGroup<CounterReceiver>>;

  // Prime 10 events using the Setup builder
  auto loop = Loop::setup()
                .prime(EventA{ .value = 1 })
                .prime(EventA{ .value = 1 })
                .prime(EventA{ .value = 1 })
                .prime(EventA{ .value = 1 })
                .prime(EventA{ .value = 1 })
                .prime(EventA{ .value = 1 })
                .prime(EventA{ .value = 1 })
                .prime(EventA{ .value = 1 })
                .prime(EventA{ .value = 1 })
                .prime(EventA{ .value = 1 })
                .create_unique();

  std::thread runner([&loop] {
    loop->run<0>(); // Run until stopped
  });

  // Wait for all events to be processed
  REQUIRE(wait_for([&] { return loop->get<CounterReceiver>().count.load() == 10; }));
  loop->stop();
  runner.join();
}

// Test manual polling with a count limit (simulating run_while behavior)
TEST_CASE("Manual polling with count limit", "[group_event_loop]")
{
  using Loop = ev_loop::GroupEventLoop<ev_loop::SpinGroup<CounterReceiver>>;

  // Build loop with 5 events
  auto loop = Loop::setup()
                .prime(EventA{ .value = 1 })
                .prime(EventA{ .value = 1 })
                .prime(EventA{ .value = 1 })
                .prime(EventA{ .value = 1 })
                .prime(EventA{ .value = 1 })
                .create_unique();

  // Poll manually with a count limit
  int poll_count = 0;
  while (loop->poll_group<0>()) { ++poll_count; }

  REQUIRE(loop->get<CounterReceiver>().count.load() == 5);
  REQUIRE(poll_count == 5);
}

// =============================================================================
// join() tests - explicit join
// =============================================================================

TEST_CASE("GroupEventLoop join() waits for all threads", "[group_event_loop]")
{
  using Loop = ev_loop::GroupEventLoop<ev_loop::SpinGroup<ReceiverA>, ev_loop::WaitGroup<ReceiverB>>;

  auto loop = Loop::setup().prime(EventA{ .value = kTestValue1 }).prime(EventB{ .value = kTestValue2 }).create_unique();

  loop->start();

  // Wait for events to be processed
  REQUIRE(wait_for([&] {
    return loop->get<ReceiverA>().count.load() == kTestValue1 && loop->get<ReceiverB>().count.load() == kTestValue2;
  }));

  // Stop signals all threads to stop
  loop->stop(); // This calls join internally

  // Verify threads have joined
  REQUIRE_FALSE(loop->is_running());
}

// =============================================================================
// poll_group return value tests
// =============================================================================

// NOLINTBEGIN(readability-function-cognitive-complexity)
TEST_CASE("poll_group return value", "[group_event_loop]")
{
  using Loop = ev_loop::GroupEventLoop<ev_loop::SpinGroup<ReceiverA>>;

  SECTION("returns true when work was done")
  {
    auto loop = Loop::setup().prime(EventA{ .value = kTestValue1 }).create_unique();

    REQUIRE(loop->poll_group<0>());
    REQUIRE_FALSE(loop->poll_group<0>()); // No more work
  }

  SECTION("returns false when no work available")
  {
    auto loop = Loop::setup().create_unique(); // No primed events

    REQUIRE_FALSE(loop->poll_group<0>());
  }
}

// NOLINTEND(readability-function-cognitive-complexity)

// =============================================================================
// GroupStorage receivers() accessor test
// =============================================================================

TEST_CASE("GroupStorage receivers() returns tuple", "[group_event_loop]")
{
  using Group = ev_loop::SpinGroup<ReceiverA, ReceiverB>;
  ev_loop::detail::GroupStorage<Group> storage;

  auto& tuple = storage.receivers();
  std::get<0>(tuple).count.store(kTestValue1);
  std::get<1>(tuple).count.store(kTestValue2);

  REQUIRE(storage.get<ReceiverA>().count.load() == kTestValue1);
  REQUIRE(storage.get<ReceiverB>().count.load() == kTestValue2);
}

// =============================================================================
// const access tests (deducing this)
// =============================================================================

// NOLINTBEGIN(readability-function-cognitive-complexity)
TEST_CASE("const access via deducing this", "[group_event_loop]")
{
  SECTION("GroupEventLoop const access via get()")
  {
    using Loop = ev_loop::GroupEventLoop<ev_loop::SpinGroup<ReceiverA>>;

    auto loop = Loop::setup().create_unique();
    loop->get<ReceiverA>().count.store(kTestValue1);

    const auto& const_loop = *loop;
    REQUIRE(const_loop.get<ReceiverA>().count.load() == kTestValue1);
  }

  SECTION("GroupStorage const access")
  {
    using Group = ev_loop::SpinGroup<ReceiverA>;
    ev_loop::detail::GroupStorage<Group> storage;
    storage.get<ReceiverA>().count.store(kTestValue1);

    const auto& const_storage = storage;
    REQUIRE(const_storage.get<ReceiverA>().count.load() == kTestValue1);
  }
}
// NOLINTEND(readability-function-cognitive-complexity)

} // namespace
