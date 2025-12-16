// NOLINTBEGIN(misc-include-cleaner)
#include <catch2/catch_test_macros.hpp>
#include <ev_loop/ev.hpp>
// NOLINTEND(misc-include-cleaner)

TEST_CASE("type_list size is computed at compile time", "[type_list][constexpr]")
{
  SECTION("empty list") { STATIC_REQUIRE(ev_loop::type_list<>::size == 0); }
  SECTION("single element") { STATIC_REQUIRE(ev_loop::type_list<int>::size == 1); }
  SECTION("multiple elements") { STATIC_REQUIRE(ev_loop::type_list<int, float, double>::size == 3); }
}

TEST_CASE("contains_v is computed at compile time", "[type_list][constexpr]")
{
  using list = ev_loop::type_list<int, float, double>;

  SECTION("types in list")
  {
    STATIC_REQUIRE(ev_loop::detail::contains_v<list, int>);
    STATIC_REQUIRE(ev_loop::detail::contains_v<list, float>);
    STATIC_REQUIRE(ev_loop::detail::contains_v<list, double>);
  }

  SECTION("types not in list")
  {
    STATIC_REQUIRE_FALSE(ev_loop::detail::contains_v<list, char>);
    STATIC_REQUIRE_FALSE(ev_loop::detail::contains_v<list, long>);
  }
}

TEST_CASE("index_of_v is computed at compile time", "[type_list][constexpr]")
{
  SECTION("first element") { STATIC_REQUIRE(ev_loop::detail::index_of_v<int, int, float, double> == 0); }
  SECTION("middle element") { STATIC_REQUIRE(ev_loop::detail::index_of_v<float, int, float, double> == 1); }
  SECTION("last element") { STATIC_REQUIRE(ev_loop::detail::index_of_v<double, int, float, double> == 2); }
}

TEST_CASE("const_max computes maximum at compile time", "[type_list][constexpr]")
{
  SECTION("ascending") { STATIC_REQUIRE(ev_loop::detail::const_max<1, 2, 3>() == 3); }
  SECTION("descending") { STATIC_REQUIRE(ev_loop::detail::const_max<3, 2, 1>() == 3); }
  SECTION("single value") { STATIC_REQUIRE(ev_loop::detail::const_max<5>() == 5); }
  SECTION("max in middle") { STATIC_REQUIRE(ev_loop::detail::const_max<1, 100, 50>() == 100); }
}
