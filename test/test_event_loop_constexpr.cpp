#include <catch2/catch_test_macros.hpp>
#include <ev_loop/ev.hpp>
#include <type_traits>
#include <utility>

namespace {

struct ConstexprTestEvent
{
};

struct ConstexprTestReceiver
{
  using receives = ev_loop::type_list<ConstexprTestEvent>;
  using emits = ev_loop::type_list<ConstexprTestEvent>;
  using thread_mode = ev_loop::SameThread;
  template<typename D> static void on_event(ConstexprTestEvent /*unused*/, D& /*unused*/) {}
};

} // namespace

// =============================================================================
// constexpr tests - ref qualifiers and noexcept
// =============================================================================

TEST_CASE("EventLoop::queue is ref-qualified", "[event_loop][constexpr]")
{
  using Loop = ev_loop::EventLoop<ConstexprTestReceiver>;

  SECTION("is callable")
  {
    STATIC_REQUIRE(requires { std::declval<Loop&>().queue(); });
  }
  SECTION("returns correct type")
  {
    STATIC_REQUIRE(std::is_same_v<decltype(std::declval<Loop&>().queue()), typename Loop::queue_type&>);
  }
}

TEST_CASE("Typed dispatcher constructors are noexcept", "[event_loop][constexpr]")
{
  using Loop = ev_loop::EventLoop<ConstexprTestReceiver>;

  SECTION("SameThreadTypedDispatcher")
  {
    STATIC_REQUIRE(noexcept(ev_loop::SameThreadTypedDispatcher<ConstexprTestReceiver, Loop>(std::declval<Loop*>())));
  }
  SECTION("OwnThreadTypedDispatcher")
  {
    STATIC_REQUIRE(noexcept(ev_loop::OwnThreadTypedDispatcher<ConstexprTestReceiver, Loop>(std::declval<Loop*>())));
  }
}
