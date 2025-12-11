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
  HeapEvent(const HeapEvent &) = delete;
  HeapEvent &operator=(const HeapEvent &) = delete;
  HeapEvent(HeapEvent &&) = default;
  HeapEvent &operator=(HeapEvent &&) = default;
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
    ev_loop::TaggedEvent<TrivialEvent, int> tagged_event;
    tagged_event.store(TrivialEvent{ .x_coord = kTrivialX, .y_coord = kTrivialY });
    REQUIRE(tagged_event.index() == 0U);
    REQUIRE(tagged_event.get<0>().x_coord == kTrivialX);
    REQUIRE(tagged_event.get<0>().y_coord == kTrivialY);
  }

  SECTION("multiple types")
  {
    ev_loop::TaggedEvent<TrivialEvent, int, double> tagged_event;

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
    reset_tracking();
    {
      ev_loop::TaggedEvent<TrackedString, int> tagged_event;
      tagged_event.store(TrackedString{ "hello" });
      REQUIRE(tagged_event.index() == 0U);
      REQUIRE(tagged_event.get<0>().value == "hello");
    }
    REQUIRE(constructed_count == destructed_count);
  }

  SECTION("string overwrite")
  {
    reset_tracking();
    {
      ev_loop::TaggedEvent<TrackedString, int> tagged_event;
      tagged_event.store(TrackedString{ "first" });
      REQUIRE(tagged_event.get<0>().value == "first");

      tagged_event.store(TrackedString{ "second" });
      REQUIRE(tagged_event.get<0>().value == "second");

      tagged_event.store(kTestInt2);
      REQUIRE(tagged_event.index() == 1U);
      REQUIRE(tagged_event.get<1>() == kTestInt2);
    }
    REQUIRE(constructed_count == destructed_count);
  }

  SECTION("move constructor")
  {
    reset_tracking();
    {
      ev_loop::TaggedEvent<TrackedString, int> tagged_event1;
      tagged_event1.store(TrackedString{ "moveme" });

      const int before_construct = constructed_count;
      const int before_destruct = destructed_count;

      ev_loop::TaggedEvent<TrackedString, int> tagged_event2(std::move(tagged_event1));
      REQUIRE(tagged_event2.get<0>().value == "moveme");
      REQUIRE(tagged_event2.index() == 0U);
      // NOLINTNEXTLINE(bugprone-use-after-move,hicpp-invalid-access-moved,clang-analyzer-cplusplus.Move)
      REQUIRE(tagged_event1.index() == kUninitializedTag);// intentionally testing post-move state

      REQUIRE(constructed_count == before_construct + 1);
      REQUIRE(destructed_count == before_destruct + 1);
    }
    REQUIRE(constructed_count == destructed_count);
  }

  SECTION("move assignment")
  {
    reset_tracking();
    {
      ev_loop::TaggedEvent<TrackedString, int> tagged_event1;
      tagged_event1.store(TrackedString{ "source" });

      ev_loop::TaggedEvent<TrackedString, int> tagged_event2;
      tagged_event2.store(TrackedString{ "dest" });

      const int before_destruct = destructed_count;

      tagged_event2 = std::move(tagged_event1);
      REQUIRE(tagged_event2.get<0>().value == "source");
      // NOLINTNEXTLINE(bugprone-use-after-move,hicpp-invalid-access-moved,clang-analyzer-cplusplus.Move)
      REQUIRE(tagged_event1.index() == kUninitializedTag);// intentionally testing post-move state

      REQUIRE(destructed_count > before_destruct);
    }
    REQUIRE(constructed_count == destructed_count);
  }

  SECTION("copy constructor")
  {
    reset_tracking();
    {
      ev_loop::TaggedEvent<TrackedString, int> tagged_event1;
      tagged_event1.store(TrackedString{ "copyme" });

      ev_loop::TaggedEvent<TrackedString, int> tagged_event2(tagged_event1);
      REQUIRE(tagged_event2.get<0>().value == "copyme");
      REQUIRE(tagged_event1.get<0>().value == "copyme");
    }
    REQUIRE(constructed_count == destructed_count);
    REQUIRE(copy_count > 0);
  }

  SECTION("copy constructor with second type")
  {
    ev_loop::TaggedEvent<int, TrackedString> tagged_event1;
    tagged_event1.store(TrackedString{ "second" });
    REQUIRE(tagged_event1.index() == 1U);

    const ev_loop::TaggedEvent<int, TrackedString> tagged_event2(tagged_event1);
    REQUIRE(tagged_event2.index() == 1U);
    REQUIRE(tagged_event2.get<1>().value == "second");
  }

  SECTION("move constructor with second type")
  {
    ev_loop::TaggedEvent<int, TrackedString> tagged_event1;
    tagged_event1.store(TrackedString{ "moveme" });
    REQUIRE(tagged_event1.index() == 1U);

    const ev_loop::TaggedEvent<int, TrackedString> tagged_event2(std::move(tagged_event1));
    REQUIRE(tagged_event2.index() == 1U);
    REQUIRE(tagged_event2.get<1>().value == "moveme");
  }

  SECTION("destroy with second type")
  {
    reset_tracking();
    {
      ev_loop::TaggedEvent<int, TrackedString> tagged_event;
      tagged_event.store(TrackedString{ "destroy" });
      REQUIRE(tagged_event.index() == 1U);
    }
    REQUIRE(constructed_count == destructed_count);
  }

  SECTION("copy assignment")
  {
    reset_tracking();
    {
      ev_loop::TaggedEvent<TrackedString, int> tagged_event1;
      tagged_event1.store(TrackedString{ "source" });

      ev_loop::TaggedEvent<TrackedString, int> tagged_event2;
      tagged_event2.store(TrackedString{ "dest" });

      tagged_event2 = tagged_event1;
      REQUIRE(tagged_event2.get<0>().value == "source");
      REQUIRE(tagged_event1.get<0>().value == "source");
    }
    REQUIRE(constructed_count == destructed_count);
  }

  SECTION("self assignment")
  {
    reset_tracking();
    {
      ev_loop::TaggedEvent<TrackedString, int> tagged_event;
      tagged_event.store(TrackedString{ "selftest" });

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wself-assign-overloaded"
#endif
      tagged_event = tagged_event;
#ifdef __clang__
#pragma clang diagnostic pop
#endif
      REQUIRE(tagged_event.get<0>().value == "selftest");

// NOLINTBEGIN(readability-use-concise-preprocessor-directives) - #elifdef not portable
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wself-move"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wself-move"
#endif
      // MSVC does not warn on self-move
      tagged_event = std::move(tagged_event);
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
      // NOLINTEND(readability-use-concise-preprocessor-directives)
      REQUIRE(tagged_event.get<0>().value == "selftest");
    }
    REQUIRE(constructed_count == destructed_count);
  }

  SECTION("move only type")
  {
    ev_loop::TaggedEvent<HeapEvent, int> tagged_event;
    tagged_event.store(HeapEvent{ kTestInt });
    REQUIRE(tagged_event.get<0>().value() == kTestInt);

    ev_loop::TaggedEvent<HeapEvent, int> tagged_event2(std::move(tagged_event));
    REQUIRE(tagged_event2.get<0>().value() == kTestInt);
  }

  SECTION("get returns correct reference types")
  {
    using Tagged = ev_loop::TaggedEvent<std::string, int>;

    // Non-const lvalue -> T&
    Tagged tagged_event;
    tagged_event.store(std::string{ "test" });
    static_assert(std::is_same_v<decltype(tagged_event.get<0>()), std::string &>);

    // Const lvalue -> const T&
    const Tagged &const_ref = tagged_event;
    static_assert(std::is_same_v<decltype(const_ref.get<0>()), const std::string &>);

    // Verify no copies when accessing via reference
    reset_tracking();
    {
      ev_loop::TaggedEvent<TrackedString, int> tracked;
      tracked.store(TrackedString{ "reftest" });
      const int constructs_before = constructed_count;

      // Getting reference should not copy or move
      const TrackedString &ref = tracked.get<0>();
      REQUIRE(ref.value == "reftest");
      REQUIRE(constructed_count == constructs_before);
      REQUIRE(copy_count == 0);
    }
  }

  SECTION("empty after move")
  {
    reset_tracking();
    ev_loop::TaggedEvent<TrackedString, int> tagged_event1;
    tagged_event1.store(TrackedString{ "test" });

    const ev_loop::TaggedEvent<TrackedString, int> tagged_event2(std::move(tagged_event1));

    // After move, index should be max value for the tag type (uninitialized)
    // NOLINTNEXTLINE(bugprone-use-after-move,hicpp-invalid-access-moved,clang-analyzer-cplusplus.Move)
    REQUIRE(tagged_event1.index() == std::numeric_limits<ev_loop::tag_type_t<2>>::max());
  }
}

// =============================================================================
// Tag type selector tests
// =============================================================================

TEST_CASE("tag_type_selector", "[tagged_event]")
{
  constexpr std::size_t kTagTypeCount100 = 100;
  constexpr std::size_t kTagTypeCount254 = 254;
  constexpr std::size_t kTagTypeCount255 = 255;
  constexpr std::size_t kTagTypeCount256 = 256;
  constexpr std::size_t kTagTypeCount1000 = 1000;
  constexpr std::size_t kTagTypeCount65534 = 65534;
  constexpr std::size_t kTagTypeCount65535 = 65535;
  constexpr std::size_t kTagTypeCount100000 = 100000;
  constexpr std::size_t kTagTypeSizeSmall = 1;
  constexpr std::size_t kTagTypeSizeMedium = 2;
  constexpr std::size_t kTagTypeSizeLarge = 4;
  constexpr std::uint8_t kUninitializedTag = 255;

  SECTION("static assertions")
  {
    // Static assertions to verify tag_type_selector at compile time
    static_assert(std::is_same_v<ev_loop::tag_type_t<0>, std::uint8_t>);
    static_assert(std::is_same_v<ev_loop::tag_type_t<1>, std::uint8_t>);
    static_assert(std::is_same_v<ev_loop::tag_type_t<kTagTypeCount100>, std::uint8_t>);
    static_assert(std::is_same_v<ev_loop::tag_type_t<kTagTypeCount254>, std::uint8_t>);
    // 255 events need uint16_t since max(uint8_t)=255 is reserved for uninitialized
    static_assert(std::is_same_v<ev_loop::tag_type_t<kTagTypeCount255>, std::uint16_t>);
    static_assert(std::is_same_v<ev_loop::tag_type_t<kTagTypeCount256>, std::uint16_t>);
    static_assert(std::is_same_v<ev_loop::tag_type_t<kTagTypeCount1000>, std::uint16_t>);
    static_assert(std::is_same_v<ev_loop::tag_type_t<kTagTypeCount65534>, std::uint16_t>);
    // 65535 events need uint32_t
    static_assert(std::is_same_v<ev_loop::tag_type_t<kTagTypeCount65535>, std::uint32_t>);
    static_assert(std::is_same_v<ev_loop::tag_type_t<kTagTypeCount100000>, std::uint32_t>);
  }

  SECTION("uninitialized_tag is max value for selected type")
  {
    // For small event counts, tag is uint8_t and uninitialized is 255
    using SmallTagged = ev_loop::TaggedEvent<int, double>;
    const SmallTagged small_tagged{};
    REQUIRE(small_tagged.index() == kUninitializedTag);

    // Verify the tag type sizes are what we expect
    REQUIRE(sizeof(ev_loop::tag_type_t<kTagTypeCount100>) == kTagTypeSizeSmall);// uint8_t
    REQUIRE(sizeof(ev_loop::tag_type_t<kTagTypeCount255>) == kTagTypeSizeMedium);// uint16_t
    REQUIRE(sizeof(ev_loop::tag_type_t<kTagTypeCount65535>) == kTagTypeSizeLarge);// uint32_t
  }
}

// =============================================================================
// Trivial-only type tests (exercises different code paths via if constexpr)
// =============================================================================

TEST_CASE("TaggedEvent trivial types", "[tagged_event]")
{
  using TrivialTagged = ev_loop::TaggedEvent<int, double, char>;
  constexpr int kTestInt = 42;
  constexpr double kTestDouble = 3.14;
  constexpr char kTestChar = 'x';
  constexpr std::uint8_t kUninitializedTag = 255;

  SECTION("default constructor")
  {
    const TrivialTagged tagged{};
    REQUIRE(tagged.index() == kUninitializedTag);
  }

  SECTION("store and get")
  {
    TrivialTagged tagged;
    tagged.store(kTestInt);
    REQUIRE(tagged.index() == 0U);
    REQUIRE(tagged.get<0>() == kTestInt);

    tagged.store(kTestDouble);
    REQUIRE(tagged.index() == 1U);

    tagged.store(kTestChar);
    REQUIRE(tagged.index() == 2U);
    REQUIRE(tagged.get<2>() == kTestChar);
  }

  SECTION("copy constructor")
  {
    TrivialTagged tagged1;
    tagged1.store(kTestInt);

    const TrivialTagged tagged2(tagged1);
    REQUIRE(tagged2.index() == 0U);
    REQUIRE(tagged2.get<0>() == kTestInt);
    REQUIRE(tagged1.get<0>() == kTestInt);
  }

  SECTION("copy assignment")
  {
    TrivialTagged tagged1;
    tagged1.store(kTestInt);

    TrivialTagged tagged2;
    tagged2.store(kTestDouble);

    tagged2 = tagged1;
    REQUIRE(tagged2.index() == 0U);
    REQUIRE(tagged2.get<0>() == kTestInt);
  }

  SECTION("move constructor")
  {
    TrivialTagged tagged1;
    tagged1.store(kTestInt);

    const TrivialTagged tagged2(std::move(tagged1));
    REQUIRE(tagged2.index() == 0U);
    REQUIRE(tagged2.get<0>() == kTestInt);
    // NOLINTNEXTLINE(bugprone-use-after-move,hicpp-invalid-access-moved,clang-analyzer-cplusplus.Move)
    REQUIRE(tagged1.index() == kUninitializedTag);
  }

  SECTION("move assignment")
  {
    TrivialTagged tagged1;
    tagged1.store(kTestInt);

    TrivialTagged tagged2;
    tagged2.store(kTestDouble);

    tagged2 = std::move(tagged1);
    REQUIRE(tagged2.index() == 0U);
    REQUIRE(tagged2.get<0>() == kTestInt);
    // NOLINTNEXTLINE(bugprone-use-after-move,hicpp-invalid-access-moved,clang-analyzer-cplusplus.Move)
    REQUIRE(tagged1.index() == kUninitializedTag);
  }

  SECTION("copy uninitialized")
  {
    const TrivialTagged tagged1{};
    // NOLINTNEXTLINE(performance-unnecessary-copy-initialization) - testing copy constructor
    const TrivialTagged tagged2(tagged1);
    REQUIRE(tagged2.index() == kUninitializedTag);
  }

  SECTION("move uninitialized")
  {
    TrivialTagged tagged1;
    const TrivialTagged tagged2(std::move(tagged1));
    REQUIRE(tagged2.index() == kUninitializedTag);
  }

  SECTION("converting constructor")
  {
    const TrivialTagged tagged(kTestInt);
    REQUIRE(tagged.index() == 0U);
    REQUIRE(tagged.get<0>() == kTestInt);
  }

  SECTION("copy with second type")
  {
    TrivialTagged tagged1;
    tagged1.store(kTestDouble);
    REQUIRE(tagged1.index() == 1U);

    // NOLINTNEXTLINE(performance-unnecessary-copy-initialization) - testing copy constructor
    const TrivialTagged tagged2(tagged1);
    REQUIRE(tagged2.index() == 1U);
  }

  SECTION("copy with third type")
  {
    TrivialTagged tagged1;
    tagged1.store(kTestChar);
    REQUIRE(tagged1.index() == 2U);

    // NOLINTNEXTLINE(performance-unnecessary-copy-initialization) - testing copy constructor
    const TrivialTagged tagged2(tagged1);
    REQUIRE(tagged2.index() == 2U);
    REQUIRE(tagged2.get<2>() == kTestChar);
  }

  SECTION("move with second type")
  {
    TrivialTagged tagged1;
    tagged1.store(kTestDouble);

    const TrivialTagged tagged2(std::move(tagged1));
    REQUIRE(tagged2.index() == 1U);
  }

  SECTION("move with third type")
  {
    TrivialTagged tagged1;
    tagged1.store(kTestChar);

    const TrivialTagged tagged2(std::move(tagged1));
    REQUIRE(tagged2.index() == 2U);
    REQUIRE(tagged2.get<2>() == kTestChar);
  }
}