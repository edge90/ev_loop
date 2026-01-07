// Compile-failure test: verifies that get_external_emitter rejects unknown types
// This file should FAIL to compile due to static_assert

#include <ev_loop/ev.hpp>

struct DummyReceiver
{
  using receives = ev_loop::type_list<int>;
  void on(int /*unused*/) {}
};

struct RegisteredInputs
{
  using emits = ev_loop::type_list<int>;
};

struct UnregisteredInputs
{
  using emits = ev_loop::type_list<int>;
};

using Loop = ev_loop::GroupEventLoop<ev_loop::SpinGroup<DummyReceiver>, ev_loop::ExternalGroup<RegisteredInputs>>;

int main()
{
  auto loop = Loop::setup().create_unique();
  // This should fail: UnregisteredInputs is not in the loop's ExternalGroups
  [[maybe_unused]] auto emitter = loop->get_external_emitter<UnregisteredInputs>();
  return 0;
}
