#include <catch2/catch_test_macros.hpp>
#include <ev_loop/ev.hpp>
#include <memory>
#include <utility>

namespace {

struct TestEvent
{
  int value;
};

struct SameThreadReceiver
{
  using receives = ev_loop::type_list<TestEvent>;
  // cppcheck-suppress unusedStructMember
  [[maybe_unused]] static constexpr ev_loop::ThreadMode thread_mode = ev_loop::ThreadMode::SameThread;
  template<typename D> void on_event(TestEvent /*unused*/, D& /*unused*/) {}
};

struct TestExternalEmitter
{
  using emits = ev_loop::type_list<TestEvent>;
};

} // namespace

// =============================================================================
// constexpr tests - TypedExternalEmitter noexcept specifications
// =============================================================================

TEST_CASE("TypedExternalEmitter constructor is noexcept", "[external_emitter][constexpr]")
{
  using Loop = ev_loop::EventLoop<SameThreadReceiver, TestExternalEmitter>;

  STATIC_REQUIRE(
    noexcept(ev_loop::TypedExternalEmitter<TestExternalEmitter, Loop>(std::declval<std::shared_ptr<Loop>>())));
}

TEST_CASE("TypedExternalEmitter::is_valid is noexcept", "[external_emitter][constexpr]")
{
  using Loop = ev_loop::EventLoop<SameThreadReceiver, TestExternalEmitter>;
  using Emitter = ev_loop::TypedExternalEmitter<TestExternalEmitter, Loop>;

  STATIC_REQUIRE(noexcept(std::declval<const Emitter&>().is_valid()));
}
