// This file should FAIL to compile.
// It tests that the Builder correctly rejects duplicate receiver types.

#include <ev_loop/ev.hpp>

struct DummyEvent
{
  int value;
};

struct MyReceiver
{
  using receives = ev_loop::type_list<DummyEvent>;
  // cppcheck-suppress unusedStructMember
  [[maybe_unused]] static constexpr ev_loop::ThreadMode thread_mode = ev_loop::ThreadMode::SameThread;
  template<typename D> void on_event(DummyEvent /*unused*/, D& /*unused*/) {}
};

int main()
{
  // This should fail to compile with:
  // "Receiver type is already registered in this Builder"
  auto loop = ev_loop::Builder{}.add<MyReceiver>().add<MyReceiver>().build();

  (void)loop;
  return 0;
}
