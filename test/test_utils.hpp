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
  // cppcheck-suppress unusedStructMember
  std::atomic<int> constructed_count{ 0 };
  // cppcheck-suppress unusedStructMember
  std::atomic<int> destructed_count{ 0 };
  // cppcheck-suppress unusedStructMember
  std::atomic<int> move_count{ 0 };
  // cppcheck-suppress unusedStructMember
  std::atomic<int> copy_count{ 0 };

  [[nodiscard]] bool balanced() const { return constructed_count.load() == destructed_count.load(); }
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
    counter->constructed_count.fetch_add(1, std::memory_order_relaxed);
  }

  TrackedString(std::shared_ptr<TrackingCounter> cnt, std::string str) : counter(std::move(cnt)), value(std::move(str))
  {
    assert(counter);
    counter->constructed_count.fetch_add(1, std::memory_order_relaxed);
  }

  TrackedString(const TrackedString& other) : counter(other.counter), value(other.value)
  {
    assert(counter);
    counter->constructed_count.fetch_add(1, std::memory_order_relaxed);
    counter->copy_count.fetch_add(1, std::memory_order_relaxed);
  }

  // Intentionally copy counter (not move) so moved-from object tracks destruction
  TrackedString(TrackedString&& other) noexcept
    : counter(other.counter) // NOLINT(performance-move-constructor-init, cert-oop11-cpp)
      ,
      value(std::move(other.value))
  {
    assert(counter);
    counter->constructed_count.fetch_add(1, std::memory_order_relaxed);
    counter->move_count.fetch_add(1, std::memory_order_relaxed);
  }

  TrackedString& operator=(const TrackedString& other)
  {
    if (this != &other) {
      assert(counter && other.counter);
      if (counter != other.counter) { counter->destructed_count.fetch_add(1, std::memory_order_relaxed); }
      counter = other.counter;
      value = other.value;
      counter->copy_count.fetch_add(1, std::memory_order_relaxed);
    }
    return *this;
  }

  // Intentionally copy counter (not move) so moved-from object tracks destruction
  TrackedString& operator=(TrackedString&& other) noexcept
  {
    if (this != &other) {
      assert(counter && other.counter);
      if (counter != other.counter) { counter->destructed_count.fetch_add(1, std::memory_order_relaxed); }
      counter = other.counter; // NOLINT(performance-move-const-arg)
      value = std::move(other.value);
      counter->move_count.fetch_add(1, std::memory_order_relaxed);
    }
    return *this;
  }

  ~TrackedString()
  {
    assert(counter);
    counter->destructed_count.fetch_add(1, std::memory_order_relaxed);
  }

  bool operator==(const TrackedString& other) const { return value == other.value; }
};
