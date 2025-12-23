// Compile-failure test: verifies that ExternalGroup<T> requires T to define emits
// This file should FAIL to compile

#include <ev_loop/ev.hpp>

struct TestReceiver
{
  using receives = ev_loop::type_list<>;
  using emits = ev_loop::type_list<>;
  template<typename D> void on_event(int /*unused*/, D& /*unused*/) {}
};

// This type does NOT define emits, should fail when used with ExternalGroup
struct BadExternalType
{
  // Missing: using emits = ev_loop::type_list<...>;
};

// This should fail: ExternalGroup requires T::emits
using Loop = ev_loop::GroupEventLoop<ev_loop::SpinGroup<TestReceiver>, ev_loop::ExternalGroup<BadExternalType>>;

int main()
{
  [[maybe_unused]] auto loop = Loop::setup().create_unique();
  return 0;
}
