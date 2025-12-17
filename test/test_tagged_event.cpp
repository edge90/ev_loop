// NOLINTBEGIN(misc-include-cleaner)
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <ev_loop/ev.hpp>
#include <limits>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>

#include "test_utils.hpp"
// NOLINTEND(misc-include-cleaner)

struct TrivialEvent
{
  int x_coord;
  int y_coord;
};

struct HeapEvent
{
  std::unique_ptr<int> ptr;

  HeapEvent() = default;
  explicit HeapEvent(int val) : ptr(std::make_unique<int>(val)) {}
  HeapEvent(const HeapEvent&) = delete;
  HeapEvent& operator=(const HeapEvent&) = delete;
  HeapEvent(HeapEvent&&) = default;
  HeapEvent& operator=(HeapEvent&&) = default;
  ~HeapEvent() = default;

  [[nodiscard]] int value() const { return ptr ? *ptr : -1; }
};

// =============================================================================
// Tests
// =============================================================================

TEST_CASE("TaggedEvent", "[tagged_event]")
{
  constexpr int kTrivialX = 10;
  constexpr int kTrivialY = 20;
  constexpr int kTestInt = 42;
  constexpr int kTestInt2 = 123;
  constexpr double kTestDouble = 3.14;
  constexpr double kTestDoubleLow = 3.13;
  constexpr double kTestDoubleHigh = 3.15;
  constexpr std::uint8_t kUninitializedTag = 255;

  SECTION("trivial store and get")
  {
    ev_loop::detail::TaggedEvent<TrivialEvent, int> tagged_event;
    tagged_event.store(TrivialEvent{ .x_coord = kTrivialX, .y_coord = kTrivialY });
    REQUIRE(tagged_event.index() == 0U);
    REQUIRE(tagged_event.get<0>().x_coord == kTrivialX);
    REQUIRE(tagged_event.get<0>().y_coord == kTrivialY);
  }

  SECTION("multiple types")
  {
    ev_loop::detail::TaggedEvent<TrivialEvent, int, double> tagged_event;

    tagged_event.store(TrivialEvent{ .x_coord = 1, .y_coord = 2 });
    REQUIRE(tagged_event.index() == 0U);

    tagged_event.store(kTestInt);
    REQUIRE(tagged_event.index() == 1U);
    REQUIRE(tagged_event.get<1>() == kTestInt);

    tagged_event.store(kTestDouble);
    REQUIRE(tagged_event.index() == 2U);
    REQUIRE(tagged_event.get<2>() > kTestDoubleLow);
    REQUIRE(tagged_event.get<2>() < kTestDoubleHigh);
  }

  SECTION("string store and destroy")
  {
    auto counter = std::make_shared<TrackingCounter>();
    {
      ev_loop::detail::TaggedEvent<TrackedString, int> tagged_event;
      tagged_event.store(TrackedString{ counter, "hello" });
      REQUIRE(tagged_event.index() == 0U);
      REQUIRE(tagged_event.get<0>().value == "hello");
    }
    REQUIRE(counter->balanced());
  }

  SECTION("string overwrite")
  {
    auto counter = std::make_shared<TrackingCounter>();
    {
      ev_loop::detail::TaggedEvent<TrackedString, int> tagged_event;
      tagged_event.store(TrackedString{ counter, "first" });
      REQUIRE(tagged_event.get<0>().value == "first");

      tagged_event.store(TrackedString{ counter, "second" });
      REQUIRE(tagged_event.get<0>().value == "second");

      tagged_event.store(kTestInt2);
      REQUIRE(tagged_event.index() == 1U);
      REQUIRE(tagged_event.get<1>() == kTestInt2);
    }
    REQUIRE(counter->balanced());
  }

  SECTION("move constructor")
  {
    auto counter = std::make_shared<TrackingCounter>();
    {
      ev_loop::detail::TaggedEvent<TrackedString, int> tagged_event1;
      tagged_event1.store(TrackedString{ counter, "moveme" });

      const int before_construct = counter->constructed_count.load();
      const int before_destruct = counter->destructed_count.load();

      ev_loop::detail::TaggedEvent<TrackedString, int> tagged_event2(std::move(tagged_event1));
      REQUIRE(tagged_event2.get<0>().value == "moveme");
      REQUIRE(tagged_event2.index() == 0U);
      // intentionally testing post-move state
      // cppcheck-suppress accessMoved
      // NOLINTNEXTLINE(bugprone-use-after-move,hicpp-invalid-access-moved,clang-analyzer-cplusplus.Move)
      REQUIRE(tagged_event1.index() == kUninitializedTag);

      REQUIRE(counter->constructed_count.load() == before_construct + 1);
      REQUIRE(counter->destructed_count.load() == before_destruct + 1);
    }
    REQUIRE(counter->balanced());
  }

  SECTION("move assignment")
  {
    auto counter = std::make_shared<TrackingCounter>();
    {
      ev_loop::detail::TaggedEvent<TrackedString, int> tagged_event1;
      tagged_event1.store(TrackedString{ counter, "source" });

      ev_loop::detail::TaggedEvent<TrackedString, int> tagged_event2;
      tagged_event2.store(TrackedString{ counter, "dest" });

      const int before_destruct = counter->destructed_count.load();

      tagged_event2 = std::move(tagged_event1);
      REQUIRE(tagged_event2.get<0>().value == "source");
      // intentionally testing post-move state
      // cppcheck-suppress accessMoved
      // NOLINTNEXTLINE(bugprone-use-after-move,hicpp-invalid-access-moved,clang-analyzer-cplusplus.Move)
      REQUIRE(tagged_event1.index() == kUninitializedTag);

      REQUIRE(counter->destructed_count.load() > before_destruct);
    }
    REQUIRE(counter->balanced());
  }

  SECTION("copy constructor")
  {
    auto counter = std::make_shared<TrackingCounter>();
    {
      ev_loop::detail::TaggedEvent<TrackedString, int> tagged_event1;
      tagged_event1.store(TrackedString{ counter, "copyme" });

      ev_loop::detail::TaggedEvent<TrackedString, int> tagged_event2(tagged_event1);
      REQUIRE(tagged_event2.get<0>().value == "copyme");
      REQUIRE(tagged_event1.get<0>().value == "copyme");
    }
    REQUIRE(counter->balanced());
    REQUIRE(counter->copy_count.load() > 0);
  }

  SECTION("copy assignment")
  {
    auto counter = std::make_shared<TrackingCounter>();
    {
      ev_loop::detail::TaggedEvent<TrackedString, int> tagged_event1;
      tagged_event1.store(TrackedString{ counter, "source" });

      ev_loop::detail::TaggedEvent<TrackedString, int> tagged_event2;
      tagged_event2.store(TrackedString{ counter, "dest" });

      tagged_event2 = tagged_event1;
      REQUIRE(tagged_event2.get<0>().value == "source");
      REQUIRE(tagged_event1.get<0>().value == "source");
    }
    REQUIRE(counter->balanced());
  }

  SECTION("self assignment")
  {
    auto counter = std::make_shared<TrackingCounter>();
    {
      ev_loop::detail::TaggedEvent<TrackedString, int> tagged_event;
      tagged_event.store(TrackedString{ counter, "selftest" });

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wself-assign-overloaded"
#endif
      tagged_event = tagged_event;
#ifdef __clang__
#pragma clang diagnostic pop
#endif
      REQUIRE(tagged_event.get<0>().value == "selftest");

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wself-move"
#elifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wself-move"
#endif
      // MSVC does not warn on self-move
      tagged_event = std::move(tagged_event);
#ifdef __clang__
#pragma clang diagnostic pop
#elifdef __GNUC__
#pragma GCC diagnostic pop
#endif
      REQUIRE(tagged_event.get<0>().value == "selftest");
    }
    REQUIRE(counter->balanced());
  }

  SECTION("move only type")
  {
    ev_loop::detail::TaggedEvent<HeapEvent, int> tagged_event;
    tagged_event.store(HeapEvent{ kTestInt });
    REQUIRE(tagged_event.get<0>().value() == kTestInt);

    ev_loop::detail::TaggedEvent<HeapEvent, int> tagged_event2(std::move(tagged_event));
    REQUIRE(tagged_event2.get<0>().value() == kTestInt);
  }

  SECTION("move assignment with move only type")
  {
    ev_loop::detail::TaggedEvent<HeapEvent, int> tagged_event1;
    tagged_event1.store(HeapEvent{ kTestInt });

    ev_loop::detail::TaggedEvent<HeapEvent, int> tagged_event2;
    tagged_event2.store(HeapEvent{ kTestInt2 });

    tagged_event2 = std::move(tagged_event1);
    REQUIRE(tagged_event2.get<0>().value() == kTestInt);
    // cppcheck-suppress accessMoved
    // NOLINTNEXTLINE(bugprone-use-after-move,hicpp-invalid-access-moved,clang-analyzer-cplusplus.Move)
    REQUIRE(tagged_event1.index() == kUninitializedTag);
  }

  SECTION("move assignment to uninitialized")
  {
    auto counter = std::make_shared<TrackingCounter>();
    {
      ev_loop::detail::TaggedEvent<TrackedString, int> tagged_event1;
      tagged_event1.store(TrackedString{ counter, "source" });

      ev_loop::detail::TaggedEvent<TrackedString, int> tagged_event2; // uninitialized

      tagged_event2 = std::move(tagged_event1);
      REQUIRE(tagged_event2.get<0>().value == "source");
      // cppcheck-suppress accessMoved
      // NOLINTNEXTLINE(bugprone-use-after-move,hicpp-invalid-access-moved,clang-analyzer-cplusplus.Move)
      REQUIRE(tagged_event1.index() == kUninitializedTag);
    }
    REQUIRE(counter->balanced());
  }

  SECTION("move assignment from uninitialized")
  {
    auto counter = std::make_shared<TrackingCounter>();
    {
      ev_loop::detail::TaggedEvent<TrackedString, int> tagged_event1; // uninitialized

      ev_loop::detail::TaggedEvent<TrackedString, int> tagged_event2;
      tagged_event2.store(TrackedString{ counter, "dest" });

      const int before_destruct = counter->destructed_count.load();
      tagged_event2 = std::move(tagged_event1);
      REQUIRE(tagged_event2.index() == kUninitializedTag);
      REQUIRE(counter->destructed_count.load() > before_destruct); // dest was destroyed
    }
    REQUIRE(counter->balanced());
  }

  SECTION("get returns correct reference types")
  {
    using Tagged = ev_loop::detail::TaggedEvent<std::string, int>;

    // Non-const lvalue -> T&
    Tagged tagged_event;
    tagged_event.store(std::string{ "test" });
    static_assert(std::is_same_v<decltype(tagged_event.get<0>()), std::string&>);

    // Const lvalue -> const T&
    const Tagged& const_ref = tagged_event;
    static_assert(std::is_same_v<decltype(const_ref.get<0>()), const std::string&>);

    // Verify no copies when accessing via reference
    auto counter = std::make_shared<TrackingCounter>();
    {
      ev_loop::detail::TaggedEvent<TrackedString, int> tracked;
      tracked.store(TrackedString{ counter, "reftest" });
      const int constructs_before = counter->constructed_count.load();

      // Getting reference should not copy or move
      const TrackedString& ref = tracked.get<0>();
      REQUIRE(ref.value == "reftest");
      REQUIRE(counter->constructed_count.load() == constructs_before);
      REQUIRE(counter->copy_count.load() == 0);
    }
  }

  SECTION("empty after move")
  {
    auto counter = std::make_shared<TrackingCounter>();
    ev_loop::detail::TaggedEvent<TrackedString, int> tagged_event1;
    tagged_event1.store(TrackedString{ counter, "test" });

    const ev_loop::detail::TaggedEvent<TrackedString, int> tagged_event2(std::move(tagged_event1));

    // After move, index should be max value for the tag type (uninitialized)
    // cppcheck-suppress accessMoved
    // NOLINTNEXTLINE(bugprone-use-after-move,hicpp-invalid-access-moved,clang-analyzer-cplusplus.Move)
    REQUIRE(tagged_event1.index() == std::numeric_limits<ev_loop::detail::tag_type_t<2>>::max());
  }
}
