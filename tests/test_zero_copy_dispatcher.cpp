#include "zero_copy_dispatcher.hpp"

#include <atomic>
#include <cassert>
#include <iostream>

namespace {

struct InPlaceMessage {
    int id;
    char fill;

    InPlaceMessage(int id_, char fill_) : id(id_), fill(fill_) {}
    InPlaceMessage(const InPlaceMessage&) = delete;
    InPlaceMessage& operator=(const InPlaceMessage&) = delete;
    InPlaceMessage(InPlaceMessage&&) = delete;
    InPlaceMessage& operator=(InPlaceMessage&&) = delete;
};

struct TrackedMessage {
    static std::atomic<int> constructions;
    static std::atomic<int> destructions;

    int value;

    explicit TrackedMessage(int v) : value(v) { constructions.fetch_add(1, std::memory_order_relaxed); }
    TrackedMessage(const TrackedMessage&) = delete;
    TrackedMessage& operator=(const TrackedMessage&) = delete;
    TrackedMessage(TrackedMessage&&) = delete;
    TrackedMessage& operator=(TrackedMessage&&) = delete;
    ~TrackedMessage() { destructions.fetch_add(1, std::memory_order_relaxed); }
};

std::atomic<int> TrackedMessage::constructions{0};
std::atomic<int> TrackedMessage::destructions{0};

void test_basic_fanout_zero_copy() {
    hft::ZeroCopyDispatcher<InPlaceMessage, 8, 8, 2, 8> dispatcher;

    const auto sub_a = dispatcher.add_subscriber();
    const auto sub_b = dispatcher.add_subscriber();
    assert(sub_a.has_value());
    assert(sub_b.has_value());

    assert(dispatcher.try_publish(42, 'x'));
    assert(dispatcher.dispatch_one());

    auto lease_a = dispatcher.try_consume(*sub_a);
    auto lease_b = dispatcher.try_consume(*sub_b);
    assert(lease_a.has_value());
    assert(lease_b.has_value());

    assert(lease_a->get() == lease_b->get());
    assert((*lease_a)->id == 42);
    assert((*lease_b)->fill == 'x');
    std::cout << "  [PASS] basic fanout zero-copy\n";
}

void test_lease_lifetime() {
    TrackedMessage::constructions.store(0, std::memory_order_relaxed);
    TrackedMessage::destructions.store(0, std::memory_order_relaxed);

    hft::ZeroCopyDispatcher<TrackedMessage, 4, 4, 2, 4> dispatcher;
    const auto sub_a = dispatcher.add_subscriber();
    const auto sub_b = dispatcher.add_subscriber();
    assert(sub_a.has_value());
    assert(sub_b.has_value());

    assert(dispatcher.try_publish(7));
    assert(dispatcher.dispatch_one());
    assert(TrackedMessage::constructions.load(std::memory_order_relaxed) == 1);
    assert(TrackedMessage::destructions.load(std::memory_order_relaxed) == 0);

    {
        auto lease_a = dispatcher.try_consume(*sub_a);
        auto lease_b = dispatcher.try_consume(*sub_b);
        assert(lease_a.has_value());
        assert(lease_b.has_value());
        assert((*lease_a)->value == 7);
        assert((*lease_b)->value == 7);
        assert(TrackedMessage::destructions.load(std::memory_order_relaxed) == 0);
    }

    assert(TrackedMessage::destructions.load(std::memory_order_relaxed) == 1);
    std::cout << "  [PASS] lease lifetime\n";
}

void test_pending_when_subscriber_full() {
    hft::ZeroCopyDispatcher<InPlaceMessage, 4, 4, 1, 1> dispatcher;
    const auto sub = dispatcher.add_subscriber();
    assert(sub.has_value());

    assert(dispatcher.try_publish(1, 'a'));
    assert(dispatcher.dispatch_one());

    assert(dispatcher.try_publish(2, 'b'));
    assert(!dispatcher.dispatch_one());
    assert(dispatcher.has_pending());

    {
        auto lease = dispatcher.try_consume(*sub);
        assert(lease.has_value());
        assert((*lease)->id == 1);
    }

    assert(dispatcher.dispatch_one());
    assert(!dispatcher.has_pending());

    auto lease = dispatcher.try_consume(*sub);
    assert(lease.has_value());
    assert((*lease)->id == 2);
    std::cout << "  [PASS] pending dispatch on full subscriber\n";
}

} // namespace

int main() {
    std::cout << "=== ZeroCopyDispatcher Tests ===\n";
    test_basic_fanout_zero_copy();
    test_lease_lifetime();
    test_pending_when_subscriber_full();
    std::cout << "All ZeroCopyDispatcher tests passed!\n";
    return 0;
}
