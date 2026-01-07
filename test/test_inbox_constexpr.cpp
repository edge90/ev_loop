// NOLINTBEGIN(misc-include-cleaner)
#include <catch2/catch_test_macros.hpp>
#include <ev_loop/ev.hpp>
#include <type_traits>
// NOLINTEND(misc-include-cleaner)

// =============================================================================
// Test events and receivers for queue selection tests
// =============================================================================

namespace {
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
  template<typename D> static void on_event(QueueTestEventA /*unused*/, D& /*unused*/) {}
};

// Group2 receives B, emits A
struct QueueTestGroup2Recv
{
  using receives = ev_loop::type_list<QueueTestEventB>;
  using emits = ev_loop::type_list<QueueTestEventA>;
  template<typename D> static void on_event(QueueTestEventB /*unused*/, D& /*unused*/) {}
};

// Group3 receives A, emits B (same as Group1 - creates MPSC scenario for Group2)
struct QueueTestGroup3Recv
{
  using receives = ev_loop::type_list<QueueTestEventA>;
  using emits = ev_loop::type_list<QueueTestEventB>;
  template<typename D> static void on_event(QueueTestEventA /*unused*/, D& /*unused*/) {}
};

using QueueTestGroup1 = ev_loop::SpinGroup<QueueTestGroup1Recv>;
using QueueTestGroup2 = ev_loop::SpinGroup<QueueTestGroup2Recv>;
using QueueTestGroup3 = ev_loop::SpinGroup<QueueTestGroup3Recv>;
} // namespace

// =============================================================================
// Queue type selection tests (compile-time SPSC vs MPSC)
// =============================================================================

TEST_CASE("Queue selection - SPSC for single producer", "[inbox][constexpr]")
{
  // 2-group ping-pong: each event has exactly 1 producer
  // Group1 emits B (only producer of B for Group2)
  // Group2 emits A (only producer of A for Group1)

  using namespace ev_loop::detail;

  // EventA to Group1: only Group2 emits A -> 1 producer -> SPSC
  STATIC_REQUIRE(count_event_producers_v<QueueTestGroup1, QueueTestEventA, QueueTestGroup1, QueueTestGroup2> == 1);

  // EventB to Group2: only Group1 emits B -> 1 producer -> SPSC
  STATIC_REQUIRE(count_event_producers_v<QueueTestGroup2, QueueTestEventB, QueueTestGroup1, QueueTestGroup2> == 1);

  // Verify SPSC queue is selected
  STATIC_REQUIRE(std::is_same_v<select_queue_t<QueueTestEventA, 1>, SpscInbox<QueueTestEventA>>);
  STATIC_REQUIRE(std::is_same_v<select_queue_t<QueueTestEventB, 1>, SpscInbox<QueueTestEventB>>);
}

TEST_CASE("Queue selection - MPSC for multiple producers", "[inbox][constexpr]")
{
  // 3-group setup: Group1 AND Group3 both emit B to Group2
  // Group1 emits B, Group3 also emits B -> 2 producers for Group2's B queue

  using namespace ev_loop::detail;

  // EventB to Group2: Group1 emits B, Group3 emits B -> 2 producers -> MPSC
  STATIC_REQUIRE(
    count_event_producers_v<QueueTestGroup2, QueueTestEventB, QueueTestGroup1, QueueTestGroup2, QueueTestGroup3> == 2);

  // EventA to Group1: only Group2 emits A -> 1 producer -> SPSC
  STATIC_REQUIRE(
    count_event_producers_v<QueueTestGroup1, QueueTestEventA, QueueTestGroup1, QueueTestGroup2, QueueTestGroup3> == 1);

  // Verify MPSC queue is selected for 2+ producers
  STATIC_REQUIRE(std::is_same_v<select_queue_t<QueueTestEventB, 2>, MpscInbox<QueueTestEventB>>);
  STATIC_REQUIRE(std::is_same_v<select_queue_t<QueueTestEventB, 3>, MpscInbox<QueueTestEventB>>);
}
