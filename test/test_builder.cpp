#include <catch2/catch_test_macros.hpp>
#include <ev_loop/ev.hpp>

struct TestEvent
{
  int value;
};

struct BuilderReceiverA
{
  using receives = ev_loop::type_list<TestEvent>;
  using thread_mode = ev_loop::SameThread;
  int sum = 0;
  template<typename Dispatcher> void on_event(TestEvent event, Dispatcher& /*dispatcher*/) { sum += event.value; }
};

struct BuilderReceiverB
{
  using receives = ev_loop::type_list<TestEvent>;
  using thread_mode = ev_loop::SameThread;
  int sum = 0;
  template<typename Dispatcher> void on_event(TestEvent event, Dispatcher& /*dispatcher*/) { sum += event.value; }
};

namespace {
constexpr int kTestValue = 42;
} // namespace

TEST_CASE("Builder creates working EventLoop", "[builder]")
{
  auto loop = ev_loop::Builder{}.add<BuilderReceiverA>().add<BuilderReceiverB>().build();

  loop.start();
  loop.emit(TestEvent{ kTestValue });
  while (ev_loop::Spin{ loop }.poll()) {}
  loop.stop();

  REQUIRE(loop.get<BuilderReceiverA>().sum == kTestValue);
  REQUIRE(loop.get<BuilderReceiverB>().sum == kTestValue);
}
