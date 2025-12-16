#include <catch2/catch_test_macros.hpp>
#include <ev_loop/ev.hpp>
#include <utility>

namespace {

struct TestEvent
{
  int value;
};

struct TestReceiver
{
  using receives = ev_loop::type_list<TestEvent>;
  // cppcheck-suppress unusedStructMember
  [[maybe_unused]] static constexpr ev_loop::ThreadMode thread_mode = ev_loop::ThreadMode::SameThread;
  template<typename D> void on_event(TestEvent /*unused*/, D& /*unused*/) {}
};

using TestLoop = ev_loop::EventLoop<TestReceiver>;

} // namespace

// =============================================================================
// constexpr tests - Strategy constructors are noexcept
// =============================================================================

TEST_CASE("Strategy constructors are noexcept", "[strategies][constexpr]")
{
  SECTION("Spin") { STATIC_REQUIRE(noexcept(ev_loop::Spin<TestLoop>(std::declval<TestLoop&>()))); }
  SECTION("Wait") { STATIC_REQUIRE(noexcept(ev_loop::Wait<TestLoop>(std::declval<TestLoop&>()))); }
  SECTION("Yield") { STATIC_REQUIRE(noexcept(ev_loop::Yield<TestLoop>(std::declval<TestLoop&>()))); }
  SECTION("Hybrid")
  {
    STATIC_REQUIRE(noexcept(ev_loop::Hybrid<TestLoop>(std::declval<TestLoop&>())));
    STATIC_REQUIRE(noexcept(ev_loop::Hybrid<TestLoop>(std::declval<TestLoop&>(), 500)));
  }
}
