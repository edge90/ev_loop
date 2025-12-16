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

// Count occurrences of type T in type list (for ECS-style counting)
template<typename List, typename T> struct count_of;

template<typename T, typename... Ts>
struct count_of<type_list<Ts...>, T> : std::integral_constant<std::size_t, ((std::is_same_v<T, Ts> ? 1 : 0) + ... + 0)>
{
};

template<typename List, typename T> inline constexpr std::size_t count_of_v = count_of<List, T>::value;

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

// Type-level search result (for fold-based searching)
template<std::size_t N> struct found_at
{
  static constexpr std::size_t value = N;
};
struct not_found
{
  static constexpr std::size_t value = ~std::size_t{ 0 };
};

// Fold operators for type-level search (first match wins)
template<std::size_t L> constexpr auto operator+(found_at<L>, auto) -> found_at<L>;

template<std::size_t R> constexpr auto operator+(not_found, found_at<R>) -> found_at<R>;

constexpr auto operator+(not_found, not_found) -> not_found;

// Filter type list by predicate - forward declaration, implementation after concat_type_lists
template<template<typename> class Pred, typename... Ts> struct filter_fold;
template<template<typename> class Pred, typename List> struct filter_list;


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

  // Direct construction - avoids default init + store() overhead.
  // Uses std::construct_at with parentheses (not braces) to avoid initializer_list ambiguity.
  template<typename E>
    requires(contains_v<type_list<Events...>, std::decay_t<E>>)
  explicit TaggedEvent(E&& event) : tag(index_of_v<std::decay_t<E>, Events...>)
  {
    std::construct_at(reinterpret_cast<std::decay_t<E>*>(storage.data()), std::forward<E>(event));
  }

  template<typename E> void store(E&& event)
  {
    destroy();
    using Decayed = std::decay_t<E>;
    tag = index_of_v<Decayed, Events...>;
    std::construct_at(reinterpret_cast<Decayed*>(storage.data()), std::forward<E>(event));
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
    const bool dispatched = ((tag == Is ? (destroy_type<Is>(), true) : false) || ...);
    [[assume(dispatched)]];
  }

  template<std::size_t I> void destroy_type()
  {
    using T = type_at_t<I, Events...>;
    if constexpr (!std::is_trivially_destructible_v<T>) {
      // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
      std::destroy_at(reinterpret_cast<T*>(storage.data()));
    }
  }

  void copy_construct_from(const TaggedEvent& other)
  {
    copy_at_index(other, std::make_index_sequence<sizeof...(Events)>{});
  }

  template<std::size_t... Is> void copy_at_index(const TaggedEvent& other, std::index_sequence<Is...> /*unused*/)
  {
    const bool dispatched = ((tag == Is ? (copy_type<Is>(other), true) : false) || ...);
    [[assume(dispatched)]];
  }

  template<std::size_t I> void copy_type(const TaggedEvent& other)
  {
    using T = type_at_t<I, Events...>;
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    std::construct_at(reinterpret_cast<T*>(storage.data()), *reinterpret_cast<const T*>(other.storage.data()));
  }

  void move_construct_from(TaggedEvent&& other)
  {
    move_at_index(std::move(other), std::make_index_sequence<sizeof...(Events)>{});
  }

  template<std::size_t... Is> void move_at_index(TaggedEvent&& other, std::index_sequence<Is...> /*unused*/)
  {
    const bool dispatched = ((tag == Is ? (move_type<Is>(std::move(other)), true) : false) || ...);
    [[assume(dispatched)]];
  }

  // NOLINTNEXTLINE(cppcoreguidelines-rvalue-reference-param-not-moved)
  template<std::size_t I> void move_type(TaggedEvent&& other)
  {
    using T = type_at_t<I, Events...>;
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    std::construct_at(reinterpret_cast<T*>(storage.data()), std::move(*reinterpret_cast<T*>(other.storage.data())));
  }
};

// Convert type_list to tagged event
template<typename List> struct to_tagged_event;

template<typename... Ts> struct to_tagged_event<type_list<Ts...>>
{
  using type = TaggedEvent<Ts...>;
};

template<typename List> using to_tagged_event_t = typename to_tagged_event<List>::type;

// Fast tagged event dispatch
template<typename Tagged, typename Func, std::size_t... Is>
// NOLINTNEXTLINE(cppcoreguidelines-missing-std-forward)
constexpr void fast_dispatch_impl(Tagged& tagged, Func&& func, std::index_sequence<Is...> /*unused*/)
{
  const bool dispatched = ((tagged.index() == Is ? (func(tagged.template get<Is>()), true) : false) || ...);
  [[assume(dispatched)]];
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

// Event-specific receiver predicates for ECS-style filtering
// Usage: filter_list_t<same_thread_receiver_for<Event>::template pred, receiver_list>
template<typename Event> struct same_thread_receiver_for
{
  template<typename R>
  struct pred
    : std::bool_constant<is_receiver<R> && get_thread_mode<R>() == ThreadMode::SameThread && can_receive<R, Event>>
  {
  };
};

template<typename Event> struct own_thread_receiver_for
{
  template<typename R>
  struct pred
    : std::bool_constant<is_receiver<R> && get_thread_mode<R>() == ThreadMode::OwnThread && can_receive<R, Event>>
  {
  };
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
      if (stop_.load(std::memory_order_relaxed)) [[unlikely]] { return nullptr; }
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
      if (stop_.load(std::memory_order_acquire)) [[unlikely]] { return nullptr; }
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
  template<typename E> void push_local_event(E&& event)
  {
    auto* slot = local_queue_.alloc_slot();
    if (slot) [[likely]] {
      slot->store(std::forward<E>(event));
      local_queue_.commit_push();
    }
  }

  // Called from other threads (synchronized) - wakes up waiting consumer
  template<typename E> void push_remote_event(E&& event)
  {
    TaggedEventType tagged(std::forward<E>(event));
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

template<typename EmitterType, typename EventLoopType> class TypedExternalEmitter;

// Forward declare typed dispatchers - simple wrappers for emit routing
template<typename EmitterType, typename EventLoopType> class SameThreadTypedDispatcher;
template<typename EmitterType, typename EventLoopType> class OwnThreadTypedDispatcher;

// =============================================================================
// Same-thread receiver wrapper
// =============================================================================

template<typename Receiver, typename EventLoopType> class SameThreadWrapper
{
public:
  // Use typed dispatcher for per-emitter routing tables
  using dispatcher_type = SameThreadTypedDispatcher<Receiver, EventLoopType>;

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
  using receives_list = get_receives_t<Receiver>;
  using tagged_event = to_tagged_event_t<receives_list>;

  // Automatically select queue type based on producer count
  // SPSC is safe when at most 1 producer thread, otherwise need MPSC
  static constexpr std::size_t producer_count = EventLoopType::template producer_count_for<Receiver>;
  using queue_type =
    std::conditional_t<(producer_count < 2), SPSCQueue<tagged_event>, ThreadSafeRingBuffer<tagged_event>>;

  using dispatcher_type = OwnThreadTypedDispatcher<Receiver, EventLoopType>;

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
    queue_.push(tagged_event(std::forward<Event>(event)));
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

// Operator for fold-based concatenation (ADL will find it)
template<typename... Ls, typename... Rs>
constexpr auto operator+(type_list<Ls...>, type_list<Rs...>) -> type_list<Ls..., Rs...>;

// Concatenate multiple type_lists using fold expression (no recursion)
template<typename... Lists> struct concat_type_lists
{
  using type = decltype((type_list<>{} + ... + std::declval<Lists>()));
};

template<> struct concat_type_lists<>
{
  using type = type_list<>;
};


// Collect all same-thread events - just concatenate without deduplication
// Assumes event types are unique across receivers (common case)
// For cases with duplicates, TaggedEvent handles it correctly (same type appears multiple times)
template<typename... Receivers>
using collect_same_thread_events_t = typename concat_type_lists<same_thread_events_from<Receivers>...>::type;

// Helper to get events received by a single OwnThread receiver (or empty list)
template<typename R>
using own_thread_events_from =
  std::conditional_t<is_receiver<R> && get_thread_mode<R>() == ThreadMode::OwnThread, get_receives_t<R>, type_list<>>;

// Collect all events received by OwnThread receivers
template<typename... Receivers>
using collect_own_thread_events_t = typename concat_type_lists<own_thread_events_from<Receivers>...>::type;

// Helper to get events emitted by a single OwnThread receiver (or empty list)
template<typename R>
using ot_emitted_events_from =
  std::conditional_t<get_thread_mode<R>() == ThreadMode::OwnThread, get_emits_t<R>, type_list<>>;

// Collect all events emitted by OwnThread receivers (ECS-style precomputation)
template<typename... Receivers>
using collect_ot_emitted_events_t = typename concat_type_lists<ot_emitted_events_from<Receivers>...>::type;

// Helper to get events emitted by external emitters (or empty list)
template<typename R>
using ext_emitted_events_from = std::conditional_t<is_external_emitter<R>, get_emits_t<R>, type_list<>>;

// Collect all events emitted by external emitters
template<typename... Receivers>
using collect_ext_emitted_events_t = typename concat_type_lists<ext_emitted_events_from<Receivers>...>::type;

// =============================================================================
// Filter implementation using concat (O(log N) depth instead of O(N) recursion)
// =============================================================================

template<template<typename> class Pred, typename... Ts> struct filter_fold
{
  using type = typename concat_type_lists<std::conditional_t<Pred<Ts>::value, type_list<Ts>, type_list<>>...>::type;
};

template<template<typename> class Pred, typename... Ts> using filter_t = typename filter_fold<Pred, Ts...>::type;

// Filter a type_list by predicate (expands type_list into filter_t)
template<template<typename> class Pred, typename... Ts> struct filter_list<Pred, type_list<Ts...>>
{
  using type = filter_t<Pred, Ts...>;
};

template<template<typename> class Pred, typename List> using filter_list_t = typename filter_list<Pred, List>::type;

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
  using own_thread_events = collect_own_thread_events_t<Receivers...>;
  using tagged_event = to_tagged_event_t<same_thread_events>;
  using queue_type = DualQueue<tagged_event>;

  // ECS-style precomputed emitter event lists
  using ot_emitted_events = collect_ot_emitted_events_t<Receivers...>;
  using ext_emitted_events = collect_ext_emitted_events_t<Receivers...>;

  // ECS-style per-event receiver lists - computed once per Event type, not per dispatch
  template<typename Event>
  using same_thread_receivers_for = filter_list_t<same_thread_receiver_for<Event>::template pred, receiver_list>;

  template<typename Event>
  using own_thread_receivers_for = filter_list_t<own_thread_receiver_for<Event>::template pred, receiver_list>;

  // Compile-time flag: true if OwnThread receivers emit to SameThread events
  // When true, the DualQueue's remote (thread-safe) queue is needed
private:
  template<typename OTEvents, std::size_t... Is>
  static consteval bool needs_remote_queue_impl(std::index_sequence<Is...> /*unused*/)
  {
    // Check if any OwnThread-emitted event is handled by SameThread receivers
    return ((contains_v<same_thread_events, type_list_at_t<Is, OTEvents>>) || ...);
  }

public:
  static constexpr bool needs_remote_queue =
    needs_remote_queue_impl<ot_emitted_events>(std::make_index_sequence<type_list_size_v<ot_emitted_events>>{});

  // Consteval checks for receiver existence - uses precomputed event lists
  template<typename Event> static consteval bool has_same_thread_receivers()
  {
    return contains_v<same_thread_events, Event>;
  }

  template<typename Event> static consteval bool has_own_thread_receivers()
  {
    return contains_v<own_thread_events, Event>;
  }

  // Count same-thread receivers for an event - uses precomputed same_thread_events list
  template<typename Event> static consteval std::size_t count_same_thread_receivers()
  {
    // Count occurrences in precomputed event list (O(M) simple comparisons vs O(N) complex checks)
    return count_of_v<same_thread_events, Event>;
  }

  // Find index of first SameThread receiver for an event using fold-based search
  template<typename Event, typename R, std::size_t I>
  using st_receiver_match = std::conditional_t<is_receiver<R> && get_thread_mode<R>() == ThreadMode::SameThread
                                                 && contains_v<get_receives_t<R>, Event>,
    found_at<I>,
    not_found>;

  template<typename Event, std::size_t... Is>
  static consteval std::size_t find_st_receiver_index_impl(std::index_sequence<Is...> /*unused*/)
  {
    using result =
      decltype((not_found{} + ... + std::declval<st_receiver_match<Event, type_list_at_t<Is, receiver_list>, Is>>()));
    return result::value;
  }

  template<typename Event> static consteval std::size_t find_st_receiver_index()
  {
    return find_st_receiver_index_impl<Event>(std::index_sequence_for<Receivers...>{});
  }

private:
  // ECS-style producer counting using consteval to avoid template instantiation overhead
  // Count SameThread emitters for an event (using consteval, not filter_list_t)
  template<typename Event> static consteval bool has_st_emitter_for_event()
  {
    return ((!is_external_emitter<Receivers> && get_thread_mode<Receivers>() == ThreadMode::SameThread
              && contains_v<get_emits_t<Receivers>, Event>)
            || ...);
  }

  // Count OwnThread emitters for an event
  template<typename Event> static consteval std::size_t count_ot_emitters_for_event()
  {
    return ((get_thread_mode<Receivers>() == ThreadMode::OwnThread && contains_v<get_emits_t<Receivers>, Event> ? 1 : 0)
            + ... + 0);
  }

  // Count external emitters for an event
  template<typename Event> static consteval std::size_t count_ext_emitters_for_event()
  {
    return ((is_external_emitter<Receivers> && contains_v<get_emits_t<Receivers>, Event> ? 1 : 0) + ... + 0);
  }

  // Count producers for a receiver that receives a single event
  template<typename Event> static consteval std::size_t count_producers_for_single_event()
  {
    return (has_st_emitter_for_event<Event>() ? 1 : 0) + count_ot_emitters_for_event<Event>()
           + count_ext_emitters_for_event<Event>();
  }

  // Compute producer count for a receiver using ECS emitter counts
  template<typename Receiver> struct producer_count_ecs
  {
    using receives = get_receives_t<Receiver>;
    static constexpr std::size_t num_receives = type_list_size_v<receives>;

    // Fast path: single event - direct consteval count
    static constexpr std::size_t value =
      (num_receives == 1) ? count_producers_for_single_event<type_list_at_t<0, receives>>() : 0;
  };

public:
  // Helper to compute producer count for a specific receiver - ECS-style
  template<typename Receiver> static constexpr std::size_t producer_count_for = producer_count_ecs<Receiver>::value;

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
    if constexpr (needs_remote_queue) {
      return queue_.try_pop();
    } else {
      return queue_.try_pop_local();
    }
  }

  void dispatch_event(tagged_event& event)
  {
    fast_dispatch(event, [this]<typename E>(E& event2) {
      // Use consteval count to avoid filter_list_t instantiation
      constexpr std::size_t count = count_same_thread_receivers<std::decay_t<E>>();
      if constexpr (count == 1) {
        // Use direct index lookup - no filter needed
        this->template dispatch_single_direct<std::decay_t<E>>(std::move(event2));
      } else if constexpr (count > 1) {
        // Multi-receiver case still needs the filtered list
        using receivers = same_thread_receivers_for<std::decay_t<E>>;
        this->fanout_to_same_thread_ecs(event2, receivers{});
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
  template<typename Event> void emit(Event&& event)
  {
    using E = std::decay_t<Event>;
    // Use consteval checks to avoid filter_list_t instantiation
    constexpr bool to_queue = has_same_thread_receivers<E>();
    constexpr bool to_threads = has_own_thread_receivers<E>();

    if constexpr (to_queue && to_threads) {
      queue_.push_local_event(event);
      push_to_own_thread(std::forward<Event>(event));
    } else if constexpr (to_queue) {
      queue_.push_local_event(std::forward<Event>(event));
    } else if constexpr (to_threads) {
      push_to_own_thread(std::forward<Event>(event));
    }
  }

  template<typename Receiver, typename Self> [[nodiscard]] auto& get(this Self& self) noexcept
  {
    return std::get<ReceiverStorage<Receiver, self_type>>(self.receivers_)->get();
  }

private:
  template<typename, typename> friend class TypedExternalEmitter;
  template<typename, typename> friend class SameThreadTypedDispatcher;
  template<typename, typename> friend class OwnThreadTypedDispatcher;
  template<typename...> friend class SharedEventLoopPtr;

  template<std::size_t I> void start_one()
  {
    if constexpr (is_receiver<type_list_at_t<I, receiver_list>>
                  && get_thread_mode<type_list_at_t<I, receiver_list>>() == ThreadMode::OwnThread) {
      std::get<I>(receivers_)->start();
    }
  }

  template<std::size_t I> void stop_one()
  {
    if constexpr (is_receiver<type_list_at_t<I, receiver_list>>
                  && get_thread_mode<type_list_at_t<I, receiver_list>>() == ThreadMode::OwnThread) {
      std::get<I>(receivers_)->stop();
    }
  }

  template<std::size_t... Is> void start_all(std::index_sequence<Is...> /*unused*/) { (start_one<Is>(), ...); }

  template<std::size_t... Is> void stop_all(std::index_sequence<Is...> /*unused*/) { (stop_one<Is>(), ...); }

  // ECS-style fanout: copy to first N-1, move to last
  template<typename Event, typename ReceiverList, std::size_t... Is>
  void fanout_copy_n(Event& event, std::index_sequence<Is...> /*unused*/)
  {
    (std::get<ReceiverStorage<type_list_at_t<Is, ReceiverList>, self_type>>(receivers_)->dispatch(event), ...);
  }

  template<typename Event, typename ReceiverList> void fanout_to_same_thread_ecs(Event& event, ReceiverList /*unused*/)
  {
    constexpr std::size_t count = type_list_size_v<ReceiverList>;
    if constexpr (count == 1) {
      using R = type_list_at_t<0, ReceiverList>;
      std::get<ReceiverStorage<R, self_type>>(receivers_)->dispatch(std::move(event));
    } else {
      // Copy to first N-1 receivers
      fanout_copy_n<Event, ReceiverList>(event, std::make_index_sequence<count - 1>{});
      // Move to last receiver
      using LastR = type_list_at_t<count - 1, ReceiverList>;
      std::get<ReceiverStorage<LastR, self_type>>(receivers_)->dispatch(std::move(event));
    }
  }

  // Direct dispatch using consteval index lookup - avoids filter_list_t instantiation
  template<typename Event> void dispatch_single_direct(Event&& event)
  {
    constexpr std::size_t idx = find_st_receiver_index<std::decay_t<Event>>();
    // Use index-based std::get (O(1)) instead of type-based (O(N) template instantiation)
    std::get<idx>(receivers_)->dispatch(std::forward<Event>(event));
  }

  // ECS-style push: copy to first N-1, move to last
  template<typename Event, typename ReceiverList, std::size_t... Is>
  void push_copy_n(Event& event, std::index_sequence<Is...> /*unused*/)
  {
    (std::get<ReceiverStorage<type_list_at_t<Is, ReceiverList>, self_type>>(receivers_)->push(event), ...);
  }

  template<typename Event> void push_to_own_thread(Event&& event)
  {
    using ReceiverList = own_thread_receivers_for<std::decay_t<Event>>;
    constexpr std::size_t count = type_list_size_v<ReceiverList>;
    if constexpr (count == 1) {
      using R = type_list_at_t<0, ReceiverList>;
      std::get<ReceiverStorage<R, self_type>>(receivers_)->push(std::forward<Event>(event));
    } else if constexpr (count > 1) {
      // Copy to first N-1 receivers
      push_copy_n<std::decay_t<Event>, ReceiverList>(event, std::make_index_sequence<count - 1>{});
      // Move to last receiver
      using LastR = type_list_at_t<count - 1, ReceiverList>;
      std::get<ReceiverStorage<LastR, self_type>>(receivers_)->push(std::forward<Event>(event));
    }
  }

  std::tuple<ReceiverStorage<Receivers, self_type>...> receivers_;
  queue_type queue_;
  std::atomic<bool> running_{ false };
};

// =============================================================================
// Typed dispatchers - each handles routing for its specific context
// =============================================================================

template<typename EmitterType, typename EventLoopType> class SameThreadTypedDispatcher
{
  // Use consteval checks to avoid filter_list_t instantiation
  template<typename E> static constexpr bool to_queue = EventLoopType::template has_same_thread_receivers<E>();
  template<typename E> static constexpr bool to_threads = EventLoopType::template has_own_thread_receivers<E>();

public:
  explicit SameThreadTypedDispatcher(EventLoopType* loop) : event_loop_(loop) {}

  template<typename Event>
    requires contains_v<get_emits_t<EmitterType>, std::decay_t<Event>>
  void emit(Event&& event)
  {
    using E = std::decay_t<Event>;
    if constexpr (to_queue<E> && to_threads<E>) {
      event_loop_->queue_.push_local_event(event);
      event_loop_->push_to_own_thread(std::forward<Event>(event));
    } else if constexpr (to_queue<E>) {
      event_loop_->queue_.push_local_event(std::forward<Event>(event));
    } else if constexpr (to_threads<E>) {
      event_loop_->push_to_own_thread(std::forward<Event>(event));
    }
  }

private:
  EventLoopType* event_loop_;
};

template<typename EmitterType, typename EventLoopType> class OwnThreadTypedDispatcher
{
  // Use consteval checks to avoid filter_list_t instantiation
  template<typename E> static constexpr bool to_queue = EventLoopType::template has_same_thread_receivers<E>();
  template<typename E> static constexpr bool to_threads = EventLoopType::template has_own_thread_receivers<E>();

public:
  explicit OwnThreadTypedDispatcher(EventLoopType* loop) : event_loop_(loop) {}

  template<typename Event>
    requires contains_v<get_emits_t<EmitterType>, std::decay_t<Event>>
  void emit(Event&& event)
  {
    using E = std::decay_t<Event>;
    if constexpr (to_queue<E> && to_threads<E>) {
      event_loop_->queue_.push_remote_event(event);
      event_loop_->push_to_own_thread(std::forward<Event>(event));
    } else if constexpr (to_queue<E>) {
      event_loop_->queue_.push_remote_event(std::forward<Event>(event));
    } else if constexpr (to_threads<E>) {
      event_loop_->push_to_own_thread(std::forward<Event>(event));
    }
  }

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
  using dispatcher_type = OwnThreadTypedDispatcher<EmitterType, EventLoopType>;

public:
  explicit TypedExternalEmitter(std::shared_ptr<EventLoopType> loop) noexcept : loop_(std::move(loop)) {}

  // Only allow emitting events declared in EmitterType::emits
  // Returns true if event was queued, false if EventLoop was destroyed
  template<typename Event>
    requires can_emit<EmitterType, Event>
  bool emit(Event&& event)
  {
    if (auto locked = loop_.lock()) {
      dispatcher_type dispatcher(locked.get());
      dispatcher.emit(std::forward<Event>(event));
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

} // namespace ev_loop
