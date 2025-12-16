// This file should FAIL to compile.
// It tests that tag_type_selector rejects N >= uint32_t max.

#include <cstdint>
#include <ev_loop/ev.hpp>
#include <limits>

int main()
{
  // This should fail to compile with:
  // "Too many event types (max ~4 billion)"
  constexpr std::size_t overflow_value = std::numeric_limits<std::uint32_t>::max();
  [[maybe_unused]] ev_loop::detail::tag_type_t<overflow_value> tag{};
  return 0;
}
