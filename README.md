# ev_loop

[![ci](https://github.com/edge90/ev_loop/actions/workflows/ci.yml/badge.svg)](https://github.com/edge90/ev_loop/actions/workflows/ci.yml)
[![codecov](https://codecov.io/gh/edge90/ev_loop/branch/main/graph/badge.svg)](https://codecov.io/gh/edge90/ev_loop)
[![CodeQL](https://github.com/edge90/ev_loop/actions/workflows/codeql-analysis.yml/badge.svg)](https://github.com/edge90/ev_loop/actions/workflows/codeql-analysis.yml)

A high-performance, header-only C++23 event loop library with compile-time event routing and flexible threading models.

## Features

- **Header-only**: Single header `<ev_loop/ev.hpp>`
- **Compile-time event routing**: Zero runtime dispatch overhead for event type resolution
- **Flexible threading**: Receivers can run on the event loop thread (`SameThread`) or their own thread (`OwnThread`)
- **Type-safe**: Events and receivers are validated at compile time
- **Lock-free queues**: SPSC queues for high-throughput inter-thread communication
- **Multiple polling strategies**: Spin, Yield, Wait, and Hybrid strategies
- **Fan-out support**: Single event can be delivered to multiple receivers
- **External event injection**: Thread-safe `ExternalEmitter` for injecting events from outside the loop

## Quick Start

```cpp
#include <ev_loop/ev.hpp>

// Define events as simple structs
struct PingEvent { int value; };
struct PongEvent { int value; };

// Define receivers with type declarations
struct PingReceiver {
  using receives = ev_loop::type_list<PongEvent>;
  using emits = ev_loop::type_list<PingEvent>;
  static constexpr ev_loop::ThreadMode thread_mode = ev_loop::ThreadMode::SameThread;

  template<typename Dispatcher>
  void on_event(PongEvent event, Dispatcher& dispatcher) {
    if (event.value < 10) {
      dispatcher.emit(PingEvent{ event.value + 1 });
    }
  }
};

struct PongReceiver {
  using receives = ev_loop::type_list<PingEvent>;
  using emits = ev_loop::type_list<PongEvent>;
  static constexpr ev_loop::ThreadMode thread_mode = ev_loop::ThreadMode::SameThread;

  template<typename Dispatcher>
  void on_event(PingEvent event, Dispatcher& dispatcher) {
    dispatcher.emit(PongEvent{ event.value + 1 });
  }
};

int main() {
  ev_loop::EventLoop<PingReceiver, PongReceiver> loop;
  loop.start();

  loop.emit(PingEvent{ 0 });

  // Process events until queue is empty
  while (ev_loop::Spin{ loop }.poll()) {}

  loop.stop();
}
```

## Threading Models

### SameThread
Receivers run on the event loop thread. Events are dispatched through a central queue, preventing stack recursion.

### OwnThread
Receivers run on their own dedicated thread with a private SPSC queue for high-throughput scenarios.

```cpp
struct BackgroundProcessor {
  using receives = ev_loop::type_list<DataEvent>;
  static constexpr ev_loop::ThreadMode thread_mode = ev_loop::ThreadMode::OwnThread;

  template<typename Dispatcher>
  void on_event(DataEvent event, Dispatcher& dispatcher) {
    // Runs on dedicated thread
  }
};
```

## Polling Strategies

| Strategy | Description |
|----------|-------------|
| `Spin`   | Busy-wait with CPU pause hints |
| `Yield`  | Yields to scheduler between polls |
| `Wait`   | Blocks until events arrive (uses condition variable) |
| `Hybrid` | Spins for N iterations, then falls back to Wait |

```cpp
// Spin strategy (lowest latency)
ev_loop::Spin{ loop }.run();

// Wait strategy (lowest CPU usage)
ev_loop::Wait{ loop }.run();

// Hybrid strategy (balanced)
ev_loop::Hybrid{ loop, 1000 }.run();  // spin 1000 times before waiting
```

## External Event Injection

Inject events from threads outside the event loop:

```cpp
auto emitter = loop.get_external_emitter();

std::thread producer([&emitter] {
  emitter.emit(MyEvent{ 42 });
});
```

## Requirements

- C++23 compiler (GCC 13+, Clang 17+, MSVC 19.36+)
- CMake 3.21+

## Building

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

## More Details

 * [Dependency Setup](README_dependencies.md)
 * [Building Details](README_building.md)
 * [Docker](README_docker.md)
