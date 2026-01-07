// NOLINTBEGIN(misc-include-cleaner)
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <ev_loop/ev.hpp>
// NOLINTEND(misc-include-cleaner)

TEST_CASE("is_hybrid_strategy_v is computed at compile time", "[strategy][constexpr]")
{
  constexpr std::size_t kCustomSpinCount = 500;

  SECTION("HybridWith<N> is detected for any N")
  {
    STATIC_REQUIRE(ev_loop::detail::is_hybrid_strategy_v<ev_loop::Hybrid>);
    STATIC_REQUIRE(ev_loop::detail::is_hybrid_strategy_v<ev_loop::HybridWith<1>>);
    STATIC_REQUIRE(ev_loop::detail::is_hybrid_strategy_v<ev_loop::HybridWith<kCustomSpinCount>>);
  }

  SECTION("other strategies are not hybrid")
  {
    STATIC_REQUIRE_FALSE(ev_loop::detail::is_hybrid_strategy_v<ev_loop::Spin>);
    STATIC_REQUIRE_FALSE(ev_loop::detail::is_hybrid_strategy_v<ev_loop::Wait>);
    STATIC_REQUIRE_FALSE(ev_loop::detail::is_hybrid_strategy_v<ev_loop::Yield>);
    STATIC_REQUIRE_FALSE(ev_loop::detail::is_hybrid_strategy_v<int>);
  }
}

TEST_CASE("HybridWith spin_count is computed at compile time", "[strategy][constexpr]")
{
  constexpr std::size_t kCustomSpinCount = 500;
  constexpr std::size_t kMinSpinCount = 1;

  SECTION("default Hybrid uses kDefaultHybridSpinCount")
  {
    STATIC_REQUIRE(ev_loop::Hybrid::spin_count == ev_loop::kDefaultHybridSpinCount);
  }

  SECTION("custom HybridWith<N> has spin_count of N")
  {
    STATIC_REQUIRE(ev_loop::HybridWith<kCustomSpinCount>::spin_count == kCustomSpinCount);
    STATIC_REQUIRE(ev_loop::HybridWith<kMinSpinCount>::spin_count == kMinSpinCount);
  }
}
