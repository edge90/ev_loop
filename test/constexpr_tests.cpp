// NOLINTBEGIN(misc-include-cleaner)
#include <catch2/catch_test_macros.hpp>
#include <ev_loop/ev.hpp>
// NOLINTEND(misc-include-cleaner)

// Test type_list utilities at compile time
TEST_CASE("type_list size is computed at compile time", "[constexpr]")
{
  STATIC_REQUIRE(ev::type_list<>::size == 0);
  STATIC_REQUIRE(ev::type_list<int>::size == 1);
  STATIC_REQUIRE(ev::type_list<int, float, double>::size == 3);
}

TEST_CASE("contains_v is computed at compile time", "[constexpr]")
{
  using list = ev::type_list<int, float, double>;
  STATIC_REQUIRE(ev::contains_v<list, int>);
  STATIC_REQUIRE(ev::contains_v<list, float>);
  STATIC_REQUIRE(ev::contains_v<list, double>);
  STATIC_REQUIRE_FALSE(ev::contains_v<list, char>);
  STATIC_REQUIRE_FALSE(ev::contains_v<list, long>);
}

TEST_CASE("index_of_v is computed at compile time", "[constexpr]")
{
  STATIC_REQUIRE(ev::index_of_v<int, int, float, double> == 0);
  STATIC_REQUIRE(ev::index_of_v<float, int, float, double> == 1);
  STATIC_REQUIRE(ev::index_of_v<double, int, float, double> == 2);
}

TEST_CASE("const_max computes maximum at compile time", "[constexpr]")
{
  STATIC_REQUIRE(ev::const_max<1, 2, 3>() == 3);
  STATIC_REQUIRE(ev::const_max<3, 2, 1>() == 3);
  STATIC_REQUIRE(ev::const_max<5>() == 5);
  STATIC_REQUIRE(ev::const_max<1, 100, 50>() == 100);
}

TEST_CASE("tag_type_t selects smallest sufficient type", "[constexpr]")
{
  STATIC_REQUIRE(sizeof(ev::tag_type_t<10>) == 1);    // fits in uint8_t
  STATIC_REQUIRE(sizeof(ev::tag_type_t<200>) == 1);   // fits in uint8_t
  STATIC_REQUIRE(sizeof(ev::tag_type_t<300>) == 2);   // needs uint16_t
}
