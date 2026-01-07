// NOLINTBEGIN(misc-include-cleaner)
#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <ev_loop/ev.hpp>
#include <utility>
// NOLINTEND(misc-include-cleaner)

namespace {
constexpr int kTestValue1 = 10;

struct EventA
{
  int value;
};

struct ReceiverA
{
  using receives = ev_loop::type_list<EventA>;
  using emits = ev_loop::type_list<>;

  std::atomic<int> count{ 0 };

  template<typename Dispatcher> void on_event(EventA event, Dispatcher& /*unused*/)
  {
    count.fetch_add(event.value, std::memory_order_relaxed);
  }
};
} // namespace

// =============================================================================
// Setup builder tests
// =============================================================================

// NOLINTBEGIN(readability-function-cognitive-complexity)
TEST_CASE("Setup builder", "[setup]")
{
  using Loop = ev_loop::GroupEventLoop<ev_loop::SpinGroup<ReceiverA>>;

  SECTION("can create multiple independent loops")
  {
    auto loop1 = Loop::setup().prime(EventA{ .value = kTestValue1 }).create_unique();
    auto loop2 = Loop::setup().prime(EventA{ .value = kTestValue1 }).create_unique();

    while (loop1->poll_group<0>()) {}
    while (loop2->poll_group<0>()) {}

    REQUIRE(loop1->get<ReceiverA>().count.load() == kTestValue1);
    REQUIRE(loop2->get<ReceiverA>().count.load() == kTestValue1);
  }

  SECTION("prime chaining accumulates events")
  {
    auto loop =
      Loop::setup().prime(EventA{ .value = 1 }).prime(EventA{ .value = 2 }).prime(EventA{ .value = 3 }).create_unique();

    while (loop->poll_group<0>()) {}

    REQUIRE(loop->get<ReceiverA>().count.load() == 6); // 1 + 2 + 3
  }

  SECTION("prime rvalue moves tuple")
  {
    auto setup = Loop::setup();
    auto loop = std::move(setup).prime(EventA{ .value = kTestValue1 }).create_unique();

    while (loop->poll_group<0>()) {}

    REQUIRE(loop->get<ReceiverA>().count.load() == kTestValue1);
  }
}
// NOLINTEND(readability-function-cognitive-complexity)
