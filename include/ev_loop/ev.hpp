#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <concepts>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <tuple>
#include <type_traits>
#include <utility>

namespace ev_loop {

// =============================================================================
// Portable CPU pause hint for spin loops
// =============================================================================

// LCOV_EXCL_START - inline assembly not trackable by coverage tools
inline void cpu_pause() noexcept
{
// NOLINTBEGIN(readability-use-concise-preprocessor-directives) - multi-condition checks
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#ifdef _MSC_VER
  _mm_pause();
#else
  __builtin_ia32_pause();
#endif
#elif defined(__aarch64__) || defined(_M_ARM64)
#ifdef _MSC_VER
  __yield();
#else
  __asm__ volatile("yield" ::: "memory");
#endif
#else
  // Fallback: no-op or minimal delay
  std::atomic_signal_fence(std::memory_order_seq_cst);
#endif
  // NOLINTEND(readability-use-concise-preprocessor-directives)
}
// LCOV_EXCL_STOP

// =============================================================================
// Type list utilities for compile-time event registration
// =============================================================================

template<typename... Ts> struct type_list
{
  static constexpr std::size_t size = sizeof...(Ts);
};

template<typename List, typename T> struct contains : std::false_type
{
};

// Use fold expression instead of std::disjunction for fewer template instantiations
template<typename T, typename... Ts>
struct contains<type_list<Ts...>, T> : std::bool_constant<(std::is_same_v<T, Ts> || ...)>
{
};

template<typename List, typename T> inline constexpr bool contains_v = contains<List, T>::value;

// Type list index lookup using O(1) fold expression
template<typename T, typename... Ts> struct index_of
{
private:
  template<std::size_t... Is> static consteval std::size_t find(std::index_sequence<Is...> /*unused*/)
  {
    std::size_t result = sizeof...(Ts); // Invalid index if not found
    // NOLINTNEXTLINE(readability-simplify-boolean-expr)
    (void)((std::is_same_v<T, Ts> ? (result = Is, true) : false) || ...);
    return result;
  }

public:
  static constexpr std::size_t value = find(std::make_index_sequence<sizeof...(Ts)>{});
};

template<typename T, typename... Ts> inline constexpr std::size_t index_of_v = index_of<T, Ts...>::value;

// Type at index
template<std::size_t I, typename... Ts> using type_at_t = std::tuple_element_t<I, std::tuple<Ts...>>;

// Check if two type_lists have any overlapping types
template<typename List1, typename List2> struct lists_overlap;

template<typename... Ts, typename List2>
struct lists_overlap<type_list<Ts...>, List2> : std::bool_constant<(contains_v<List2, Ts> || ...)>
{
};

template<typename List2> struct lists_overlap<type_list<>, List2> : std::false_type
{
};

template<typename List1, typename List2> inline constexpr bool lists_overlap_v = lists_overlap<List1, List2>::value;

// Filter type list by predicate
template<template<typename> class Pred, typename Result, typename... Ts> struct filter_impl;

template<template<typename> class Pred, typename... Rs> struct filter_impl<Pred, type_list<Rs...>>
{
  using type = type_list<Rs...>;
};

template<template<typename> class Pred, typename... Rs, typename T, typename... Ts>
struct filter_impl<Pred, type_list<Rs...>, T, Ts...>
{
  using type =
    typename filter_impl<Pred, std::conditional_t<Pred<T>::value, type_list<Rs..., T>, type_list<Rs...>>, Ts...>::type;
};

template<template<typename> class Pred, typename... Ts>
using filter_t = typename filter_impl<Pred, type_list<>, Ts...>::type;

// =============================================================================
// Tagged union (faster than std::variant)
// =============================================================================

template<std::size_t... Vs> consteval std::size_t const_max()
{
  std::size_t result = 0;
  ((result = Vs > result ? Vs : result), ...);
  return result;
}

// Select smallest unsigned type that can hold N event types + 1 (for uninitialized)
template<std::size_t N> struct tag_type_selector
{
  static_assert(N < std::numeric_limits<std::uint32_t>::max(), "Too many event types (max ~4 billion)");
  using type = std::conditional_t < N < std::numeric_limits<std::uint8_t>::max(), std::uint8_t,
        std::conditional_t<N<std::numeric_limits<std::uint16_t>::max(), std::uint16_t, std::uint32_t>>;
};

template<std::size_t N> using tag_type_t = typename tag_type_selector<N>::type;

// NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init) - storage is raw bytes, initialized via placement new
template<typename... Events> class TaggedEvent
{
  using tag_type = tag_type_t<sizeof...(Events)>;
  static constexpr tag_type uninitialized_tag = std::numeric_limits<tag_type>::max();
  static constexpr std::size_t storage_size = sizeof...(Events) == 0 ? 1 : const_max<sizeof(Events)...>();
  static constexpr std::size_t storage_align = sizeof...(Events) == 0 ? 1 : const_max<alignof(Events)...>();
  static constexpr bool all_trivial = (std::is_trivially_copyable_v<Events> && ...);

  // MSVC C4324: structure was padded due to alignment specifier
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4324)
#endif
  // NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays)
  alignas(storage_align) std::array<std::byte, storage_size> storage;
#ifdef _MSC_VER
#pragma warning(pop)
#endif
  tag_type tag = uninitialized_tag;

public:
  TaggedEvent() = default;

  ~TaggedEvent() { destroy(); }

  // cppcheck-suppress missingMemberCopy ; storage is initialized via placement new in copy_construct_from
  TaggedEvent(const TaggedEvent& other) : tag(other.tag)
  {
    if (tag != uninitialized_tag) { copy_construct_from(other); }
  }

  // cppcheck-suppress missingMemberCopy ; storage is initialized via placement new in move_construct_from
  TaggedEvent(TaggedEvent&& other) noexcept : tag(other.tag)
  {
    if (tag != uninitialized_tag) {
      move_construct_from(std::move(other));
      other.destroy(); // cppcheck-suppress accessMoved ; other.destroy() only resets tag, the moved-from object is in
                       // valid state
    }
  }

  // cppcheck-suppress operatorEqVarError ; storage is initialized via placement new in copy_construct_from
  TaggedEvent& operator=(const TaggedEvent& other)
  {
    if (this != &other) {
      destroy();
      tag = other.tag;
      if (tag != uninitialized_tag) { copy_construct_from(other); }
    }
    return *this;
  }

  // cppcheck-suppress operatorEqVarError ; storage is initialized via placement new in move_construct_from
  TaggedEvent& operator=(TaggedEvent&& other) noexcept
  {
    if (this != &other) {
      destroy();
      tag = other.tag;
      if (tag != uninitialized_tag) {
        move_construct_from(std::move(other));
        other.destroy(); // cppcheck-suppress accessMoved ; other.destroy() only resets tag, the moved-from object is in
                         // valid state
      }
    }
    return *this;
  }

  template<typename E>
    requires(contains_v<type_list<Events...>, std::decay_t<E>>)
  explicit TaggedEvent(E&& event)
  {
    store(std::forward<E>(event));
  }

  template<typename E> void store(E&& event)
  {
    destroy();
    using Decayed = std::decay_t<E>;
    tag = index_of_v<Decayed, Events...>;
    new (storage.data()) Decayed(std::forward<E>(event));
  }

  // cppcheck-suppress functionStatic ; explicit object parameter functions cannot be static
  template<std::size_t I, typename Self> auto& get(this Self& self)
  {
    using Base = type_at_t<I, Events...>;
    using T = std::conditional_t<std::is_const_v<Self>, const Base, Base>;
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    return *reinterpret_cast<T*>(self.storage.data());
  }

  [[nodiscard]] std::size_t index() const noexcept { return tag; }

private:
  void destroy()
  {
    if constexpr (!all_trivial) {
      if (tag != uninitialized_tag) { destroy_at_index(std::make_index_sequence<sizeof...(Events)>{}); }
    }
    tag = uninitialized_tag;
  }

  template<std::size_t... Is> void destroy_at_index(std::index_sequence<Is...> /*unused*/)
  {
    using destroy_fn = void (TaggedEvent::*)();
    static constexpr std::array<destroy_fn, sizeof...(Is)> table = { &TaggedEvent::destroy_type<Is>... };
    (this->*table[tag])();
  }

  template<std::size_t I> void destroy_type()
  {
    using T = type_at_t<I, Events...>;
    if constexpr (!std::is_trivially_destructible_v<T>) {
      // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
      reinterpret_cast<T*>(storage.data())->~T();
    }
  }

  void copy_construct_from(const TaggedEvent& other)
  {
    copy_at_index(other, std::make_index_sequence<sizeof...(Events)>{});
  }

  template<std::size_t... Is> void copy_at_index(const TaggedEvent& other, std::index_sequence<Is...> /*unused*/)
  {
    using copy_fn = void (TaggedEvent::*)(const TaggedEvent&);
    static constexpr std::array<copy_fn, sizeof...(Is)> table = { &TaggedEvent::copy_type<Is>... };
    (this->*table[tag])(other);
  }

  template<std::size_t I> void copy_type(const TaggedEvent& other)
  {
    using T = type_at_t<I, Events...>;
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    new (storage.data()) T(*reinterpret_cast<const T*>(other.storage.data()));
  }

  void move_construct_from(TaggedEvent&& other)
  {
    move_at_index(std::move(other), std::make_index_sequence<sizeof...(Events)>{});
  }

  template<std::size_t... Is> void move_at_index(TaggedEvent&& other, std::index_sequence<Is...> /*unused*/)
  {
    using move_fn = void (TaggedEvent::*)(TaggedEvent&&);
    static constexpr std::array<move_fn, sizeof...(Is)> table = { &TaggedEvent::move_type<Is>... };
    (this->*table[tag])(std::move(other));
  }

  // NOLINTNEXTLINE(cppcoreguidelines-rvalue-reference-param-not-moved)
  template<std::size_t I> void move_type(TaggedEvent&& other)
  {
    using T = type_at_t<I, Events...>;
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    new (storage.data()) T(std::move(*reinterpret_cast<T*>(other.storage.data())));
  }
};

// Convert type_list to tagged event
template<typename List> struct to_tagged_event;

template<typename... Ts> struct to_tagged_event<type_list<Ts...>>
{
  using type = TaggedEvent<Ts...>;
};

template<typename List> using to_tagged_event_t = typename to_tagged_event<List>::type;

// Fast tagged event dispatch using lookup table
template<typename Tagged, typename Func, std::size_t... Is>
// NOLINTNEXTLINE(cppcoreguidelines-missing-std-forward)
constexpr void fast_dispatch_impl(Tagged& tagged, Func&& func, std::index_sequence<Is...> /*unused*/)
{
  using dispatch_fn = void (*)(Tagged&, Func&&);
  static constexpr std::array<dispatch_fn, sizeof...(Is)> table = { +[](Tagged& t, Func&& f) {
    f(t.template get<Is>());
  }... };
  table[tagged.index()](tagged, std::forward<Func>(func));
}

template<typename... Events, typename Func> constexpr void fast_dispatch(TaggedEvent<Events...>& tagged, Func&& func)
{
  fast_dispatch_impl(tagged, std::forward<Func>(func), std::make_index_sequence<sizeof...(Events)>{});
}

// =============================================================================
// Ring buffer (faster than std::queue)
// =============================================================================

// NOLINTNEXTLINE(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)
template<typename T, std::size_t Capacity = 4096> class RingBuffer
{
  static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be power of 2");
  static constexpr std::size_t mask_ = Capacity - 1;

public:
  bool push(T&& event)
  {
    if (size() >= Capacity) [[unlikely]] { return false; }
    buffer_[tail_++ & mask_] = std::move(event);
    return true;
  }

  bool push(const T& event)
  {
    if (size() >= Capacity) [[unlikely]] { return false; }
    buffer_[tail_++ & mask_] = event;
    return true;
  }

  // Get slot for in-place construction, then call commit_push()
  [[nodiscard]] T* alloc_slot()
  {
    if (size() >= Capacity) [[unlikely]] { return nullptr; }
    return &buffer_[tail_ & mask_];
  }
  void commit_push() { ++tail_; }

  [[nodiscard]] T* try_pop()
  {
    if (head_ == tail_) [[unlikely]] { return nullptr; }
    return &buffer_[head_++ & mask_];
  }

  [[nodiscard]] bool empty() const noexcept { return head_ == tail_; }
  [[nodiscard]] std::size_t size() const noexcept { return tail_ - head_; }

private:
  std::array<T, Capacity> buffer_{};
  std::size_t head_ = 0;
  std::size_t tail_ = 0;
};

// =============================================================================
// Threading mode specification
// =============================================================================

enum class ThreadMode : std::uint8_t {
  SameThread, // Runs on event loop thread, uses queue dispatch
  OwnThread // Runs on its own thread, direct push from emitters
};

// =============================================================================
// Concepts for receiver/emitter detection
// =============================================================================

template<typename T>
concept has_receives = requires { typename T::receives; };

template<typename T>
concept has_emits = requires { typename T::emits; };

template<typename T>
concept has_thread_mode = requires {
  { T::thread_mode } -> std::convertible_to<ThreadMode>;
};

// Get receives type list, defaults to empty
template<typename T> struct get_receives
{
  using type = type_list<>;
};

template<has_receives T> struct get_receives<T>
{
  using type = typename T::receives;
};

template<typename T> using get_receives_t = typename get_receives<T>::type;

// Get emits type list, defaults to empty
template<typename T> struct get_emits
{
  using type = type_list<>;
};

template<has_emits T> struct get_emits<T>
{
  using type = typename T::emits;
};

template<typename T> using get_emits_t = typename get_emits<T>::type;

// Get thread mode, defaults to SameThread
template<typename T> consteval ThreadMode get_thread_mode()
{
  if constexpr (has_thread_mode<T>) {
    return T::thread_mode;
  } else {
    return ThreadMode::SameThread;
  }
}

// Check if receiver can handle event type
template<typename Receiver, typename Event>
concept can_receive = contains_v<get_receives_t<Receiver>, std::decay_t<Event>>;

// External emitter: has emits but no receives (emits-only, not a receiver)
template<typename T>
concept is_external_emitter = has_emits<T> && !has_receives<T>;

// Receiver: has receives (may also have emits)
template<typename T>
concept is_receiver = has_receives<T>;

// Check if external emitter can emit event type
template<typename Emitter, typename Event>
concept can_emit = is_external_emitter<Emitter> && contains_v<get_emits_t<Emitter>, std::decay_t<Event>>;

// Predicate wrappers for filtering (concepts can't be used as template template parameters)
template<typename T> struct is_receiver_pred : std::bool_constant<is_receiver<T>>
{
};

template<typename T> struct is_external_emitter_pred : std::bool_constant<is_external_emitter<T>>
{
};

// Get size of type_list
template<typename List> struct type_list_size;

template<typename... Ts> struct type_list_size<type_list<Ts...>> : std::integral_constant<std::size_t, sizeof...(Ts)>
{
};

template<typename List> inline constexpr std::size_t type_list_size_v = type_list_size<List>::value;

// Get type at index from type_list
template<std::size_t I, typename List> struct type_list_at;

template<std::size_t I, typename... Ts> struct type_list_at<I, type_list<Ts...>>
{
  using type = type_at_t<I, Ts...>;
};

template<std::size_t I, typename List> using type_list_at_t = typename type_list_at<I, List>::type;

// =============================================================================
// Lock-free SPSC ring buffer for maximum throughput
// =============================================================================

// Cache line size for padding to avoid false sharing
inline constexpr std::size_t cache_line_size = 64;

// NOLINTNEXTLINE(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)
template<typename T, std::size_t Capacity = 4096> class SPSCQueue
{
  static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be power of 2");
  static constexpr std::size_t mask_ = Capacity - 1;

public:
  bool push(T event)
  {
    const std::size_t head = head_.load(std::memory_order_acquire);
    const std::size_t tail = tail_.load(std::memory_order_relaxed);
    if (tail - head >= Capacity) [[unlikely]] { return false; }
    buffer_[tail & mask_] = std::move(event);
    tail_.store(tail + 1, std::memory_order_release);
    return true;
  }

  [[nodiscard]] T* try_pop()
  {
    const std::size_t head = head_.load(std::memory_order_relaxed);
    if (head == tail_.load(std::memory_order_acquire)) { return nullptr; }
    current_ = std::move(buffer_[head & mask_]);
    head_.store(head + 1, std::memory_order_release);
    return &current_;
  }

  [[nodiscard]] T* pop_spin()
  {
    const std::size_t head = head_.load(std::memory_order_relaxed);
    std::size_t tail = tail_.load(std::memory_order_acquire);
    // LCOV_EXCL_START - spin loop iteration counts confuse coverage tools
    while (head == tail) {
      if (stop_.load(std::memory_order_relaxed)) { return nullptr; }
      cpu_pause();
      tail = tail_.load(std::memory_order_acquire);
    }
    // LCOV_EXCL_STOP
    current_ = std::move(buffer_[head & mask_]);
    head_.store(head + 1, std::memory_order_release);
    return &current_;
  }

  // cppcheck-suppress functionStatic ; interface consistency with ThreadSafeRingBuffer
  void notify() { /* No-op for lock-free */ }

  void stop() { stop_.store(true, std::memory_order_release); }

  [[nodiscard]] bool is_stopped() const noexcept { return stop_.load(std::memory_order_acquire); }

private:
  // MSVC C4324: structure was padded due to alignment specifier (intentional for cache line separation)
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4324)
#endif
  alignas(cache_line_size) std::array<T, Capacity> buffer_{};
  alignas(cache_line_size) T current_{};
  alignas(cache_line_size) std::atomic<std::size_t> head_{ 0 };
  alignas(cache_line_size) std::atomic<std::size_t> tail_{ 0 };
  alignas(cache_line_size) std::atomic<bool> stop_{ false };
#ifdef _MSC_VER
#pragma warning(pop)
#endif
};

// =============================================================================
// Thread-safe queue for threaded receivers (mutex-based, for MPSC cases)
// =============================================================================

// Number of pause iterations in spin loops before rechecking condition
inline constexpr int spin_pause_iterations = 32;

// NOLINTNEXTLINE(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)
template<typename T, std::size_t Capacity = 4096> class ThreadSafeRingBuffer
{
  static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be power of 2");
  static constexpr std::size_t mask_ = Capacity - 1;

public:
  bool push(T event)
  {
    std::scoped_lock lock(mutex_);
    if (tail_ - head_ >= Capacity) [[unlikely]] { return false; }
    buffer_[tail_++ & mask_] = std::move(event);
    has_data_.store(true, std::memory_order_release);
    return true;
  }

  [[nodiscard]] T* try_pop()
  {
    if (!has_data_.load(std::memory_order_acquire)) { return nullptr; }
    std::scoped_lock lock(mutex_);
    // LCOV_EXCL_START - race condition: another thread consumed last item between flag check and lock
    if (head_ == tail_) {
      has_data_.store(false, std::memory_order_release);
      return nullptr;
    }
    // LCOV_EXCL_STOP
    current_ = std::move(buffer_[head_++ & mask_]);
    if (head_ == tail_) { has_data_.store(false, std::memory_order_release); }
    return &current_;
  }

  [[nodiscard]] T* pop_wait_for(std::chrono::milliseconds timeout)
  {
    if (has_data_.load(std::memory_order_acquire)) {
      std::scoped_lock lock(mutex_);
      if (head_ != tail_) {
        current_ = std::move(buffer_[head_++ & mask_]);
        if (head_ == tail_) { has_data_.store(false, std::memory_order_release); }
        return &current_;
      }
    }
    std::unique_lock lock(mutex_);
    if (!cv_.wait_for(lock, timeout, [this] { return head_ != tail_ || stop_; })) { return nullptr; }
    if (stop_ && head_ == tail_) { return nullptr; }
    current_ = std::move(buffer_[head_++ & mask_]);
    if (head_ == tail_) { has_data_.store(false, std::memory_order_release); }
    return &current_;
  }

  [[nodiscard]] T* pop_spin()
  {
    // LCOV_EXCL_START - spin loop iteration counts confuse coverage tools
    while (!has_data_.load(std::memory_order_acquire)) {
      if (stop_.load(std::memory_order_acquire)) { return nullptr; }
      for (int i = 0; i < spin_pause_iterations; ++i) { cpu_pause(); }
    }
    // LCOV_EXCL_STOP
    std::scoped_lock lock(mutex_);
    if (head_ == tail_) { return nullptr; }
    current_ = std::move(buffer_[head_++ & mask_]);
    if (head_ == tail_) { has_data_.store(false, std::memory_order_release); }
    return &current_;
  }

  void notify() { cv_.notify_one(); }

  void stop()
  {
    stop_.store(true, std::memory_order_release);
    cv_.notify_all();
  }

  [[nodiscard]] bool is_stopped() const noexcept { return stop_.load(std::memory_order_acquire); }

private:
  std::array<T, Capacity> buffer_{};
  T current_{};
  std::size_t head_ = 0;
  std::size_t tail_ = 0;
  std::mutex mutex_;
  std::condition_variable cv_;
  std::atomic<bool> has_data_{ false };
  std::atomic<bool> stop_{ false };
};

// =============================================================================
// Dual queue: unsynchronized for same-thread, synchronized for cross-thread
// Uses ring buffer for fast same-thread access
// =============================================================================

template<typename TaggedEventType> class DualQueue
{
public:
  // Called from same thread (no sync needed)
  void push_local(TaggedEventType event) { local_queue_.push(std::move(event)); }

  template<typename E> void push_local_event(E&& event)
  {
    auto* slot = local_queue_.alloc_slot();
    if (slot) [[likely]] {
      slot->store(std::forward<E>(event));
      local_queue_.commit_push();
    }
  }

  // Called from other threads (synchronized) - wakes up waiting consumer
  void push_remote(TaggedEventType event)
  {
    {
      std::scoped_lock lock(mutex_);
      remote_queue_.push(std::move(event));
    }
    has_remote_.store(true, std::memory_order_release);
    // Only notify if consumer is actually waiting (not spinning)
    if (waiting_.load(std::memory_order_acquire)) { cv_.notify_one(); }
  }

  template<typename E> void push_remote_event(E&& event)
  {
    TaggedEventType tagged;
    tagged.store(std::forward<E>(event));
    {
      std::scoped_lock lock(mutex_);
      remote_queue_.push(std::move(tagged));
    }
    has_remote_.store(true, std::memory_order_release);
    // Only notify if consumer is actually waiting (not spinning)
    if (waiting_.load(std::memory_order_acquire)) { cv_.notify_one(); }
  }

  // Called from same thread only - checks local first, then drains remote
  [[nodiscard]] TaggedEventType* try_pop()
  {
    // Fast path: check local queue first (no atomic/lock)
    if (auto* event = local_queue_.try_pop()) { return event; }
    // Local empty - bulk drain remote queue
    drain_remote();
    return local_queue_.try_pop();
  }

  // Pop from local queue only (no remote check) - for batch processing
  [[nodiscard]] TaggedEventType* try_pop_local() { return local_queue_.try_pop(); }

  // Drain remote queue if there are pending events
  void drain_remote_if_pending() { drain_remote(); }

  // Block until an event is available or timeout expires
  // Returns nullptr on timeout, pointer to event otherwise
  template<typename Rep, typename Period>
  [[nodiscard]] TaggedEventType* wait_pop(std::chrono::duration<Rep, Period> timeout)
  {
    // First check local queue (fast path)
    if (auto* event = local_queue_.try_pop()) { return event; }

    // Wait for remote events
    {
      std::unique_lock lock(mutex_);
      waiting_.store(true, std::memory_order_release);
      const bool signaled = cv_.wait_for(lock, timeout, [this] { return !remote_queue_.empty() || stop_; });
      waiting_.store(false, std::memory_order_release);
      if (signaled) {
        if (stop_ && remote_queue_.empty()) { return nullptr; }
        // Drain while holding lock
        while (!remote_queue_.empty()) {
          local_queue_.push(std::move(remote_queue_.front()));
          remote_queue_.pop();
        }
      }
    }

    return local_queue_.try_pop();
  }

  // Block until an event is available (no busy-wait)
  // 1. Check local queue (no sync)
  // 2. If empty, drain remote (one lock)
  // 3. If still empty, wait on CV
  [[nodiscard]] TaggedEventType* wait_pop_any()
  {
    // Fast path: check local queue first
    if (auto* event = local_queue_.try_pop()) { return event; }

    // Try draining remote without waiting
    if (has_remote_.load(std::memory_order_acquire)) {
      std::scoped_lock lock(mutex_);
      while (!remote_queue_.empty()) {
        local_queue_.push(std::move(remote_queue_.front()));
        remote_queue_.pop();
      }
      has_remote_.store(false, std::memory_order_release);
    }

    // Check local again after drain
    if (auto* event = local_queue_.try_pop()) { return event; }

    // Both empty - wait on CV for remote events
    {
      std::unique_lock lock(mutex_);
      waiting_.store(true, std::memory_order_release);
      cv_.wait(lock, [this] { return !remote_queue_.empty() || stop_; });
      waiting_.store(false, std::memory_order_release);

      if (stop_ && remote_queue_.empty()) { return nullptr; }

      // Drain while holding lock
      while (!remote_queue_.empty()) {
        local_queue_.push(std::move(remote_queue_.front()));
        remote_queue_.pop();
      }
      has_remote_.store(false, std::memory_order_release);
    }

    return local_queue_.try_pop();
  }

  [[nodiscard]] bool empty()
  {
    drain_remote();
    return local_queue_.empty();
  }

  void stop()
  {
    {
      std::scoped_lock lock(mutex_);
      stop_ = true;
    }
    cv_.notify_one();
  }

private:
  void drain_remote()
  {
    // Fast path: check atomic flag before taking lock
    if (!has_remote_.load(std::memory_order_acquire)) { return; }
    std::scoped_lock lock(mutex_);
    while (!remote_queue_.empty()) {
      local_queue_.push(std::move(remote_queue_.front()));
      remote_queue_.pop();
    }
    has_remote_.store(false, std::memory_order_release);
  }

  RingBuffer<TaggedEventType> local_queue_; // Same-thread access only
  std::queue<TaggedEventType> remote_queue_; // Cross-thread, protected by mutex
  std::mutex mutex_;
  std::condition_variable cv_;
  std::atomic<bool> has_remote_{ false };
  std::atomic<bool> waiting_{ false }; // True when consumer is blocked on CV
  bool stop_ = false;
};

// =============================================================================
// Forward declarations
// =============================================================================

template<typename... Receivers> class EventLoop;

template<typename... Receivers> class SharedEventLoopPtr;

template<typename EventLoopType> class SameThreadDispatcher;

template<typename Emitter, typename EventLoopType> class OwnThreadDispatcher;

template<typename EmitterType, typename EventLoopType> class TypedExternalEmitter;

// =============================================================================
// Same-thread receiver wrapper
// =============================================================================

template<typename Receiver, typename EventLoopType> class SameThreadWrapper
{
public:
  using receives_list = get_receives_t<Receiver>;
  using dispatcher_type = SameThreadDispatcher<EventLoopType>;

  template<typename... Args>
  explicit SameThreadWrapper(EventLoopType* event_loop, Args&&... args)
    : receiver_(std::forward<Args>(args)...), dispatcher_(event_loop)
  {}

  ~SameThreadWrapper() = default;

  SameThreadWrapper(const SameThreadWrapper&) = delete;
  SameThreadWrapper& operator=(const SameThreadWrapper&) = delete;
  SameThreadWrapper(SameThreadWrapper&&) noexcept = default;
  SameThreadWrapper& operator=(SameThreadWrapper&&) noexcept = default;

  // Called by EventLoop to dispatch a queued event
  template<typename Event>
    requires can_receive<Receiver, std::decay_t<Event>>
  void dispatch(Event&& event)
  {
    receiver_.on_event(std::forward<Event>(event), dispatcher_);
  }

  [[nodiscard]] Receiver& get() & noexcept { return receiver_; }
  [[nodiscard]] const Receiver& get() const& noexcept { return receiver_; }

  // cppcheck-suppress functionStatic ; interface consistency with OwnThreadWrapper
  void start() noexcept {}
  // cppcheck-suppress functionStatic ; interface consistency with OwnThreadWrapper
  void stop() noexcept {}

  static constexpr ThreadMode mode = ThreadMode::SameThread;

private:
  Receiver receiver_;
  dispatcher_type dispatcher_;
};

// =============================================================================
// Own-thread receiver wrapper
// =============================================================================

template<typename Receiver, typename EventLoopType> class OwnThreadWrapper
{
public:
  using receiver_type = Receiver;
  using receives_list = get_receives_t<Receiver>;
  using tagged_event = to_tagged_event_t<receives_list>;

  // Automatically select queue type based on producer count
  // SPSC is safe when at most 1 producer thread, otherwise need MPSC
  static constexpr std::size_t producer_count = EventLoopType::template producer_count_for<Receiver>;
  using queue_type =
    std::conditional_t<(producer_count < 2), SPSCQueue<tagged_event>, ThreadSafeRingBuffer<tagged_event>>;

  using dispatcher_type = OwnThreadDispatcher<Receiver, EventLoopType>;

  template<typename... Args>
  explicit OwnThreadWrapper(EventLoopType* event_loop, Args&&... args)
    : receiver_(std::forward<Args>(args)...), ev_(event_loop)
  {}

  ~OwnThreadWrapper() { stop(); }

  OwnThreadWrapper(const OwnThreadWrapper&) = delete;
  OwnThreadWrapper& operator=(const OwnThreadWrapper&) = delete;
  OwnThreadWrapper(OwnThreadWrapper&&) = delete;
  OwnThreadWrapper& operator=(OwnThreadWrapper&&) = delete;

  void start()
  {
    if (running_.exchange(true)) { return; }
    thread_ = std::thread([this] { run_loop(); });
  }

  void stop()
  {
    if (!running_.exchange(false)) { return; }
    queue_.stop();
    if (thread_.joinable()) { thread_.join(); }
  }

  // Push from any thread (synchronized)
  template<typename Event>
    requires can_receive<Receiver, Event>
  void push(Event&& event)
  {
    tagged_event tagged;
    tagged.store(std::forward<Event>(event));
    queue_.push(std::move(tagged));
    queue_.notify(); // Wake up consumer
  }

  [[nodiscard]] Receiver& get() & noexcept { return receiver_; }
  [[nodiscard]] const Receiver& get() const& noexcept { return receiver_; }

  static constexpr ThreadMode mode = ThreadMode::OwnThread;

private:
  void run_loop()
  {
    dispatcher_type dispatcher(ev_);
    while (running_.load(std::memory_order_relaxed)) {
      auto* result = queue_.pop_spin();
      if (result) {
        fast_dispatch(*result, [this, &dispatcher](auto& event) { receiver_.on_event(std::move(event), dispatcher); });
      }
    }
  }

  Receiver receiver_;
  EventLoopType* ev_;
  std::thread thread_;
  std::atomic<bool> running_{ false };
  queue_type queue_;
};

// =============================================================================
// Empty wrapper for external emitters (no storage needed, just type marker)
// =============================================================================

template<typename Emitter, typename EventLoopType> class ExternalEmitterWrapper
{
public:
  explicit ExternalEmitterWrapper(EventLoopType* /*loop*/) {}
};

// =============================================================================
// Wrapper type selector (avoids instantiating wrong branch)
// =============================================================================

template<typename T, typename EventLoopType, bool IsReceiver = is_receiver<T>> struct wrapper_selector;

// Receiver case: select based on thread mode
template<typename Receiver, typename EventLoopType> struct wrapper_selector<Receiver, EventLoopType, true>
{
  using type = std::conditional_t<get_thread_mode<Receiver>() == ThreadMode::OwnThread,
    OwnThreadWrapper<Receiver, EventLoopType>,
    SameThreadWrapper<Receiver, EventLoopType>>;
};

// External emitter case: use empty wrapper
template<typename Emitter, typename EventLoopType> struct wrapper_selector<Emitter, EventLoopType, false>
{
  using type = ExternalEmitterWrapper<Emitter, EventLoopType>;
};

template<typename T, typename EventLoopType> using wrapper_for = typename wrapper_selector<T, EventLoopType>::type;

// =============================================================================
// Receiver storage - handles non-movable wrappers via unique_ptr
// =============================================================================

template<typename Receiver, typename EventLoopType> class ReceiverStorage
{
public:
  using wrapper_type = wrapper_for<Receiver, EventLoopType>;

  template<typename... Args>
  explicit ReceiverStorage(EventLoopType* loop, Args&&... args)
    : wrapper_(std::make_unique<wrapper_type>(loop, std::forward<Args>(args)...))
  {}

  ~ReceiverStorage() = default;

  ReceiverStorage(const ReceiverStorage&) = delete;
  ReceiverStorage& operator=(const ReceiverStorage&) = delete;
  ReceiverStorage(ReceiverStorage&&) noexcept = default;
  ReceiverStorage& operator=(ReceiverStorage&&) noexcept = default;

  wrapper_type& operator*() & noexcept { return *wrapper_; }
  const wrapper_type& operator*() const& noexcept { return *wrapper_; }
  wrapper_type* operator->() & noexcept { return wrapper_.get(); }
  const wrapper_type* operator->() const& noexcept { return wrapper_.get(); }

private:
  std::unique_ptr<wrapper_type> wrapper_;
};

// =============================================================================
// Collect all event types that same-thread receivers handle
// =============================================================================

// Helper to get events from a single same-thread receiver (or empty list)
template<typename R>
using same_thread_events_from =
  std::conditional_t<is_receiver<R> && get_thread_mode<R>() == ThreadMode::SameThread, get_receives_t<R>, type_list<>>;

// Concatenate multiple type_lists into one using O(log N) recursion depth
template<typename... Lists> struct concat_type_lists;

template<> struct concat_type_lists<>
{
  using type = type_list<>;
};

template<typename... Ts> struct concat_type_lists<type_list<Ts...>>
{
  using type = type_list<Ts...>;
};

template<typename... T1s, typename... T2s> struct concat_type_lists<type_list<T1s...>, type_list<T2s...>>
{
  using type = type_list<T1s..., T2s...>;
};

// Binary tree reduction: process 4 lists at a time, reducing depth by 4x
template<typename L1, typename L2, typename L3, typename L4, typename... Rest>
struct concat_type_lists<L1, L2, L3, L4, Rest...>
{
  using type = typename concat_type_lists<typename concat_type_lists<L1, L2>::type,
    typename concat_type_lists<L3, L4>::type,
    Rest...>::type;
};

// Handle 3 lists case
template<typename L1, typename L2, typename L3> struct concat_type_lists<L1, L2, L3>
{
  using type = typename concat_type_lists<typename concat_type_lists<L1, L2>::type, L3>::type;
};

// Compute indices of first occurrence for each type using consteval
// Returns array where first_occurrence[i] = j means type at index i first appears at index j
template<typename... Ts> consteval auto compute_first_occurrences()
{
  std::array<std::size_t, sizeof...(Ts)> result{};
  // Use fold expression to find first occurrence of each type
  std::size_t idx = 0;
  ((result[idx] = index_of_v<Ts, Ts...>, ++idx), ...);
  return result;
}

// Compute which indices contain first occurrences (unique types)
template<std::size_t N> consteval auto compute_unique_indices(const std::array<std::size_t, N>& first_occ)
{
  std::size_t count = 0;
  for (std::size_t i = 0; i < N; ++i) {
    if (first_occ[i] == i) ++count;
  }
  std::array<std::size_t, N> indices{}; // Over-allocate, actual count stored at index 0 position N-1
  std::size_t j = 0;
  for (std::size_t i = 0; i < N; ++i) {
    if (first_occ[i] == i) indices[j++] = i;
  }
  return std::pair{ indices, count };
}

// Select types at specific indices from a type_list
template<typename List, std::size_t... Is> struct select_types;

template<typename... Ts, std::size_t... Is> struct select_types<type_list<Ts...>, Is...>
{
  using type = type_list<type_at_t<Is, Ts...>...>;
};

// Helper to extract unique types using precomputed indices
template<typename List, typename IndicesSeq> struct unique_types_from_indices;

template<typename List, std::size_t... Is> struct unique_types_from_indices<List, std::index_sequence<Is...>>
{
  using type = typename select_types<List, Is...>::type;
};

// Build index_sequence from constexpr array and count
template<typename List, std::size_t MaxN> struct unique_types_builder
{
  static constexpr auto first_occ = []<typename... Ts>(type_list<Ts...>) { return compute_first_occurrences<Ts...>(); }(
                                      List{});
  static constexpr auto idx_info = compute_unique_indices(first_occ);
  static constexpr auto unique_idx_array = idx_info.first;
  static constexpr std::size_t unique_count = idx_info.second;

  // Build the unique type_list by selecting at computed indices
  template<std::size_t... Is>
  static auto make_type(std::index_sequence<Is...> /*unused*/)
    -> type_list<type_at_t<unique_idx_array[Is], std::tuple<>>...>;

  // We need to convert array to index_sequence - use recursive helper
  template<std::size_t... Is> struct apply_indices
  {
    using type = typename select_types<List, unique_idx_array[Is]...>::type;
  };
};

// Generate index sequence 0, 1, 2, ..., N-1 and apply to array
template<typename List, std::size_t Count, std::size_t... Is>
auto unique_types_apply(std::index_sequence<Is...> /*unused*/) ->
  typename unique_types_builder<List, type_list_size_v<List>>::template apply_indices<Is...>::type;

template<typename List> struct unique_types_impl
{
  static constexpr std::size_t max_n = type_list_size_v<List>;
  using builder = unique_types_builder<List, max_n>;
  using type =
    decltype(unique_types_apply<List, builder::unique_count>(std::make_index_sequence<builder::unique_count>{}));
};

// Handle empty list
template<> struct unique_types_impl<type_list<>>
{
  using type = type_list<>;
};

template<typename List> using unique_types_t = typename unique_types_impl<List>::type;

// Collect and deduplicate: concatenate all same-thread receives lists, then unique
template<typename... Receivers> struct collect_same_thread_events
{
  using concatenated = typename concat_type_lists<same_thread_events_from<Receivers>...>::type;
  using type = unique_types_t<concatenated>;
};

template<typename... Receivers>
using collect_same_thread_events_t = typename collect_same_thread_events<Receivers...>::type;

// =============================================================================
// Check if any OwnThread receiver emits events to SameThread receivers
// =============================================================================

// Check if any event in EmitsList is contained in SameThreadEvents
template<typename EmitsList, typename SameThreadEvents> struct emits_to_same_thread;

template<typename... Es, typename SameThreadEvents>
struct emits_to_same_thread<type_list<Es...>, SameThreadEvents>
  : std::bool_constant<(contains_v<SameThreadEvents, Es> || ...)>
{
};

template<typename SameThreadEvents> struct emits_to_same_thread<type_list<>, SameThreadEvents> : std::false_type
{
};

// Check if any OwnThread receiver emits to same-thread receivers
template<typename SameThreadEvents, typename... Receivers> struct has_remote_producers
{
  template<typename R>
  static constexpr bool is_remote_producer =
    get_thread_mode<R>() == ThreadMode::OwnThread && emits_to_same_thread<get_emits_t<R>, SameThreadEvents>::value;

  static constexpr bool value = (is_remote_producer<Receivers> || ...);
};

template<typename SameThreadEvents> struct has_remote_producers<SameThreadEvents>
{
  static constexpr bool value = false;
};

template<typename SameThreadEvents, typename... Receivers>
inline constexpr bool has_remote_producers_v = has_remote_producers<SameThreadEvents, Receivers...>::value;

// =============================================================================
// Count OwnThread receivers that can produce events to a target receiver
// Used to determine if SPSC queue is safe (count <= 1) or MPSC needed (count > 1)
// =============================================================================

template<typename TargetReceives, typename... Receivers> struct count_ownthread_producers
{
  template<typename R>
  static constexpr std::size_t count_one =
    (get_thread_mode<R>() == ThreadMode::OwnThread && lists_overlap_v<get_emits_t<R>, TargetReceives>) ? 1 : 0;

  static constexpr std::size_t value = (count_one<Receivers> + ... + 0);
};

template<typename TargetReceives, typename... Receivers>
inline constexpr std::size_t count_ownthread_producers_v =
  count_ownthread_producers<TargetReceives, Receivers...>::value;

// Check if any SameThread receiver emits events that target receives
// (meaning event loop thread can be a producer)
template<typename TargetReceives, typename... Receivers> struct has_samethread_producer
{
  // Only consider actual receivers (not external emitters) - external emitters are counted separately
  template<typename R>
  static constexpr bool is_producer = !is_external_emitter<R> && get_thread_mode<R>() == ThreadMode::SameThread
                                      && lists_overlap_v<get_emits_t<R>, TargetReceives>;

  static constexpr bool value = (is_producer<Receivers> || ...);
};

template<typename TargetReceives> struct has_samethread_producer<TargetReceives>
{
  static constexpr bool value = false;
};

template<typename TargetReceives, typename... Receivers>
inline constexpr bool has_samethread_producer_v = has_samethread_producer<TargetReceives, Receivers...>::value;

// Count external emitters that can produce events to a target receiver
template<typename TargetReceives, typename... Items> struct count_external_producers
{
  template<typename T>
  static constexpr std::size_t count_one =
    (is_external_emitter<T> && lists_overlap_v<get_emits_t<T>, TargetReceives>) ? 1 : 0;

  static constexpr std::size_t value = (count_one<Items> + ... + 0);
};

template<typename TargetReceives, typename... Items>
inline constexpr std::size_t count_external_producers_v = count_external_producers<TargetReceives, Items...>::value;

// Check if any external emitter is registered
template<typename... Items> struct has_external_emitters
{
  static constexpr bool value = (is_external_emitter<Items> || ...);
};

template<> struct has_external_emitters<>
{
  static constexpr bool value = false;
};

template<typename... Items> inline constexpr bool has_external_emitters_v = has_external_emitters<Items...>::value;

// Total producer count for an OwnThread receiver
// Event loop thread counts as 1 if any SameThread receiver emits to target
// Each external emitter that can emit to target counts as 1
template<typename TargetReceives, typename... Items>
inline constexpr std::size_t total_producer_count_v =
  (has_samethread_producer_v<TargetReceives, Items...> ? 1 : 0) + count_ownthread_producers_v<TargetReceives, Items...>
  + count_external_producers_v<TargetReceives, Items...>;

// =============================================================================
// Poll strategies - use with loop.run<Strategy>() or Strategy{loop}.run()
// =============================================================================

// Spin strategy: never blocks, maximum throughput, burns CPU when idle
template<typename EventLoop> struct Spin
{
  // NOLINTNEXTLINE(cppcoreguidelines-avoid-const-or-ref-data-members)
  EventLoop& event_loop;
  explicit Spin(EventLoop& loop) : event_loop(loop) {}

  [[nodiscard]] bool poll()
  {
    auto* event = event_loop.try_get_event();
    if (event == nullptr) { return false; }
    event_loop.dispatch_event(*event);
    return true;
  }

  void run()
  {
    while (event_loop.is_running()) { (void)poll(); }
  }

  // NOLINTNEXTLINE(cppcoreguidelines-missing-std-forward)
  template<typename Predicate> void run_while(Predicate&& pred)
  {
    while (event_loop.is_running() && pred()) { (void)poll(); }
  }
};

// Wait strategy: blocks on CV when idle, zero CPU when idle, higher latency
template<typename EventLoop> struct Wait
{
  // NOLINTNEXTLINE(cppcoreguidelines-avoid-const-or-ref-data-members)
  EventLoop& event_loop;
  explicit Wait(EventLoop& loop) : event_loop(loop) {}

  [[nodiscard]] bool poll()
  {
    auto* event = event_loop.queue().wait_pop_any();
    if (event == nullptr) { return false; }
    event_loop.dispatch_event(*event);
    return true;
  }

  void run()
  {
    while (event_loop.is_running()) { (void)poll(); }
  }

  // NOLINTNEXTLINE(cppcoreguidelines-missing-std-forward)
  template<typename Predicate> void run_while(Predicate&& pred)
  {
    while (event_loop.is_running() && pred()) { (void)poll(); }
  }
};

// Yield strategy: yields to OS when no events, balance of throughput and CPU
template<typename EventLoop> struct Yield
{
  // NOLINTNEXTLINE(cppcoreguidelines-avoid-const-or-ref-data-members)
  EventLoop& event_loop;
  explicit Yield(EventLoop& loop) : event_loop(loop) {}

  [[nodiscard]] bool poll()
  {
    auto* event = event_loop.try_get_event();
    if (event == nullptr) {
      std::this_thread::yield();
      return false;
    }
    event_loop.dispatch_event(*event);
    return true;
  }

  void run()
  {
    while (event_loop.is_running()) { (void)poll(); }
  }

  // NOLINTNEXTLINE(cppcoreguidelines-missing-std-forward)
  template<typename Predicate> void run_while(Predicate&& pred)
  {
    while (event_loop.is_running() && pred()) { (void)poll(); }
  }
};

// Hybrid strategy: spins for a number of iterations, then falls back to wait
template<typename EventLoop> struct Hybrid
{
  // NOLINTNEXTLINE(cppcoreguidelines-avoid-const-or-ref-data-members)
  EventLoop& event_loop;
  std::size_t spin_count;
  std::size_t empty_spins{ 0 };

  // NOLINTNEXTLINE(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)
  explicit Hybrid(EventLoop& loop, std::size_t spins = 1000) : event_loop(loop), spin_count(spins) {}

  [[nodiscard]] bool poll()
  {
    // Try to get an event without blocking
    auto* event = event_loop.try_get_event();
    if (event != nullptr) {
      event_loop.dispatch_event(*event);
      empty_spins = 0; // Reset counter on successful dispatch
      return true;
    }

    // No event available - count empty spins
    ++empty_spins;
    if (empty_spins < spin_count) { return false; }

    // Exceeded spin count - fall back to wait
    empty_spins = 0;
    event = event_loop.queue().wait_pop_any();
    if (event == nullptr) { return false; }
    event_loop.dispatch_event(*event);
    return true;
  }

  void run() { run_while(std::true_type{}); }

  // pred is only invoked, not forwarded - no std::forward needed
  // NOLINTNEXTLINE(cppcoreguidelines-missing-std-forward)
  template<typename Predicate> void run_while(Predicate&& pred)
  {
    while (event_loop.is_running() && pred()) { std::ignore = poll(); }
  }
};

// =============================================================================
// Event Loop - the core dispatcher
// =============================================================================

template<typename... Receivers> class EventLoop
{
public:
  using self_type = EventLoop<Receivers...>;
  using receiver_list = type_list<Receivers...>;
  using same_thread_events = collect_same_thread_events_t<Receivers...>;
  using tagged_event = to_tagged_event_t<same_thread_events>;
  using queue_type = DualQueue<tagged_event>;

  // Compile-time flag: true if any OwnThread receiver can emit to SameThread
  static constexpr bool has_remote_producers = has_remote_producers_v<same_thread_events, Receivers...>;

  // Helper to compute producer count for a specific receiver
  template<typename Receiver>
  static constexpr std::size_t producer_count_for = total_producer_count_v<get_receives_t<Receiver>, Receivers...>;

  // Helper to get the queue type used for an OwnThread receiver
  template<typename Receiver> using queue_type_for = typename OwnThreadWrapper<Receiver, self_type>::queue_type;

  EventLoop() : receivers_(ReceiverStorage<Receivers, self_type>(this)...) {}

  ~EventLoop() { stop(); }

  EventLoop(const EventLoop&) = delete;
  EventLoop& operator=(const EventLoop&) = delete;
  EventLoop(EventLoop&&) = delete;
  EventLoop& operator=(EventLoop&&) = delete;

  void start()
  {
    running_.store(true, std::memory_order_release);
    start_all(std::index_sequence_for<Receivers...>{});
  }

  // Strategy accessors - use with Strategy{loop}.run()
  [[nodiscard]] bool is_running() const noexcept { return running_.load(std::memory_order_acquire); }

  // cppcheck-suppress functionStatic ; accesses queue_ member
  [[nodiscard]] queue_type& queue() & noexcept { return queue_; }

  // cppcheck-suppress functionStatic ; accesses queue_ member
  [[nodiscard]] tagged_event* try_get_event()
  {
    if constexpr (has_remote_producers) {
      return queue_.try_pop();
    } else {
      return queue_.try_pop_local();
    }
  }

  void dispatch_event(tagged_event& event)
  {
    fast_dispatch(event, [this]<typename E>(E& event2) {
      constexpr std::size_t event_idx = index_of_in_list<std::decay_t<E>>(same_thread_events{});
      constexpr std::size_t count = same_thread_count_table_[event_idx];
      if constexpr (count == 1) {
        this->template dispatch_single_fast<0>(std::move(event2));
      } else if constexpr (count > 1) {
        this->template fanout_to_same_thread_fold<0>(event2, std::make_index_sequence<sizeof...(Receivers)>{});
      }
    });
  }

  void stop()
  {
    running_.store(false, std::memory_order_release);
    queue_.stop();
    stop_all(std::index_sequence_for<Receivers...>{});
  }

  // Emit from EV thread (uses local queue)
  template<typename Event> void emit(Event&& event) { queue_event<false>(std::forward<Event>(event)); }

  template<typename Receiver, typename Self> [[nodiscard]] auto& get(this Self& self) noexcept
  {
    return std::get<ReceiverStorage<Receiver, self_type>>(self.receivers_)->get();
  }

private:
  friend class SameThreadDispatcher<self_type>;
  template<typename, typename> friend class OwnThreadDispatcher;
  template<typename, typename> friend class TypedExternalEmitter;
  template<typename...> friend class SharedEventLoopPtr;

  // Called by SameThreadDispatcher (uses local queue)
  template<typename Event> void queue_local(Event&& event) { queue_event<false>(std::forward<Event>(event)); }

  // Called by OwnThreadDispatcher and TypedExternalEmitter (uses remote queue with synchronization)
  template<typename Event> void queue_remote(Event&& event) { queue_event<true>(std::forward<Event>(event)); }

  template<bool Remote, typename Event> void queue_event(Event&& event)
  {
    constexpr bool has_same_thread = contains_v<same_thread_events, std::decay_t<Event>>;
    constexpr std::size_t own_thread_count = count_own_thread_receivers<0, std::decay_t<Event>>();

    if constexpr (has_same_thread && own_thread_count > 0) {
      if constexpr (Remote) {
        queue_.push_remote_event(event);
      } else {
        queue_.push_local_event(event);
      }
      push_to_own_thread<0>(std::forward<Event>(event));
    } else if constexpr (has_same_thread) {
      if constexpr (Remote) {
        queue_.push_remote_event(std::forward<Event>(event));
      } else {
        queue_.push_local_event(std::forward<Event>(event));
      }
    } else if constexpr (own_thread_count > 0) {
      push_to_own_thread<0>(std::forward<Event>(event));
    }
  }

  template<std::size_t I> void start_one()
  {
    if constexpr (is_receiver<std::tuple_element_t<I, std::tuple<Receivers...>>>) { std::get<I>(receivers_)->start(); }
  }

  template<std::size_t I> void stop_one()
  {
    if constexpr (is_receiver<std::tuple_element_t<I, std::tuple<Receivers...>>>) { std::get<I>(receivers_)->stop(); }
  }

  template<std::size_t... Is> void start_all(std::index_sequence<Is...> /*unused*/) { (start_one<Is>(), ...); }

  template<std::size_t... Is> void stop_all(std::index_sequence<Is...> /*unused*/) { (stop_one<Is>(), ...); }

  // Count same-thread receivers that handle Event using fold expression (O(1) depth)
  template<std::size_t I, typename Event, std::size_t... Is>
  static consteval std::size_t count_same_thread_receivers_impl(std::index_sequence<Is...> /*unused*/)
  {
    return ((Is >= I && get_thread_mode<std::tuple_element_t<Is, std::tuple<Receivers...>>>() == ThreadMode::SameThread
                  && can_receive<std::tuple_element_t<Is, std::tuple<Receivers...>>, Event>
                ? 1
                : 0)
            + ... + 0);
  }

  template<std::size_t I, typename Event> static consteval std::size_t count_same_thread_receivers()
  {
    return count_same_thread_receivers_impl<I, Event>(std::make_index_sequence<sizeof...(Receivers)>{});
  }

  // Count own-thread receivers that handle Event using fold expression (O(1) depth)
  template<std::size_t I, typename Event, std::size_t... Is>
  static consteval std::size_t count_own_thread_receivers_impl(std::index_sequence<Is...> /*unused*/)
  {
    return ((Is >= I && get_thread_mode<std::tuple_element_t<Is, std::tuple<Receivers...>>>() == ThreadMode::OwnThread
                  && can_receive<std::tuple_element_t<Is, std::tuple<Receivers...>>, Event>
                ? 1
                : 0)
            + ... + 0);
  }

  template<std::size_t I, typename Event> static consteval std::size_t count_own_thread_receivers()
  {
    return count_own_thread_receivers_impl<I, Event>(std::make_index_sequence<sizeof...(Receivers)>{});
  }

  // Fan out event to all same-thread receivers that handle it using fold expression
  template<std::size_t I, typename Event, std::size_t... Is>
  void fanout_to_same_thread_fold(Event& event, std::index_sequence<Is...> /*unused*/)
  {
    constexpr std::size_t count = count_same_thread_receivers<I, std::decay_t<Event>>();
    std::size_t dispatched = 0;
    (
      [&]<std::size_t Idx>() {
        using Receiver = std::tuple_element_t<Idx, std::tuple<Receivers...>>;
        if constexpr (get_thread_mode<Receiver>() == ThreadMode::SameThread
                      && can_receive<Receiver, std::decay_t<Event>>) {
          ++dispatched;
          if (dispatched == count) {
            std::get<Idx>(receivers_)->dispatch(std::move(event));
          } else {
            std::get<Idx>(receivers_)->dispatch(event);
          }
        }
      }.template operator()<Is>(),
      ...);
  }

  template<std::size_t I, typename Event> void fanout_to_same_thread(Event& event)
  {
    constexpr std::size_t count = count_same_thread_receivers<I, std::decay_t<Event>>();
    if constexpr (count == 1) {
      // Fast path: single receiver, find it and dispatch directly
      dispatch_to_single_receiver<I>(std::move(event));
    } else if constexpr (count > 1) {
      fanout_to_same_thread_fold<I>(event, std::make_index_sequence<sizeof...(Receivers)>{});
    }
  }

  // Find at compile time which receiver index handles an event type
  template<typename Event, std::size_t... Is>
  static consteval std::size_t find_single_receiver_impl(std::index_sequence<Is...> /*unused*/)
  {
    std::size_t result = sizeof...(Receivers);
    // NOLINTNEXTLINE(readability-simplify-boolean-expr)
    (void)((get_thread_mode<std::tuple_element_t<Is, std::tuple<Receivers...>>>() == ThreadMode::SameThread
                 && can_receive<std::tuple_element_t<Is, std::tuple<Receivers...>>, Event>
               ? (result == sizeof...(Receivers) ? (result = Is, true) : false)
               : false)
           || ...);
    return result;
  }

  template<typename Event> static consteval std::size_t find_single_receiver_index()
  {
    return find_single_receiver_impl<Event>(std::make_index_sequence<sizeof...(Receivers)>{});
  }

  // Helper to get index in type_list
  template<typename T, typename... Ts> static consteval std::size_t index_of_in_list(type_list<Ts...> /*unused*/ = {})
  {
    return index_of_v<T, Ts...>;
  }

  // Helper to update tables for a single receiver's receives list - O(R) where R is receives count
  template<std::size_t RecvIdx, typename... AllEvents, typename... ReceivedEvents>
  static consteval void update_tables_for_receiver(std::array<std::size_t, sizeof...(AllEvents)>& dispatch,
    std::array<std::size_t, sizeof...(AllEvents)>& count,
    type_list<AllEvents...> /*unused*/,
    type_list<ReceivedEvents...> /*unused*/)
  {
    // Only iterate events this receiver actually handles (not all events)
    (
      [&]<typename E>() {
        constexpr std::size_t idx = index_of_v<E, AllEvents...>;
        if constexpr (idx < sizeof...(AllEvents)) {
          if (dispatch[idx] == sizeof...(Receivers)) { dispatch[idx] = RecvIdx; }
          ++count[idx];
        }
      }.template operator()<ReceivedEvents>(),
      ...);
  }

  // Build dispatch and count tables - O(N + sum of receives) not O(N*M)
  // Returns pair of {dispatch_table, count_table}
  template<typename... Events, std::size_t... RecvIs>
  static consteval auto build_tables_from_receivers(type_list<Events...> events,
    std::index_sequence<RecvIs...> /*unused*/)
  {
    constexpr std::size_t num_events = sizeof...(Events);
    std::array<std::size_t, num_events> dispatch_table{};
    std::array<std::size_t, num_events> count_table{};

    // Initialize dispatch table to invalid
    for (auto& d : dispatch_table) { d = sizeof...(Receivers); }

    // For each receiver, update tables only for events it actually handles
    (
      [&]<std::size_t RecvIdx>() {
        using Receiver = std::tuple_element_t<RecvIdx, std::tuple<Receivers...>>;
        if constexpr (get_thread_mode<Receiver>() == ThreadMode::SameThread) {
          update_tables_for_receiver<RecvIdx>(dispatch_table, count_table, events, get_receives_t<Receiver>{});
        }
      }.template operator()<RecvIs>(),
      ...);

    return std::pair{ dispatch_table, count_table };
  }

  static constexpr auto dispatch_tables_ =
    build_tables_from_receivers(same_thread_events{}, std::make_index_sequence<sizeof...(Receivers)>{});
  static constexpr auto single_dispatch_table_ = dispatch_tables_.first;
  static constexpr auto same_thread_count_table_ = dispatch_tables_.second;

  // Dispatch using precomputed table - one lookup instead of N checks per dispatch
  template<std::size_t I, typename Event> void dispatch_single_fast(Event&& event)
  {
    constexpr std::size_t event_idx = index_of_in_list<std::decay_t<Event>>(same_thread_events{});
    constexpr std::size_t recv_idx = single_dispatch_table_[event_idx];
    if constexpr (recv_idx < sizeof...(Receivers)) {
      std::get<recv_idx>(receivers_)->dispatch(std::forward<Event>(event));
    }
  }

  // Direct dispatch using precomputed receiver index - O(1) template depth
  template<std::size_t I, typename Event> void dispatch_to_single_receiver(Event&& event)
  {
    constexpr std::size_t receiver_idx = find_single_receiver_index<std::decay_t<Event>>();
    if constexpr (receiver_idx < sizeof...(Receivers)) {
      std::get<receiver_idx>(receivers_)->dispatch(std::forward<Event>(event));
    }
  }

  // Push to own-thread receivers using fold expression
  template<std::size_t I, typename Event, std::size_t... Is>
  void push_to_own_thread_fold(Event&& event, std::index_sequence<Is...> /*unused*/)
  {
    constexpr std::size_t count = count_own_thread_receivers<I, std::decay_t<Event>>();
    std::size_t pushed = 0;
    (
      [&]<std::size_t Idx>() {
        using Receiver = std::tuple_element_t<Idx, std::tuple<Receivers...>>;
        if constexpr (get_thread_mode<Receiver>() == ThreadMode::OwnThread
                      && can_receive<Receiver, std::decay_t<Event>>) {
          ++pushed;
          if (pushed == count) {
            std::get<Idx>(receivers_)->push(std::forward<Event>(event));
          } else {
            std::get<Idx>(receivers_)->push(event);
          }
        }
      }.template operator()<Is>(),
      ...);
  }

  template<std::size_t I, typename Event> void push_to_own_thread(Event&& event)
  {
    constexpr std::size_t count = count_own_thread_receivers<I, std::decay_t<Event>>();
    if constexpr (count > 0) {
      push_to_own_thread_fold<I>(std::forward<Event>(event), std::make_index_sequence<sizeof...(Receivers)>{});
    }
  }

  std::tuple<ReceiverStorage<Receivers, self_type>...> receivers_;
  queue_type queue_;
  std::atomic<bool> running_{ false };
};

// =============================================================================
// Dispatchers - injected into on_event based on receiver's thread mode
// =============================================================================

// Dispatcher for same-thread receivers: uses local queue (no sync)
template<typename EventLoopType> class SameThreadDispatcher
{
public:
  explicit SameThreadDispatcher(EventLoopType* loop) : event_loop_(loop) {}

  template<typename Event> void emit(Event&& event) { event_loop_->queue_local(std::forward<Event>(event)); }

private:
  EventLoopType* event_loop_;
};

// Dispatcher for own-thread receivers: uses remote queue (synchronized)
template<typename Emitter, typename EventLoopType> class OwnThreadDispatcher
{
public:
  explicit OwnThreadDispatcher(EventLoopType* loop) : event_loop_(loop) {}

  template<typename Event> void emit(Event&& event) { event_loop_->queue_remote(std::forward<Event>(event)); }

private:
  EventLoopType* event_loop_;
};

// =============================================================================
// External emitter - allows code outside the event loop to inject events
// EmitterType specifies which events this emitter is allowed to emit
// Uses weak_ptr for safe access after EventLoop destruction
// =============================================================================

template<typename EmitterType, typename EventLoopType> class TypedExternalEmitter
{
public:
  explicit TypedExternalEmitter(std::shared_ptr<EventLoopType> loop) noexcept : loop_(std::move(loop)) {}

  // Only allow emitting events declared in EmitterType::emits
  // Returns true if event was queued, false if EventLoop was destroyed
  template<typename Event>
    requires can_emit<EmitterType, Event>
  bool emit(Event&& event)
  {
    if (auto locked = loop_.lock()) {
      locked->queue_remote(std::forward<Event>(event));
      return true;
    }
    return false;
  }

  // Check if the EventLoop is still alive
  [[nodiscard]] bool is_valid() const noexcept { return !loop_.expired(); }

private:
  std::weak_ptr<EventLoopType> loop_;
};

// =============================================================================
// SharedEventLoopPtr - value-type wrapper that enables external emitters
// Use this when you need external emitters; use EventLoop directly otherwise
// Copyable (shares ownership), movable, default destructible
// =============================================================================

template<typename... Receivers> class SharedEventLoopPtr
{
public:
  using loop_type = EventLoop<Receivers...>;

  SharedEventLoopPtr() : loop_(std::make_shared<loop_type>()) {}

  ~SharedEventLoopPtr() = default;
  SharedEventLoopPtr(const SharedEventLoopPtr&) = default;
  SharedEventLoopPtr& operator=(const SharedEventLoopPtr&) = default;
  SharedEventLoopPtr(SharedEventLoopPtr&&) = default;
  SharedEventLoopPtr& operator=(SharedEventLoopPtr&&) = default;

  void start() { loop_->start(); }
  void stop() { loop_->stop(); }

  [[nodiscard]] bool is_running() const noexcept { return loop_->is_running(); }

  template<typename Event> void emit(Event&& event) { loop_->emit(std::forward<Event>(event)); }

  template<typename Receiver, typename Self> [[nodiscard]] auto& get(this Self& self) noexcept
  {
    return self.loop_->template get<Receiver>();
  }

  // Access underlying loop for strategies (e.g., Spin{*shared_loop}.run())
  [[nodiscard]] loop_type& operator*() noexcept { return *loop_; }
  [[nodiscard]] const loop_type& operator*() const noexcept { return *loop_; }
  [[nodiscard]] loop_type* operator->() noexcept { return loop_.get(); }
  [[nodiscard]] const loop_type* operator->() const noexcept { return loop_.get(); }

  // Get a typed external emitter handle
  // The returned emitter uses weak_ptr and is safe to use after SharedEventLoopPtr is destroyed
  template<typename EmitterType>
    requires(is_external_emitter<EmitterType> && contains_v<type_list<Receivers...>, EmitterType>)
  [[nodiscard]] TypedExternalEmitter<EmitterType, loop_type> get_external_emitter() noexcept
  {
    return TypedExternalEmitter<EmitterType, loop_type>(loop_);
  }

private:
  std::shared_ptr<loop_type> loop_;
};

// =============================================================================
// Compile-time builder for EventLoop
// Usage: Builder{}.add<Ping>().add<Pong>().build() -> EventLoop<Ping, Pong>
// =============================================================================

template<typename... Receivers> struct Builder
{
  // cppcheck-suppress functionStatic ; follows builder pattern convention
  template<typename NewReceiver> constexpr auto add() const
  {
    static_assert(
      !contains_v<type_list<Receivers...>, NewReceiver>, "Receiver type is already registered in this Builder");

    return Builder<Receivers..., NewReceiver>{};
  }

  // cppcheck-suppress functionStatic ; follows builder pattern convention
  constexpr auto build() const { return EventLoop<Receivers...>{}; }

  using loop_type = EventLoop<Receivers...>;
};

// =============================================================================
// Compile-time validation concepts for better error messages
// =============================================================================

// Check if receiver has on_event method for a given event type
template<typename Receiver, typename Event, typename Dispatcher>
concept has_on_event_for = requires(Receiver& receiver, Event event, Dispatcher& dispatcher) {
  receiver.on_event(std::move(event), dispatcher);
};

// Validator to check receives/on_event consistency at compile time
template<typename Receiver, typename EventLoopType> struct ReceiverValidator
{
  using dispatcher_type = std::conditional_t<get_thread_mode<Receiver>() == ThreadMode::SameThread,
    SameThreadDispatcher<EventLoopType>,
    OwnThreadDispatcher<Receiver, EventLoopType>>;

  template<typename Event> static consteval bool check_event()
  {
    static_assert(has_on_event_for<Receiver, Event, dispatcher_type>,
      "Receiver declares event in 'receives' but is missing "
      "on_event(Event, Dispatcher&) method");
    return true;
  }

  template<typename... Events> static consteval bool check_all(type_list<Events...> /*unused*/)
  {
    return (check_event<Events>() && ...);
  }

  static consteval bool validate() { return check_all(get_receives_t<Receiver>{}); }
};

// Validate all receivers in an EventLoop
template<typename... Receivers> struct ValidateReceivers
{
  template<typename EventLoopType> static consteval bool validate()
  {
    return (ReceiverValidator<Receivers, EventLoopType>::validate() && ...);
  }
};
} // namespace ev_loop
