#include <chrono>
#include <ev_loop/ev.hpp>
#include <print>
#include <string>
#include <thread>

// =============================================================================
// Define event types
// =============================================================================

struct StartEvent
{
  int id;
};

struct DataEvent
{
  std::string data;
};

struct ProcessedEvent
{
  std::string result;
  int source_id{};
};

struct LogEvent
{
  std::string message;
};

struct ChainEvent
{
  int depth;
};

// =============================================================================
// Receivers for Group 0 (main thread via manual polling)
// =============================================================================

struct Logger
{
  using receives = ev_loop::type_list<LogEvent, ProcessedEvent>;

  // cppcheck-suppress functionStatic
  template<typename Dispatcher> void on_event(const LogEvent& event, Dispatcher& /*dispatcher*/)
  {
    std::println("[LOG] {}", event.message);
  }

  // cppcheck-suppress functionStatic
  template<typename Dispatcher> void on_event(const ProcessedEvent& event, Dispatcher& /*dispatcher*/)
  {
    std::println("[RESULT] Source {}: {}", event.source_id, event.result);
  }
};

struct Controller
{
  using receives = ev_loop::type_list<StartEvent>;
  using emits = ev_loop::type_list<DataEvent, LogEvent>;

  // cppcheck-suppress functionStatic
  template<typename Dispatcher> void on_event(StartEvent event, Dispatcher& dispatcher)
  {
    dispatcher.emit(LogEvent{ "Controller received start event #" + std::to_string(event.id) });
    dispatcher.emit(DataEvent{ "payload_" + std::to_string(event.id) });
  }
};

struct ChainHandler
{
  using receives = ev_loop::type_list<ChainEvent>;
  using emits = ev_loop::type_list<ChainEvent, LogEvent>;

  // cppcheck-suppress functionStatic
  template<typename Dispatcher> void on_event(ChainEvent event, Dispatcher& dispatcher)
  {
    static constexpr int max_depth = 5;

    dispatcher.emit(LogEvent{ "ChainHandler at depth " + std::to_string(event.depth) });
    if (event.depth < max_depth) {
      // This goes through the central queue, not direct recursion!
      dispatcher.emit(ChainEvent{ event.depth + 1 });
    }
  }
};

// =============================================================================
// Receiver for Group 1 (runs on background thread)
// =============================================================================

struct Processor
{
  using receives = ev_loop::type_list<DataEvent>;
  using emits = ev_loop::type_list<ProcessedEvent, LogEvent>;

  int counter = 0;

  template<typename Dispatcher> void on_event(const DataEvent& event, Dispatcher& dispatcher)
  {
    ++counter;
    const std::string result = "processed(" + event.data + ")";
    dispatcher.emit(LogEvent{ "Processor handled: " + event.data });
    dispatcher.emit(ProcessedEvent{ .result = result, .source_id = counter });
  }
};

// =============================================================================
// Main
// =============================================================================

namespace {
constexpr int kThreadedReceiverDelayMs = 50;
} // namespace

// NOLINTNEXTLINE(bugprone-exception-escape)
int main()
{
  std::println("=== Event Loop Demo ===\n");

  // Test 1: Normal event flow with two groups
  std::println("--- Test 1: Normal event flow (2 groups) ---");
  {
    // Group 0: Logger, Controller - polled on main thread
    // Group 1: Processor - runs on background thread
    using Loop = ev_loop::GroupEventLoop<ev_loop::SpinGroup<Logger, Controller>, ev_loop::SpinGroup<Processor>>;

    // Prime events and start
    auto loop = Loop::setup().prime(StartEvent{ 1 }).prime(StartEvent{ 2 }).start_unique();

    // Wait for processing to complete
    std::this_thread::sleep_for(std::chrono::milliseconds(kThreadedReceiverDelayMs));

    loop->stop();
    std::println("Processor handled {} events\n", loop->get<Processor>().counter);
  }

  // Test 2: Chain events (demonstrates queue-based dispatch prevents recursion)
  // Single group to show intra-group event chaining
  std::println("--- Test 2: Chain events (queue prevents recursion) ---");
  {
    using Loop = ev_loop::GroupEventLoop<ev_loop::SpinGroup<Logger, ChainHandler>>;

    // Prime chain event then poll manually
    auto loop = Loop::setup().prime(ChainEvent{ 1 }).create_unique();

    // Each ChainEvent handler emits another ChainEvent via queue
    // Without queue dispatch, this would cause stack recursion
    while (loop->poll_group<0>()) {}
  }

  std::println("\n=== Demo Complete ===");
  return 0;
}
