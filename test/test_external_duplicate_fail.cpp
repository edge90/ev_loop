// Compile-failure test: verifies that duplicate ExternalGroup types are rejected
// This file should FAIL to compile due to static_assert

#include <ev_loop/ev.hpp>

struct DummyReceiver
{
  using receives = ev_loop::type_list<int>;
  void on(int /*unused*/) {}
};

struct ExternalInputs
{
  using emits = ev_loop::type_list<int>;
};

// This should fail: duplicate ExternalGroup<ExternalInputs>
using Loop = ev_loop::GroupEventLoop<ev_loop::SpinGroup<DummyReceiver>,
  ev_loop::ExternalGroup<ExternalInputs>,
  ev_loop::ExternalGroup<ExternalInputs>>;

int main()
{
  [[maybe_unused]] auto loop = Loop::setup().create_unique();
  return 0;
}
