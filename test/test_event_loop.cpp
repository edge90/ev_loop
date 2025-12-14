#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <ev_loop/ev.hpp>
#include <string>
#include <utility>
#include <vector>

namespace {
constexpr int kPingPongLimit = 10;
constexpr int kPingPongExpectedCount = 6;
constexpr int kPingPongLastValue = 11;
constexpr std::size_t kLongStringSize = 1000;
constexpr std::size_t kHybridSpinCount = 10;
}  // namespace

// =============================================================================
// Event types
// =============================================================================

struct PingEvent
{
  int value;
};
struct PongEvent
{
  int value;
};
struct StringEvent
{
  std::string data;
};

// =============================================================================
// Same thread receivers
// =============================================================================

struct PingReceiver
{
  using receives = ev_loop::type_list<PongEvent>;
  using emits = ev_loop::type_list<PingEvent>;
  // cppcheck-suppress unusedStructMember
  static constexpr ev_loop::ThreadMode thread_mode = ev_loop::ThreadMode::SameThread;

  int received_count = 0;
  int last_value = 0;

  template<typename Dispatcher> void on_event(PongEvent event, Dispatcher& dispatcher)
  {
    ++received_count;
    last_value = event.value;
    if (event.value < kPingPongLimit) { dispatcher.emit(PingEvent{ event.value + 1 }); }
  }
};

struct PongReceiver
{
  using receives = ev_loop::type_list<PingEvent>;
  using emits = ev_loop::type_list<PongEvent>;
  // cppcheck-suppress unusedStructMember
  static constexpr ev_loop::ThreadMode thread_mode = ev_loop::ThreadMode::SameThread;

  int received_count = 0;

  template<typename Dispatcher> void on_event(PingEvent event, Dispatcher& dispatcher)
  {
    ++received_count;
    dispatcher.emit(PongEvent{ event.value + 1 });
  }
};

struct StringReceiver
{
  using receives = ev_loop::type_list<StringEvent>;
  // cppcheck-suppress unusedStructMember
  static constexpr ev_loop::ThreadMode thread_mode = ev_loop::ThreadMode::SameThread;

  std::vector<std::string> received;

  template<typename Dispatcher> void on_event(StringEvent event, Dispatcher& /*dispatcher*/)
  {
    received.push_back(std::move(event.data));
  }
};

// =============================================================================
// Same thread tests
// =============================================================================

TEST_CASE("EventLoop ping pong with Spin strategy", "[event_loop][spin]")
{
  ev_loop::EventLoop<PingReceiver, PongReceiver> loop;
  loop.start();

  loop.emit(PingEvent{ 0 });

  while (ev_loop::Spin{ loop }.poll()) {}

  REQUIRE(loop.get<PingReceiver>().received_count == kPingPongExpectedCount);
  REQUIRE(loop.get<PongReceiver>().received_count == kPingPongExpectedCount);
  REQUIRE(loop.get<PingReceiver>().last_value == kPingPongLastValue);

  loop.stop();
}

TEST_CASE("EventLoop ping pong with Yield strategy", "[event_loop][yield]")
{
  ev_loop::EventLoop<PingReceiver, PongReceiver> loop;
  loop.start();

  loop.emit(PingEvent{ 0 });

  while (ev_loop::Yield{ loop }.poll()) {}

  REQUIRE(loop.get<PingReceiver>().received_count == kPingPongExpectedCount);
  REQUIRE(loop.get<PongReceiver>().received_count == kPingPongExpectedCount);
  REQUIRE(loop.get<PingReceiver>().last_value == kPingPongLastValue);

  loop.stop();
}

TEST_CASE("EventLoop ping pong with Hybrid strategy", "[event_loop][hybrid]")
{
  ev_loop::EventLoop<PingReceiver, PongReceiver> loop;
  loop.start();

  loop.emit(PingEvent{ 0 });

  ev_loop::Hybrid strategy{ loop, kHybridSpinCount };
  while (strategy.poll()) {}

  REQUIRE(loop.get<PingReceiver>().received_count == kPingPongExpectedCount);
  REQUIRE(loop.get<PongReceiver>().received_count == kPingPongExpectedCount);
  REQUIRE(loop.get<PingReceiver>().last_value == kPingPongLastValue);

  loop.stop();
}

TEST_CASE("EventLoop string events", "[event_loop]")
{
  constexpr std::size_t kExpectedCount = 3;

  ev_loop::EventLoop<StringReceiver> loop;
  loop.start();

  loop.emit(StringEvent{ "hello" });
  loop.emit(StringEvent{ "world" });
  loop.emit(StringEvent{ std::string(kLongStringSize, 'x') });

  while (ev_loop::Spin{ loop }.poll()) {}

  REQUIRE(loop.get<StringReceiver>().received.size() == kExpectedCount);
  REQUIRE(loop.get<StringReceiver>().received[0] == "hello");
  REQUIRE(loop.get<StringReceiver>().received[1] == "world");
  REQUIRE(loop.get<StringReceiver>().received[2].size() == kLongStringSize);

  loop.stop();
}

// =============================================================================
// Fan-out tests
// =============================================================================

struct FanoutEvent
{
  int value;
};

struct FanoutReceiverA
{
  using receives = ev_loop::type_list<FanoutEvent>;
  // cppcheck-suppress unusedStructMember
  static constexpr ev_loop::ThreadMode thread_mode = ev_loop::ThreadMode::SameThread;
  std::vector<int> values;
  template<typename Dispatcher> void on_event(FanoutEvent event, Dispatcher& /*dispatcher*/)
  {
    values.push_back(event.value);
  }
};

struct FanoutReceiverB
{
  using receives = ev_loop::type_list<FanoutEvent>;
  // cppcheck-suppress unusedStructMember
  static constexpr ev_loop::ThreadMode thread_mode = ev_loop::ThreadMode::SameThread;
  std::vector<int> values;
  template<typename Dispatcher> void on_event(FanoutEvent event, Dispatcher& /*dispatcher*/)
  {
    values.push_back(event.value);
  }
};

struct FanoutReceiverC
{
  using receives = ev_loop::type_list<FanoutEvent>;
  // cppcheck-suppress unusedStructMember
  static constexpr ev_loop::ThreadMode thread_mode = ev_loop::ThreadMode::SameThread;
  std::vector<int> values;
  template<typename Dispatcher> void on_event(FanoutEvent event, Dispatcher& /*dispatcher*/)
  {
    values.push_back(event.value);
  }
};

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("EventLoop fanout", "[event_loop]")
{
  constexpr std::size_t kExpectedCount = 3;

  ev_loop::EventLoop<FanoutReceiverA, FanoutReceiverB, FanoutReceiverC> loop;
  loop.start();

  loop.emit(FanoutEvent{ 1 });
  loop.emit(FanoutEvent{ 2 });
  loop.emit(FanoutEvent{ 3 });

  while (ev_loop::Spin{ loop }.poll()) {}

  loop.stop();

  REQUIRE(loop.get<FanoutReceiverA>().values.size() == kExpectedCount);
  REQUIRE(loop.get<FanoutReceiverB>().values.size() == kExpectedCount);
  REQUIRE(loop.get<FanoutReceiverC>().values.size() == kExpectedCount);

  // Check values match expected sequence [1, 2, 3]
  REQUIRE(loop.get<FanoutReceiverA>().values[0] == 1);
  REQUIRE(loop.get<FanoutReceiverA>().values[1] == 2);
  REQUIRE(loop.get<FanoutReceiverA>().values[2] == 3);
  REQUIRE(loop.get<FanoutReceiverB>().values[0] == 1);
  REQUIRE(loop.get<FanoutReceiverB>().values[1] == 2);
  REQUIRE(loop.get<FanoutReceiverB>().values[2] == 3);
  REQUIRE(loop.get<FanoutReceiverC>().values[0] == 1);
  REQUIRE(loop.get<FanoutReceiverC>().values[1] == 2);
  REQUIRE(loop.get<FanoutReceiverC>().values[2] == 3);
}

