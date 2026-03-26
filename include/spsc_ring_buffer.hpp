#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <new>
#include <optional>
#include <type_traits>
#include <utility>

namespace hft {

// ============================================================================
// SPSCRingBuffer — Lock-free single-producer single-consumer ring buffer
//
// Design choices for HFT:
//   - Power-of-2 capacity for branchless modulo (bitwise AND)
//   - Cache-line padding between head/tail to prevent false sharing
//   - Sequentially consistent loads on the "own" index, relaxed on the "other"
//   - No locks, no CAS — just atomic load/store
// ============================================================================

#ifdef __cpp_lib_hardware_destructive_interference_size
    inline constexpr std::size_t kCacheLineSize =
        std::hardware_destructive_interference_size;
#else
    inline constexpr std::size_t kCacheLineSize = 64;
#endif

template <typename T, std::size_t Capacity>
class SPSCRingBuffer {
    static_assert(Capacity > 0, "Capacity must be > 0");
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of 2");
    static_assert(std::is_nothrow_move_constructible_v<T> || std::is_nothrow_copy_constructible_v<T>,
                  "T must be nothrow move- or copy-constructible for lock-free guarantees");

    static constexpr std::size_t kMask = Capacity - 1;

    // Slot storage: aligned, uninitialized
    struct alignas(T) Slot {
        alignas(T) unsigned char storage[sizeof(T)];

        T* ptr() noexcept { return reinterpret_cast<T*>(storage); }
        const T* ptr() const noexcept { return reinterpret_cast<const T*>(storage); }
    };

    // Separate head and tail into different cache lines
    alignas(kCacheLineSize) std::atomic<std::size_t> head_{0}; // written by consumer
    alignas(kCacheLineSize) std::atomic<std::size_t> tail_{0}; // written by producer
    alignas(kCacheLineSize) Slot slots_[Capacity];

public:
    SPSCRingBuffer() = default;

    ~SPSCRingBuffer() {
        // Destroy any remaining elements
        auto h = head_.load(std::memory_order_relaxed);
        auto t = tail_.load(std::memory_order_relaxed);
        while (h != t) {
            slots_[h & kMask].ptr()->~T();
            ++h;
        }
    }

    SPSCRingBuffer(const SPSCRingBuffer&) = delete;
    SPSCRingBuffer& operator=(const SPSCRingBuffer&) = delete;

    // --- Producer API (single thread) ---

    // Try to push a copy. Returns false if full.
    bool try_push(const T& value) noexcept(std::is_nothrow_copy_constructible_v<T>) {
        const auto t = tail_.load(std::memory_order_relaxed);
        const auto h = head_.load(std::memory_order_acquire);

        if (t - h >= Capacity) return false; // full

        new (slots_[t & kMask].ptr()) T(value);
        tail_.store(t + 1, std::memory_order_release);
        return true;
    }

    // Try to push via move. Returns false if full.
    bool try_push(T&& value) noexcept(std::is_nothrow_move_constructible_v<T>) {
        const auto t = tail_.load(std::memory_order_relaxed);
        const auto h = head_.load(std::memory_order_acquire);

        if (t - h >= Capacity) return false;

        new (slots_[t & kMask].ptr()) T(std::move(value));
        tail_.store(t + 1, std::memory_order_release);
        return true;
    }

    // Construct in-place. Returns false if full.
    template <typename... Args>
    bool try_emplace(Args&&... args) noexcept(std::is_nothrow_constructible_v<T, Args...>) {
        const auto t = tail_.load(std::memory_order_relaxed);
        const auto h = head_.load(std::memory_order_acquire);

        if (t - h >= Capacity) return false;

        new (slots_[t & kMask].ptr()) T(std::forward<Args>(args)...);
        tail_.store(t + 1, std::memory_order_release);
        return true;
    }

    // --- Consumer API (single thread) ---

    // Try to pop into `out`. Returns false if empty.
    bool try_pop(T& out) noexcept(std::is_nothrow_move_assignable_v<T>) {
        const auto h = head_.load(std::memory_order_relaxed);
        const auto t = tail_.load(std::memory_order_acquire);

        if (h == t) return false; // empty

        T* slot = slots_[h & kMask].ptr();
        out = std::move(*slot);
        slot->~T();
        head_.store(h + 1, std::memory_order_release);
        return true;
    }

    // Try to pop, returning optional.
    std::optional<T> try_pop() noexcept(std::is_nothrow_move_constructible_v<T>) {
        const auto h = head_.load(std::memory_order_relaxed);
        const auto t = tail_.load(std::memory_order_acquire);

        if (h == t) return std::nullopt;

        T* slot = slots_[h & kMask].ptr();
        std::optional<T> result(std::move(*slot));
        slot->~T();
        head_.store(h + 1, std::memory_order_release);
        return result;
    }

    // Peek at front element without consuming. Returns nullptr if empty.
    const T* front() const noexcept {
        const auto h = head_.load(std::memory_order_relaxed);
        const auto t = tail_.load(std::memory_order_acquire);
        if (h == t) return nullptr;
        return slots_[h & kMask].ptr();
    }

    // --- Capacity queries (safe from any thread) ---

    [[nodiscard]] std::size_t size() const noexcept {
        auto t = tail_.load(std::memory_order_acquire);
        auto h = head_.load(std::memory_order_acquire);
        return t - h;
    }

    [[nodiscard]] bool empty() const noexcept { return size() == 0; }

    static constexpr std::size_t capacity() noexcept { return Capacity; }
};

} // namespace hft
