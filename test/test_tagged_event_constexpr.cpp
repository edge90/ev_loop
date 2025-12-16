#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <ev_loop/ev.hpp>
#include <type_traits>
#include <utility>

// =============================================================================
// constexpr tests - noexcept and ref qualifiers
// =============================================================================

TEST_CASE("TaggedEvent::index is noexcept", "[tagged_event][constexpr]")
{
  SECTION("mutable") { STATIC_REQUIRE(noexcept(std::declval<ev_loop::detail::TaggedEvent<int>&>().index())); }
  SECTION("const") { STATIC_REQUIRE(noexcept(std::declval<const ev_loop::detail::TaggedEvent<int>&>().index())); }
}

TEST_CASE("TaggedEvent::get is callable on lvalue references", "[tagged_event][constexpr]")
{
  SECTION("mutable")
  {
    STATIC_REQUIRE(requires(ev_loop::detail::TaggedEvent<int>& tagged) { tagged.index(); });
  }
  SECTION("const")
  {
    STATIC_REQUIRE(requires(const ev_loop::detail::TaggedEvent<int>& tagged) { tagged.index(); });
  }
}

// =============================================================================
// constexpr tests - tag_type_t selection
// =============================================================================

TEST_CASE("tag_type_t selects correct integer type", "[tagged_event][constexpr]")
{
  SECTION("uint8_t for 0-254 events (255 reserved for uninitialized)")
  {
    STATIC_REQUIRE(std::is_same_v<ev_loop::detail::tag_type_t<0>, std::uint8_t>);
    STATIC_REQUIRE(std::is_same_v<ev_loop::detail::tag_type_t<1>, std::uint8_t>);
    STATIC_REQUIRE(std::is_same_v<ev_loop::detail::tag_type_t<100>, std::uint8_t>);
    STATIC_REQUIRE(std::is_same_v<ev_loop::detail::tag_type_t<254>, std::uint8_t>);
  }

  SECTION("uint16_t for 255-65534 events")
  {
    STATIC_REQUIRE(std::is_same_v<ev_loop::detail::tag_type_t<255>, std::uint16_t>);
    STATIC_REQUIRE(std::is_same_v<ev_loop::detail::tag_type_t<256>, std::uint16_t>);
    STATIC_REQUIRE(std::is_same_v<ev_loop::detail::tag_type_t<1000>, std::uint16_t>);
    STATIC_REQUIRE(std::is_same_v<ev_loop::detail::tag_type_t<65534>, std::uint16_t>);
  }

  SECTION("uint32_t for 65535+ events")
  {
    STATIC_REQUIRE(std::is_same_v<ev_loop::detail::tag_type_t<65535>, std::uint32_t>);
    STATIC_REQUIRE(std::is_same_v<ev_loop::detail::tag_type_t<100000>, std::uint32_t>);
  }
}
