#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <utility>

#include "test_utils.hpp"

TEST_CASE("TrackingCounter tracks operations", "[test_utils]")
{
  auto counter = std::make_shared<TrackingCounter>();

  SECTION("initialized to zero")
  {
    REQUIRE(counter->constructed_count.load() == 0);
    REQUIRE(counter->destructed_count.load() == 0);
    REQUIRE(counter->move_count.load() == 0);
    REQUIRE(counter->copy_count.load() == 0);
  }

  SECTION("copy construction")
  {
    const TrackedString str1{ counter, "test" };
    // NOLINTNEXTLINE(performance-unnecessary-copy-initialization)
    const TrackedString str2{ str1 };
    REQUIRE(counter->constructed_count.load() == 2);
    REQUIRE(counter->copy_count.load() == 1);
    REQUIRE(counter->move_count.load() == 0);
    REQUIRE(str1 == str2);
  }

  SECTION("move construction")
  {
    TrackedString str1{ counter, "test" };
    const TrackedString str2{ std::move(str1) };
    REQUIRE(counter->constructed_count.load() == 2);
    REQUIRE(counter->move_count.load() == 1);
    REQUIRE(counter->copy_count.load() == 0);
  }

  SECTION("copy assignment")
  {
    const TrackedString str1{ counter, "test" };
    TrackedString str2{ counter };
    str2 = str1;
    REQUIRE(counter->constructed_count.load() == 2);
    REQUIRE(counter->copy_count.load() == 1);
    REQUIRE(counter->move_count.load() == 0);
  }

  SECTION("move assignment")
  {
    TrackedString str1{ counter, "test" };
    TrackedString str2{ counter };
    str2 = std::move(str1);
    REQUIRE(counter->constructed_count.load() == 2);
    REQUIRE(counter->move_count.load() == 1);
    REQUIRE(counter->copy_count.load() == 0);
  }

  REQUIRE(counter->balanced());
}
