#include <catch2/catch_test_macros.hpp>
#include <ev_loop/ev.hpp>
#include <type_traits>
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

} // namespace

// =============================================================================
// constexpr tests - SharedEventLoopPtr operators are ref-qualified
// =============================================================================

TEST_CASE("SharedEventLoopPtr operators are ref-qualified", "[shared_event_loop_ptr][constexpr]")
{
  using Ptr = ev_loop::SharedEventLoopPtr<TestReceiver>;
  using Loop = ev_loop::EventLoop<TestReceiver>;

  SECTION("operator* is callable")
  {
    STATIC_REQUIRE(requires { std::declval<Ptr&>().operator*(); });
    STATIC_REQUIRE(requires { std::declval<const Ptr&>().operator*(); });
  }

  SECTION("operator-> is callable")
  {
    STATIC_REQUIRE(requires { std::declval<Ptr&>().operator->(); });
    STATIC_REQUIRE(requires { std::declval<const Ptr&>().operator->(); });
  }

  SECTION("operator* returns correct type")
  {
    STATIC_REQUIRE(std::is_same_v<decltype(std::declval<Ptr&>().operator*()), Loop&>);
    STATIC_REQUIRE(std::is_same_v<decltype(std::declval<const Ptr&>().operator*()), const Loop&>);
  }

  SECTION("operator-> returns correct type")
  {
    STATIC_REQUIRE(std::is_same_v<decltype(std::declval<Ptr&>().operator->()), Loop*>);
    STATIC_REQUIRE(std::is_same_v<decltype(std::declval<const Ptr&>().operator->()), const Loop*>);
  }
}

TEST_CASE("SharedEventLoopPtr get uses deducing this correctly", "[shared_event_loop_ptr][constexpr]")
{
  using Ptr = ev_loop::SharedEventLoopPtr<TestReceiver>;

  SECTION("mutable")
  {
    STATIC_REQUIRE(requires { std::declval<Ptr&>().template get<TestReceiver>(); });
  }
  SECTION("const")
  {
    STATIC_REQUIRE(requires { std::declval<const Ptr&>().template get<TestReceiver>(); });
  }
}
