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
  int source_id;
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
// Same-thread receiver: Logger
// Runs on the event loop thread, receives via queue dispatch
// =============================================================================

struct Logger
{
  using receives = ev::type_list<LogEvent, ProcessedEvent>;
  // cppcheck-suppress unusedStructMember
  static constexpr ev::ThreadMode thread_mode = ev::ThreadMode::SameThread;

  // cppcheck-suppress functionStatic ; on_event must be member function for ev library
  template<typename Dispatcher> void on_event(LogEvent event, Dispatcher& /*dispatcher*/)
  {
    std::println("[LOG] {}", event.message);
  }

  // cppcheck-suppress functionStatic ; on_event must be member function for ev library
  template<typename Dispatcher> void on_event(ProcessedEvent event, Dispatcher& /*dispatcher*/)
  {
    std::println("[RESULT] Source {}: {}", event.source_id, event.result);
  }
};

// =============================================================================
// Same-thread receiver: Controller
// Coordinates the system, emits events to start processing
// =============================================================================

struct Controller
{
  using receives = ev::type_list<StartEvent>;
  using emits = ev::type_list<DataEvent, LogEvent>;
  // cppcheck-suppress unusedStructMember
  static constexpr ev::ThreadMode thread_mode = ev::ThreadMode::SameThread;

  // cppcheck-suppress functionStatic ; on_event must be member function for ev library
  template<typename Dispatcher> void on_event(StartEvent event, Dispatcher& dispatcher)
  {
    dispatcher.emit(LogEvent{ "Controller received start event #" + std::to_string(event.id) });
    dispatcher.emit(DataEvent{ "payload_" + std::to_string(event.id) });
  }
};

// =============================================================================
// Own-thread receiver: Processor
// Runs on its own thread, receives direct pushes from emitters
// =============================================================================

struct Processor
{
  using receives = ev::type_list<DataEvent>;
  using emits = ev::type_list<ProcessedEvent, LogEvent>;
  // cppcheck-suppress unusedStructMember
  static constexpr ev::ThreadMode thread_mode = ev::ThreadMode::OwnThread;

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
// Same-thread receiver: ChainHandler
// Demonstrates that same-thread -> same-thread emission goes through queue
// (preventing stack recursion)
// =============================================================================

struct ChainHandler
{
  using receives = ev::type_list<ChainEvent>;
  using emits = ev::type_list<ChainEvent, LogEvent>;
  // cppcheck-suppress unusedStructMember
  static constexpr ev::ThreadMode thread_mode = ev::ThreadMode::SameThread;

  int max_depth = 5;

  // cppcheck-suppress functionConst ; on_event must be non-const for ev library
  template<typename Dispatcher> void on_event(ChainEvent event, Dispatcher& dispatcher)
  {
    dispatcher.emit(LogEvent{ "ChainHandler at depth " + std::to_string(event.depth) });
    if (event.depth < max_depth) {
      // This goes through the central queue, not direct recursion!
      dispatcher.emit(ChainEvent{ event.depth + 1 });
    }
  }
};

// =============================================================================
// Main
// =============================================================================

namespace {
constexpr int kMaxPollIterations = 100;
constexpr int kThreadedReceiverDelayMs = 50;
}  // namespace

// NOLINTNEXTLINE(bugprone-exception-escape) - std::println may throw but we accept that in examples
int main()
{
  ev::EventLoop<Logger, Controller, Processor, ChainHandler> loop;

  loop.start();

  std::println("=== Event Loop Demo ===\n");

  // Test 1: Normal event flow
  std::println("--- Test 1: Normal event flow ---");
  loop.emit(StartEvent{ 1 });
  loop.emit(StartEvent{ 2 });

  // Process events
  ev::Spin strategy{ loop };
  for (int i = 0; i < kMaxPollIterations && strategy.poll(); ++i) {}

  // Give threaded receiver time
  std::this_thread::sleep_for(std::chrono::milliseconds(kThreadedReceiverDelayMs));
  for (int i = 0; i < kMaxPollIterations && strategy.poll(); ++i) {}

  // Test 2: Chain events (demonstrates queue-based dispatch prevents recursion)
  std::println("\n--- Test 2: Chain events (queue prevents recursion) ---");
  loop.emit(ChainEvent{ 1 });

  // Each ChainEvent handler emits another ChainEvent via queue
  // Without queue dispatch, this would cause stack recursion
  for (int i = 0; i < kMaxPollIterations && strategy.poll(); ++i) {}

  loop.stop();

  std::println("\n=== Demo Complete ===");
  std::println("Processor handled {} events", loop.get<Processor>().counter);

  return 0;
}
