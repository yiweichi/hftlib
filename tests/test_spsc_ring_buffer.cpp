#include "spsc_ring_buffer.hpp"

#include <cassert>
#include <chrono>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

void test_basic_push_pop() {
    hft::SPSCRingBuffer<int, 4> rb;
    assert(rb.empty());
    assert(rb.size() == 0);

    assert(rb.try_push(10));
    assert(rb.try_push(20));
    assert(rb.size() == 2);

    int out;
    assert(rb.try_pop(out));
    assert(out == 10);
    assert(rb.try_pop(out));
    assert(out == 20);
    assert(rb.empty());
    std::cout << "  [PASS] basic push/pop\n";
}

void test_full_buffer() {
    hft::SPSCRingBuffer<int, 4> rb;
    assert(rb.try_push(1));
    assert(rb.try_push(2));
    assert(rb.try_push(3));
    assert(rb.try_push(4));
    assert(!rb.try_push(5)); // full

    int out;
    assert(rb.try_pop(out));
    assert(out == 1);
    assert(rb.try_push(5)); // now there's room
    std::cout << "  [PASS] full buffer\n";
}

void test_empty_pop() {
    hft::SPSCRingBuffer<int, 4> rb;
    int out = -1;
    assert(!rb.try_pop(out));
    assert(out == -1);

    auto opt = rb.try_pop();
    assert(!opt.has_value());
    std::cout << "  [PASS] empty pop\n";
}

void test_optional_pop() {
    hft::SPSCRingBuffer<int, 4> rb;
    rb.try_push(42);
    auto val = rb.try_pop();
    assert(val.has_value());
    assert(*val == 42);
    std::cout << "  [PASS] optional pop\n";
}

void test_emplace() {
    hft::SPSCRingBuffer<std::string, 4> rb;
    assert(rb.try_emplace(5, 'x'));

    std::string out;
    assert(rb.try_pop(out));
    assert(out == "xxxxx");
    std::cout << "  [PASS] emplace\n";
}

void test_front_peek() {
    hft::SPSCRingBuffer<int, 4> rb;
    assert(rb.front() == nullptr);

    rb.try_push(99);
    rb.try_push(88);

    const int* f = rb.front();
    assert(f && *f == 99);

    int out;
    rb.try_pop(out);
    f = rb.front();
    assert(f && *f == 88);
    std::cout << "  [PASS] front peek\n";
}

void test_wraparound() {
    hft::SPSCRingBuffer<int, 4> rb;

    // Fill → drain → fill again, exercises index wraparound
    for (int round = 0; round < 10; ++round) {
        for (int i = 0; i < 4; ++i) assert(rb.try_push(round * 4 + i));
        assert(!rb.try_push(-1));
        for (int i = 0; i < 4; ++i) {
            int out;
            assert(rb.try_pop(out));
            assert(out == round * 4 + i);
        }
        assert(rb.empty());
    }
    std::cout << "  [PASS] wraparound (10 rounds)\n";
}

struct MoveOnly {
    int val;
    MoveOnly(int v) : val(v) {}
    MoveOnly(const MoveOnly&) = delete;
    MoveOnly& operator=(const MoveOnly&) = delete;
    MoveOnly(MoveOnly&& o) noexcept : val(o.val) { o.val = -1; }
    MoveOnly& operator=(MoveOnly&& o) noexcept { val = o.val; o.val = -1; return *this; }
};

void test_move_only_type() {
    hft::SPSCRingBuffer<MoveOnly, 4> rb;
    rb.try_push(MoveOnly(42));
    rb.try_emplace(99);

    MoveOnly out(0);
    assert(rb.try_pop(out));
    assert(out.val == 42);
    assert(rb.try_pop(out));
    assert(out.val == 99);
    std::cout << "  [PASS] move-only type\n";
}

void test_concurrent_spsc() {
    constexpr std::size_t kCount = 1'000'000;
    hft::SPSCRingBuffer<std::uint64_t, 1024> rb;

    std::thread producer([&] {
        for (std::uint64_t i = 0; i < kCount; ++i) {
            while (!rb.try_push(i)) {
                // spin
            }
        }
    });

    std::vector<std::uint64_t> received;
    received.reserve(kCount);

    std::thread consumer([&] {
        std::uint64_t val;
        while (received.size() < kCount) {
            if (rb.try_pop(val)) {
                received.push_back(val);
            }
        }
    });

    producer.join();
    consumer.join();

    assert(received.size() == kCount);
    for (std::uint64_t i = 0; i < kCount; ++i) {
        assert(received[i] == i);
    }
    std::cout << "  [PASS] concurrent SPSC (1M messages)\n";
}

void test_concurrent_throughput() {
    constexpr std::size_t kCount = 10'000'000;
    hft::SPSCRingBuffer<std::uint64_t, 4096> rb;

    auto start = std::chrono::high_resolution_clock::now();

    std::thread producer([&] {
        for (std::uint64_t i = 0; i < kCount; ++i) {
            while (!rb.try_push(i)) {}
        }
    });

    std::uint64_t sum = 0;
    std::thread consumer([&] {
        std::uint64_t val;
        for (std::size_t i = 0; i < kCount;) {
            if (rb.try_pop(val)) {
                sum += val;
                ++i;
            }
        }
    });

    producer.join();
    consumer.join();

    auto end = std::chrono::high_resolution_clock::now();
    auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();

    std::uint64_t expected_sum = (kCount - 1) * kCount / 2;
    assert(sum == expected_sum);

    double ops_per_sec = static_cast<double>(kCount) / (ns / 1e9);
    double ns_per_op = static_cast<double>(ns) / kCount;

    std::cout << "  [PASS] throughput: "
              << static_cast<long long>(ops_per_sec / 1e6) << "M ops/s, "
              << ns_per_op << " ns/op\n";
}

int main() {
    std::cout << "=== SPSCRingBuffer Tests ===\n";
    test_basic_push_pop();
    test_full_buffer();
    test_empty_pop();
    test_optional_pop();
    test_emplace();
    test_front_peek();
    test_wraparound();
    test_move_only_type();
    test_concurrent_spsc();
    test_concurrent_throughput();
    std::cout << "All SPSCRingBuffer tests passed!\n";
    return 0;
}
