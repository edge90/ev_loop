#pragma once

#include <array>
#include <atomic>
#include <cstddef>
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

  // Type at index
  template<std::size_t I, typename... Ts> using type_at_t = std::tuple_element_t<I, std::tuple<Ts...>>;

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
  // InboxBase: CRTP base for SPSC and MPSC ring buffers
  // Provides public interface, derived classes implement the details
  // =============================================================================

  // NOLINTNEXTLINE(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)
  template<typename Derived, typename T, std::size_t Capacity = 4096> class InboxBase
  {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be power of 2");

    friend Derived;

    [[nodiscard]] Derived& derived() noexcept { return static_cast<Derived&>(*this); }
    [[nodiscard]] const Derived& derived() const noexcept { return static_cast<const Derived&>(*this); }

  protected:
    static constexpr std::size_t mask_ = Capacity - 1;
    static constexpr std::size_t capacity_ = Capacity;

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

  public:
    // Public interface - delegates to derived
    bool push(T event) { return derived().push_impl(std::move(event)); }
    bool try_pop(T& out) { return derived().try_pop_impl(out); }
    [[nodiscard]] bool empty() const noexcept { return derived().empty_impl(); }

    // size() is the same for both SPSC and MPSC
    [[nodiscard]] std::size_t size() const noexcept
    {
      const std::size_t tail = tail_.load(std::memory_order_acquire);
      const std::size_t head = head_.load(std::memory_order_acquire);
      return tail - head;
    }

    // drain() uses public try_pop
    template<typename Func> std::size_t drain(Func&& func)
    {
      std::size_t count = 0;
      T item;
      while (try_pop(item)) {
        std::forward<Func>(func)(std::move(item));
        ++count;
      }
      return count;
    }
  };

  // =============================================================================
  // SpscInbox: single-producer single-consumer ring buffer
  // =============================================================================

  template<typename T, std::size_t Capacity = 4096> class SpscInbox;

  // NOLINTNEXTLINE(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)
  template<typename T, std::size_t Capacity> class SpscInbox : public InboxBase<SpscInbox<T, Capacity>, T, Capacity>
  {
    using Base = InboxBase<SpscInbox<T, Capacity>, T, Capacity>;
    friend Base;

    using Base::buffer_;
    using Base::capacity_;
    using Base::head_;
    using Base::mask_;
    using Base::tail_;

    // Implementation methods (called by base)
    bool push_impl(T event)
    {
      const std::size_t head = head_.load(std::memory_order_acquire);
      const std::size_t tail = tail_.load(std::memory_order_relaxed);
      if (tail - head >= capacity_) [[unlikely]] { return false; }
      buffer_[tail & mask_] = std::move(event);
      tail_.store(tail + 1, std::memory_order_release);
      return true;
    }

    bool try_pop_impl(T& out)
    {
      const std::size_t tail = tail_.load(std::memory_order_acquire);
      const std::size_t head = head_.load(std::memory_order_relaxed);
      if (head >= tail) { return false; }
      out = std::move(buffer_[head & mask_]);
      head_.store(head + 1, std::memory_order_release);
      return true;
    }

    [[nodiscard]] bool empty_impl() const noexcept
    {
      return head_.load(std::memory_order_acquire) >= tail_.load(std::memory_order_acquire);
    }
  };

  // =============================================================================
  // MpscInbox: multi-producer single-consumer ring buffer
  // Uses per-slot ready flags to handle out-of-order producer completion
  // =============================================================================

  template<typename T, std::size_t Capacity = 4096> class MpscInbox;

  // NOLINTNEXTLINE(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)
  template<typename T, std::size_t Capacity> class MpscInbox : public InboxBase<MpscInbox<T, Capacity>, T, Capacity>
  {
    using Base = InboxBase<MpscInbox<T, Capacity>, T, Capacity>;
    friend Base;

    using Base::buffer_;
    using Base::capacity_;
    using Base::head_;
    using Base::mask_;
    using Base::tail_;

    // Per-slot ready flags - set when producer finishes writing to slot
    alignas(cache_line_size) std::array<std::atomic<bool>, Capacity> ready_{};

    // Implementation methods (called by base)
    bool push_impl(T event)
    {
      // Atomically claim a slot using compare-exchange
      std::size_t tail = tail_.load(std::memory_order_relaxed);
      std::size_t head = 0;

      do {
        head = head_.load(std::memory_order_acquire);
        if (tail - head >= capacity_) [[unlikely]] { return false; }
      } while (!tail_.compare_exchange_weak(tail, tail + 1, std::memory_order_relaxed, std::memory_order_relaxed));

      // Successfully claimed slot 'tail' - write data and mark ready
      buffer_[tail & mask_] = std::move(event);
      ready_[tail & mask_].store(true, std::memory_order_release);
      return true;
    }

    bool try_pop_impl(T& out)
    {
      const std::size_t head = head_.load(std::memory_order_relaxed);

      // Check if the next slot is ready (producer finished writing)
      if (!ready_[head & mask_].load(std::memory_order_acquire)) { return false; }

      out = std::move(buffer_[head & mask_]);
      ready_[head & mask_].store(false, std::memory_order_relaxed);
      head_.store(head + 1, std::memory_order_release);
      return true;
    }

    [[nodiscard]] bool empty_impl() const noexcept
    {
      const std::size_t head = head_.load(std::memory_order_relaxed);
      return !ready_[head & mask_].load(std::memory_order_acquire);
    }
  };

  // Legacy Inbox alias - use SpscInbox for backward compatibility
  template<typename T, std::size_t Capacity = 4096> using Inbox = SpscInbox<T, Capacity>;

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

    // Get current signal value (acquire semantics)
    // Call BEFORE polling to avoid lost wakeup race
    [[nodiscard]] std::size_t get_signal() const noexcept { return signal_.load(std::memory_order_acquire); }

    // Consumer blocks until signal changes from expected_signal or stopped
    // Returns false if stopped, true if work might be available
    // IMPORTANT: Call get_signal() BEFORE polling, then pass that value here
    [[nodiscard]] bool wait_for_work(std::size_t expected_signal) noexcept
    {
      if (stop_.load(std::memory_order_acquire)) [[unlikely]] { return false; }
      signal_.wait(expected_signal, std::memory_order_acquire);
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

    // Bring base class members into scope (avoids this-> for dependent names)
    using Base::event_loop_;
    using Base::is_running;
    using Base::signal_;

  public:
    using Base::Base;

    // Single poll iteration - returns true if work was done
    [[nodiscard]] bool poll() { return event_loop_.template poll_group<GroupIndex>(); }

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
        while (is_running() && pred()) { (void)poll(); }
      } else if constexpr (std::is_same_v<Strategy, Wait>) {
        while (is_running() && pred()) {
          const auto sig = signal_.get_signal();
          if (!poll()) { std::ignore = signal_.wait_for_work(sig); }
        }
      } else if constexpr (std::is_same_v<Strategy, Yield>) {
        while (is_running() && pred()) {
          if (!poll()) { std::this_thread::yield(); }
        }
      } else if constexpr (std::is_same_v<Strategy, Hybrid>) {
        run_hybrid_while(std::forward<Predicate>(pred));
      }
    }

  private:
    void run_spin()
    {
      while (is_running()) { (void)poll(); }
    }

    void run_wait()
    {
      while (is_running()) {
        // Load signal BEFORE polling to avoid lost wakeup race:
        // If we poll first and find nothing, then producer pushes and signals,
        // we'd wait for a signal that already happened.
        const auto sig = signal_.get_signal();
        if (!poll()) { std::ignore = signal_.wait_for_work(sig); }
      }
    }

    void run_yield()
    {
      while (is_running()) {
        if (!poll()) { std::this_thread::yield(); }
      }
    }

    void run_hybrid()
    {
      constexpr std::size_t default_spin_count = 1000;
      std::size_t empty_spins = 0;
      while (is_running()) {
        const auto sig = signal_.get_signal();
        if (poll()) {
          empty_spins = 0;
        } else {
          ++empty_spins;
          if (empty_spins >= default_spin_count) {
            empty_spins = 0;
            std::ignore = signal_.wait_for_work(sig);
          }
        }
      }
    }

    template<typename Predicate> void run_hybrid_while(Predicate&& pred)
    {
      constexpr std::size_t default_spin_count = 1000;
      std::size_t empty_spins = 0;
      while (is_running() && std::forward<Predicate>(pred)()) {
        const auto sig = signal_.get_signal();
        if (poll()) {
          empty_spins = 0;
        } else {
          ++empty_spins;
          if (empty_spins >= default_spin_count) {
            empty_spins = 0;
            std::ignore = signal_.wait_for_work(sig);
          }
        }
      }
    }
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

  // Check if a group can emit a specific event (any receiver in the group emits it)
  template<typename Group, typename Event> struct group_can_emit_event;

  template<typename Strategy, typename... Receivers, typename Event>
  struct group_can_emit_event<ThreadGroup<Strategy, Receivers...>, Event>
    : std::bool_constant<(contains_v<get_emits_t<Receivers>, Event> || ...)>
  {
  };

  template<typename Group, typename Event>
  inline constexpr bool group_can_emit_event_v = group_can_emit_event<Group, Event>::value;

  // Count how many groups (excluding DestGroup) can emit Event to DestGroup
  // This determines whether we need SPSC (<=1 producer) or MPSC (>1 producers)
  // For GroupEventLoop: only internal producers count (no external emit after start)
  template<typename DestGroup, typename Event, typename... AllGroups> struct count_event_producers
  {
    // Count internal producers (other groups that emit this event)
    static constexpr std::size_t value =
      ((std::is_same_v<DestGroup, AllGroups> ? 0 : (group_can_emit_event_v<AllGroups, Event> ? 1 : 0)) + ... + 0);
  };

  template<typename DestGroup, typename Event, typename... AllGroups>
  inline constexpr std::size_t count_event_producers_v = count_event_producers<DestGroup, Event, AllGroups...>::value;

  // Select queue type based on producer count
  template<typename Event, std::size_t ProducerCount>
  using select_queue_t = std::conditional_t<ProducerCount <= 1, SpscInbox<Event>, MpscInbox<Event>>;

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
  // Automatically selects SPSC or MPSC based on producer count at compile time
  // =============================================================================

  // Create tuple of queues, selecting SPSC vs MPSC per event based on producer count
  template<typename DestGroup, typename GroupList, typename EventList> struct make_event_queues;

  template<typename DestGroup, typename... AllGroups, typename... Events>
  struct make_event_queues<DestGroup, type_list<AllGroups...>, type_list<Events...>>
  {
    // For each event, count producers and select appropriate queue type
    template<typename Event>
    using queue_for = select_queue_t<Event, count_event_producers_v<DestGroup, Event, AllGroups...>>;

    using type = std::tuple<queue_for<Events>...>;
  };

  template<typename DestGroup, typename GroupList, typename EventList>
  using make_event_queues_t = typename make_event_queues<DestGroup, GroupList, EventList>::type;

  // GroupEventQueues: holds per-event-type queues for a group
  // Automatically uses SPSC when only 1 producer, MPSC when multiple
  template<typename Group, typename... AllGroups> struct GroupEventQueues
  {
    using handled_events = unique_list_t<group_all_events_t<Group>>;
    using queues_type = make_event_queues_t<Group, type_list<AllGroups...>, handled_events>;

    queues_type queues_;

    // Get the queue for a specific event type (type depends on producer count)
    template<typename Event> auto& queue_for_event()
    {
      using queue_type = select_queue_t<Event, count_event_producers_v<Group, Event, AllGroups...>>;
      return std::get<queue_type>(queues_);
    }

    template<typename Event> const auto& queue_for_event() const
    {
      using queue_type = select_queue_t<Event, count_event_producers_v<Group, Event, AllGroups...>>;
      return std::get<queue_type>(queues_);
    }

    // Push to the queue for a specific event type (copy)
    template<typename Event> bool push(const Event& event) { return queue_for_event<Event>().push(Event{ event }); }

    // Push to the queue for a specific event type (move)
    template<typename Event> bool push(Event&& event)
    {
      return queue_for_event<std::decay_t<Event>>().push(std::forward<Event>(event));
    }

    // Try to pop from a specific event type's queue
    template<typename Event> bool try_pop(Event& out) { return queue_for_event<Event>().try_pop(out); }

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

  // Helper to create tuple of N identical types (forward declaration for Builder)
  template<typename T, typename> struct repeat_type;
  template<typename T, std::size_t... Is> struct repeat_type<T, std::index_sequence<Is...>>
  {
    template<std::size_t> using type_at = T;
    using type = std::tuple<type_at<Is>...>;
  };
  template<typename T, std::size_t N> using repeat_type_t = typename repeat_type<T, std::make_index_sequence<N>>::type;

public:
  using self_type = GroupEventLoop<Groups...>;
  static constexpr std::size_t group_count = sizeof...(Groups);

  // ==========================================================================
  // Setup - constexpr builder that stores primed events in a tuple
  // Usage: Loop loop = Loop::setup().prime(EventA{}).prime(EventB{}).create();
  // ==========================================================================
  template<typename... PrimedEvents> class Setup
  {
    std::tuple<PrimedEvents...> events_;

    // Allow Setup<Other...> to access events_ for chaining
    template<typename...> friend class Setup;

  public:
    constexpr Setup() = default;

    // Internal constructor for chaining (from tuple)
    constexpr explicit Setup(std::tuple<PrimedEvents...> events) : events_(std::move(events)) {}

    // Prime returns a new Setup with the event added (lvalue - copies tuple)
    template<typename Event> [[nodiscard]] constexpr auto prime(Event&& event) const&
    {
      return Setup<PrimedEvents..., std::decay_t<Event>>{ std::tuple_cat(
        events_, std::make_tuple(std::forward<Event>(event))) };
    }

    // Prime returns a new Setup with the event added (rvalue - moves tuple)
    template<typename Event> [[nodiscard]] constexpr auto prime(Event&& event) &&
    {
      return Setup<PrimedEvents..., std::decay_t<Event>>{ std::tuple_cat(
        std::move(events_), std::make_tuple(std::forward<Event>(event))) };
    }

    // Create EventLoop on stack (relies on NRVO)
    [[nodiscard]] auto create() const& -> GroupEventLoop
    {
      GroupEventLoop loop;
      std::apply([&loop](const auto&... events) { (loop.prime_event(events), ...); }, events_);
      return loop;
    }

    // Create EventLoop on stack (moves events - rvalue overload)
    [[nodiscard]] auto create() && -> GroupEventLoop
    {
      GroupEventLoop loop;
      std::apply([&loop](auto&&... events) { (loop.prime_event(std::forward<decltype(events)>(events)), ...); },
        std::move(events_));
      return loop;
    }

    // Create with factory (e.g., std::make_unique<Loop>)
    template<typename Factory> [[nodiscard]] auto create(Factory&& factory) const&
    {
      auto loop = std::invoke(std::forward<Factory>(factory));
      std::apply([&loop](const auto&... events) { (loop->prime_event(events), ...); }, events_);
      return loop;
    }

    // Create with factory (moves events - rvalue overload)
    template<typename Factory> [[nodiscard]] auto create(Factory&& factory) &&
    {
      auto loop = std::invoke(std::forward<Factory>(factory));
      std::apply([&loop](auto&&... events) { (loop->prime_event(std::forward<decltype(events)>(events)), ...); },
        std::move(events_));
      return loop;
    }

    // Create on heap (returns unique_ptr, not started)
    [[nodiscard]] auto create_unique() && -> std::unique_ptr<GroupEventLoop>
    {
      auto ptr = std::unique_ptr<GroupEventLoop>(new GroupEventLoop());
      std::apply([&ptr](auto&&... events) { (ptr->prime_event(std::forward<decltype(events)>(events)), ...); },
        std::move(events_));
      return ptr;
    }

    // Create on heap and start threads (returns unique_ptr)
    [[nodiscard]] auto start_unique() && -> std::unique_ptr<GroupEventLoop>
    {
      auto ptr = std::move(*this).create_unique();
      ptr->start();
      return ptr;
    }
  };

  // Factory method - returns an empty Setup for fluent chaining
  [[nodiscard]] static constexpr Setup<> setup() { return {}; }

  ~GroupEventLoop() { stop(); }

  GroupEventLoop(const GroupEventLoop&) = delete;
  GroupEventLoop& operator=(const GroupEventLoop&) = delete;
  GroupEventLoop(GroupEventLoop&&) = delete;
  GroupEventLoop& operator=(GroupEventLoop&&) = delete;

private:
  // Private default constructor - use setup().prime().create()
  GroupEventLoop() = default;
  template<typename...> friend class Setup;

public:
  // Start all groups on their own threads (returns immediately)
  // Only useful if you used build() instead of start()
  void start() { start_threads(); }

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
  // Private constructor from builder state
  GroupEventLoop(std::tuple<detail::GroupStorage<Groups>...>&& storage,
    repeat_type_t<detail::GroupWorkSignal, group_count>&& signals,
    std::tuple<detail::GroupEventQueues<Groups, Groups...>...>&& queues)
    : storage_(std::move(storage)), signals_(std::move(signals)), queues_(std::move(queues))
  {}

  // Start all threads (called by Builder::start() or public start())
  void start_threads()
  {
    running_.store(true, std::memory_order_release);
    start_all_groups(std::make_index_sequence<group_count>{});
  }

  // Prime an event before starting (called by Builder::prime())
  template<typename Event> void prime_event(Event&& event)
  {
    route_internal_event<0>(std::forward<Event>(event), std::make_index_sequence<group_count>{});
  }
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
    using handled_events = typename detail::GroupEventQueues<Group, Groups...>::handled_events;
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

  // Storage
  std::tuple<detail::GroupStorage<Groups>...> storage_;
  repeat_type_t<std::thread, group_count> threads_{};
  repeat_type_t<detail::GroupWorkSignal, group_count> signals_{};
  std::atomic<bool> running_{ false };

  // Per-event-type queues: one set per group (ECS/data-oriented design)
  // Each GroupEventQueues gets the full group list for SPSC/MPSC selection
  std::tuple<detail::GroupEventQueues<Groups, Groups...>...> queues_{};
};

} // namespace ev_loop
