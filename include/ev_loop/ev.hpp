#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <thread>
#include <tuple>
#include <type_traits>
#include <utility>

// MSVC doesn't support [[assume]] yet, use __assume instead
#ifdef _MSC_VER
#define EV_ASSUME(expr) __assume(expr)
#else
#define EV_ASSUME(expr) [[assume(expr)]]
#endif

namespace ev_loop {

// =============================================================================
// Public API types
// =============================================================================

template<typename... Ts> struct type_list
{
  // cppcheck-suppress unusedStructMember
  static constexpr std::size_t size = sizeof...(Ts);
};

// =============================================================================
// Strategy tag types for ThreadGroup
// =============================================================================

struct Spin
{
};
struct Wait
{
};
struct Yield
{
};
struct Hybrid
{
  // NOLINTNEXTLINE(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)
  std::size_t spin_count = 1000;
};

// =============================================================================
// ThreadGroup - groups receivers that share a thread with a strategy
// =============================================================================

template<typename Strategy, typename... Receivers> struct ThreadGroup
{
  using strategy = Strategy;
  using receivers = type_list<Receivers...>;
  static constexpr std::size_t receiver_count = sizeof...(Receivers);
};

// Convenience aliases
template<typename... Receivers> using SpinGroup = ThreadGroup<Spin, Receivers...>;
template<typename... Receivers> using WaitGroup = ThreadGroup<Wait, Receivers...>;
template<typename... Receivers> using YieldGroup = ThreadGroup<Yield, Receivers...>;
template<typename... Receivers> using HybridGroup = ThreadGroup<Hybrid, Receivers...>;

// =============================================================================
// Implementation details
// =============================================================================

namespace detail {

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
  struct count_of<type_list<Ts...>, T>
    : std::integral_constant<std::size_t, ((std::is_same_v<T, Ts> ? 1 : 0) + ... + 0)>
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
      // cppcheck-suppress redundantInitialization
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
    // cppcheck-suppress unusedStructMember
    static constexpr std::size_t value = N;
  };
  struct not_found
  {
    // cppcheck-suppress unusedStructMember
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
  // ThreadGroup type traits
  // =============================================================================

  // Detect if T is a ThreadGroup
  template<typename T> struct is_thread_group : std::false_type
  {
  };

  template<typename Strategy, typename... Receivers>
  struct is_thread_group<ThreadGroup<Strategy, Receivers...>> : std::true_type
  {
  };

  template<typename T> inline constexpr bool is_thread_group_v = is_thread_group<T>::value;

  // Extract receivers type_list from a ThreadGroup
  template<typename Group> struct group_receivers;

  template<typename Strategy, typename... Receivers> struct group_receivers<ThreadGroup<Strategy, Receivers...>>
  {
    using type = type_list<Receivers...>;
  };

  template<typename Group> using group_receivers_t = typename group_receivers<Group>::type;

  // Extract strategy from a ThreadGroup
  template<typename Group> struct group_strategy;

  template<typename Strategy, typename... Receivers> struct group_strategy<ThreadGroup<Strategy, Receivers...>>
  {
    using type = Strategy;
  };

  template<typename Group> using group_strategy_t = typename group_strategy<Group>::type;

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
        // cppcheck-suppress accessMoved ; destroy() only resets tag, safe on moved-from object
        other.destroy();
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
          // cppcheck-suppress accessMoved ; destroy() only resets tag, safe on moved-from object
          other.destroy();
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
      // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
      std::construct_at(reinterpret_cast<std::decay_t<E>*>(storage.data()), std::forward<E>(event));
    }

    template<typename E> void store(E&& event)
    {
      destroy();
      using Decayed = std::decay_t<E>;
      tag = index_of_v<Decayed, Events...>;
      // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
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

    [[nodiscard]] constexpr std::size_t index() const noexcept { return tag; }

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
      // cppcheck-suppress unreadVariable ; used by EV_ASSUME
      const bool dispatched = ((tag == Is ? (destroy_type<Is>(), true) : false) || ...);
      EV_ASSUME(dispatched);
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
      // cppcheck-suppress unreadVariable ; used by EV_ASSUME
      const bool dispatched = ((tag == Is ? (copy_type<Is>(other), true) : false) || ...);
      EV_ASSUME(dispatched);
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
      // cppcheck-suppress unreadVariable ; used by EV_ASSUME
      const bool dispatched = ((tag == Is ? (move_type<Is>(std::move(other)), true) : false) || ...);
      EV_ASSUME(dispatched);
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

  // Fast tagged event dispatch using fold expression
  template<typename Tagged, typename Func, std::size_t... Is>
  // NOLINTNEXTLINE(cppcoreguidelines-missing-std-forward)
  constexpr void fast_dispatch_impl(Tagged& tagged, Func&& func, std::index_sequence<Is...> /*unused*/)
  {
    // cppcheck-suppress unreadVariable ; used by EV_ASSUME
    const bool dispatched = ((tagged.index() == Is ? (func(tagged.template get<Is>()), true) : false) || ...);
    EV_ASSUME(dispatched);
  }

  template<typename... Events, typename Func> constexpr void fast_dispatch(TaggedEvent<Events...>& tagged, Func&& func)
  {
    fast_dispatch_impl(tagged, std::forward<Func>(func), std::make_index_sequence<sizeof...(Events)>{});
  }

  // =============================================================================
  // Concepts for receiver/emitter detection
  // =============================================================================

  template<typename T>
  concept has_receives = requires { typename T::receives; };

  template<typename T>
  concept has_emits = requires { typename T::emits; };

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

  // Cache line size for padding to avoid false sharing
  inline constexpr std::size_t cache_line_size = 64;

  // =============================================================================
  // Inbox: atomic ring buffer for cross-thread communication
  // Other groups and external emitters push here, owning group drains
  // =============================================================================

  // NOLINTNEXTLINE(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)
  template<typename T, std::size_t Capacity = 4096> class Inbox
  {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be power of 2");
    static constexpr std::size_t mask_ = Capacity - 1;

  public:
    // Push an event (producer side, may be called from other threads)
    bool push(T event)
    {
      const std::size_t head = head_.load(std::memory_order_acquire);
      const std::size_t tail = tail_.load(std::memory_order_relaxed);
      if (tail - head >= Capacity) [[unlikely]] { return false; }
      buffer_[tail & mask_] = std::move(event);
      tail_.store(tail + 1, std::memory_order_release);
      return true;
    }

    // Try to pop a single event (consumer side)
    bool try_pop(T& out)
    {
      const std::size_t tail = tail_.load(std::memory_order_acquire);
      const std::size_t head = head_.load(std::memory_order_relaxed);
      if (head >= tail) { return false; }
      out = std::move(buffer_[head & mask_]);
      head_.store(head + 1, std::memory_order_release);
      return true;
    }

    // Drain all events, calling func for each (consumer side)
    template<typename Func> std::size_t drain(Func&& func)
    {
      const std::size_t tail = tail_.load(std::memory_order_acquire);
      std::size_t head = head_.load(std::memory_order_relaxed);
      const std::size_t count = tail - head;

      for (std::size_t i = 0; i < count; ++i) {
        std::forward<Func>(func)(buffer_[head & mask_]);
        ++head;
      }

      head_.store(head, std::memory_order_release);
      return count;
    }

    // Check if empty (consumer side)
    [[nodiscard]] bool empty() const noexcept
    {
      return head_.load(std::memory_order_acquire) == tail_.load(std::memory_order_acquire);
    }

    [[nodiscard]] std::size_t size() const noexcept
    {
      const std::size_t tail = tail_.load(std::memory_order_acquire);
      const std::size_t head = head_.load(std::memory_order_acquire);
      return tail - head;
    }

  private:
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4324)
#endif
    alignas(cache_line_size) std::array<T, Capacity> buffer_{};
    alignas(cache_line_size) std::atomic<std::size_t> head_{ 0 };
    alignas(cache_line_size) std::atomic<std::size_t> tail_{ 0 };
#ifdef _MSC_VER
#pragma warning(pop)
#endif
  };

  // Backwards compatibility alias
  // NOLINTNEXTLINE(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)
  template<typename T, std::size_t Capacity = 4096> using Outbox = Inbox<T, Capacity>;

  // =============================================================================
  // GroupWorkSignal: notification primitive for inter-group communication
  // Producer groups signal, consumer groups wait or poll
  // =============================================================================

  class GroupWorkSignal
  {
  public:
    // Producer calls this to signal that work is available
    void notify_work_available() noexcept
    {
      signal_.fetch_add(1, std::memory_order_release);
      signal_.notify_one();
    }

    // Consumer blocks until work is signaled or stopped
    // Returns false if stopped, true if work might be available
    [[nodiscard]] bool wait_for_work() noexcept
    {
      if (stop_.load(std::memory_order_acquire)) [[unlikely]] { return false; }
      const auto sig = signal_.load(std::memory_order_acquire);
      // Check stop again after loading signal (avoid lost wakeup)
      if (stop_.load(std::memory_order_acquire)) [[unlikely]] { return false; }
      signal_.wait(sig, std::memory_order_acquire);
      return !stop_.load(std::memory_order_acquire);
    }

    // Non-blocking check if work was signaled since last check
    // Returns true if work might be available
    [[nodiscard]] bool try_consume() noexcept
    {
      // Just check if there have been any signals - the actual work
      // availability is determined by checking the outbox
      return signal_.load(std::memory_order_acquire) > 0;
    }

    // Stop the signal, wake all waiters
    void stop() noexcept
    {
      stop_.store(true, std::memory_order_release);
      signal_.fetch_add(1, std::memory_order_release);
      signal_.notify_all();
    }

    // Check if stopped
    [[nodiscard]] bool is_stopped() const noexcept { return stop_.load(std::memory_order_acquire); }

    // Reset signal state (for reuse)
    void reset() noexcept
    {
      stop_.store(false, std::memory_order_release);
      signal_.store(0, std::memory_order_release);
    }

  private:
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4324)
#endif
    alignas(cache_line_size) std::atomic<std::size_t> signal_{ 0 };
    alignas(cache_line_size) std::atomic<bool> stop_{ false };
#ifdef _MSC_VER
#pragma warning(pop)
#endif
  };

  // =============================================================================
  // GroupStorage: holds receivers for a single ThreadGroup
  // All receivers in a group share a thread, so no per-receiver wrapping needed
  // =============================================================================

  template<typename Group> class GroupStorage;

  template<typename Strategy, typename... Receivers> class GroupStorage<ThreadGroup<Strategy, Receivers...>>
  {
  public:
    using group_type = ThreadGroup<Strategy, Receivers...>;
    using strategy_type = Strategy;
    using receiver_tuple = std::tuple<Receivers...>;
    static constexpr std::size_t receiver_count = sizeof...(Receivers);

    // Default construction - receivers are default-constructed
    GroupStorage() = default;

    // Construction with specific receiver instances
    explicit GroupStorage(Receivers... receivers) : receivers_(std::move(receivers)...) {}

    // Access receiver by type
    template<typename Receiver, typename Self> [[nodiscard]] auto& get(this Self& self) noexcept
    {
      static_assert(contains_v<type_list<Receivers...>, Receiver>, "Receiver not in this group");
      return std::get<Receiver>(self.receivers_);
    }

    // Access receiver by index
    template<std::size_t I, typename Self> [[nodiscard]] auto& get_at(this Self& self) noexcept
    {
      static_assert(I < receiver_count, "Index out of bounds");
      return std::get<I>(self.receivers_);
    }

    // Get the underlying tuple
    template<typename Self> [[nodiscard]] auto& receivers(this Self& self) noexcept { return self.receivers_; }

  private:
    receiver_tuple receivers_;
  };

  // =============================================================================
  // GroupRunner: runs the event loop for a ThreadGroup with its strategy
  // =============================================================================

  // Forward declaration - will be specialized for each strategy
  template<typename Group, std::size_t GroupIndex, typename EventLoopType> class GroupRunner;

  // Base functionality shared by all group runners
  template<typename Group, std::size_t GroupIndex, typename EventLoopType> class GroupRunnerBase
  {
  protected:
    using storage_type = GroupStorage<Group>;
    using strategy_type = typename Group::strategy;
    static constexpr std::size_t group_index = GroupIndex;

    // NOLINTNEXTLINE(cppcoreguidelines-avoid-const-or-ref-data-members)
    EventLoopType& event_loop_;
    GroupWorkSignal& signal_;
    std::atomic<bool>& running_;

  public:
    GroupRunnerBase(EventLoopType& loop, GroupWorkSignal& sig, std::atomic<bool>& running) noexcept
      : event_loop_(loop), signal_(sig), running_(running)
    {}

    [[nodiscard]] bool is_running() const noexcept { return running_.load(std::memory_order_acquire); }

    void stop() noexcept { signal_.stop(); }
  };

  // Strategy runner: runs with strategy-specific behavior
  template<typename Strategy, typename... Receivers, std::size_t GroupIndex, typename EventLoopType>
  class GroupRunner<ThreadGroup<Strategy, Receivers...>, GroupIndex, EventLoopType>
    : public GroupRunnerBase<ThreadGroup<Strategy, Receivers...>, GroupIndex, EventLoopType>
  {
    using Base = GroupRunnerBase<ThreadGroup<Strategy, Receivers...>, GroupIndex, EventLoopType>;
    using Group = ThreadGroup<Strategy, Receivers...>;

  public:
    using Base::Base;

    // Single poll iteration - returns true if work was done
    [[nodiscard]] bool poll() { return this->event_loop_.template poll_group<GroupIndex>(); }

    // Run until stopped - strategy-specific behavior
    void run()
    {
      if constexpr (std::is_same_v<Strategy, Spin>) {
        run_spin();
      } else if constexpr (std::is_same_v<Strategy, Wait>) {
        run_wait();
      } else if constexpr (std::is_same_v<Strategy, Yield>) {
        run_yield();
      } else if constexpr (std::is_same_v<Strategy, Hybrid>) {
        run_hybrid();
      }
    }

    template<typename Predicate> void run_while(Predicate&& pred)
    {
      if constexpr (std::is_same_v<Strategy, Spin>) {
        while (this->is_running() && pred()) { (void)poll(); }
      } else if constexpr (std::is_same_v<Strategy, Wait>) {
        while (this->is_running() && pred()) {
          if (!poll()) { this->signal_.wait_for_work(); }
        }
      } else if constexpr (std::is_same_v<Strategy, Yield>) {
        while (this->is_running() && pred()) {
          if (!poll()) { std::this_thread::yield(); }
        }
      } else if constexpr (std::is_same_v<Strategy, Hybrid>) {
        run_hybrid_while(std::forward<Predicate>(pred));
      }
    }

  private:
    void run_spin()
    {
      while (this->is_running()) { (void)poll(); }
    }

    void run_wait()
    {
      while (this->is_running()) {
        if (!poll()) { this->signal_.wait_for_work(); }
      }
    }

    void run_yield()
    {
      while (this->is_running()) {
        if (!poll()) { std::this_thread::yield(); }
      }
    }

    void run_hybrid()
    {
      constexpr std::size_t default_spin_count = 1000;
      std::size_t empty_spins = 0;
      while (this->is_running()) {
        if (poll()) {
          empty_spins = 0;
        } else {
          ++empty_spins;
          if (empty_spins >= default_spin_count) {
            empty_spins = 0;
            this->signal_.wait_for_work();
          }
        }
      }
    }

    template<typename Predicate> void run_hybrid_while(Predicate&& pred)
    {
      constexpr std::size_t default_spin_count = 1000;
      std::size_t empty_spins = 0;
      while (this->is_running() && std::forward<Predicate>(pred)()) {
        if (poll()) {
          empty_spins = 0;
        } else {
          ++empty_spins;
          if (empty_spins >= default_spin_count) {
            empty_spins = 0;
            this->signal_.wait_for_work();
          }
        }
      }
    }
  };

  // =============================================================================
  // GroupOutboxes: per-group outbox management for inter-group communication
  // Each group has one outbox per destination group (pull-based model)
  // =============================================================================

  // NOLINTNEXTLINE(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)
  template<std::size_t GroupCount, typename TaggedEvent, std::size_t Capacity = 4096> class GroupOutboxes
  {
    static_assert(GroupCount > 0, "Must have at least one group");

  public:
    // Push an event to a specific destination group's outbox
    // Returns false if the outbox is full
    template<std::size_t DestGroup> bool push(TaggedEvent event)
    {
      static_assert(DestGroup < GroupCount, "Destination group index out of bounds");
      return outboxes_[DestGroup].push(std::move(event));
    }

    // Push to destination group by runtime index
    bool push_to(std::size_t dest_group, TaggedEvent event)
    {
      if (dest_group >= GroupCount) [[unlikely]] { return false; }
      return outboxes_[dest_group].push(std::move(event));
    }

    // Get outbox for a specific destination (for direct access)
    template<std::size_t DestGroup> [[nodiscard]] Outbox<TaggedEvent, Capacity>& outbox() noexcept
    {
      static_assert(DestGroup < GroupCount, "Destination group index out of bounds");
      return outboxes_[DestGroup];
    }

    template<std::size_t DestGroup> [[nodiscard]] const Outbox<TaggedEvent, Capacity>& outbox() const noexcept
    {
      static_assert(DestGroup < GroupCount, "Destination group index out of bounds");
      return outboxes_[DestGroup];
    }

    // Check if any outbox has pending events
    [[nodiscard]] bool any_pending() const noexcept
    {
      for (const auto& outbox : outboxes_) {
        if (!outbox.empty()) { return true; }
      }
      return false;
    }

    // Get total pending count across all outboxes
    [[nodiscard]] std::size_t total_pending() const noexcept
    {
      std::size_t total = 0;
      for (const auto& outbox : outboxes_) { total += outbox.size(); }
      return total;
    }

  private:
    std::array<Outbox<TaggedEvent, Capacity>, GroupCount> outboxes_;
  };

  // =============================================================================
  // GroupDispatcher: allows receivers to emit events to other groups
  // =============================================================================

  template<typename SourceGroup, std::size_t GroupIndex, typename EventLoopType> class GroupDispatcher
  {
  public:
    explicit GroupDispatcher(EventLoopType& loop) noexcept : event_loop_(loop) {}

    // Emit an event - routes to appropriate group(s)
    template<typename Event> void emit(Event&& event)
    {
      event_loop_.template emit_from_group<GroupIndex>(std::forward<Event>(event));
    }

  private:
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-const-or-ref-data-members)
    EventLoopType& event_loop_;
  };

  // Operator for fold-based concatenation
  template<typename... Ls, typename... Rs>
  consteval auto operator+(type_list<Ls...> /*unused*/, type_list<Rs...> /*unused*/) -> type_list<Ls..., Rs...>
  {
    return {};
  }

  // Helper function for MSVC - fold in function body lets compiler deduce type
  template<typename... Lists> consteval auto concat_type_lists_fn() { return (type_list<>{} + ... + Lists{}); }

  // Concatenate multiple type_lists using fold expression (O(1) instantiation depth)
  template<typename... Lists> struct concat_type_lists
  {
    using type = decltype(concat_type_lists_fn<Lists...>());
  };

  template<> struct concat_type_lists<>
  {
    using type = type_list<>;
  };


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
  // Unique type list (deduplication) - O(N^2) but N is typically small
  // =============================================================================

  // Check if T is already in Seen list
  template<typename T, typename Seen> struct not_in_seen : std::bool_constant<!contains_v<Seen, T>>
  {
  };

  // Fold-based unique: accumulate unique types
  template<typename Seen, typename T>
  using unique_accumulate =
    std::conditional_t<contains_v<Seen, T>, Seen, typename concat_type_lists<Seen, type_list<T>>::type>;

  // Unique implementation using fold
  template<typename... Ts> struct unique_fold
  {
    template<typename Acc, typename T> using folder = unique_accumulate<Acc, T>;

    // Manual fold - start with empty, accumulate each type
    template<typename Acc, typename First, typename... Rest> static consteval auto fold_impl()
    {
      if constexpr (sizeof...(Rest) == 0) {
        return folder<Acc, First>{};
      } else {
        return fold_impl<folder<Acc, First>, Rest...>();
      }
    }

    using type = std::conditional_t<sizeof...(Ts) == 0, type_list<>, decltype(fold_impl<type_list<>, Ts...>())>;
  };

  template<typename... Ts> using unique_t = typename unique_fold<Ts...>::type;

  // Unique over a type_list
  template<typename List> struct unique_list;
  template<typename... Ts> struct unique_list<type_list<Ts...>>
  {
    using type = unique_t<Ts...>;
  };
  template<typename List> using unique_list_t = typename unique_list<List>::type;

} // namespace detail

// =============================================================================
// GroupEventLoop - group-based event loop with explicit threading control
// =============================================================================

namespace detail {

  // Collect all events handled by receivers in a group
  template<typename Group> struct group_all_events;

  template<typename Strategy, typename... Receivers> struct group_all_events<ThreadGroup<Strategy, Receivers...>>
  {
    using type = typename concat_type_lists<get_receives_t<Receivers>...>::type;
  };

  template<typename Group> using group_all_events_t = typename group_all_events<Group>::type;

  // Collect all events emitted by receivers in a group
  template<typename Group> struct group_emitted_events;

  template<typename Strategy, typename... Receivers> struct group_emitted_events<ThreadGroup<Strategy, Receivers...>>
  {
    using type = typename concat_type_lists<get_emits_t<Receivers>...>::type;
  };

  template<typename Group> using group_emitted_events_t = typename group_emitted_events<Group>::type;

  // Check if a group handles a specific event
  template<typename Group, typename Event> struct group_handles_event;

  template<typename Strategy, typename... Receivers, typename Event>
  struct group_handles_event<ThreadGroup<Strategy, Receivers...>, Event>
    : std::bool_constant<(contains_v<get_receives_t<Receivers>, Event> || ...)>
  {
  };

  template<typename Group, typename Event>
  inline constexpr bool group_handles_event_v = group_handles_event<Group, Event>::value;

  // Find receivers in a group that handle a specific event
  template<typename Event> struct group_receiver_for_event
  {
    template<typename R> using pred = std::bool_constant<is_receiver<R> && contains_v<get_receives_t<R>, Event>>;
  };

  template<typename Group, typename Event> struct group_receivers_for_event;

  template<typename Strategy, typename... Receivers, typename Event>
  struct group_receivers_for_event<ThreadGroup<Strategy, Receivers...>, Event>
  {
    using type = filter_list_t<group_receiver_for_event<Event>::template pred, type_list<Receivers...>>;
  };

  template<typename Group, typename Event>
  using group_receivers_for_event_t = typename group_receivers_for_event<Group, Event>::type;

  // Count receivers in a group that handle a specific event
  template<typename Group, typename Event>
  inline constexpr std::size_t group_receiver_count_for_event_v =
    type_list_size_v<group_receivers_for_event_t<Group, Event>>;

  // Count groups that handle a specific event (for move optimization)
  template<typename Event, typename... Groups>
  inline constexpr std::size_t count_groups_handling_event_v =
    ((group_handles_event_v<Groups, Event> ? 1 : 0) + ... + 0);

  // Get indices of groups that handle a specific event
  template<typename Event, typename... Groups> struct groups_handling_event_indices
  {
  private:
    template<std::size_t... Is> static consteval auto filter_impl(std::index_sequence<Is...> /*unused*/)
    {
      // Build array of indices for groups that handle the event
      constexpr std::size_t count = count_groups_handling_event_v<Event, Groups...>;
      std::array<std::size_t, count> result{};
      std::size_t idx = 0;
      // cppcheck-suppress unreadVariable ; used by assignment
      // Use comma fold (not || which short-circuits after first match)
      ((void)(group_handles_event_v<type_at_t<Is, Groups...>, Event> ? (result[idx++] = Is) : 0), ...);
      return result;
    }

  public:
    static constexpr auto indices = filter_impl(std::make_index_sequence<sizeof...(Groups)>{});
  };

  // =============================================================================
  // Per-event-type queues (ECS/data-oriented design)
  // =============================================================================

  // Create tuple of Inbox<Event> for each unique event type
  template<typename EventList> struct make_event_queues;
  template<typename... Events> struct make_event_queues<type_list<Events...>>
  {
    using type = std::tuple<Inbox<Events>...>;
  };
  template<typename EventList> using make_event_queues_t = typename make_event_queues<EventList>::type;

  // GroupEventQueues: holds per-event-type queues for a group
  template<typename Group> struct GroupEventQueues
  {
    using handled_events = unique_list_t<group_all_events_t<Group>>;
    using queues_type = make_event_queues_t<handled_events>;

    queues_type queues_;

    // Push to the queue for a specific event type (copy)
    template<typename Event> bool push(const Event& event)
    {
      return std::get<Inbox<Event>>(queues_).push(Event{ event });
    }

    // Push to the queue for a specific event type (move)
    template<typename Event> bool push(Event&& event)
    {
      return std::get<Inbox<std::decay_t<Event>>>(queues_).push(std::forward<Event>(event));
    }

    // Try to pop from a specific event type's queue
    template<typename Event> bool try_pop(Event& out) { return std::get<Inbox<Event>>(queues_).try_pop(out); }

    // Check if all queues are empty
    template<std::size_t... Is> [[nodiscard]] bool empty_impl(std::index_sequence<Is...> /*unused*/) const noexcept
    {
      return (std::get<Is>(queues_).empty() && ...);
    }
    [[nodiscard]] bool empty() const noexcept
    {
      return empty_impl(std::make_index_sequence<std::tuple_size_v<queues_type>>{});
    }
  };

} // namespace detail

template<typename... Groups> class GroupEventLoop
{
  static_assert(sizeof...(Groups) > 0, "At least one ThreadGroup is required");
  static_assert((detail::is_thread_group_v<Groups> && ...), "All parameters must be ThreadGroups");

public:
  using self_type = GroupEventLoop<Groups...>;
  static constexpr std::size_t group_count = sizeof...(Groups);

  // Collect all events handled across all groups
  using all_events = typename detail::concat_type_lists<detail::group_all_events_t<Groups>...>::type;
  using tagged_event = detail::to_tagged_event_t<all_events>;

  GroupEventLoop() = default;
  ~GroupEventLoop() { stop(); }

  GroupEventLoop(const GroupEventLoop&) = delete;
  GroupEventLoop& operator=(const GroupEventLoop&) = delete;
  GroupEventLoop(GroupEventLoop&&) = delete;
  GroupEventLoop& operator=(GroupEventLoop&&) = delete;

  // Start all groups on their own threads (returns immediately)
  void start()
  {
    running_.store(true, std::memory_order_release);
    start_all_groups(std::make_index_sequence<group_count>{});
  }

  // Run group I on the current thread, start others on their own threads
  // Blocks until stopped
  template<std::size_t I> void run()
  {
    static_assert(I < group_count, "Group index out of bounds");
    running_.store(true, std::memory_order_release);
    start_groups_except<I>(std::make_index_sequence<group_count>{});
    run_group<I>();
  }

  // Wait for all groups to finish
  void join() { join_all_groups(std::make_index_sequence<group_count>{}); }

  // Stop all groups
  void stop()
  {
    running_.store(false, std::memory_order_release);
    stop_all_signals(std::make_index_sequence<group_count>{});
    join();
  }

  // Check if running
  [[nodiscard]] bool is_running() const noexcept { return running_.load(std::memory_order_acquire); }

  // Access a receiver by type (searches all groups)
  template<typename Receiver, typename Self> [[nodiscard]] auto& get(this Self& self) noexcept
  {
    constexpr std::size_t group_idx = find_receiver_group_index<Receiver>();
    static_assert(group_idx < group_count, "Receiver not found in any group");
    return std::get<group_idx>(self.storage_).template get<Receiver>();
  }

  // Access group storage by index
  template<std::size_t I, typename Self> [[nodiscard]] auto& group(this Self& self) noexcept
  {
    static_assert(I < group_count, "Group index out of bounds");
    return std::get<I>(self.storage_);
  }

  // Emit an event from external (finds appropriate groups and routes)
  template<typename Event> void emit(Event&& event)
  {
    route_external_event(std::forward<Event>(event), std::make_index_sequence<group_count>{});
  }

  // Emit an event from a specific group (called by GroupDispatcher)
  template<std::size_t SourceGroup, typename Event> void emit_from_group(Event&& event)
  {
    static_assert(SourceGroup < group_count, "Source group index out of bounds");
    route_internal_event<SourceGroup>(std::forward<Event>(event), std::make_index_sequence<group_count>{});
  }

  // Poll a specific group - drains inbound events and dispatches to receivers
  // Returns true if any work was done
  template<std::size_t GroupIndex> bool poll_group()
  {
    static_assert(GroupIndex < group_count, "Group index out of bounds");
    return poll_group_impl<GroupIndex>(std::make_index_sequence<group_count>{});
  }

private:
  // Find which group contains a receiver type
  template<typename Receiver> static consteval std::size_t find_receiver_group_index()
  {
    return find_receiver_group_index_impl<Receiver, 0, Groups...>();
  }

  template<typename Receiver, std::size_t I> static consteval std::size_t find_receiver_group_index_impl()
  {
    return group_count; // Not found
  }

  template<typename Receiver, std::size_t I, typename First, typename... Rest>
  static consteval std::size_t find_receiver_group_index_impl()
  {
    if constexpr (detail::contains_v<detail::group_receivers_t<First>, Receiver>) {
      return I;
    } else {
      return find_receiver_group_index_impl<Receiver, I + 1, Rest...>();
    }
  }

  // Start all groups on threads
  template<std::size_t... Is> void start_all_groups(std::index_sequence<Is...> /*unused*/)
  {
    (start_group_thread<Is>(), ...);
  }

  // Start all groups except one
  template<std::size_t Skip, std::size_t... Is> void start_groups_except(std::index_sequence<Is...> /*unused*/)
  {
    (start_group_if_not<Is, Skip>(), ...);
  }

  template<std::size_t I, std::size_t Skip> void start_group_if_not()
  {
    if constexpr (I != Skip) { start_group_thread<I>(); }
  }

  template<std::size_t I> void start_group_thread()
  {
    std::get<I>(threads_) = std::thread([this] { run_group<I>(); });
  }

  template<std::size_t I> void run_group()
  {
    using Group = detail::type_at_t<I, Groups...>;
    detail::GroupRunner<Group, I, self_type> runner(*this, std::get<I>(signals_), running_);
    runner.run();
  }

  // Join all group threads
  template<std::size_t... Is> void join_all_groups(std::index_sequence<Is...> /*unused*/) { (join_group<Is>(), ...); }

  template<std::size_t I> void join_group()
  {
    if (std::get<I>(threads_).joinable()) { std::get<I>(threads_).join(); }
  }

  // Stop all signals
  template<std::size_t... Is> void stop_all_signals(std::index_sequence<Is...> /*unused*/)
  {
    (std::get<Is>(signals_).stop(), ...);
  }

  // Route an external event to appropriate groups (via external queue)
  // Uses copy-to-N-1, move-to-last optimization
  template<typename Event, std::size_t... Is>
  void route_external_event(Event&& event, std::index_sequence<Is...> /*unused*/)
  {
    using E = std::decay_t<Event>;
    constexpr std::size_t handler_count = detail::count_groups_handling_event_v<E, Groups...>;

    if constexpr (handler_count == 0) {
      // No groups handle this event
    } else if constexpr (handler_count == 1) {
      // Single handler: move directly
      (push_external_move<Is, E>(std::forward<Event>(event)), ...);
    } else {
      // Multiple handlers: copy to N-1, move to last
      constexpr auto indices = detail::groups_handling_event_indices<E, Groups...>::indices;
      push_external_copy_n<E>(event, indices, std::make_index_sequence<handler_count - 1>{});
      // Move to last
      constexpr std::size_t last_group = indices[handler_count - 1];
      push_external_move<last_group, E>(std::forward<Event>(event));
    }
  }

  // Push external event with move semantics
  template<std::size_t DestGroup, typename Event, typename E> void push_external_move(E&& event)
  {
    using Group = detail::type_at_t<DestGroup, Groups...>;
    if constexpr (detail::group_handles_event_v<Group, Event>) {
      std::get<DestGroup>(queues_).template push<Event>(std::forward<E>(event));
      std::get<DestGroup>(signals_).notify_work_available();
    }
  }

  // Push external event with copy semantics
  template<std::size_t DestGroup, typename Event> void push_external_copy(const Event& event)
  {
    using Group = detail::type_at_t<DestGroup, Groups...>;
    if constexpr (detail::group_handles_event_v<Group, Event>) {
      std::get<DestGroup>(queues_).template push<Event>(event);
      std::get<DestGroup>(signals_).notify_work_available();
    }
  }

  // Copy to first N-1 external groups
  template<typename Event, std::size_t N, std::size_t... Is>
  void push_external_copy_n(const Event& event,
    const std::array<std::size_t, N>& indices,
    std::index_sequence<Is...> /*unused*/)
  {
    (push_external_by_index_copy<Event>(event, indices[Is]), ...);
  }

  template<typename Event> void push_external_by_index_copy(const Event& event, std::size_t dest_group)
  {
    push_external_by_index_copy_impl<Event>(event, dest_group, std::make_index_sequence<group_count>{});
  }

  template<typename Event, std::size_t... Is>
  void
    push_external_by_index_copy_impl(const Event& event, std::size_t dest_group, std::index_sequence<Is...> /*unused*/)
  {
    // NOLINTNEXTLINE(readability-simplify-boolean-expr)
    (void)((dest_group == Is ? (push_external_copy<Is, Event>(event), true) : false) || ...);
  }

  // Route an internal event (from a receiver in SourceGroup) to appropriate groups
  // Uses copy-to-N-1, move-to-last optimization via pure compile-time iteration
  template<std::size_t SourceGroup, typename Event, std::size_t... Is>
  void route_internal_event(Event&& event, std::index_sequence<Is...> /*unused*/)
  {
    using E = std::decay_t<Event>;
    constexpr std::size_t handler_count = detail::count_groups_handling_event_v<E, Groups...>;

    if constexpr (handler_count == 0) {
      // No groups handle this event
    } else if constexpr (handler_count == 1) {
      // Single handler: move directly (only one will actually push due to if constexpr)
      (push_to_group_impl<SourceGroup, Is, E, true>(std::forward<Event>(event)), ...);
    } else {
      // Multiple handlers: copy to N-1, move to last using compile-time counter
      route_to_multiple_groups<SourceGroup, E, 0, handler_count>(
        std::forward<Event>(event), std::index_sequence<Is...>{});
    }
  }

  // Route to multiple groups with compile-time counter for copy vs move decision
  template<std::size_t SourceGroup, typename Event, std::size_t Seen, std::size_t Total>
  void route_to_multiple_groups(Event&& /*event*/, std::index_sequence<> /*unused*/)
  {
    // Base case: no more groups to check
  }

  template<std::size_t SourceGroup,
    typename Event,
    std::size_t Seen,
    std::size_t Total,
    std::size_t First,
    std::size_t... Rest>
  void route_to_multiple_groups(Event&& event, std::index_sequence<First, Rest...> /*unused*/)
  {
    using Group = detail::type_at_t<First, Groups...>;
    if constexpr (detail::group_handles_event_v<Group, Event>) {
      constexpr std::size_t new_seen = Seen + 1;
      constexpr bool is_last = (new_seen == Total);
      if constexpr (is_last) {
        // Last handler: move
        push_to_group_impl<SourceGroup, First, Event, true>(std::forward<Event>(event));
      } else {
        // Not last: copy
        push_to_group_impl<SourceGroup, First, Event, false>(event);
        route_to_multiple_groups<SourceGroup, Event, new_seen, Total>(
          std::forward<Event>(event), std::index_sequence<Rest...>{});
      }
    } else {
      // This group doesn't handle the event, continue to next
      route_to_multiple_groups<SourceGroup, Event, Seen, Total>(
        std::forward<Event>(event), std::index_sequence<Rest...>{});
    }
  }

  // Unified push implementation: Move=true for move, Move=false for copy
  template<std::size_t SourceGroup, std::size_t DestGroup, typename Event, bool Move, typename E>
  void push_to_group_impl(E&& event)
  {
    using Group = detail::type_at_t<DestGroup, Groups...>;
    if constexpr (detail::group_handles_event_v<Group, Event>) {
      if constexpr (Move) {
        std::get<DestGroup>(queues_).template push<Event>(std::forward<E>(event));
      } else {
        std::get<DestGroup>(queues_).template push<Event>(Event{ event }); // explicit copy
      }
      if constexpr (SourceGroup != DestGroup) { std::get<DestGroup>(signals_).notify_work_available(); }
    }
  }

  // ECS-style poll: drain all events of each type sequentially (cache-friendly)
  // Returns true if any work was done
  template<std::size_t GroupIndex, std::size_t... /*SourceGroups*/>
  bool poll_group_impl(std::index_sequence<> /*unused*/)
  {
    using Group = detail::type_at_t<GroupIndex, Groups...>;
    using handled_events = typename detail::GroupEventQueues<Group>::handled_events;
    return poll_all_event_types<GroupIndex>(handled_events{});
  }

  // Overload for index_sequence with indices (forward to parameterless version)
  template<std::size_t GroupIndex, std::size_t... SourceGroups>
  bool poll_group_impl(std::index_sequence<SourceGroups...> /*unused*/)
  {
    return poll_group_impl<GroupIndex>(std::index_sequence<>{});
  }

  // Poll one event from each event type's queue in order
  template<std::size_t GroupIndex, typename... Events> bool poll_all_event_types(type_list<Events...> /*unused*/)
  {
    // Try each event type's queue, return true if any had work
    return (poll_one_event_type<GroupIndex, Events>() || ...);
  }

  // Poll one event from a specific event type's queue
  template<std::size_t GroupIndex, typename Event> bool poll_one_event_type()
  {
    auto& group_queues = std::get<GroupIndex>(queues_);
    Event event;
    if (group_queues.template try_pop<Event>(event)) {
      dispatch_typed_event_to_group<GroupIndex, Event>(event);
      return true;
    }
    return false;
  }

  // Dispatch a typed event directly to receivers (no variant overhead)
  template<std::size_t GroupIndex, typename Event> void dispatch_typed_event_to_group(Event& event)
  {
    using Group = detail::type_at_t<GroupIndex, Groups...>;
    auto& storage = std::get<GroupIndex>(storage_);
    dispatch_to_receivers_in_group<GroupIndex, Group, Event>(storage, event);
  }

  // Direct dispatch of a typed event to receivers in a specific group (no queue, no tagging)
  template<std::size_t GroupIndex, typename Event, typename E> void dispatch_event_to_group(E&& event)
  {
    using Group = detail::type_at_t<GroupIndex, Groups...>;
    auto& storage = std::get<GroupIndex>(storage_);
    Event typed_event = std::forward<E>(event);
    dispatch_to_receivers_in_group<GroupIndex, Group, Event>(storage, typed_event);
  }

  // Dispatch to all receivers in a group that handle this event type
  // Uses copy-to-N-1, move-to-last optimization
  template<std::size_t GroupIndex, typename Group, typename Event, typename Storage>
  void dispatch_to_receivers_in_group(Storage& storage, Event& event)
  {
    // Get only receivers that handle this event type (filtered at compile time)
    using handling_receivers = detail::group_receivers_for_event_t<Group, Event>;
    constexpr std::size_t count = detail::type_list_size_v<handling_receivers>;

    if constexpr (count == 0) {
      // No receivers handle this event
    } else if constexpr (count == 1) {
      // Single receiver: move
      using R = detail::type_list_at_t<0, handling_receivers>;
      dispatch_to_receiver_move<GroupIndex, R, Event>(storage, std::move(event));
    } else {
      // Multiple receivers: copy to N-1, move to last
      dispatch_to_receivers_copy_n<GroupIndex, Event>(
        storage, event, handling_receivers{}, std::make_index_sequence<count - 1>{});
      using LastR = detail::type_list_at_t<count - 1, handling_receivers>;
      dispatch_to_receiver_move<GroupIndex, LastR, Event>(storage, std::move(event));
    }
  }

  // Dispatch with copy semantics (for first N-1 receivers)
  template<std::size_t GroupIndex, typename Event, typename Storage, typename ReceiverList, std::size_t... Is>
  void dispatch_to_receivers_copy_n(Storage& storage,
    const Event& event,
    ReceiverList /*unused*/,
    std::index_sequence<Is...> /*unused*/)
  {
    (dispatch_to_receiver_copy<GroupIndex, detail::type_list_at_t<Is, ReceiverList>, Event>(storage, event), ...);
  }

  // Dispatch with copy semantics
  template<std::size_t GroupIndex, typename Receiver, typename Event, typename Storage>
  void dispatch_to_receiver_copy(Storage& storage, const Event& event)
  {
    auto& receiver = storage.template get<Receiver>();
    detail::GroupDispatcher<detail::type_at_t<GroupIndex, Groups...>, GroupIndex, self_type> dispatcher(*this);
    Event copy = event;
    receiver.on_event(copy, dispatcher);
  }

  // Dispatch with move semantics
  template<std::size_t GroupIndex, typename Receiver, typename Event, typename Storage>
  void dispatch_to_receiver_move(Storage& storage, Event&& event)
  {
    auto& receiver = storage.template get<Receiver>();
    detail::GroupDispatcher<detail::type_at_t<GroupIndex, Groups...>, GroupIndex, self_type> dispatcher(*this);
    receiver.on_event(std::forward<Event>(event), dispatcher);
  }

  template<std::size_t GroupIndex, typename Receiver, typename Event, typename Storage>
  void dispatch_to_receiver_if_handles(Storage& storage, Event& event)
  {
    using receives = typename Receiver::receives;
    if constexpr (detail::contains_v<receives, Event>) {
      auto& receiver = storage.template get<Receiver>();
      detail::GroupDispatcher<detail::type_at_t<GroupIndex, Groups...>, GroupIndex, self_type> dispatcher(*this);
      receiver.on_event(event, dispatcher);
    }
  }

  // Helper to create tuple of N identical types
  template<typename T, typename> struct repeat_type;
  template<typename T, std::size_t... Is> struct repeat_type<T, std::index_sequence<Is...>>
  {
    template<std::size_t> using type_at = T;
    using type = std::tuple<type_at<Is>...>;
  };
  template<typename T, std::size_t N> using repeat_type_t = typename repeat_type<T, std::make_index_sequence<N>>::type;

  // Storage
  std::tuple<detail::GroupStorage<Groups>...> storage_;
  repeat_type_t<std::thread, group_count> threads_{};
  repeat_type_t<detail::GroupWorkSignal, group_count> signals_{};
  std::atomic<bool> running_{ false };

  // Per-event-type queues: one set per group (ECS/data-oriented design)
  std::tuple<detail::GroupEventQueues<Groups>...> queues_{};
};

} // namespace ev_loop
