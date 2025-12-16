#include <catch2/catch_test_macros.hpp>
#include <ev_loop/ev.hpp>
#include <utility>

// =============================================================================
// constexpr tests - noexcept specifications
// =============================================================================

TEST_CASE("RingBuffer::empty and size are noexcept", "[ring_buffer][constexpr]")
{
  STATIC_REQUIRE(noexcept(std::declval<const ev_loop::detail::RingBuffer<int>&>().empty()));
  STATIC_REQUIRE(noexcept(std::declval<const ev_loop::detail::RingBuffer<int>&>().size()));
}

TEST_CASE("RingBuffer::commit_push is noexcept", "[ring_buffer][constexpr]")
{
  STATIC_REQUIRE(noexcept(std::declval<ev_loop::detail::RingBuffer<int>&>().commit_push()));
}

TEST_CASE("spsc::Queue::is_stopped is noexcept", "[spsc_queue][constexpr]")
{
  STATIC_REQUIRE(noexcept(std::declval<const ev_loop::detail::spsc::Queue<int>&>().is_stopped()));
}

TEST_CASE("mpsc::Queue::is_stopped is noexcept", "[mpsc_queue][constexpr]")
{
  STATIC_REQUIRE(noexcept(std::declval<const ev_loop::detail::mpsc::Queue<int>&>().is_stopped()));
}

TEST_CASE("DualQueue::try_pop_local is noexcept", "[dual_queue][constexpr]")
{
  using TaggedEvent = ev_loop::detail::TaggedEvent<int>;
  STATIC_REQUIRE(noexcept(std::declval<ev_loop::detail::DualQueue<TaggedEvent>&>().try_pop_local()));
}
