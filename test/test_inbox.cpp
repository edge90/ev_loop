// NOLINTBEGIN(misc-include-cleaner)
#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <ev_loop/ev.hpp>
#include <string>
#include <thread>
#include <utility>
#include <vector>
// NOLINTEND(misc-include-cleaner)

namespace {
constexpr int kTestValue1 = 10;
constexpr int kTestValue2 = 20;
} // namespace

// =============================================================================
// SpscInbox tests
// =============================================================================

// NOLINTBEGIN(readability-function-cognitive-complexity)
TEST_CASE("SpscInbox", "[inbox]")
{
  ev_loop::detail::SpscInbox<int> inbox;

  SECTION("starts empty") { REQUIRE(inbox.empty()); }

  SECTION("push and pop")
  {
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

  SECTION("drain")
  {
    inbox.push(1);
    inbox.push(2);
    inbox.push(3);

    int sum = 0;
    const auto count = inbox.drain([&sum](int val) { sum += val; });
    REQUIRE(count == 3);
    REQUIRE(sum == 6);
    REQUIRE(inbox.empty());
  }

  SECTION("push returns false when full")
  {
    ev_loop::detail::SpscInbox<int, 4> small_inbox;
    REQUIRE(small_inbox.push(1));
    REQUIRE(small_inbox.push(2));
    REQUIRE(small_inbox.push(3));
    REQUIRE(small_inbox.push(4));
    REQUIRE_FALSE(small_inbox.push(5));

    int value = 0;
    REQUIRE(small_inbox.try_pop(value));
    REQUIRE(small_inbox.push(5));
  }

  SECTION("try_pop returns false when empty")
  {
    int value = 0;
    REQUIRE_FALSE(inbox.try_pop(value));
  }
}

// NOLINTEND(readability-function-cognitive-complexity)

// =============================================================================
// MpscInbox tests
// =============================================================================

// NOLINTBEGIN(readability-function-cognitive-complexity)
TEST_CASE("MpscInbox", "[inbox]")
{
  ev_loop::detail::MpscInbox<int> inbox;

  SECTION("starts empty") { REQUIRE(inbox.empty()); }

  SECTION("push and pop")
  {
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

  SECTION("drain")
  {
    inbox.push(1);
    inbox.push(2);
    inbox.push(3);

    int sum = 0;
    const auto count = inbox.drain([&sum](int val) { sum += val; });
    REQUIRE(count == 3);
    REQUIRE(sum == 6);
    REQUIRE(inbox.empty());
  }
}

// NOLINTEND(readability-function-cognitive-complexity)

TEST_CASE("MpscInbox concurrent producers", "[inbox]")
{
  constexpr int kItemsPerThread = 1000;
  constexpr int kNumThreads = 4;

  ev_loop::detail::MpscInbox<int, 8192> inbox;
  std::atomic<int> items_pushed{ 0 };
  std::atomic<bool> producers_can_start{ false };

  // Launch multiple producer threads (they wait for signal)
  std::vector<std::thread> producers;
  producers.reserve(kNumThreads);
  for (int thread_id = 0; thread_id < kNumThreads; ++thread_id) {
    producers.emplace_back([&inbox, &items_pushed, &producers_can_start, thread_id] {
      while (!producers_can_start.load(std::memory_order_acquire)) { std::this_thread::yield(); }
      for (int idx = 0; idx < kItemsPerThread; ++idx) {
        while (!inbox.push((thread_id * kItemsPerThread) + idx)) { std::this_thread::yield(); }
        items_pushed.fetch_add(1, std::memory_order_relaxed);
      }
    });
  }

  // Consumer on main thread - first try to pop from empty queue to cover yield path
  int items_popped = 0;
  const int total_items = kNumThreads * kItemsPerThread;
  int value = 0;
  if (!inbox.try_pop(value)) { std::this_thread::yield(); } // Guaranteed to hit: queue is empty

  // Now let producers start
  producers_can_start.store(true, std::memory_order_release);

  while (items_popped < total_items) {
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

// =============================================================================
// ExternalInbox tests
// =============================================================================

namespace {
struct EventA
{
  int value;
};

struct EventB
{
  int value;
};
} // namespace

// NOLINTBEGIN(readability-function-cognitive-complexity)
TEST_CASE("ExternalInbox", "[inbox]")
{
  SECTION("push and try_pop")
  {
    ev_loop::ExternalInbox<EventA, EventB> inbox;

    REQUIRE(inbox.empty<EventA>());
    REQUIRE(inbox.empty<EventB>());

    REQUIRE(inbox.push(EventA{ .value = kTestValue1 }));
    REQUIRE(inbox.push(EventB{ .value = kTestValue2 }));

    REQUIRE_FALSE(inbox.empty<EventA>());
    REQUIRE_FALSE(inbox.empty<EventB>());

    EventA event_a{};
    EventB event_b{};

    REQUIRE(inbox.try_pop(event_a));
    REQUIRE(event_a.value == kTestValue1);

    REQUIRE(inbox.try_pop(event_b));
    REQUIRE(event_b.value == kTestValue2);

    REQUIRE(inbox.empty<EventA>());
    REQUIRE(inbox.empty<EventB>());
  }

  SECTION("push rvalue with non-trivial type")
  {
    struct StringEvent
    {
      std::string data;
    };
    ev_loop::ExternalInbox<StringEvent> inbox;

    StringEvent event{ .data = "test_data" };
    // NOLINTNEXTLINE(bugprone-use-after-move,hicpp-invalid-access-moved)
    REQUIRE(inbox.push(std::move(event)));

    StringEvent popped{};
    REQUIRE(inbox.try_pop(popped));
    REQUIRE(popped.data == "test_data");
  }

  SECTION("concurrent producers")
  {
    ev_loop::ExternalInbox<EventA> inbox;

    constexpr int kEventsPerThread = 500;
    constexpr int kNumThreads = 4;

    std::vector<std::thread> producers;
    producers.reserve(kNumThreads);

    for (int i = 0; i < kNumThreads; ++i) {
      producers.emplace_back([&inbox]() {
        for (int j = 0; j < kEventsPerThread; ++j) { inbox.push(EventA{ .value = 1 }); }
      });
    }

    for (auto& producer : producers) { producer.join(); }

    int count = 0;
    EventA event{};
    while (inbox.try_pop(event)) { count += event.value; }

    REQUIRE(count == kEventsPerThread * kNumThreads);
  }
}
// NOLINTEND(readability-function-cognitive-complexity)
