// NOLINTBEGIN(misc-include-cleaner)
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <ev_loop/ev.hpp>
#include <type_traits>
// NOLINTEND(misc-include-cleaner)

// =============================================================================
// Test types for ExternalGroup constexpr tests
// =============================================================================

namespace {
struct EventA
{
};
struct EventB
{
};
struct EventC
{
};

struct ExternalInputsA
{
  using emits = ev_loop::type_list<EventA>;
};

struct ExternalInputsB
{
  using emits = ev_loop::type_list<EventB, EventC>;
};

struct DummyReceiver
{
  using receives = ev_loop::type_list<EventA>;
  using emits = ev_loop::type_list<>;
  template<typename D> static void on_event(EventA /*unused*/, D& /*unused*/) {}
};

using TestThreadGroup = ev_loop::SpinGroup<DummyReceiver>;
using TestExternalGroupA = ev_loop::ExternalGroup<ExternalInputsA>;
using TestExternalGroupB = ev_loop::ExternalGroup<ExternalInputsB>;
} // namespace

// =============================================================================
// is_external_group_v tests
// =============================================================================

TEST_CASE("is_external_group_v detects ExternalGroup", "[external][constexpr]")
{
  SECTION("ExternalGroup is detected")
  {
    STATIC_REQUIRE(ev_loop::detail::is_external_group_v<TestExternalGroupA>);
    STATIC_REQUIRE(ev_loop::detail::is_external_group_v<TestExternalGroupB>);
  }

  SECTION("non-ExternalGroup types are not detected")
  {
    STATIC_REQUIRE_FALSE(ev_loop::detail::is_external_group_v<TestThreadGroup>);
    STATIC_REQUIRE_FALSE(ev_loop::detail::is_external_group_v<DummyReceiver>);
    STATIC_REQUIRE_FALSE(ev_loop::detail::is_external_group_v<int>);
  }
}

// =============================================================================
// external_group_events tests
// =============================================================================

TEST_CASE("external_group_events extracts emits type", "[external][constexpr]")
{
  using EventsA = ev_loop::detail::external_group_events_t<TestExternalGroupA>;
  using EventsB = ev_loop::detail::external_group_events_t<TestExternalGroupB>;

  STATIC_REQUIRE(std::is_same_v<EventsA, ev_loop::type_list<EventA>>);
  STATIC_REQUIRE(std::is_same_v<EventsB, ev_loop::type_list<EventB, EventC>>);
}

// =============================================================================
// count_external_groups tests
// =============================================================================

TEST_CASE("count_external_groups counts ExternalGroups in pack", "[external][constexpr]")
{
  SECTION("no ExternalGroups")
  {
    constexpr std::size_t count = ev_loop::detail::count_external_groups_v<TestThreadGroup>;
    STATIC_REQUIRE(count == 0);
  }

  SECTION("one ExternalGroup")
  {
    constexpr std::size_t count = ev_loop::detail::count_external_groups_v<TestThreadGroup, TestExternalGroupA>;
    STATIC_REQUIRE(count == 1);
  }

  SECTION("multiple ExternalGroups")
  {
    constexpr std::size_t count =
      ev_loop::detail::count_external_groups_v<TestThreadGroup, TestExternalGroupA, TestExternalGroupB>;
    STATIC_REQUIRE(count == 2);
  }
}

// =============================================================================
// collect_external_groups tests
// =============================================================================

TEST_CASE("collect_external_groups collects ExternalGroups into type_list", "[external][constexpr]")
{
  SECTION("filters out ThreadGroups")
  {
    using Collected =
      ev_loop::detail::collect_external_groups_t<TestThreadGroup, TestExternalGroupA, TestExternalGroupB>;
    STATIC_REQUIRE(std::is_same_v<Collected, ev_loop::type_list<TestExternalGroupA, TestExternalGroupB>>);
  }

  SECTION("empty when no ExternalGroups")
  {
    using Collected = ev_loop::detail::collect_external_groups_t<TestThreadGroup>;
    STATIC_REQUIRE(std::is_same_v<Collected, ev_loop::type_list<>>);
  }
}
