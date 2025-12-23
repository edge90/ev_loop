// Compile-failure test: verifies that InboxBase requires power-of-2 capacity
// This file should FAIL to compile due to static_assert

#include <ev_loop/ev.hpp>

struct TestEvent
{
  int value;
};

// This should fail: capacity 100 is not a power of 2
using BadInbox = ev_loop::detail::SpscInbox<TestEvent, 100>;

int main()
{
  [[maybe_unused]] BadInbox inbox;
  return 0;
}
