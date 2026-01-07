// NOLINTBEGIN(misc-include-cleaner)
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <ev_loop/ev.hpp>
#include <type_traits>
// NOLINTEND(misc-include-cleaner)

namespace {

// Minimal receiver for strategy alias tests
struct TestReceiver
{
  using receives = ev_loop::type_list<>;
  using emits = ev_loop::type_list<>;
  template<typename E, typename D> static void on_event(E /*unused*/, D& /*unused*/) {}
};

// =============================================================================
// Strategy alias tests
// =============================================================================

TEST_CASE("Strategy group aliases resolve correctly", "[strategy][constexpr]")
{
  SECTION("SpinGroup alias resolves to Spin strategy")
  {
    using Group = ev_loop::SpinGroup<TestReceiver>;
    STATIC_REQUIRE(std::is_same_v<ev_loop::detail::group_strategy_t<Group>, ev_loop::Spin>);
  }

  SECTION("WaitGroup alias resolves to Wait strategy")
  {
    using Group = ev_loop::WaitGroup<TestReceiver>;
    STATIC_REQUIRE(std::is_same_v<ev_loop::detail::group_strategy_t<Group>, ev_loop::Wait>);
  }

  SECTION("YieldGroup alias resolves to Yield strategy")
  {
    using Group = ev_loop::YieldGroup<TestReceiver>;
    STATIC_REQUIRE(std::is_same_v<ev_loop::detail::group_strategy_t<Group>, ev_loop::Yield>);
  }

  SECTION("HybridGroup alias resolves to Hybrid strategy")
  {
    using Group = ev_loop::HybridGroup<TestReceiver>;
    STATIC_REQUIRE(std::is_same_v<ev_loop::detail::group_strategy_t<Group>, ev_loop::Hybrid>);
  }
}

// =============================================================================
// is_hybrid_strategy_v tests
// =============================================================================

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

// =============================================================================
// HybridWith spin_count tests
// =============================================================================

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

} // namespace
