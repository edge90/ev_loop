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

struct OwnThreadReceiver
{
  using receives = ev_loop::type_list<ConstexprTestEvent>;
  using thread_mode = ev_loop::OwnThread;
  template<typename D> static void on_event(ConstexprTestEvent /*unused*/, D& /*unused*/) {}
};

struct ExternalEmitter
{
  using emits = ev_loop::type_list<ConstexprTestEvent>;
};

struct PlainStruct
{
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

// =============================================================================
// constexpr tests - thread_mode tag types and traits
// =============================================================================

TEST_CASE("Thread mode tag types", "[event_loop][constexpr][thread_mode]")
{
  SECTION("SameThread is empty and trivial")
  {
    STATIC_REQUIRE(std::is_empty_v<ev_loop::SameThread>);
    STATIC_REQUIRE(std::is_trivial_v<ev_loop::SameThread>);
  }

  SECTION("OwnThread is empty and trivial")
  {
    STATIC_REQUIRE(std::is_empty_v<ev_loop::OwnThread>);
    STATIC_REQUIRE(std::is_trivial_v<ev_loop::OwnThread>);
  }
}

TEST_CASE("Thread mode type traits", "[event_loop][constexpr][thread_mode]")
{
  SECTION("has_thread_mode")
  {
    STATIC_REQUIRE(ev_loop::detail::has_thread_mode<ConstexprTestReceiver>);
    STATIC_REQUIRE(ev_loop::detail::has_thread_mode<OwnThreadReceiver>);
    STATIC_REQUIRE_FALSE(ev_loop::detail::has_thread_mode<PlainStruct>);
    STATIC_REQUIRE_FALSE(ev_loop::detail::has_thread_mode<ExternalEmitter>);
  }

  SECTION("is_same_thread_v")
  {
    STATIC_REQUIRE(ev_loop::detail::is_same_thread_v<ConstexprTestReceiver>);
    STATIC_REQUIRE_FALSE(ev_loop::detail::is_same_thread_v<OwnThreadReceiver>);
    STATIC_REQUIRE(ev_loop::detail::is_same_thread_v<PlainStruct>); // defaults to true
  }

  SECTION("is_own_thread_v")
  {
    STATIC_REQUIRE_FALSE(ev_loop::detail::is_own_thread_v<ConstexprTestReceiver>);
    STATIC_REQUIRE(ev_loop::detail::is_own_thread_v<OwnThreadReceiver>);
    STATIC_REQUIRE_FALSE(ev_loop::detail::is_own_thread_v<PlainStruct>); // defaults to false
  }
}

// =============================================================================
// constexpr tests - receiver/emitter concepts and traits
// =============================================================================

TEST_CASE("Receiver and emitter concepts", "[event_loop][constexpr][concepts]")
{
  SECTION("has_receives")
  {
    STATIC_REQUIRE(ev_loop::detail::has_receives<ConstexprTestReceiver>);
    STATIC_REQUIRE(ev_loop::detail::has_receives<OwnThreadReceiver>);
    STATIC_REQUIRE_FALSE(ev_loop::detail::has_receives<ExternalEmitter>);
    STATIC_REQUIRE_FALSE(ev_loop::detail::has_receives<PlainStruct>);
  }

  SECTION("has_emits")
  {
    STATIC_REQUIRE(ev_loop::detail::has_emits<ConstexprTestReceiver>);
    STATIC_REQUIRE_FALSE(ev_loop::detail::has_emits<OwnThreadReceiver>);
    STATIC_REQUIRE(ev_loop::detail::has_emits<ExternalEmitter>);
    STATIC_REQUIRE_FALSE(ev_loop::detail::has_emits<PlainStruct>);
  }

  SECTION("is_receiver")
  {
    STATIC_REQUIRE(ev_loop::detail::is_receiver<ConstexprTestReceiver>);
    STATIC_REQUIRE(ev_loop::detail::is_receiver<OwnThreadReceiver>);
    STATIC_REQUIRE_FALSE(ev_loop::detail::is_receiver<ExternalEmitter>);
    STATIC_REQUIRE_FALSE(ev_loop::detail::is_receiver<PlainStruct>);
  }

  SECTION("is_external_emitter")
  {
    STATIC_REQUIRE_FALSE(ev_loop::detail::is_external_emitter<ConstexprTestReceiver>);
    STATIC_REQUIRE_FALSE(ev_loop::detail::is_external_emitter<OwnThreadReceiver>);
    STATIC_REQUIRE(ev_loop::detail::is_external_emitter<ExternalEmitter>);
    STATIC_REQUIRE_FALSE(ev_loop::detail::is_external_emitter<PlainStruct>);
  }
}

TEST_CASE("Type list extraction traits", "[event_loop][constexpr][traits]")
{
  SECTION("get_receives_t")
  {
    STATIC_REQUIRE(
      std::is_same_v<ev_loop::detail::get_receives_t<ConstexprTestReceiver>, ev_loop::type_list<ConstexprTestEvent>>);
    STATIC_REQUIRE(
      std::is_same_v<ev_loop::detail::get_receives_t<OwnThreadReceiver>, ev_loop::type_list<ConstexprTestEvent>>);
    STATIC_REQUIRE(std::is_same_v<ev_loop::detail::get_receives_t<ExternalEmitter>, ev_loop::type_list<>>);
    STATIC_REQUIRE(std::is_same_v<ev_loop::detail::get_receives_t<PlainStruct>, ev_loop::type_list<>>);
  }

  SECTION("get_emits_t")
  {
    STATIC_REQUIRE(
      std::is_same_v<ev_loop::detail::get_emits_t<ConstexprTestReceiver>, ev_loop::type_list<ConstexprTestEvent>>);
    STATIC_REQUIRE(std::is_same_v<ev_loop::detail::get_emits_t<OwnThreadReceiver>, ev_loop::type_list<>>);
    STATIC_REQUIRE(
      std::is_same_v<ev_loop::detail::get_emits_t<ExternalEmitter>, ev_loop::type_list<ConstexprTestEvent>>);
    STATIC_REQUIRE(std::is_same_v<ev_loop::detail::get_emits_t<PlainStruct>, ev_loop::type_list<>>);
  }
}
