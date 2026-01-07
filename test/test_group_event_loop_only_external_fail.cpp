// Compile-failure test: verifies that GroupEventLoop requires at least one ThreadGroup
// This file should FAIL to compile due to static_assert

#include <ev_loop/ev.hpp>

struct ExternalInputs
{
  using emits = ev_loop::type_list<int>;
};

// This should fail: only ExternalGroup, no ThreadGroup
using Loop = ev_loop::GroupEventLoop<ev_loop::ExternalGroup<ExternalInputs>>;

int main()
{
  [[maybe_unused]] auto loop = Loop::setup().create_unique();
  return 0;
}
