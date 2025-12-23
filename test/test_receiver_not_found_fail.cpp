// Compile-failure test: verifies that get<Receiver>() rejects unknown receivers
// This file should FAIL to compile due to static_assert

#include <ev_loop/ev.hpp>

struct RegisteredReceiver
{
  using receives = ev_loop::type_list<int>;
  void on(int) {}
};

struct UnregisteredReceiver
{
  using receives = ev_loop::type_list<int>;
  void on(int) {}
};

using Loop = ev_loop::GroupEventLoop<ev_loop::SpinGroup<RegisteredReceiver>>;

int main()
{
  auto loop = Loop::setup().create_unique();
  // This should fail: UnregisteredReceiver is not in the loop
  [[maybe_unused]] auto& receiver = loop->get<UnregisteredReceiver>();
  return 0;
}
