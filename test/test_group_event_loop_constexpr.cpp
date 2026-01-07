// NOLINTBEGIN(misc-include-cleaner)
#include <catch2/catch_test_macros.hpp>
#include <ev_loop/ev.hpp>
#include <type_traits>
// NOLINTEND(misc-include-cleaner)

namespace {

// =============================================================================
// Minimal types for constexpr tests
// =============================================================================

struct EventA
{
};

struct EventB
{
};

struct ReceiverA
{
  using receives = ev_loop::type_list<EventA>;
  using emits = ev_loop::type_list<EventB>;
  template<typename D> static void on_event(EventA /*unused*/, D& /*unused*/) {}
};

struct ReceiverB
{
  using receives = ev_loop::type_list<EventB>;
  using emits = ev_loop::type_list<>;
  template<typename D> static void on_event(EventB /*unused*/, D& /*unused*/) {}
};

// =============================================================================
// ThreadGroup type trait tests
// =============================================================================

// NOLINTBEGIN(readability-function-cognitive-complexity)
TEST_CASE("ThreadGroup type traits", "[group_event_loop][constexpr]")
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
// NOLINTEND(readability-function-cognitive-complexity)

// =============================================================================
// GroupEventLoop group_count tests
// =============================================================================

TEST_CASE("GroupEventLoop group_count", "[group_event_loop][constexpr]")
{
  SECTION("single group")
  {
    using Loop = ev_loop::GroupEventLoop<ev_loop::SpinGroup<ReceiverA>>;
    STATIC_REQUIRE(Loop::group_count == 1);
  }

  SECTION("multiple groups")
  {
    using Loop = ev_loop::GroupEventLoop<ev_loop::SpinGroup<ReceiverA>, ev_loop::WaitGroup<ReceiverB>>;
    STATIC_REQUIRE(Loop::group_count == 2);
  }
}

// =============================================================================
// Group event routing trait tests
// =============================================================================

TEST_CASE("Group handles event trait", "[group_event_loop][constexpr]")
{
  using GroupA = ev_loop::SpinGroup<ReceiverA>;
  using GroupB = ev_loop::WaitGroup<ReceiverB>;

  STATIC_REQUIRE(ev_loop::detail::group_handles_event_v<GroupA, EventA>);
  STATIC_REQUIRE_FALSE(ev_loop::detail::group_handles_event_v<GroupA, EventB>);

  STATIC_REQUIRE(ev_loop::detail::group_handles_event_v<GroupB, EventB>);
  STATIC_REQUIRE_FALSE(ev_loop::detail::group_handles_event_v<GroupB, EventA>);
}

TEST_CASE("Group all events trait", "[group_event_loop][constexpr]")
{
  using GroupA = ev_loop::SpinGroup<ReceiverA>;
  using Events = ev_loop::detail::group_all_events_t<GroupA>;
  STATIC_REQUIRE(ev_loop::detail::contains_v<Events, EventA>);
}

} // namespace
