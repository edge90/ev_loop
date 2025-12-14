#include <cstddef>
#include <cstdint>

// TODO: Implement fuzz testing for ev_loop
// cppcheck-suppress unusedFunction symbolName=LLVMFuzzerTestOneInput
extern "C" int LLVMFuzzerTestOneInput(const uint8_t * /*Data*/, size_t /*Size*/)
{
  return 0;
}
