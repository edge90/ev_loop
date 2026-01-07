# ev_loop

[![ci](https://github.com/edge90/ev_loop/actions/workflows/ci.yml/badge.svg)](https://github.com/edge90/ev_loop/actions/workflows/ci.yml)
[![codecov](https://codecov.io/gh/edge90/ev_loop/branch/main/graph/badge.svg)](https://codecov.io/gh/edge90/ev_loop)
[![CodeQL](https://github.com/edge90/ev_loop/actions/workflows/codeql-analysis.yml/badge.svg)](https://github.com/edge90/ev_loop/actions/workflows/codeql-analysis.yml)

A high-performance, header-only C++23 event loop library with compile-time event routing and flexible threading strategies.

## Features

- **Header-only**: Single header `<ev_loop/ev.hpp>`
- **Compile-time event routing**: Zero runtime dispatch overhead
- **Group-based architecture**: Organize receivers into thread groups with different strategies
- **Type-safe**: Events and receivers validated at compile time
- **Lock-free queues**: SPSC/MPSC queues auto-selected based on producer count
- **Multiple strategies**: Spin, Yield, Wait, and Hybrid polling strategies
- **External event injection**: Thread-safe event injection from outside the loop

## Quick Start

```cpp
#include <ev_loop/ev.hpp>

// Define events
struct Ping { int value; };
struct Pong { int value; };

// Define receivers
struct PingReceiver {
  using receives = ev_loop::type_list<Pong>;
  using emits = ev_loop::type_list<Ping>;

  template<typename Dispatcher>
  void on_event(Pong event, Dispatcher& d) {
    if (event.value < 10) d.emit(Ping{ event.value + 1 });
  }
};

struct PongReceiver {
  using receives = ev_loop::type_list<Ping>;
  using emits = ev_loop::type_list<Pong>;

  template<typename Dispatcher>
  void on_event(Ping event, Dispatcher& d) {
    d.emit(Pong{ event.value + 1 });
  }
};

int main() {
  using Loop = ev_loop::GroupEventLoop<
    ev_loop::SpinGroup<PingReceiver, PongReceiver>
  >;

  auto loop = Loop::setup()
    .prime(Ping{ 0 })  // Queue initial event before starting
    .create();

  loop.start();
  // ... events are processed on background thread ...
  loop.stop();
}
```

## Thread Groups

Receivers are organized into groups, each running on its own thread with a polling strategy:

```cpp
using Loop = ev_loop::GroupEventLoop<
  ev_loop::SpinGroup<FastReceiver>,      // Busy-wait (lowest latency)
  ev_loop::WaitGroup<SlowReceiver>,      // Block until events (lowest CPU)
  ev_loop::YieldGroup<MediumReceiver>,   // Yield between polls
  ev_loop::HybridGroup<BalancedReceiver> // Spin N times, then wait
>;
```

### Custom Hybrid Spin Count

```cpp
using FastHybrid = ev_loop::HybridWith<5000>;  // Spin 5000 times before waiting
using Loop = ev_loop::GroupEventLoop<
  ev_loop::ThreadGroup<FastHybrid, MyReceiver>
>;
```

## External Event Injection

Inject events from threads outside the event loop:

```cpp
struct NetworkInputs {
  using emits = ev_loop::type_list<Ping, Pong>;
};

using Loop = ev_loop::GroupEventLoop<
  ev_loop::SpinGroup<MyReceiver>,
  ev_loop::ExternalGroup<NetworkInputs>
>;

auto loop = Loop::setup().create();
auto emitter = loop.get_external_emitter<NetworkInputs>();
loop.start();

// From any thread:
emitter.emit(Ping{ 42 });

// Poll external events (typically in your main loop):
loop.poll_external<NetworkInputs>();
```

## Manual Polling

Run a group on the current thread instead of spawning a background thread:

```cpp
auto loop = Loop::setup().prime(Ping{ 0 }).create();

// Run group 0 on current thread, start others on background threads
loop.run<0>();  // Blocks until stop() is called
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
