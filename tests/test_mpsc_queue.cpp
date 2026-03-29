#include "mpsc_queue.hpp"

#include <array>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

void test_basic_push_pop() {
    hft::MPSCQueue<int, 8> q;
    assert(q.empty());
    assert(q.try_push(10));
    assert(q.try_push(20));
    assert(q.size() == 2);

    int out = 0;
    assert(q.try_pop(out));
    assert(out == 10);
    assert(q.try_pop(out));
    assert(out == 20);
    assert(q.empty());
    std::cout << "  [PASS] basic push/pop\n";
}

void test_full_queue() {
    hft::MPSCQueue<int, 4> q;
    assert(q.try_push(1));
    assert(q.try_push(2));
    assert(q.try_push(3));
    assert(q.try_push(4));
    assert(!q.try_push(5));

    int out = 0;
    assert(q.try_pop(out));
    assert(out == 1);
    assert(q.try_push(5));
    std::cout << "  [PASS] full queue\n";
}

void test_optional_pop() {
    hft::MPSCQueue<std::string, 4> q;
    assert(q.try_emplace(3, 'x'));

    auto out = q.try_pop();
    assert(out.has_value());
    assert(*out == "xxx");
    assert(!q.try_pop().has_value());
    std::cout << "  [PASS] optional pop\n";
}

struct MoveOnly {
    int val;
    explicit MoveOnly(int v) : val(v) {}
    MoveOnly(const MoveOnly&) = delete;
    MoveOnly& operator=(const MoveOnly&) = delete;
    MoveOnly(MoveOnly&& other) noexcept : val(other.val) { other.val = -1; }
    MoveOnly& operator=(MoveOnly&& other) noexcept {
        val = other.val;
        other.val = -1;
        return *this;
    }
};

void test_move_only_type() {
    hft::MPSCQueue<MoveOnly, 8> q;
    assert(q.try_push(MoveOnly(7)));
    assert(q.try_emplace(9));

    MoveOnly out(0);
    assert(q.try_pop(out));
    assert(out.val == 7);
    assert(q.try_pop(out));
    assert(out.val == 9);
    std::cout << "  [PASS] move-only type\n";
}

void test_concurrent_mpsc() {
    constexpr std::size_t kProducers = 4;
    constexpr std::size_t kPerProducer = 100'000;
    constexpr std::size_t kTotal = kProducers * kPerProducer;

    hft::MPSCQueue<std::uint64_t, 4096> q;
    std::array<std::thread, kProducers> producers;

    for (std::size_t pid = 0; pid < kProducers; ++pid) {
        producers[pid] = std::thread([&, pid] {
            for (std::uint64_t seq = 0; seq < kPerProducer; ++seq) {
                const auto value = (static_cast<std::uint64_t>(pid) << 32) | seq;
                while (!q.try_push(value)) {
                    std::this_thread::yield();
                }
            }
        });
    }

    std::array<std::uint64_t, kProducers> next_expected{};
    std::vector<std::uint64_t> seen_per_producer(kProducers, 0);

    std::thread consumer([&] {
        std::size_t received = 0;
        std::uint64_t value = 0;
        while (received < kTotal) {
            if (!q.try_pop(value)) {
                std::this_thread::yield();
                continue;
            }

            const auto pid = static_cast<std::size_t>(value >> 32);
            const auto seq = static_cast<std::uint32_t>(value & 0xffffffffu);
            assert(pid < kProducers);
            assert(seq == next_expected[pid]);
            ++next_expected[pid];
            ++seen_per_producer[pid];
            ++received;
        }
    });

    for (auto& producer : producers) producer.join();
    consumer.join();

    for (std::size_t pid = 0; pid < kProducers; ++pid) {
        assert(seen_per_producer[pid] == kPerProducer);
    }
    assert(q.empty());
    std::cout << "  [PASS] concurrent MPSC (" << kProducers << " producers)\n";
}

void test_concurrent_throughput() {
    constexpr std::size_t kProducers = 4;
    constexpr std::size_t kPerProducer = 250'000;
    constexpr std::size_t kTotal = kProducers * kPerProducer;

    hft::MPSCQueue<std::uint64_t, 4096> q;
    std::array<std::thread, kProducers> producers;

    auto start = std::chrono::high_resolution_clock::now();

    for (std::size_t pid = 0; pid < kProducers; ++pid) {
        producers[pid] = std::thread([&, pid] {
            const auto base = static_cast<std::uint64_t>(pid) << 32;
            for (std::uint64_t seq = 0; seq < kPerProducer; ++seq) {
                while (!q.try_push(base | seq)) {
                }
            }
        });
    }

    std::thread consumer([&] {
        std::size_t received = 0;
        std::uint64_t value = 0;
        while (received < kTotal) {
            if (q.try_pop(value)) ++received;
        }
    });

    for (auto& producer : producers) producer.join();
    consumer.join();

    const auto end = std::chrono::high_resolution_clock::now();
    const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
    const double ops_per_sec = static_cast<double>(kTotal) / (ns / 1e9);
    const double ns_per_op = static_cast<double>(ns) / kTotal;

    std::cout << "  [PASS] throughput: "
              << static_cast<long long>(ops_per_sec / 1e6) << "M msgs/s, "
              << ns_per_op << " ns/msg\n";
}

int main() {
    std::cout << "=== MPSCQueue Tests ===\n";
    test_basic_push_pop();
    test_full_queue();
    test_optional_pop();
    test_move_only_type();
    test_concurrent_mpsc();
    test_concurrent_throughput();
    std::cout << "All MPSCQueue tests passed!\n";
    return 0;
}
