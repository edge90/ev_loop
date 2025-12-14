#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <ev_loop/ev.hpp>
#include <string>
#include <utility>

#include "test_utils.hpp"

namespace {
constexpr std::size_t kSmallQueueCapacity = 4;
constexpr std::size_t kMediumQueueCapacity = 8;
constexpr std::size_t kLargeQueueCapacity = 16;
constexpr int kWraparoundRounds = 10;
constexpr int kMemoryTestIterations = 100;
} // namespace

// =============================================================================
// RingBuffer Tests
// =============================================================================

TEST_CASE("RingBuffer push pop", "[ring_buffer]")
{
  ev_loop::RingBuffer<int, kMediumQueueCapacity> ring_buffer;
  REQUIRE(ring_buffer.empty());

  ring_buffer.push(1);
  ring_buffer.push(2);
  ring_buffer.push(3);

  REQUIRE(ring_buffer.size() == 3U);
  REQUIRE_FALSE(ring_buffer.empty());

  REQUIRE(*ring_buffer.try_pop() == 1);
  REQUIRE(*ring_buffer.try_pop() == 2);
  REQUIRE(*ring_buffer.try_pop() == 3);
  REQUIRE(ring_buffer.try_pop() == nullptr);
}

TEST_CASE("RingBuffer wraparound", "[ring_buffer]")
{
  ev_loop::RingBuffer<int, kSmallQueueCapacity> ring_buffer;

  for (int round = 0; round < kWraparoundRounds; ++round) {
    ring_buffer.push((round * kWraparoundRounds) + 1);
    ring_buffer.push((round * kWraparoundRounds) + 2);
    REQUIRE(*ring_buffer.try_pop() == (round * kWraparoundRounds) + 1);
    REQUIRE(*ring_buffer.try_pop() == (round * kWraparoundRounds) + 2);
  }
}

TEST_CASE("RingBuffer full", "[ring_buffer]")
{
  ev_loop::RingBuffer<int, kSmallQueueCapacity> ring_buffer;

  REQUIRE(ring_buffer.push(1));
  REQUIRE(ring_buffer.push(2));
  REQUIRE(ring_buffer.push(3));
  REQUIRE(ring_buffer.push(4));
  REQUIRE_FALSE(ring_buffer.push(5));

  REQUIRE(ring_buffer.size() == kSmallQueueCapacity);
}

TEST_CASE("RingBuffer no memory leaks", "[ring_buffer]")
{
  reset_tracking();
  {
    ev_loop::RingBuffer<ev_loop::TaggedEvent<TrackedString>, kLargeQueueCapacity> ring_buffer;

    for (int idx = 0; idx < kMemoryTestIterations; ++idx) {
      ev_loop::TaggedEvent<TrackedString> tagged_event;
      tagged_event.store(TrackedString{ "item_" + std::to_string(idx) });
      if (ring_buffer.push(std::move(tagged_event))) { (void)ring_buffer.try_pop(); }
    }
  }
  REQUIRE(constructed_count == destructed_count);
}

// =============================================================================
// SPSCQueue Tests
// =============================================================================

TEST_CASE("SPSCQueue basic", "[spsc_queue]")
{
  constexpr int kTestValue1 = 10;
  constexpr int kTestValue2 = 20;

  ev_loop::SPSCQueue<int, kMediumQueueCapacity> spsc_queue;

  spsc_queue.push(kTestValue1);
  spsc_queue.push(kTestValue2);

  REQUIRE(*spsc_queue.try_pop() == kTestValue1);
  REQUIRE(*spsc_queue.try_pop() == kTestValue2);
  REQUIRE(spsc_queue.try_pop() == nullptr);
}

TEST_CASE("SPSCQueue with TaggedEvent", "[spsc_queue]")
{
  reset_tracking();
  {
    ev_loop::SPSCQueue<ev_loop::TaggedEvent<TrackedString, int>, kMediumQueueCapacity> spsc_queue;

    ev_loop::TaggedEvent<TrackedString, int> tagged_event;
    tagged_event.store(TrackedString{ "queued" });
    spsc_queue.push(std::move(tagged_event));

    auto* result = spsc_queue.try_pop();
    REQUIRE(result != nullptr);
    REQUIRE(result->get<0>().value == "queued");
  }
  REQUIRE(constructed_count == destructed_count);
}

TEST_CASE("SPSCQueue full", "[spsc_queue]")
{
  ev_loop::SPSCQueue<int, kSmallQueueCapacity> spsc_queue;

  REQUIRE(spsc_queue.push(1));
  REQUIRE(spsc_queue.push(2));
  REQUIRE(spsc_queue.push(3));
  REQUIRE(spsc_queue.push(4));
  REQUIRE_FALSE(spsc_queue.push(5));

  // Verify data integrity
  REQUIRE(*spsc_queue.try_pop() == 1);
  REQUIRE(*spsc_queue.try_pop() == 2);
  REQUIRE(*spsc_queue.try_pop() == 3);
  REQUIRE(*spsc_queue.try_pop() == 4);
  REQUIRE(spsc_queue.try_pop() == nullptr);
}

// =============================================================================
// ThreadSafeRingBuffer Tests
// =============================================================================

TEST_CASE("ThreadSafeRingBuffer full", "[thread_safe_ring_buffer]")
{
  ev_loop::ThreadSafeRingBuffer<int, kSmallQueueCapacity> thread_safe_buffer;

  REQUIRE(thread_safe_buffer.push(1));
  REQUIRE(thread_safe_buffer.push(2));
  REQUIRE(thread_safe_buffer.push(3));
  REQUIRE(thread_safe_buffer.push(4));
  REQUIRE_FALSE(thread_safe_buffer.push(5));

  // Verify data integrity
  REQUIRE(*thread_safe_buffer.try_pop() == 1);
  REQUIRE(*thread_safe_buffer.try_pop() == 2);
  REQUIRE(*thread_safe_buffer.try_pop() == 3);
  REQUIRE(*thread_safe_buffer.try_pop() == 4);
  REQUIRE(thread_safe_buffer.try_pop() == nullptr);
}

TEST_CASE("ThreadSafeRingBuffer no memory leaks", "[thread_safe_ring_buffer]")
{
  reset_tracking();
  {
    ev_loop::ThreadSafeRingBuffer<ev_loop::TaggedEvent<TrackedString>, kLargeQueueCapacity> thread_safe_buffer;

    for (int idx = 0; idx < kMemoryTestIterations; ++idx) {
      ev_loop::TaggedEvent<TrackedString> tagged_event;
      tagged_event.store(TrackedString{ "item_" + std::to_string(idx) });
      thread_safe_buffer.push(std::move(tagged_event));
      (void)thread_safe_buffer.try_pop();
    }
  }
  REQUIRE(constructed_count == destructed_count);
}
