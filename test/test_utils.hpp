#pragma once

#include <atomic>
#include <cassert>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <utility>

// =============================================================================
// Waitable mixin for OwnThread receivers (CRTP)
// =============================================================================

template<typename Derived> struct WaitableReceiver
{
  mutable std::mutex mtx;
  mutable std::condition_variable cv;

  template<typename F> void modify_and_notify(F&& modifier) const
  {
    {
      std::lock_guard lock(mtx);
      std::forward<F>(modifier)();
    }
    cv.notify_all();
  }

  template<typename Pred> void wait_until(Pred pred) const
  {
    std::unique_lock lock(mtx);
    cv.wait(lock, pred);
  }

  template<typename Pred, typename Rep, typename Period>
  bool wait_for(Pred pred, std::chrono::duration<Rep, Period> timeout) const
  {
    std::unique_lock lock(mtx);
    return cv.wait_for(lock, timeout, pred);
  }
};

// =============================================================================
// Local counter for isolated tracking per test
// =============================================================================

struct TrackingCounter
{
  // GCOVR_EXCL_START - Inline member initializers not tracked by coverage tools
  // cppcheck-suppress unusedStructMember
  std::atomic<int> constructed_count{ 0 };
  // cppcheck-suppress unusedStructMember
  std::atomic<int> destructed_count{ 0 };
  // cppcheck-suppress unusedStructMember
  std::atomic<int> move_count{ 0 };
  // cppcheck-suppress unusedStructMember
  std::atomic<int> copy_count{ 0 };
  // GCOVR_EXCL_STOP

  [[nodiscard]] bool balanced() const { return constructed_count.load() == destructed_count.load(); }
};

// =============================================================================
// Tracked type with per-instance counter (no global state)
// =============================================================================

template<typename T> struct Tracked
{
  std::shared_ptr<TrackingCounter> counter;
  T value{};

  Tracked() = default;

  // GCOVR_EXCL_START - Defensive null checks for default-constructed objects
  explicit Tracked(std::shared_ptr<TrackingCounter> cnt) : counter(std::move(cnt))
  {
    if (counter) { counter->constructed_count.fetch_add(1, std::memory_order_relaxed); }
  }

  Tracked(std::shared_ptr<TrackingCounter> cnt, T val) : counter(std::move(cnt)), value(std::move(val))
  {
    if (counter) { counter->constructed_count.fetch_add(1, std::memory_order_relaxed); }
  }

  Tracked(const Tracked& other) : counter(other.counter), value(other.value)
  {
    if (counter) {
      counter->constructed_count.fetch_add(1, std::memory_order_relaxed);
      counter->copy_count.fetch_add(1, std::memory_order_relaxed);
    }
  }

  // Intentionally copy counter (not move) so moved-from object tracks destruction
  Tracked(Tracked&& other) noexcept
    : counter(other.counter) // NOLINT(performance-move-constructor-init,cert-oop11-cpp)
      ,
      value(std::move(other.value))
  {
    if (counter) {
      counter->constructed_count.fetch_add(1, std::memory_order_relaxed);
      counter->move_count.fetch_add(1, std::memory_order_relaxed);
    }
  }

  Tracked& operator=(const Tracked& other)
  {
    if (this != &other) {
      if (counter && counter != other.counter) { counter->destructed_count.fetch_add(1, std::memory_order_relaxed); }
      counter = other.counter;
      value = other.value;
      if (counter) { counter->copy_count.fetch_add(1, std::memory_order_relaxed); }
    }
    return *this;
  }

  // Intentionally copy counter (not move) so moved-from object tracks destruction
  Tracked& operator=(Tracked&& other) noexcept
  {
    if (this != &other) {
      if (counter && counter != other.counter) { counter->destructed_count.fetch_add(1, std::memory_order_relaxed); }
      counter = other.counter; // NOLINT(performance-move-const-arg)
      value = std::move(other.value);
      if (counter) { counter->move_count.fetch_add(1, std::memory_order_relaxed); }
    }
    return *this;
  }

  ~Tracked()
  {
    if (counter) { counter->destructed_count.fetch_add(1, std::memory_order_relaxed); }
  }
  // GCOVR_EXCL_STOP

  bool operator==(const Tracked& other) const { return value == other.value; }
};

using TrackedString = Tracked<std::string>;
using TrackedInt = Tracked<int>;
