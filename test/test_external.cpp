// NOLINTBEGIN(misc-include-cleaner)
#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <ev_loop/ev.hpp>
#include <thread>
#include <vector>
// NOLINTEND(misc-include-cleaner)

namespace {
constexpr int kTestValue1 = 10;
constexpr int kTestValue2 = 20;
constexpr int kTestValue3 = 30;

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

// External input definition - defines what events can be emitted
struct PrimaryExternalInputs
{
  using emits = ev_loop::type_list<EventA, EventB>;
};

// Receiver that counts external events
struct ExternalEventCounter
{
  using receives = ev_loop::type_list<EventA, EventB>;
  using emits = ev_loop::type_list<>;

  std::atomic<int> event_a_count{ 0 };
  std::atomic<int> event_b_count{ 0 };

  template<typename Dispatcher> void on_event(EventA event, Dispatcher& /*unused*/)
  {
    event_a_count.fetch_add(event.value, std::memory_order_relaxed);
  }

  template<typename Dispatcher> void on_event(EventB event, Dispatcher& /*unused*/)
  {
    event_b_count.fetch_add(event.value, std::memory_order_relaxed);
  }
};
} // namespace

// =============================================================================
// ExternalGroup tests
// =============================================================================

using SingleExternalLoop =
  ev_loop::GroupEventLoop<ev_loop::SpinGroup<ExternalEventCounter>, ev_loop::ExternalGroup<PrimaryExternalInputs>>;

// NOLINTBEGIN(readability-function-cognitive-complexity)
TEST_CASE("ExternalGroup", "[external]")
{
  SECTION("external threads can emit events")
  {
    auto loop = SingleExternalLoop::setup().create_unique();
    auto emitter = loop->get_external_emitter<PrimaryExternalInputs>();

    REQUIRE(emitter.emit(EventA{ .value = kTestValue1 }));
    REQUIRE(emitter.emit(EventB{ .value = kTestValue2 }));

    while (loop->poll_external<PrimaryExternalInputs>()) {}
    while (loop->poll_group<0>()) {}

    REQUIRE(loop->get<ExternalEventCounter>().event_a_count.load() == kTestValue1);
    REQUIRE(loop->get<ExternalEventCounter>().event_b_count.load() == kTestValue2);
  }

  SECTION("emitter can be copied and used from multiple threads")
  {
    auto loop = SingleExternalLoop::setup().create_unique();
    auto emitter = loop->get_external_emitter<PrimaryExternalInputs>();

    constexpr int kEventsPerThread = 100;
    constexpr int kNumThreads = 4;

    std::vector<std::thread> threads;
    threads.reserve(kNumThreads);
    for (int i = 0; i < kNumThreads; ++i) {
      threads.emplace_back([emitter_copy = emitter]() {
        for (int j = 0; j < kEventsPerThread; ++j) { emitter_copy.emit(EventA{ .value = 1 }); }
      });
    }

    for (auto& thread : threads) { thread.join(); }

    while (loop->poll_external<PrimaryExternalInputs>()) {}
    while (loop->poll_group<0>()) {}

    REQUIRE(loop->get<ExternalEventCounter>().event_a_count.load() == kEventsPerThread * kNumThreads);
  }

  SECTION("emitter handle stays valid after EventLoop destruction")
  {
    ev_loop::ExternalEmitter<EventA, EventB> emitter;

    {
      auto loop = SingleExternalLoop::setup().create_unique();
      emitter = loop->get_external_emitter<PrimaryExternalInputs>();

      REQUIRE(emitter.emit(EventA{ .value = kTestValue1 }));
      REQUIRE(static_cast<bool>(emitter));
    }

    // Emitter is still valid after loop destruction
    REQUIRE(static_cast<bool>(emitter));
    REQUIRE(emitter.emit(EventA{ .value = kTestValue2 }));
  }

  SECTION("poll_external returns false when empty")
  {
    auto loop = SingleExternalLoop::setup().create_unique();

    // Poll empty queue should return false
    REQUIRE_FALSE(loop->poll_external<PrimaryExternalInputs>());

    // Emit one event
    auto emitter = loop->get_external_emitter<PrimaryExternalInputs>();
    REQUIRE(emitter.emit(EventA{ .value = kTestValue1 }));

    // First poll should return true (event processed)
    REQUIRE(loop->poll_external<PrimaryExternalInputs>());

    // Second poll should return false (queue empty again)
    REQUIRE_FALSE(loop->poll_external<PrimaryExternalInputs>());
  }
}
// NOLINTEND(readability-function-cognitive-complexity)

// =============================================================================
// ExternalEmitter tests
// =============================================================================

// NOLINTBEGIN(readability-function-cognitive-complexity)
TEST_CASE("ExternalEmitter", "[external]")
{
  SECTION("default constructed emitter returns false on emit")
  {
    const ev_loop::ExternalEmitter<EventA, EventB> emitter;

    REQUIRE_FALSE(static_cast<bool>(emitter));
    REQUIRE_FALSE(emitter.emit(EventA{ .value = kTestValue1 }));
    REQUIRE_FALSE(emitter.emit(EventB{ .value = kTestValue2 }));
  }

  SECTION("emit returns false when queue is full")
  {
    using Loop =
      ev_loop::GroupEventLoop<ev_loop::SpinGroup<ExternalEventCounter>, ev_loop::ExternalGroup<PrimaryExternalInputs>>;

    auto loop = Loop::setup().create_unique();
    auto emitter = loop->get_external_emitter<PrimaryExternalInputs>();

    // Fill the queue (default capacity is 4096)
    constexpr int kQueueCapacity = 4096;
    for (int i = 0; i < kQueueCapacity; ++i) { REQUIRE(emitter.emit(EventA{ .value = i })); }

    // Queue is now full, emit should return false
    REQUIRE_FALSE(emitter.emit(EventA{ .value = kQueueCapacity }));

    // After draining some events, emit should work again
    loop->poll_all_external();
    while (loop->poll_group<0>()) {}

    REQUIRE(emitter.emit(EventA{ .value = kQueueCapacity + 1 }));
  }
}
// NOLINTEND(readability-function-cognitive-complexity)

// =============================================================================
// Multiple ExternalGroups tests
// =============================================================================

namespace {
// External input definitions for multi-ExternalGroup tests
struct FirstExternalInputs
{
  using emits = ev_loop::type_list<EventA, EventB>;
};

struct SecondExternalInputs
{
  using emits = ev_loop::type_list<EventC>;
};

// Receiver for multi-external tests
struct MultiExternalReceiver
{
  using receives = ev_loop::type_list<EventA, EventB, EventC>;
  using emits = ev_loop::type_list<>;

  std::atomic<int> event_a_count{ 0 };
  std::atomic<int> event_b_count{ 0 };
  std::atomic<int> event_c_count{ 0 };

  template<typename Dispatcher> void on_event(EventA event, Dispatcher& /*unused*/)
  {
    event_a_count.fetch_add(event.value, std::memory_order_relaxed);
  }

  template<typename Dispatcher> void on_event(EventB event, Dispatcher& /*unused*/)
  {
    event_b_count.fetch_add(event.value, std::memory_order_relaxed);
  }

  template<typename Dispatcher> void on_event(EventC event, Dispatcher& /*unused*/)
  {
    event_c_count.fetch_add(event.value, std::memory_order_relaxed);
  }
};

using MultiExternalLoop = ev_loop::GroupEventLoop<ev_loop::SpinGroup<MultiExternalReceiver>,
  ev_loop::ExternalGroup<FirstExternalInputs>,
  ev_loop::ExternalGroup<SecondExternalInputs>>;
} // namespace

// NOLINTBEGIN(readability-function-cognitive-complexity)
TEST_CASE("Multiple ExternalGroups", "[external]")
{
  auto loop = MultiExternalLoop::setup().create_unique();
  auto emitter1 = loop->get_external_emitter<FirstExternalInputs>();
  auto emitter2 = loop->get_external_emitter<SecondExternalInputs>();

  SECTION("type-based getter")
  {
    REQUIRE(emitter1.emit(EventA{ .value = kTestValue1 }));
    REQUIRE(emitter1.emit(EventB{ .value = kTestValue2 }));
    REQUIRE(emitter2.emit(EventC{ .value = kTestValue3 }));

    while (loop->poll_all_external()) {}
    while (loop->poll_group<0>()) {}

    REQUIRE(loop->get<MultiExternalReceiver>().event_a_count.load() == kTestValue1);
    REQUIRE(loop->get<MultiExternalReceiver>().event_b_count.load() == kTestValue2);
    REQUIRE(loop->get<MultiExternalReceiver>().event_c_count.load() == kTestValue3);
  }

  SECTION("poll by type")
  {
    REQUIRE(emitter1.emit(EventA{ .value = kTestValue1 }));
    REQUIRE(emitter2.emit(EventC{ .value = kTestValue3 }));

    // Poll only the first ExternalGroup
    while (loop->poll_external<FirstExternalInputs>()) {}
    while (loop->poll_group<0>()) {}

    REQUIRE(loop->get<MultiExternalReceiver>().event_a_count.load() == kTestValue1);
    REQUIRE(loop->get<MultiExternalReceiver>().event_c_count.load() == 0);

    // Now poll the second ExternalGroup
    while (loop->poll_external<SecondExternalInputs>()) {}
    while (loop->poll_group<0>()) {}

    REQUIRE(loop->get<MultiExternalReceiver>().event_c_count.load() == kTestValue3);
  }

  SECTION("concurrent threads per group")
  {
    constexpr int kEventsPerThread = 100;

    std::thread thread1([emitter1]() {
      for (int i = 0; i < kEventsPerThread; ++i) { emitter1.emit(EventA{ .value = 1 }); }
    });

    std::thread thread2([emitter2]() {
      for (int i = 0; i < kEventsPerThread; ++i) { emitter2.emit(EventC{ .value = 1 }); }
    });

    thread1.join();
    thread2.join();

    while (loop->poll_all_external()) {}
    while (loop->poll_group<0>()) {}

    REQUIRE(loop->get<MultiExternalReceiver>().event_a_count.load() == kEventsPerThread);
    REQUIRE(loop->get<MultiExternalReceiver>().event_c_count.load() == kEventsPerThread);
  }
}
// NOLINTEND(readability-function-cognitive-complexity)
