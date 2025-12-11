#include <catch2/catch_test_macros.hpp>
#include <ev_loop/ev.hpp>
#include <type_traits>

struct TestEvent
{
  int value;
};

struct BuilderReceiverA
{
  using receives = ev::type_list<TestEvent>;
  // cppcheck-suppress unusedStructMember
  static constexpr ev::ThreadMode thread_mode = ev::ThreadMode::SameThread;
  int sum = 0;
  // cppcheck-suppress functionStatic ; on_event must be member function for ev library
  template<typename Dispatcher> void on_event(TestEvent event, Dispatcher& /*dispatcher*/) { sum += event.value; }
};

struct BuilderReceiverB
{
  using receives = ev::type_list<TestEvent>;
  // cppcheck-suppress unusedStructMember
  static constexpr ev::ThreadMode thread_mode = ev::ThreadMode::SameThread;
  int sum = 0;
  // cppcheck-suppress functionStatic ; on_event must be member function for ev library
  template<typename Dispatcher> void on_event(TestEvent event, Dispatcher& /*dispatcher*/) { sum += event.value; }
};

namespace {
constexpr int kTestValue = 42;
}  // namespace

TEST_CASE("Builder", "[builder]")
{
  SECTION("unique receivers")
  {
    auto loop = ev::Builder{}.add<BuilderReceiverA>().add<BuilderReceiverB>().build();

    loop.start();
    loop.emit(TestEvent{ kTestValue });
    while (ev::Spin{ loop }.poll()) {}
    loop.stop();

    static_assert(std::is_same_v<decltype(loop), ev::EventLoop<BuilderReceiverA, BuilderReceiverB>>);
  }

  SECTION("loop_type alias")
  {
    using MyBuilder = ev::Builder<BuilderReceiverA, BuilderReceiverB>;
    static_assert(std::is_same_v<MyBuilder::loop_type, ev::EventLoop<BuilderReceiverA, BuilderReceiverB>>);
  }
}