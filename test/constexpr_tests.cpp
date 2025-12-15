// NOLINTBEGIN(misc-include-cleaner)
#include <catch2/catch_test_macros.hpp>
#include <ev_loop/ev.hpp>
#include <type_traits>
#include <utility>
// NOLINTEND(misc-include-cleaner)

// Test type_list utilities at compile time
TEST_CASE("type_list size is computed at compile time", "[constexpr]")
{
  STATIC_REQUIRE(ev_loop::type_list<>::size == 0);
  STATIC_REQUIRE(ev_loop::type_list<int>::size == 1);
  STATIC_REQUIRE(ev_loop::type_list<int, float, double>::size == 3);
}

TEST_CASE("contains_v is computed at compile time", "[constexpr]")
{
  using list = ev_loop::type_list<int, float, double>;
  STATIC_REQUIRE(ev_loop::contains_v<list, int>);
  STATIC_REQUIRE(ev_loop::contains_v<list, float>);
  STATIC_REQUIRE(ev_loop::contains_v<list, double>);
  STATIC_REQUIRE_FALSE(ev_loop::contains_v<list, char>);
  STATIC_REQUIRE_FALSE(ev_loop::contains_v<list, long>);
}

TEST_CASE("index_of_v is computed at compile time", "[constexpr]")
{
  STATIC_REQUIRE(ev_loop::index_of_v<int, int, float, double> == 0);
  STATIC_REQUIRE(ev_loop::index_of_v<float, int, float, double> == 1);
  STATIC_REQUIRE(ev_loop::index_of_v<double, int, float, double> == 2);
}

TEST_CASE("const_max computes maximum at compile time", "[constexpr]")
{
  STATIC_REQUIRE(ev_loop::const_max<1, 2, 3>() == 3);
  STATIC_REQUIRE(ev_loop::const_max<3, 2, 1>() == 3);
  STATIC_REQUIRE(ev_loop::const_max<5>() == 5);
  STATIC_REQUIRE(ev_loop::const_max<1, 100, 50>() == 100);
}

TEST_CASE("tag_type_t selects smallest sufficient type", "[constexpr]")
{
  // uint8_t range: 0-254 (255 reserved for uninitialized)
  STATIC_REQUIRE(sizeof(ev_loop::tag_type_t<10>) == 1);
  STATIC_REQUIRE(sizeof(ev_loop::tag_type_t<200>) == 1);
  STATIC_REQUIRE(sizeof(ev_loop::tag_type_t<254>) == 1);

  // uint16_t range: 255-65534
  STATIC_REQUIRE(sizeof(ev_loop::tag_type_t<255>) == 2);
  STATIC_REQUIRE(sizeof(ev_loop::tag_type_t<300>) == 2);
  STATIC_REQUIRE(sizeof(ev_loop::tag_type_t<65534>) == 2);

  // uint32_t range: 65535 to uint32_max-1
  STATIC_REQUIRE(sizeof(ev_loop::tag_type_t<65535>) == 4);
  STATIC_REQUIRE(sizeof(ev_loop::tag_type_t<100000>) == 4);
  STATIC_REQUIRE(sizeof(ev_loop::tag_type_t<4294967294UL>) == 4); // max valid value

  // Values >= uint32_max trigger static_assert: "Too many event types (max ~4 billion)"
}

TEST_CASE("TaggedEvent::index is noexcept", "[constexpr]")
{
  STATIC_REQUIRE(noexcept(std::declval<ev_loop::TaggedEvent<int>&>().index()));
  STATIC_REQUIRE(noexcept(std::declval<const ev_loop::TaggedEvent<int>&>().index()));
}

TEST_CASE("RingBuffer::empty and size are noexcept", "[constexpr]")
{
  STATIC_REQUIRE(noexcept(std::declval<const ev_loop::RingBuffer<int>&>().empty()));
  STATIC_REQUIRE(noexcept(std::declval<const ev_loop::RingBuffer<int>&>().size()));
}

TEST_CASE("SPSCQueue::is_stopped is noexcept", "[constexpr]")
{
  STATIC_REQUIRE(noexcept(std::declval<const ev_loop::SPSCQueue<int>&>().is_stopped()));
}

TEST_CASE("ThreadSafeRingBuffer::is_stopped is noexcept", "[constexpr]")
{
  STATIC_REQUIRE(noexcept(std::declval<const ev_loop::ThreadSafeRingBuffer<int>&>().is_stopped()));
}

// Verify ref qualifiers: & qualified functions are callable on lvalues
TEST_CASE("Ref qualified functions are callable on lvalues", "[constexpr]")
{
  // RingBuffer (no ref qualifiers on empty/size - they're const)
  STATIC_REQUIRE(requires(ev_loop::RingBuffer<int>& buf) { buf.empty(); });
  STATIC_REQUIRE(requires(ev_loop::RingBuffer<int>& buf) { buf.size(); });

  // TaggedEvent::index (const, no ref qualifier needed)
  STATIC_REQUIRE(requires(ev_loop::TaggedEvent<int>& tagged) { tagged.index(); });
  STATIC_REQUIRE(requires(const ev_loop::TaggedEvent<int>& tagged) { tagged.index(); });
}

// =============================================================================
// Queue selection tests - verify compile-time producer counting
// =============================================================================

namespace {

struct EventA
{
};
struct EventB
{
};
struct EventC
{
};

// SameThread receiver that emits EventB
struct SameThreadProducerA
{
  using receives = ev_loop::type_list<EventA>;
  using emits = ev_loop::type_list<EventB>;
  static constexpr ev_loop::ThreadMode thread_mode = ev_loop::ThreadMode::SameThread;
  template<typename D> void on_event(EventA /*unused*/, D& /*unused*/) {}
};

// OwnThread receiver that receives EventB
struct OwnThreadConsumerB
{
  using receives = ev_loop::type_list<EventB>;
  using emits = ev_loop::type_list<>;
  static constexpr ev_loop::ThreadMode thread_mode = ev_loop::ThreadMode::OwnThread;
  template<typename D> void on_event(EventB /*unused*/, D& /*unused*/) {}
};

// OwnThread receiver that emits EventB (another producer)
struct OwnThreadProducerB
{
  using receives = ev_loop::type_list<EventA>;
  using emits = ev_loop::type_list<EventB>;
  static constexpr ev_loop::ThreadMode thread_mode = ev_loop::ThreadMode::OwnThread;
  template<typename D> void on_event(EventA /*unused*/, D& /*unused*/) {}
};

// External emitter that emits EventB
struct ExternalEmitterB
{
  using emits = ev_loop::type_list<EventB>;
};

// External emitter that emits EventC (not EventB)
struct ExternalEmitterC
{
  using emits = ev_loop::type_list<EventC>;
};

} // namespace

TEST_CASE("count_ownthread_producers_v counts OwnThread producers", "[constexpr][queue_selection]")
{
  using target_receives = ev_loop::type_list<EventB>;

  // No producers
  STATIC_REQUIRE(ev_loop::count_ownthread_producers_v<target_receives> == 0);

  // SameThread doesn't count as OwnThread producer
  STATIC_REQUIRE(ev_loop::count_ownthread_producers_v<target_receives, SameThreadProducerA> == 0);

  // OwnThread consumer doesn't emit EventB
  STATIC_REQUIRE(ev_loop::count_ownthread_producers_v<target_receives, OwnThreadConsumerB> == 0);

  // OwnThread producer emits EventB
  STATIC_REQUIRE(ev_loop::count_ownthread_producers_v<target_receives, OwnThreadProducerB> == 1);

  // Multiple receivers
  STATIC_REQUIRE(
    ev_loop::count_ownthread_producers_v<target_receives, SameThreadProducerA, OwnThreadProducerB, OwnThreadConsumerB>
    == 1);
}

TEST_CASE("has_samethread_producer_v detects SameThread producers", "[constexpr][queue_selection]")
{
  using target_receives = ev_loop::type_list<EventB>;

  // No receivers
  STATIC_REQUIRE_FALSE(ev_loop::has_samethread_producer_v<target_receives>);

  // SameThread that emits to target
  STATIC_REQUIRE(ev_loop::has_samethread_producer_v<target_receives, SameThreadProducerA>);

  // OwnThread doesn't count as SameThread producer
  STATIC_REQUIRE_FALSE(ev_loop::has_samethread_producer_v<target_receives, OwnThreadProducerB>);

  // External emitter doesn't count as SameThread producer
  STATIC_REQUIRE_FALSE(ev_loop::has_samethread_producer_v<target_receives, ExternalEmitterB>);

  // Mixed - still true if any SameThread producer exists
  STATIC_REQUIRE(
    ev_loop::has_samethread_producer_v<target_receives, OwnThreadProducerB, SameThreadProducerA, OwnThreadConsumerB>);
}

TEST_CASE("count_external_producers_v counts external emitters", "[constexpr][queue_selection]")
{
  using target_receives = ev_loop::type_list<EventB>;

  // No external emitters
  STATIC_REQUIRE(ev_loop::count_external_producers_v<target_receives> == 0);

  // External emitter that emits EventB
  STATIC_REQUIRE(ev_loop::count_external_producers_v<target_receives, ExternalEmitterB> == 1);

  // External emitter that doesn't emit EventB
  STATIC_REQUIRE(ev_loop::count_external_producers_v<target_receives, ExternalEmitterC> == 0);

  // Multiple external emitters
  STATIC_REQUIRE(ev_loop::count_external_producers_v<target_receives, ExternalEmitterB, ExternalEmitterC> == 1);

  // Mixed with receivers (external emitters still counted)
  STATIC_REQUIRE(
    ev_loop::count_external_producers_v<target_receives, SameThreadProducerA, ExternalEmitterB, OwnThreadConsumerB>
    == 1);
}

TEST_CASE("total_producer_count_v sums all producer types", "[constexpr][queue_selection]")
{
  using target_receives = ev_loop::type_list<EventB>;

  // No producers
  STATIC_REQUIRE(ev_loop::total_producer_count_v<target_receives> == 0);

  // Only SameThread producer (counts as 1)
  STATIC_REQUIRE(ev_loop::total_producer_count_v<target_receives, SameThreadProducerA> == 1);

  // Only OwnThread producer
  STATIC_REQUIRE(ev_loop::total_producer_count_v<target_receives, OwnThreadProducerB> == 1);

  // Only external emitter
  STATIC_REQUIRE(ev_loop::total_producer_count_v<target_receives, ExternalEmitterB> == 1);

  // SameThread + OwnThread = 2
  STATIC_REQUIRE(ev_loop::total_producer_count_v<target_receives, SameThreadProducerA, OwnThreadProducerB> == 2);

  // SameThread + external = 2
  STATIC_REQUIRE(ev_loop::total_producer_count_v<target_receives, SameThreadProducerA, ExternalEmitterB> == 2);

  // OwnThread + external = 2
  STATIC_REQUIRE(ev_loop::total_producer_count_v<target_receives, OwnThreadProducerB, ExternalEmitterB> == 2);

  // All three types = 3
  STATIC_REQUIRE(
    ev_loop::total_producer_count_v<target_receives, SameThreadProducerA, OwnThreadProducerB, ExternalEmitterB> == 3);
}

TEST_CASE("Queue type selection based on producer count", "[constexpr][queue_selection]")
{
  using ConsumerTaggedEvent = ev_loop::to_tagged_event_t<ev_loop::get_receives_t<OwnThreadConsumerB>>;

  SECTION("Single external emitter selects SPSC queue")
  {
    using Loop = ev_loop::EventLoop<OwnThreadConsumerB, ExternalEmitterB>;
    STATIC_REQUIRE(Loop::producer_count_for<OwnThreadConsumerB> == 1);
    STATIC_REQUIRE(std::is_same_v<Loop::queue_type_for<OwnThreadConsumerB>, ev_loop::SPSCQueue<ConsumerTaggedEvent>>);
  }

  SECTION("SameThread + external selects MPSC queue")
  {
    using Loop = ev_loop::EventLoop<SameThreadProducerA, OwnThreadConsumerB, ExternalEmitterB>;
    STATIC_REQUIRE(Loop::producer_count_for<OwnThreadConsumerB> == 2);
    STATIC_REQUIRE(
      std::is_same_v<Loop::queue_type_for<OwnThreadConsumerB>, ev_loop::ThreadSafeRingBuffer<ConsumerTaggedEvent>>);
  }
}
