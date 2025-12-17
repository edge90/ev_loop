// NOLINTBEGIN(misc-include-cleaner)
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <ev_loop/ev.hpp>
#include <string>
#include <utility>
#include <vector>
// NOLINTEND(misc-include-cleaner)

namespace {
constexpr int kPingPongLimit = 10;
constexpr int kPingPongExpectedCount = 6;
constexpr int kPingPongLastValue = 11;
constexpr std::size_t kLongStringSize = 1000;
constexpr std::size_t kHybridSpinCount = 10;
} // namespace

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
  using thread_mode = ev_loop::SameThread;

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
  using thread_mode = ev_loop::SameThread;

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
  using thread_mode = ev_loop::SameThread;

  std::vector<std::string> received;

  template<typename Dispatcher> void on_event(StringEvent event, Dispatcher& /*dispatcher*/)
  {
    received.push_back(std::move(event.data));
  }
};

// =============================================================================
// Same thread tests
// =============================================================================

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("EventLoop ping pong", "[event_loop]")
{
  ev_loop::EventLoop<PingReceiver, PongReceiver> loop;
  loop.start();

  SECTION("Spin strategy")
  {
    // Verify poll returns false when queue is empty
    REQUIRE_FALSE(ev_loop::Spin{ loop }.poll());

    loop.emit(PingEvent{ 0 });

    // Verify poll returns true when events are pending
    REQUIRE(ev_loop::Spin{ loop }.poll());

    // Process remaining events
    while (ev_loop::Spin{ loop }.poll()) {}
  }

  SECTION("Yield strategy")
  {
    // Verify poll returns false when queue is empty
    REQUIRE_FALSE(ev_loop::Yield{ loop }.poll());

    loop.emit(PingEvent{ 0 });

    // Verify poll returns true when events are pending
    REQUIRE(ev_loop::Yield{ loop }.poll());

    // Process remaining events
    while (ev_loop::Yield{ loop }.poll()) {}
  }

  SECTION("Hybrid strategy")
  {
    ev_loop::Hybrid strategy{ loop, kHybridSpinCount };

    // Verify poll returns false when queue is empty
    REQUIRE_FALSE(strategy.poll());

    loop.emit(PingEvent{ 0 });

    // Verify poll returns true when events are pending
    REQUIRE(strategy.poll());

    // Process remaining events
    while (strategy.poll()) {}
  }

  REQUIRE(loop.get<PingReceiver>().received_count == kPingPongExpectedCount);
  REQUIRE(loop.get<PongReceiver>().received_count == kPingPongExpectedCount);
  REQUIRE(loop.get<PingReceiver>().last_value == kPingPongLastValue);

  loop.stop();
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_CASE("EventLoop string events", "[event_loop]")
{
  constexpr std::size_t kExpectedCount = 3;

  ev_loop::EventLoop<StringReceiver> loop;
  loop.start();

  loop.emit(StringEvent{ "hello" });
  loop.emit(StringEvent{ "world" });
  loop.emit(StringEvent{ std::string(kLongStringSize, 'x') });

  for (std::size_t i = 0; i < kExpectedCount; ++i) { REQUIRE(ev_loop::Spin{ loop }.poll()); }
  REQUIRE_FALSE(ev_loop::Spin{ loop }.poll());

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
  using thread_mode = ev_loop::SameThread;
  std::vector<int> values;
  template<typename Dispatcher> void on_event(FanoutEvent event, Dispatcher& /*dispatcher*/)
  {
    values.push_back(event.value);
  }
};

struct FanoutReceiverB
{
  using receives = ev_loop::type_list<FanoutEvent>;
  using thread_mode = ev_loop::SameThread;
  std::vector<int> values;
  template<typename Dispatcher> void on_event(FanoutEvent event, Dispatcher& /*dispatcher*/)
  {
    values.push_back(event.value);
  }
};

struct FanoutReceiverC
{
  using receives = ev_loop::type_list<FanoutEvent>;
  using thread_mode = ev_loop::SameThread;
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

  for (std::size_t i = 0; i < kExpectedCount; ++i) { REQUIRE(ev_loop::Spin{ loop }.poll()); }
  REQUIRE_FALSE(ev_loop::Spin{ loop }.poll());

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
