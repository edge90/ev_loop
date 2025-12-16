// NOLINTBEGIN(misc-include-cleaner)
#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstddef>
#include <ev_loop/ev.hpp>
#include <memory>
#include <string>
#include <thread>
#include <utility>

#include "test_utils.hpp"
// NOLINTEND(misc-include-cleaner)

// NOLINTBEGIN(readability-function-cognitive-complexity)

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
  ev_loop::detail::RingBuffer<int, kMediumQueueCapacity> ring_buffer;
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

TEST_CASE("RingBuffer with small capacity", "[ring_buffer]")
{
  ev_loop::detail::RingBuffer<int, kSmallQueueCapacity> ring_buffer;

  SECTION("wraparound")
  {
    for (int round = 0; round < kWraparoundRounds; ++round) {
      ring_buffer.push((round * kWraparoundRounds) + 1);
      ring_buffer.push((round * kWraparoundRounds) + 2);
      REQUIRE(*ring_buffer.try_pop() == (round * kWraparoundRounds) + 1);
      REQUIRE(*ring_buffer.try_pop() == (round * kWraparoundRounds) + 2);
    }
  }

  SECTION("full")
  {
    REQUIRE(ring_buffer.push(1));
    REQUIRE(ring_buffer.push(2));
    REQUIRE(ring_buffer.push(3));
    REQUIRE(ring_buffer.push(4));
    REQUIRE_FALSE(ring_buffer.push(5));

    REQUIRE(ring_buffer.size() == kSmallQueueCapacity);
  }
}

TEST_CASE("RingBuffer no memory leaks", "[ring_buffer]")
{
  auto counter = std::make_shared<TrackingCounter>();
  {
    ev_loop::detail::RingBuffer<ev_loop::detail::TaggedEvent<TrackedString>, kLargeQueueCapacity> ring_buffer;

    for (int idx = 0; idx < kMemoryTestIterations; ++idx) {
      ev_loop::detail::TaggedEvent<TrackedString> tagged_event;
      tagged_event.store(TrackedString{ counter, "item_" + std::to_string(idx) });
      if (ring_buffer.push(std::move(tagged_event))) { (void)ring_buffer.try_pop(); }
    }
  }
  REQUIRE(counter->balanced());
}

// =============================================================================
// spsc::Queue Tests
// =============================================================================

TEST_CASE("spsc::Queue basic", "[spsc_queue]")
{
  constexpr int kTestValue1 = 10;
  constexpr int kTestValue2 = 20;

  ev_loop::detail::spsc::Queue<int, kMediumQueueCapacity> spsc_queue;

  spsc_queue.push(kTestValue1);
  spsc_queue.push(kTestValue2);

  REQUIRE(*spsc_queue.try_pop() == kTestValue1);
  REQUIRE(*spsc_queue.try_pop() == kTestValue2);
  REQUIRE(spsc_queue.try_pop() == nullptr);
}

TEST_CASE("spsc::Queue with TaggedEvent", "[spsc_queue]")
{
  auto counter = std::make_shared<TrackingCounter>();
  {
    ev_loop::detail::spsc::Queue<ev_loop::detail::TaggedEvent<TrackedString, int>, kMediumQueueCapacity> spsc_queue;

    ev_loop::detail::TaggedEvent<TrackedString, int> tagged_event;
    tagged_event.store(TrackedString{ counter, "queued" });
    spsc_queue.push(std::move(tagged_event));

    auto* result = spsc_queue.try_pop();
    REQUIRE(result != nullptr);
    REQUIRE(result->get<0>().value == "queued");
  }
  REQUIRE(counter->balanced());
}

TEST_CASE("spsc::Queue with small capacity", "[spsc_queue]")
{
  ev_loop::detail::spsc::Queue<int, kSmallQueueCapacity> spsc_queue;

  SECTION("full")
  {
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

  SECTION("pop_spin returns nullptr on stop")
  {
    std::atomic<bool> started{ false };
    std::atomic<int*> result{ nullptr };

    // Thread waits for data via pop_spin
    std::thread consumer([&] {
      started.store(true, std::memory_order_release);
      result.store(spsc_queue.pop_spin(), std::memory_order_release);
    });

    // Wait for consumer to enter spin loop
    while (!started.load(std::memory_order_acquire)) { std::this_thread::yield(); }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));

    // Stop the queue - should cause pop_spin to return nullptr
    spsc_queue.stop();
    consumer.join();

    REQUIRE(result.load(std::memory_order_acquire) == nullptr);
    REQUIRE(spsc_queue.is_stopped());
  }
}

// =============================================================================
// mpsc::Queue Tests
// =============================================================================

TEST_CASE("mpsc::Queue with small capacity", "[mpsc_queue]")
{
  ev_loop::detail::mpsc::Queue<int, kSmallQueueCapacity> mpsc_queue;

  SECTION("full")
  {
    REQUIRE(mpsc_queue.push(1));
    REQUIRE(mpsc_queue.push(2));
    REQUIRE(mpsc_queue.push(3));
    REQUIRE(mpsc_queue.push(4));
    REQUIRE_FALSE(mpsc_queue.push(5));

    // Verify data integrity
    REQUIRE(*mpsc_queue.try_pop() == 1);
    REQUIRE(*mpsc_queue.try_pop() == 2);
    REQUIRE(*mpsc_queue.try_pop() == 3);
    REQUIRE(*mpsc_queue.try_pop() == 4);
    REQUIRE(mpsc_queue.try_pop() == nullptr);
  }

  SECTION("pop_spin returns nullptr on stop")
  {
    std::atomic<bool> started{ false };
    std::atomic<int*> result{ nullptr };

    // Thread waits for data via pop_spin
    std::thread consumer([&] {
      started.store(true, std::memory_order_release);
      result.store(mpsc_queue.pop_spin(), std::memory_order_release);
    });

    // Wait for consumer to enter spin/wait loop
    while (!started.load(std::memory_order_acquire)) { std::this_thread::yield(); }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));

    // Stop the buffer - should cause pop_spin to return nullptr
    mpsc_queue.stop();
    consumer.join();

    REQUIRE(result.load(std::memory_order_acquire) == nullptr);
    REQUIRE(mpsc_queue.is_stopped());
  }
}

TEST_CASE("mpsc::Queue no memory leaks", "[mpsc_queue]")
{
  auto counter = std::make_shared<TrackingCounter>();
  {
    ev_loop::detail::mpsc::Queue<ev_loop::detail::TaggedEvent<TrackedString>, kLargeQueueCapacity> mpsc_queue;

    for (int idx = 0; idx < kMemoryTestIterations; ++idx) {
      ev_loop::detail::TaggedEvent<TrackedString> tagged_event;
      tagged_event.store(TrackedString{ counter, "item_" + std::to_string(idx) });
      mpsc_queue.push(std::move(tagged_event));
      (void)mpsc_queue.try_pop();
    }
  }
  REQUIRE(counter->balanced());
}

// NOLINTEND(readability-function-cognitive-complexity)
