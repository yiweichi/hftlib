#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <string_view>
#include <thread>
#include <vector>

namespace {

struct Allocation {
    void* ptr{nullptr};
    std::size_t size{0};
};

struct Workload {
    std::string_view name;
    std::size_t threads;
    std::size_t operations_per_thread;
    std::size_t live_set;
    std::size_t min_size;
    std::size_t max_size;
};

class XorShift64 {
public:
    explicit XorShift64(std::uint64_t seed) : state_(seed ? seed : 0x9e3779b97f4a7c15ULL) {}

    std::uint64_t next() noexcept {
        std::uint64_t x = state_;
        x ^= x << 13;
        x ^= x >> 7;
        x ^= x << 17;
        state_ = x;
        return x;
    }

    std::size_t uniform(std::size_t min_value, std::size_t max_value) noexcept {
        const std::uint64_t span = static_cast<std::uint64_t>(max_value - min_value + 1);
        return min_value + static_cast<std::size_t>(next() % span);
    }

private:
    std::uint64_t state_;
};

void touch_memory(void* ptr, std::size_t size, std::uint64_t marker) noexcept {
    auto* bytes = static_cast<unsigned char*>(ptr);
    bytes[0] = static_cast<unsigned char>(marker);
    bytes[size - 1] = static_cast<unsigned char>(marker >> 8);
}

std::uint64_t run_worker(const Workload& workload, std::size_t thread_id) {
    XorShift64 rng(0x123456789abcdef0ULL ^ (0x9e3779b97f4a7c15ULL * (thread_id + 1)));
    std::vector<Allocation> allocations(workload.live_set);
    std::uint64_t checksum = 0;

    for (std::size_t op = 0; op < workload.operations_per_thread; ++op) {
        const std::size_t index = rng.uniform(0, workload.live_set - 1);
        Allocation& slot = allocations[index];

        if (slot.ptr != nullptr) {
            checksum += static_cast<unsigned char*>(slot.ptr)[0];
            std::free(slot.ptr);
            slot = {};
        }

        const std::size_t size = rng.uniform(workload.min_size, workload.max_size);
        void* ptr = std::malloc(size);
        if (ptr == nullptr) {
            std::cerr << "malloc failed for size " << size << '\n';
            std::abort();
        }

        touch_memory(ptr, size, rng.next());
        slot.ptr = ptr;
        slot.size = size;
        checksum += size;
    }

    for (Allocation& slot : allocations) {
        if (slot.ptr != nullptr) {
            checksum += static_cast<unsigned char*>(slot.ptr)[slot.size - 1];
            std::free(slot.ptr);
        }
    }

    return checksum;
}

void run_workload(const Workload& workload) {
    using clock = std::chrono::steady_clock;

    std::vector<std::thread> threads;
    std::vector<std::uint64_t> checksums(workload.threads, 0);
    threads.reserve(workload.threads);

    const auto start = clock::now();
    for (std::size_t tid = 0; tid < workload.threads; ++tid) {
        threads.emplace_back([&, tid] {
            checksums[tid] = run_worker(workload, tid);
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }
    const auto end = clock::now();

    const auto ns =
        std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
    const std::size_t total_ops = workload.threads * workload.operations_per_thread;
    const double secs = static_cast<double>(ns) / 1e9;
    const double ops_per_sec = static_cast<double>(total_ops) / secs;
    const double ns_per_op = static_cast<double>(ns) / static_cast<double>(total_ops);
    const std::uint64_t checksum =
        std::accumulate(checksums.begin(), checksums.end(), std::uint64_t{0});

    std::cout << std::left << std::setw(24) << workload.name
              << " threads=" << std::setw(2) << workload.threads
              << " live_set=" << std::setw(5) << workload.live_set
              << " size=[" << workload.min_size << "," << workload.max_size << "] "
              << std::fixed << std::setprecision(2)
              << std::setw(10) << (ops_per_sec / 1e6) << " M alloc+free/s  "
              << std::setw(8) << ns_per_op << " ns/op  "
              << "checksum=" << checksum << '\n';

    std::cout << "RESULT"
              << " workload=" << workload.name
              << " threads=" << workload.threads
              << " live_set=" << workload.live_set
              << " min_size=" << workload.min_size
              << " max_size=" << workload.max_size
              << " total_ops=" << total_ops
              << " ops_per_sec=" << std::setprecision(6) << ops_per_sec
              << " ns_per_op=" << ns_per_op
              << " checksum=" << checksum
              << '\n';
}

} // namespace

int main() {
    const std::size_t hw_threads = std::max<std::size_t>(1, std::thread::hardware_concurrency());
    const std::size_t parallel_threads = std::min<std::size_t>(hw_threads, 8);

    const std::array<Workload, 4> workloads{{
        {"single_small_hot", 1, 2'000'000, 1024, 16, 128},
        {"single_mixed", 1, 1'000'000, 4096, 16, 4096},
        {"multi_small_hot", parallel_threads, 750'000, 1024, 16, 128},
        {"multi_mixed", parallel_threads, 500'000, 2048, 32, 8192},
    }};

    std::cout << "=== Allocator Benchmark (malloc/free workload) ===\n";
    std::cout << "hardware_threads=" << hw_threads
              << " benchmark_threads=" << parallel_threads << "\n\n";

    for (const auto& workload : workloads) {
        run_workload(workload);
    }

    return 0;
}
