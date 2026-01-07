// NOLINTBEGIN(misc-include-cleaner)
#include <catch2/catch_test_macros.hpp>
#include <ev_loop/ev.hpp>
#include <type_traits>
// NOLINTEND(misc-include-cleaner)

// =============================================================================
// Setup type tests - compile-time traits
// =============================================================================

namespace {
struct DummyEvent
{
};

struct DummyReceiver
{
  using receives = ev_loop::type_list<DummyEvent>;
  using emits = ev_loop::type_list<>;
  template<typename D> static void on_event(DummyEvent /*unused*/, D& /*unused*/) {}
};
} // namespace

TEST_CASE("Setup type traits", "[setup][constexpr]")
{
  using Loop = ev_loop::GroupEventLoop<ev_loop::SpinGroup<DummyReceiver>>;
  using SetupType = decltype(Loop::setup());

  SECTION("setup returns Setup type") { STATIC_REQUIRE(std::is_class_v<SetupType>); }

  SECTION("prime returns Setup type")
  {
    using PrimedType = decltype(Loop::setup().prime(DummyEvent{}));
    STATIC_REQUIRE(std::is_class_v<PrimedType>);
  }
}
