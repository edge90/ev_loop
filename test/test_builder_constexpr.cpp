#include <catch2/catch_test_macros.hpp>
#include <ev_loop/ev.hpp>
#include <type_traits>

namespace {

struct TestEvent
{
};

struct ReceiverA
{
  using receives = ev_loop::type_list<TestEvent>;
  using thread_mode = ev_loop::SameThread;
  template<typename D> static void on_event(TestEvent /*unused*/, D& /*unused*/) {}
};

struct ReceiverB
{
  using receives = ev_loop::type_list<TestEvent>;
  using thread_mode = ev_loop::SameThread;
  template<typename D> static void on_event(TestEvent /*unused*/, D& /*unused*/) {}
};

struct ReceiverC
{
  using receives = ev_loop::type_list<TestEvent>;
  using thread_mode = ev_loop::OwnThread;
  template<typename D> static void on_event(TestEvent /*unused*/, D& /*unused*/) {}
};

} // namespace

// =============================================================================
// constexpr tests - Builder type deduction
// =============================================================================

TEST_CASE("Builder::build returns correct EventLoop type", "[builder][constexpr]")
{
  SECTION("single receiver")
  {
    using BuilderType = ev_loop::Builder<ReceiverA>;
    STATIC_REQUIRE(std::is_same_v<typename BuilderType::loop_type, ev_loop::EventLoop<ReceiverA>>);
  }

  SECTION("multiple receivers")
  {
    using BuilderType = ev_loop::Builder<ReceiverA, ReceiverB>;
    STATIC_REQUIRE(std::is_same_v<typename BuilderType::loop_type, ev_loop::EventLoop<ReceiverA, ReceiverB>>);
  }

  SECTION("mixed thread modes")
  {
    using BuilderType = ev_loop::Builder<ReceiverA, ReceiverC>;
    STATIC_REQUIRE(std::is_same_v<typename BuilderType::loop_type, ev_loop::EventLoop<ReceiverA, ReceiverC>>);
  }
}

TEST_CASE("Builder::add returns correct Builder type", "[builder][constexpr]")
{
  SECTION("adding first receiver")
  {
    using ResultType = decltype(ev_loop::Builder{}.add<ReceiverA>());
    STATIC_REQUIRE(std::is_same_v<ResultType, ev_loop::Builder<ReceiverA>>);
  }

  SECTION("adding second receiver")
  {
    using ResultType = decltype(ev_loop::Builder<ReceiverA>{}.add<ReceiverB>());
    STATIC_REQUIRE(std::is_same_v<ResultType, ev_loop::Builder<ReceiverA, ReceiverB>>);
  }

  SECTION("chained adds")
  {
    using ResultType = decltype(ev_loop::Builder{}.add<ReceiverA>().add<ReceiverB>().add<ReceiverC>());
    STATIC_REQUIRE(std::is_same_v<ResultType, ev_loop::Builder<ReceiverA, ReceiverB, ReceiverC>>);
  }
}

TEST_CASE("Builder preserves receiver order", "[builder][constexpr]")
{
  using BuilderABC = decltype(ev_loop::Builder{}.add<ReceiverA>().add<ReceiverB>().add<ReceiverC>());
  using BuilderCBA = decltype(ev_loop::Builder{}.add<ReceiverC>().add<ReceiverB>().add<ReceiverA>());

  STATIC_REQUIRE(std::is_same_v<typename BuilderABC::loop_type, ev_loop::EventLoop<ReceiverA, ReceiverB, ReceiverC>>);
  STATIC_REQUIRE(std::is_same_v<typename BuilderCBA::loop_type, ev_loop::EventLoop<ReceiverC, ReceiverB, ReceiverA>>);
  STATIC_REQUIRE_FALSE(std::is_same_v<typename BuilderABC::loop_type, typename BuilderCBA::loop_type>);
}
