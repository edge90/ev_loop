#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstddef>
#include <ev_loop/ev.hpp>
#include <thread>

// =============================================================================
// Test helper types
// =============================================================================

namespace {

struct TestEvent
{
  int value;
};

struct TestReceiver
{
  using receives = ev_loop::type_list<TestEvent>;
  using thread_mode = ev_loop::SameThread;
  int count = 0;
  int sum = 0;
  template<typename D> void on_event(TestEvent event, D& /*unused*/)
  {
    ++count;
    sum += event.value;
  }
};

using TestLoop = ev_loop::EventLoop<TestReceiver>;

struct ExternalTestEmitter
{
  using emits = ev_loop::type_list<TestEvent>;
};

} // namespace

// =============================================================================
// Strategy construction tests
// =============================================================================

TEST_CASE("Strategy construction from EventLoop reference", "[strategies]")
{
  TestLoop loop;
  loop.start();

  SECTION("Spin")
  {
    ev_loop::Spin strategy{ loop };
    loop.emit(TestEvent{ .value = 1 });
    REQUIRE(strategy.poll());
    REQUIRE(loop.get<TestReceiver>().count == 1);
  }

  SECTION("Wait")
  {
    ev_loop::Wait strategy{ loop };
    loop.emit(TestEvent{ .value = 1 });
    REQUIRE(strategy.poll());
    REQUIRE(loop.get<TestReceiver>().count == 1);
  }

  SECTION("Yield")
  {
    ev_loop::Yield strategy{ loop };
    loop.emit(TestEvent{ .value = 1 });
    REQUIRE(strategy.poll());
    REQUIRE(loop.get<TestReceiver>().count == 1);
  }

  SECTION("Hybrid with default spin count")
  {
    ev_loop::Hybrid strategy{ loop };
    loop.emit(TestEvent{ .value = 1 });
    REQUIRE(strategy.poll());
    REQUIRE(loop.get<TestReceiver>().count == 1);
  }

  SECTION("Hybrid with custom spin count")
  {
    constexpr std::size_t kSpinCount = 500;
    ev_loop::Hybrid strategy{ loop, kSpinCount };
    loop.emit(TestEvent{ .value = 1 });
    REQUIRE(strategy.poll());
    REQUIRE(loop.get<TestReceiver>().count == 1);
  }

  loop.stop();
}

// =============================================================================
// Strategy poll behavior tests
// =============================================================================

TEST_CASE("Strategy poll returns false on empty queue", "[strategies]")
{
  TestLoop loop;
  loop.start();

  SECTION("Spin") { REQUIRE_FALSE(ev_loop::Spin{ loop }.poll()); }

  // Wait is not tested here - it blocks until events arrive by design

  SECTION("Yield") { REQUIRE_FALSE(ev_loop::Yield{ loop }.poll()); }

  SECTION("Hybrid") { REQUIRE_FALSE(ev_loop::Hybrid{ loop }.poll()); }

  loop.stop();
}

TEST_CASE("Strategy poll returns true when events pending", "[strategies]")
{
  TestLoop loop;
  loop.start();
  loop.emit(TestEvent{ .value = 1 });

  SECTION("Spin") { REQUIRE(ev_loop::Spin{ loop }.poll()); }

  SECTION("Wait") { REQUIRE(ev_loop::Wait{ loop }.poll()); }

  SECTION("Yield") { REQUIRE(ev_loop::Yield{ loop }.poll()); }

  SECTION("Hybrid") { REQUIRE(ev_loop::Hybrid{ loop }.poll()); }

  loop.stop();
}

// =============================================================================
// Temporary strategy pattern tests (Spin{loop}.poll())
// =============================================================================

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("Strategies work as temporaries", "[strategies]")
{
  TestLoop loop;
  loop.start();
  loop.emit(TestEvent{ .value = 1 });
  loop.emit(TestEvent{ .value = 2 });

  SECTION("Spin")
  {
    REQUIRE(ev_loop::Spin{ loop }.poll());
    REQUIRE(ev_loop::Spin{ loop }.poll());
    REQUIRE_FALSE(ev_loop::Spin{ loop }.poll());
    REQUIRE(loop.get<TestReceiver>().count == 2);
    REQUIRE(loop.get<TestReceiver>().sum == 3);
  }

  SECTION("Yield")
  {
    REQUIRE(ev_loop::Yield{ loop }.poll());
    REQUIRE(ev_loop::Yield{ loop }.poll());
    REQUIRE_FALSE(ev_loop::Yield{ loop }.poll());
    REQUIRE(loop.get<TestReceiver>().count == 2);
    REQUIRE(loop.get<TestReceiver>().sum == 3);
  }

  loop.stop();
}

// =============================================================================
// Blocking strategy tests
// =============================================================================

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("Blocking strategies", "[strategies]")
{
  constexpr int kTestEventValue = 42;
  constexpr auto kThreadSleepMs = std::chrono::milliseconds(10);

  ev_loop::SharedEventLoopPtr<TestReceiver, ExternalTestEmitter> loop;
  loop->start();
  auto emitter = loop.get_external_emitter<ExternalTestEmitter>();

  SECTION("Wait blocks until event arrives")
  {
    ev_loop::Wait strategy{ *loop };

    std::thread producer([&] {
      std::this_thread::sleep_for(kThreadSleepMs);
      emitter.emit(TestEvent{ .value = kTestEventValue });
    });

    REQUIRE(strategy.poll());
    REQUIRE(loop->get<TestReceiver>().count == 1);
    producer.join();
  }

  SECTION("Hybrid falls back to wait after spin count exceeded")
  {
    constexpr std::size_t kSpinCount = 2;
    ev_loop::Hybrid strategy{ *loop, kSpinCount };

    REQUIRE_FALSE(strategy.poll()); // First spin

    std::thread producer([&] {
      std::this_thread::sleep_for(kThreadSleepMs);
      emitter.emit(TestEvent{ .value = kTestEventValue });
    });

    REQUIRE(strategy.poll()); // Exceeds spin count, blocks until event
    REQUIRE(loop->get<TestReceiver>().count == 1);
    producer.join();
  }

  SECTION("Wait returns false when loop stopped")
  {
    ev_loop::Wait strategy{ *loop };

    std::thread stopper([&] {
      std::this_thread::sleep_for(kThreadSleepMs);
      loop->stop();
    });

    REQUIRE_FALSE(strategy.poll());
    stopper.join();
    return; // Skip loop->stop() below
  }

  loop->stop();
}
