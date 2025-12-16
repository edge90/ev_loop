// NOLINTBEGIN(misc-include-cleaner)
#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstddef>
#include <ev_loop/ev.hpp>
#include <thread>
// NOLINTEND(misc-include-cleaner)

// NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)

// =============================================================================
// Test helper types
// =============================================================================

namespace {

constexpr int kPollDelayMs = 1;

struct TestEvent
{
  int value;
};

struct TestReceiver
{
  using receives = ev_loop::type_list<TestEvent>;
  // cppcheck-suppress unusedStructMember
  [[maybe_unused]] static constexpr ev_loop::ThreadMode thread_mode = ev_loop::ThreadMode::SameThread;
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
  // cppcheck-suppress unusedStructMember
  [[maybe_unused]] static constexpr ev_loop::ThreadMode thread_mode = ev_loop::ThreadMode::OwnThread;
  std::atomic<int> count{ 0 };
  template<typename D> void on_event(TestEvent /*unused*/, D& /*unused*/)
  {
    count.fetch_add(1, std::memory_order_relaxed);
  }
};

struct TestExternalEmitter
{
  using emits = ev_loop::type_list<TestEvent>;
};

using TestPtr = ev_loop::SharedEventLoopPtr<TestReceiver>;
using TestPtrWithEmitter = ev_loop::SharedEventLoopPtr<TestReceiver, TestExternalEmitter>;
using TestPtrOwnThread = ev_loop::SharedEventLoopPtr<OwnThreadReceiver, TestExternalEmitter>;

} // namespace

// =============================================================================
// SharedEventLoopPtr basic functionality
// =============================================================================

TEST_CASE("SharedEventLoopPtr basic operations", "[shared_event_loop_ptr]")
{
  TestPtr ptr;

  SECTION("start and stop")
  {
    ptr.start();
    ptr.stop();
  }

  SECTION("emit events")
  {
    ptr.start();
    ptr.emit(TestEvent{ 42 });
    while (ev_loop::Spin{ *ptr }.poll()) {}
    REQUIRE(ptr.get<TestReceiver>().count == 1);
    REQUIRE(ptr.get<TestReceiver>().sum == 42);
    ptr.stop();
  }

  SECTION("dereference operators")
  {
    ptr.start();
    ptr->emit(TestEvent{ 10 });
    while (ev_loop::Spin{ *ptr }.poll()) {}
    REQUIRE((*ptr).get<TestReceiver>().count == 1);
    ptr.stop();
  }
}

// =============================================================================
// SharedEventLoopPtr copyability
// =============================================================================

TEST_CASE("SharedEventLoopPtr is copyable", "[shared_event_loop_ptr]")
{
  TestPtr ptr1;
  ptr1.start();
  ptr1.emit(TestEvent{ 1 });

  TestPtr ptr2 = ptr1; // Copy

  // Both point to same underlying loop
  ptr2.emit(TestEvent{ 2 });
  while (ev_loop::Spin{ *ptr1 }.poll()) {}

  REQUIRE(ptr1.get<TestReceiver>().count == 2);
  REQUIRE(ptr2.get<TestReceiver>().count == 2);

  ptr1.stop();
}

// =============================================================================
// SharedEventLoopPtr with external emitters
// =============================================================================

TEST_CASE("SharedEventLoopPtr get_external_emitter", "[shared_event_loop_ptr]")
{
  TestPtrOwnThread ptr;
  ptr.start();

  auto emitter = ptr.get_external_emitter<TestExternalEmitter>();
  REQUIRE(emitter.is_valid());
  REQUIRE(emitter.emit(TestEvent{ 100 }));

  // Wait for OwnThread receiver to process event
  while (ptr.get<OwnThreadReceiver>().count < 1) {
    std::this_thread::sleep_for(std::chrono::milliseconds(kPollDelayMs));
  }

  REQUIRE(ptr.get<OwnThreadReceiver>().count == 1);

  ptr.stop();
}

// NOLINTEND(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)
