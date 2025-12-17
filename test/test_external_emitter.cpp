// NOLINTBEGIN(misc-include-cleaner)
#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <ev_loop/ev.hpp>
#include <memory>
#include <thread>
#include <utility>
// NOLINTEND(misc-include-cleaner)

// =============================================================================
// Test helper types
// =============================================================================

namespace {

constexpr int kEventCount = 100;
constexpr int kPollDelayMs = 1;

struct TestEvent
{
  int value;
};

struct SameThreadReceiver
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

struct OwnThreadReceiver
{
  using receives = ev_loop::type_list<TestEvent>;
  using thread_mode = ev_loop::OwnThread;
  std::atomic<int> count{ 0 };
  std::atomic<int> sum{ 0 };
  template<typename D> void on_event(TestEvent event, D& /*unused*/)
  {
    count.fetch_add(1, std::memory_order_relaxed);
    sum.fetch_add(event.value, std::memory_order_relaxed);
  }
};

struct TestExternalEmitter
{
  using emits = ev_loop::type_list<TestEvent>;
};

} // namespace

// =============================================================================
// TypedExternalEmitter basic functionality
// =============================================================================

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("TypedExternalEmitter basic operations", "[external_emitter]")
{
  ev_loop::SharedEventLoopPtr<OwnThreadReceiver, TestExternalEmitter> loop;
  loop.start();

  auto emitter = loop.get_external_emitter<TestExternalEmitter>();

  SECTION("is_valid returns true while loop exists") { REQUIRE(emitter.is_valid()); }

  SECTION("emit returns true while loop exists")
  {
    REQUIRE(emitter.emit(TestEvent{ 42 }));
    // Wait for OwnThread receiver to process event
    while (loop.get<OwnThreadReceiver>().count < 1) {
      std::this_thread::sleep_for(std::chrono::milliseconds(kPollDelayMs));
    }
    REQUIRE(loop.get<OwnThreadReceiver>().count == 1);
    REQUIRE(loop.get<OwnThreadReceiver>().sum == 42);
  }

  loop.stop();
}

// =============================================================================
// TypedExternalEmitter from separate thread
// =============================================================================

TEST_CASE("TypedExternalEmitter from another thread", "[external_emitter][threaded]")
{
  ev_loop::SharedEventLoopPtr<OwnThreadReceiver, TestExternalEmitter> loop;
  loop.start();

  auto emitter = loop.get_external_emitter<TestExternalEmitter>();

  // Emit events from a separate thread
  std::thread producer([&emitter] {
    for (int i = 1; i <= kEventCount; ++i) { emitter.emit(TestEvent{ i }); }
  });

  producer.join();

  // Wait for OwnThread receiver to process all events
  while (loop.get<OwnThreadReceiver>().count < kEventCount) {
    std::this_thread::sleep_for(std::chrono::milliseconds(kPollDelayMs));
  }

  loop.stop();

  // Sum of 1..100 = 5050
  constexpr int kExpectedSum = kEventCount * (kEventCount + 1) / 2;
  REQUIRE(loop.get<OwnThreadReceiver>().count == kEventCount);
  REQUIRE(loop.get<OwnThreadReceiver>().sum == kExpectedSum);
}

// =============================================================================
// TypedExternalEmitter safety after loop destruction
// =============================================================================

TEST_CASE("TypedExternalEmitter safe after SharedEventLoopPtr destruction", "[external_emitter]")
{
  auto emitter = []() {
    ev_loop::SharedEventLoopPtr<OwnThreadReceiver, TestExternalEmitter> loop;
    loop.start();
    auto ext_emitter = loop.get_external_emitter<TestExternalEmitter>();
    // Emitter should be valid while loop exists
    REQUIRE(ext_emitter.is_valid());
    REQUIRE(ext_emitter.emit(TestEvent{ 1 }) == true);
    // Wait for event to be processed
    while (loop.get<OwnThreadReceiver>().count < 1) {
      std::this_thread::sleep_for(std::chrono::milliseconds(kPollDelayMs));
    }
    loop.stop();
    return ext_emitter;
  }();

  // Emitter should be invalid after loop is destroyed
  REQUIRE_FALSE(emitter.is_valid());
  // emit() should return false (not crash) when loop is gone
  REQUIRE_FALSE(emitter.emit(TestEvent{ 2 }));
}

// =============================================================================
// constexpr tests - TypedExternalEmitter noexcept specifications
// =============================================================================

TEST_CASE("TypedExternalEmitter constructor is noexcept", "[external_emitter][constexpr]")
{
  using Loop = ev_loop::EventLoop<SameThreadReceiver, TestExternalEmitter>;

  STATIC_REQUIRE(
    noexcept(ev_loop::TypedExternalEmitter<TestExternalEmitter, Loop>(std::declval<std::shared_ptr<Loop>>())));
}

TEST_CASE("TypedExternalEmitter::is_valid is noexcept", "[external_emitter][constexpr]")
{
  using Loop = ev_loop::EventLoop<SameThreadReceiver, TestExternalEmitter>;
  using Emitter = ev_loop::TypedExternalEmitter<TestExternalEmitter, Loop>;

  STATIC_REQUIRE(noexcept(std::declval<const Emitter&>().is_valid()));
}
