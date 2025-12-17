#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <ev_loop/ev.hpp>
#include <memory>
#include <string>
#include <thread>

#include "test_utils.hpp"

namespace {
constexpr int kPollDelayMs = 1;
} // namespace

// =============================================================================
// Same thread receivers
// =============================================================================

struct TrackedReceiver1
{
  using receives = ev_loop::type_list<TrackedString>;
  using thread_mode = ev_loop::SameThread;
  int received = 0;
  // NOLINTNEXTLINE(performance-unnecessary-value-param) - testing move optimization requires by-value
  template<typename Dispatcher> void on_event(TrackedString /*event*/, Dispatcher& /*dispatcher*/) { ++received; }
};

struct TrackedReceiver2
{
  using receives = ev_loop::type_list<TrackedString>;
  using thread_mode = ev_loop::SameThread;
  int received = 0;
  // NOLINTNEXTLINE(performance-unnecessary-value-param) - testing move optimization requires by-value
  template<typename Dispatcher> void on_event(TrackedString /*event*/, Dispatcher& /*dispatcher*/) { ++received; }
};

struct TrackedReceiver3
{
  using receives = ev_loop::type_list<TrackedString>;
  using thread_mode = ev_loop::SameThread;
  int received = 0;
  // NOLINTNEXTLINE(performance-unnecessary-value-param) - testing move optimization requires by-value
  template<typename Dispatcher> void on_event(TrackedString /*event*/, Dispatcher& /*dispatcher*/) { ++received; }
};

// =============================================================================
// Own thread receivers
// =============================================================================

struct TrackedOwnThreadReceiver1
{
  using receives = ev_loop::type_list<TrackedString>;
  using thread_mode = ev_loop::OwnThread;
  std::atomic<int> received{ 0 };
  // NOLINTNEXTLINE(performance-unnecessary-value-param) - testing move optimization requires by-value
  template<typename Dispatcher> void on_event(TrackedString /*event*/, Dispatcher& /*dispatcher*/) { ++received; }
};

struct TrackedOwnThreadReceiver2
{
  using receives = ev_loop::type_list<TrackedString>;
  using thread_mode = ev_loop::OwnThread;
  std::atomic<int> received{ 0 };
  // NOLINTNEXTLINE(performance-unnecessary-value-param) - testing move optimization requires by-value
  template<typename Dispatcher> void on_event(TrackedString /*event*/, Dispatcher& /*dispatcher*/) { ++received; }
};

// =============================================================================
// Tests
// =============================================================================

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("Move optimization SameThread", "[move_optimization]")
{
  SECTION("single receiver gets move")
  {
    auto counter = std::make_shared<TrackingCounter>();
    {
      ev_loop::EventLoop<TrackedReceiver1> loop;
      loop.start();

      loop.emit(TrackedString{ counter, "test" });

      REQUIRE(ev_loop::Spin{ loop }.poll());
      REQUIRE_FALSE(ev_loop::Spin{ loop }.poll());

      loop.stop();

      REQUIRE(loop.get<TrackedReceiver1>().received == 1);
    }
    REQUIRE(counter->balanced());
    REQUIRE(counter->copy_count == 0);
  }

  SECTION("fanout copies to N-1 moves to last")
  {
    auto counter = std::make_shared<TrackingCounter>();
    {
      ev_loop::EventLoop<TrackedReceiver1, TrackedReceiver2, TrackedReceiver3> loop;
      loop.start();

      loop.emit(TrackedString{ counter, "test" });

      REQUIRE(ev_loop::Spin{ loop }.poll());
      REQUIRE_FALSE(ev_loop::Spin{ loop }.poll());

      loop.stop();

      REQUIRE(loop.get<TrackedReceiver1>().received == 1);
      REQUIRE(loop.get<TrackedReceiver2>().received == 1);
      REQUIRE(loop.get<TrackedReceiver3>().received == 1);
    }
    REQUIRE(counter->balanced());
    REQUIRE(counter->copy_count == 2);
  }
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("Move optimization OwnThread", "[move_optimization][threaded]")
{
  SECTION("single receiver gets move")
  {
    auto counter = std::make_shared<TrackingCounter>();
    ev_loop::EventLoop<TrackedOwnThreadReceiver1> loop;
    loop.start();

    loop.emit(TrackedString{ counter, "test" });

    while (loop.get<TrackedOwnThreadReceiver1>().received.load() < 1) {
      std::this_thread::sleep_for(std::chrono::milliseconds(kPollDelayMs));
    }

    loop.stop();

    REQUIRE(loop.get<TrackedOwnThreadReceiver1>().received.load() == 1);
    REQUIRE(counter->copy_count == 0);
  }

  SECTION("multiple receivers copy optimization")
  {
    auto counter = std::make_shared<TrackingCounter>();
    ev_loop::EventLoop<TrackedOwnThreadReceiver1, TrackedOwnThreadReceiver2> loop;
    loop.start();

    loop.emit(TrackedString{ counter, "test" });

    while (loop.get<TrackedOwnThreadReceiver1>().received.load() < 1
           || loop.get<TrackedOwnThreadReceiver2>().received.load() < 1) {
      std::this_thread::sleep_for(std::chrono::milliseconds(kPollDelayMs));
    }

    loop.stop();

    REQUIRE(loop.get<TrackedOwnThreadReceiver1>().received.load() == 1);
    REQUIRE(loop.get<TrackedOwnThreadReceiver2>().received.load() == 1);
    REQUIRE(counter->copy_count == 1);
  }

  SECTION("mixed same and ownthread")
  {
    auto counter = std::make_shared<TrackingCounter>();
    ev_loop::EventLoop<TrackedReceiver1, TrackedOwnThreadReceiver1> loop;
    loop.start();

    loop.emit(TrackedString{ counter, "test" });

    REQUIRE(ev_loop::Spin{ loop }.poll());
    REQUIRE_FALSE(ev_loop::Spin{ loop }.poll());

    while (loop.get<TrackedOwnThreadReceiver1>().received.load() < 1) {
      std::this_thread::sleep_for(std::chrono::milliseconds(kPollDelayMs));
    }

    loop.stop();

    REQUIRE(loop.get<TrackedReceiver1>().received == 1);
    REQUIRE(loop.get<TrackedOwnThreadReceiver1>().received.load() == 1);
    REQUIRE(counter->copy_count == 1);
  }
}
