// Compile-failure test: verifies that GroupEventLoop rejects invalid group types
// This file should FAIL to compile due to static_assert

#include <ev_loop/ev.hpp>

struct NotAGroup
{
};

struct DummyReceiver
{
  using receives = ev_loop::type_list<int>;
  void on(int /*unused*/) {}
};

// This should fail: NotAGroup is not a ThreadGroup or ExternalGroup
using Loop = ev_loop::GroupEventLoop<ev_loop::SpinGroup<DummyReceiver>, NotAGroup>;

int main()
{
  [[maybe_unused]] auto loop = Loop::setup().create_unique();
  return 0;
}
