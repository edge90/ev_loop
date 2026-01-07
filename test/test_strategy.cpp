// NOLINTBEGIN(misc-include-cleaner)
#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstddef>
#include <ev_loop/ev.hpp>
#include <thread>
// NOLINTEND(misc-include-cleaner)

namespace {
constexpr int kTestValue1 = 10;
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
// Event and receiver types for strategy tests
// =============================================================================

struct EventA
{
  int value;
};

struct ReceiverA
{
  using receives = ev_loop::type_list<EventA>;
  using emits = ev_loop::type_list<>;

  std::atomic<int> count{ 0 };

  template<typename Dispatcher> void on_event(EventA event, Dispatcher& /*dispatcher*/)
  {
    count.fetch_add(event.value, std::memory_order_relaxed);
  }
};

} // namespace

// =============================================================================
// Spin strategy tests
// =============================================================================

TEST_CASE("Spin strategy", "[strategy]")
{
  SECTION("SpinGroup processes events")
  {
    using Loop = ev_loop::GroupEventLoop<ev_loop::ThreadGroup<ev_loop::Spin, ReceiverA>>;

    auto loop = Loop::setup().prime(EventA{ .value = kTestValue1 }).create_unique();
    loop->start();

    REQUIRE(wait_for([&] { return loop->get<ReceiverA>().count.load() == kTestValue1; }));
    loop->stop();
  }
}

// =============================================================================
// Wait strategy tests
// =============================================================================

// NOLINTBEGIN(readability-function-cognitive-complexity)
TEST_CASE("Wait strategy", "[strategy]")
{
  SECTION("WaitGroup processes events")
  {
    using Loop = ev_loop::GroupEventLoop<ev_loop::ThreadGroup<ev_loop::Wait, ReceiverA>>;

    auto loop = Loop::setup().prime(EventA{ .value = kTestValue1 }).create_unique();
    loop->start();

    REQUIRE(wait_for([&] { return loop->get<ReceiverA>().count.load() == kTestValue1; }));
    loop->stop();
  }

  SECTION("WaitGroup wakes on external event")
  {
    // Test that WaitGroup properly wakes from wait when event arrives
    struct ExtEvents
    {
      using emits = ev_loop::type_list<EventA>;
    };
    using Loop =
      ev_loop::GroupEventLoop<ev_loop::ThreadGroup<ev_loop::Wait, ReceiverA>, ev_loop::ExternalGroup<ExtEvents>>;

    auto loop = Loop::setup().create_unique();
    auto emitter = loop->get_external_emitter<ExtEvents>();
    loop->start();

    // Send event to wake the waiting thread
    while (!emitter.emit(EventA{ .value = kTestValue1 })) { std::this_thread::yield(); }
    while (!loop->poll_external<ExtEvents>()) { std::this_thread::yield(); }

    REQUIRE(wait_for([&] { return loop->get<ReceiverA>().count.load() == kTestValue1; }));
    loop->stop();
  }
}
// NOLINTEND(readability-function-cognitive-complexity)

// =============================================================================
// Yield strategy tests
// =============================================================================

TEST_CASE("Yield strategy", "[strategy]")
{
  SECTION("YieldGroup processes events")
  {
    using Loop = ev_loop::GroupEventLoop<ev_loop::ThreadGroup<ev_loop::Yield, ReceiverA>>;

    auto loop = Loop::setup().prime(EventA{ .value = kTestValue1 }).create_unique();
    loop->start();

    REQUIRE(wait_for([&] { return loop->get<ReceiverA>().count.load() == kTestValue1; }));
    loop->stop();
  }
}

// =============================================================================
// Hybrid strategy tests
// =============================================================================

// NOLINTBEGIN(readability-function-cognitive-complexity)
TEST_CASE("Hybrid strategy", "[strategy]")
{
  SECTION("HybridGroup processes events")
  {
    using Loop = ev_loop::GroupEventLoop<ev_loop::ThreadGroup<ev_loop::Hybrid, ReceiverA>>;

    auto loop = Loop::setup().prime(EventA{ .value = kTestValue1 }).create_unique();
    loop->start();

    REQUIRE(wait_for([&] { return loop->get<ReceiverA>().count.load() == kTestValue1; }));
    loop->stop();
  }

  SECTION("enters wait mode after spin exhaustion")
  {
    // This test verifies the hybrid wait path is exercised.
    // Use HybridWith<1> so it enters wait mode after just 1 empty poll.
    struct ExtEvents
    {
      using emits = ev_loop::type_list<EventA>;
    };
    constexpr std::size_t kImmediateWaitSpinCount = 1;
    using FastHybrid = ev_loop::HybridWith<kImmediateWaitSpinCount>;
    using Loop =
      ev_loop::GroupEventLoop<ev_loop::ThreadGroup<FastHybrid, ReceiverA>, ev_loop::ExternalGroup<ExtEvents>>;

    auto loop = Loop::setup().create_unique();
    auto emitter = loop->get_external_emitter<ExtEvents>();
    loop->start();

    // Send event - the loop will be in wait mode almost immediately (after 1 empty poll)
    while (!emitter.emit(EventA{ .value = kTestValue1 })) { std::this_thread::yield(); }

    // Poll external events on main thread, which routes to receiver and wakes the waiting thread
    while (!loop->poll_external<ExtEvents>()) { std::this_thread::yield(); }

    REQUIRE(wait_for([&] { return loop->get<ReceiverA>().count.load() == kTestValue1; }));
    loop->stop();
  }
}
// NOLINTEND(readability-function-cognitive-complexity)
