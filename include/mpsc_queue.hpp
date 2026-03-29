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
// MPSCQueue — Lock-free bounded multi-producer single-consumer queue
//
// Design choices for HFT:
//   - Power-of-2 capacity for branchless indexing
//   - Per-slot sequence numbers to avoid locks and global producer stalls
//   - Producers reserve slots with CAS on tail
//   - Consumer owns head exclusively, so dequeue stays simple
// ============================================================================

#ifdef __cpp_lib_hardware_destructive_interference_size
inline constexpr std::size_t kMPSCQueueCacheLineSize =
    std::hardware_destructive_interference_size;
#else
inline constexpr std::size_t kMPSCQueueCacheLineSize = 64;
#endif

template <typename T, std::size_t Capacity>
class MPSCQueue {
    static_assert(Capacity > 0, "Capacity must be > 0");
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of 2");
    static_assert(std::is_nothrow_move_constructible_v<T> || std::is_nothrow_copy_constructible_v<T>,
                  "T must be nothrow move- or copy-constructible");

    static constexpr std::size_t kMask = Capacity - 1;

    struct Slot {
        std::atomic<std::size_t> sequence;
        alignas(T) unsigned char storage[sizeof(T)];

        T* ptr() noexcept { return reinterpret_cast<T*>(storage); }
        const T* ptr() const noexcept { return reinterpret_cast<const T*>(storage); }
    };

    alignas(kMPSCQueueCacheLineSize) std::atomic<std::size_t> head_{0};
    alignas(kMPSCQueueCacheLineSize) std::atomic<std::size_t> tail_{0};
    alignas(kMPSCQueueCacheLineSize) Slot slots_[Capacity];

public:
    MPSCQueue() noexcept {
        for (std::size_t i = 0; i < Capacity; ++i) {
            slots_[i].sequence.store(i, std::memory_order_relaxed);
        }
    }

    ~MPSCQueue() {
        // Destruction must happen after producers/consumer have stopped.
        auto h = head_.load(std::memory_order_relaxed);
        const auto t = tail_.load(std::memory_order_relaxed);
        while (h != t) {
            Slot& slot = slots_[h & kMask];
            if (slot.sequence.load(std::memory_order_relaxed) == h + 1) {
                slot.ptr()->~T();
                slot.sequence.store(h + Capacity, std::memory_order_relaxed);
            }
            ++h;
        }
    }

    MPSCQueue(const MPSCQueue&) = delete;
    MPSCQueue& operator=(const MPSCQueue&) = delete;

    bool try_push(const T& value) noexcept(std::is_nothrow_copy_constructible_v<T>) {
        return try_emplace(value);
    }

    bool try_push(T&& value) noexcept(std::is_nothrow_move_constructible_v<T>) {
        return try_emplace(std::move(value));
    }

    template <typename... Args>
    bool try_emplace(Args&&... args) noexcept(std::is_nothrow_constructible_v<T, Args...>) {
        std::size_t pos = tail_.load(std::memory_order_relaxed);

        for (;;) {
            Slot& slot = slots_[pos & kMask];
            const auto seq = slot.sequence.load(std::memory_order_acquire);
            const auto diff = static_cast<std::ptrdiff_t>(seq) - static_cast<std::ptrdiff_t>(pos);

            if (diff == 0) {
                if (tail_.compare_exchange_weak(pos, pos + 1,
                                                std::memory_order_acq_rel,
                                                std::memory_order_relaxed)) {
                    new (slot.ptr()) T(std::forward<Args>(args)...);
                    slot.sequence.store(pos + 1, std::memory_order_release);
                    return true;
                }
            } else if (diff < 0) {
                return false; // full
            } else {
                pos = tail_.load(std::memory_order_relaxed);
            }
        }
    }

    bool try_pop(T& out) noexcept(std::is_nothrow_move_assignable_v<T>) {
        const auto pos = head_.load(std::memory_order_relaxed);
        Slot& slot = slots_[pos & kMask];
        const auto seq = slot.sequence.load(std::memory_order_acquire);
        const auto diff = static_cast<std::ptrdiff_t>(seq) - static_cast<std::ptrdiff_t>(pos + 1);

        if (diff < 0) return false; // empty

        T* item = slot.ptr();
        out = std::move(*item);
        item->~T();
        slot.sequence.store(pos + Capacity, std::memory_order_release);
        head_.store(pos + 1, std::memory_order_relaxed);
        return true;
    }

    std::optional<T> try_pop() noexcept(std::is_nothrow_move_constructible_v<T>) {
        const auto pos = head_.load(std::memory_order_relaxed);
        Slot& slot = slots_[pos & kMask];
        const auto seq = slot.sequence.load(std::memory_order_acquire);
        const auto diff = static_cast<std::ptrdiff_t>(seq) - static_cast<std::ptrdiff_t>(pos + 1);

        if (diff < 0) return std::nullopt;

        T* item = slot.ptr();
        std::optional<T> result(std::move(*item));
        item->~T();
        slot.sequence.store(pos + Capacity, std::memory_order_release);
        head_.store(pos + 1, std::memory_order_relaxed);
        return result;
    }

    [[nodiscard]] std::size_t size() const noexcept {
        const auto tail = tail_.load(std::memory_order_acquire);
        const auto head = head_.load(std::memory_order_acquire);
        return tail - head;
    }

    [[nodiscard]] bool empty() const noexcept { return size() == 0; }
    static constexpr std::size_t capacity() noexcept { return Capacity; }
};

} // namespace hft
