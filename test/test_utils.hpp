#pragma once

#include <string>
#include <utility>

// =============================================================================
// Tracked type for counting copies/moves
// =============================================================================

// Use inline to ensure single definitions across translation units
inline int constructed_count = 0;
inline int destructed_count = 0;
inline int move_count = 0;
inline int copy_count = 0;

inline void reset_tracking()
{
  constructed_count = 0;
  destructed_count = 0;
  move_count = 0;
  copy_count = 0;
}

struct TrackedString
{
  std::string value;

  TrackedString() { ++constructed_count; }
  explicit TrackedString(std::string str) : value(std::move(str)) { ++constructed_count; }
  TrackedString(const TrackedString& other) : value(other.value)
  {
    ++constructed_count;
    ++copy_count;
  }
  TrackedString(TrackedString&& other) noexcept : value(std::move(other.value))
  {
    ++constructed_count;
    ++move_count;
  }
  TrackedString& operator=(const TrackedString& other)
  {
    if (this != &other) {
      value = other.value;
      ++copy_count;
    }
    return *this;
  }
  TrackedString& operator=(TrackedString&& other) noexcept
  {
    if (this != &other) {
      value = std::move(other.value);
      ++move_count;
    }
    return *this;
  }
  ~TrackedString() { ++destructed_count; }

  bool operator==(const TrackedString& other) const { return value == other.value; }
};
