// Compile-failure test: verifies that GroupEventLoop requires at least one group
// This file should FAIL to compile due to static_assert

#include <ev_loop/ev.hpp>

// This should fail: no groups provided
using Loop = ev_loop::GroupEventLoop<>;

int main()
{
  [[maybe_unused]] auto loop = Loop::setup().create_unique();
  return 0;
}
