#pragma once

#include <cassert>
#include <memory>
#include <string>
#include <utility>

// =============================================================================
// Local counter for isolated tracking per test
// =============================================================================

struct TrackingCounter
{
  // cppcheck-suppress unusedStructMember
  int constructed_count = 0;
  // cppcheck-suppress unusedStructMember
  int destructed_count = 0;
  // cppcheck-suppress unusedStructMember
  int move_count = 0;
  // cppcheck-suppress unusedStructMember
  int copy_count = 0;

  [[nodiscard]] bool balanced() const { return constructed_count == destructed_count; }
};

// =============================================================================
// Tracked type with per-instance counter (no global state)
// =============================================================================

struct TrackedString
{
  std::shared_ptr<TrackingCounter> counter;
  std::string value;

  TrackedString() = default;

  explicit TrackedString(std::shared_ptr<TrackingCounter> cnt) : counter(std::move(cnt))
  {
    assert(counter);
    ++counter->constructed_count;
  }

  TrackedString(std::shared_ptr<TrackingCounter> cnt, std::string str) : counter(std::move(cnt)), value(std::move(str))
  {
    assert(counter);
    ++counter->constructed_count;
  }

  TrackedString(const TrackedString& other) : counter(other.counter), value(other.value)
  {
    assert(counter);
    ++counter->constructed_count;
    ++counter->copy_count;
  }

  // Intentionally copy counter (not move) so moved-from object tracks destruction
  TrackedString(TrackedString&& other) noexcept
    : counter(other.counter) // NOLINT(performance-move-constructor-init, cert-oop11-cpp)
      ,
      value(std::move(other.value))
  {
    assert(counter);
    ++counter->constructed_count;
    ++counter->move_count;
  }

  TrackedString& operator=(const TrackedString& other)
  {
    if (this != &other) {
      assert(counter && other.counter);
      if (counter != other.counter) { ++counter->destructed_count; }
      counter = other.counter;
      value = other.value;
      ++counter->copy_count;
    }
    return *this;
  }

  // Intentionally copy counter (not move) so moved-from object tracks destruction
  TrackedString& operator=(TrackedString&& other) noexcept
  {
    if (this != &other) {
      assert(counter && other.counter);
      if (counter != other.counter) { ++counter->destructed_count; }
      counter = other.counter; // NOLINT(performance-move-const-arg)
      value = std::move(other.value);
      ++counter->move_count;
    }
    return *this;
  }

  ~TrackedString()
  {
    assert(counter);
    ++counter->destructed_count;
  }

  bool operator==(const TrackedString& other) const { return value == other.value; }
};
