// NOLINTBEGIN(misc-include-cleaner)
#include <catch2/catch_test_macros.hpp>
#include <ev_loop/ev.hpp>
#include <type_traits>
#include <utility>
// NOLINTEND(misc-include-cleaner)

// Test type_list utilities at compile time
TEST_CASE("type_list size is computed at compile time", "[constexpr]")
{
  STATIC_REQUIRE(ev_loop::type_list<>::size == 0);
  STATIC_REQUIRE(ev_loop::type_list<int>::size == 1);
  STATIC_REQUIRE(ev_loop::type_list<int, float, double>::size == 3);
}

TEST_CASE("contains_v is computed at compile time", "[constexpr]")
{
  using list = ev_loop::type_list<int, float, double>;
  STATIC_REQUIRE(ev_loop::contains_v<list, int>);
  STATIC_REQUIRE(ev_loop::contains_v<list, float>);
  STATIC_REQUIRE(ev_loop::contains_v<list, double>);
  STATIC_REQUIRE_FALSE(ev_loop::contains_v<list, char>);
  STATIC_REQUIRE_FALSE(ev_loop::contains_v<list, long>);
}

TEST_CASE("index_of_v is computed at compile time", "[constexpr]")
{
  STATIC_REQUIRE(ev_loop::index_of_v<int, int, float, double> == 0);
  STATIC_REQUIRE(ev_loop::index_of_v<float, int, float, double> == 1);
  STATIC_REQUIRE(ev_loop::index_of_v<double, int, float, double> == 2);
}

TEST_CASE("const_max computes maximum at compile time", "[constexpr]")
{
  STATIC_REQUIRE(ev_loop::const_max<1, 2, 3>() == 3);
  STATIC_REQUIRE(ev_loop::const_max<3, 2, 1>() == 3);
  STATIC_REQUIRE(ev_loop::const_max<5>() == 5);
  STATIC_REQUIRE(ev_loop::const_max<1, 100, 50>() == 100);
}

TEST_CASE("tag_type_t selects smallest sufficient type", "[constexpr]")
{
  // uint8_t range: 0-254 (255 reserved for uninitialized)
  STATIC_REQUIRE(sizeof(ev_loop::tag_type_t<10>) == 1);
  STATIC_REQUIRE(sizeof(ev_loop::tag_type_t<200>) == 1);
  STATIC_REQUIRE(sizeof(ev_loop::tag_type_t<254>) == 1);

  // uint16_t range: 255-65534
  STATIC_REQUIRE(sizeof(ev_loop::tag_type_t<255>) == 2);
  STATIC_REQUIRE(sizeof(ev_loop::tag_type_t<300>) == 2);
  STATIC_REQUIRE(sizeof(ev_loop::tag_type_t<65534>) == 2);

  // uint32_t range: 65535 to uint32_max-1
  STATIC_REQUIRE(sizeof(ev_loop::tag_type_t<65535>) == 4);
  STATIC_REQUIRE(sizeof(ev_loop::tag_type_t<100000>) == 4);
  STATIC_REQUIRE(sizeof(ev_loop::tag_type_t<4294967294UL>) == 4); // max valid value

  // Values >= uint32_max trigger static_assert: "Too many event types (max ~4 billion)"
}

TEST_CASE("TaggedEvent::index is noexcept", "[constexpr]")
{
  STATIC_REQUIRE(noexcept(std::declval<ev_loop::TaggedEvent<int>&>().index()));
  STATIC_REQUIRE(noexcept(std::declval<const ev_loop::TaggedEvent<int>&>().index()));
}

TEST_CASE("RingBuffer::empty and size are noexcept", "[constexpr]")
{
  STATIC_REQUIRE(noexcept(std::declval<const ev_loop::RingBuffer<int>&>().empty()));
  STATIC_REQUIRE(noexcept(std::declval<const ev_loop::RingBuffer<int>&>().size()));
}

TEST_CASE("SPSCQueue::is_stopped is noexcept", "[constexpr]")
{
  STATIC_REQUIRE(noexcept(std::declval<const ev_loop::SPSCQueue<int>&>().is_stopped()));
}

TEST_CASE("ThreadSafeRingBuffer::is_stopped is noexcept", "[constexpr]")
{
  STATIC_REQUIRE(noexcept(std::declval<const ev_loop::ThreadSafeRingBuffer<int>&>().is_stopped()));
}

// Verify ref qualifiers: & qualified functions are callable on lvalues
TEST_CASE("Ref qualified functions are callable on lvalues", "[constexpr]")
{
  // RingBuffer (no ref qualifiers on empty/size - they're const)
  STATIC_REQUIRE(requires(ev_loop::RingBuffer<int>& buf) { buf.empty(); });
  STATIC_REQUIRE(requires(ev_loop::RingBuffer<int>& buf) { buf.size(); });

  // TaggedEvent::index (const, no ref qualifier needed)
  STATIC_REQUIRE(requires(ev_loop::TaggedEvent<int>& tagged) { tagged.index(); });
  STATIC_REQUIRE(requires(const ev_loop::TaggedEvent<int>& tagged) { tagged.index(); });
}
