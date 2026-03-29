#pragma once

#include "mpsc_queue.hpp"
#include "spsc_ring_buffer.hpp"

#include <array>
#include <atomic>
#include <cassert>
#include <cstddef>
#include <limits>
#include <new>
#include <optional>
#include <type_traits>
#include <utility>

namespace hft {

// ============================================================================
// ZeroCopyDispatcher — preallocated fan-out dispatcher for HFT-style pipelines
//
// Design choices:
//   - Message bodies live in a fixed pool and are never copied during fan-out
//   - Producers publish handles into an MPSC ingress queue
//   - A single dispatch thread fans those handles out into per-subscriber SPSC queues
//   - Subscribers consume lightweight leases that release the pooled message on drop
// ============================================================================

template <typename T,
          std::size_t PoolCapacity,
          std::size_t IngressCapacity,
          std::size_t MaxSubscribers,
          std::size_t SubscriberCapacity>
class ZeroCopyDispatcher {
    static_assert(PoolCapacity > 0, "PoolCapacity must be > 0");
    static_assert(IngressCapacity > 0, "IngressCapacity must be > 0");
    static_assert(MaxSubscribers > 0, "MaxSubscribers must be > 0");
    static_assert(SubscriberCapacity > 0, "SubscriberCapacity must be > 0");
    static_assert(std::is_destructible_v<T>, "T must be destructible");

    static constexpr std::size_t kReservedRefCount =
        std::numeric_limits<std::size_t>::max();

    struct MessageSlot {
        std::atomic<std::size_t> ref_count{0}; // 0 = free
        alignas(T) unsigned char storage[sizeof(T)];

        T* ptr() noexcept { return std::launder(reinterpret_cast<T*>(storage)); }
        const T* ptr() const noexcept { return std::launder(reinterpret_cast<const T*>(storage)); }
    };

    struct SubscriberSlot {
        bool active{false};
        SPSCRingBuffer<std::size_t, SubscriberCapacity> queue;
    };

public:
    class Lease {
    public:
        Lease() = default;

        Lease(const Lease&) = delete;
        Lease& operator=(const Lease&) = delete;

        Lease(Lease&& other) noexcept
            : dispatcher_(other.dispatcher_), slot_index_(other.slot_index_) {
            other.dispatcher_ = nullptr;
        }

        Lease& operator=(Lease&& other) noexcept {
            if (this != &other) {
                reset();
                dispatcher_ = other.dispatcher_;
                slot_index_ = other.slot_index_;
                other.dispatcher_ = nullptr;
            }
            return *this;
        }

        ~Lease() { reset(); }

        const T& operator*() const noexcept { return *dispatcher_->pool_[slot_index_].ptr(); }
        const T* operator->() const noexcept { return dispatcher_->pool_[slot_index_].ptr(); }
        const T* get() const noexcept {
            return dispatcher_ ? dispatcher_->pool_[slot_index_].ptr() : nullptr;
        }

        explicit operator bool() const noexcept { return dispatcher_ != nullptr; }

        void reset() noexcept {
            if (dispatcher_) {
                dispatcher_->release_slot(slot_index_);
                dispatcher_ = nullptr;
            }
        }

    private:
        friend class ZeroCopyDispatcher;

        Lease(ZeroCopyDispatcher* dispatcher, std::size_t slot_index) noexcept
            : dispatcher_(dispatcher), slot_index_(slot_index) {}

        ZeroCopyDispatcher* dispatcher_{nullptr};
        std::size_t slot_index_{0};
    };

    ZeroCopyDispatcher() = default;

    ~ZeroCopyDispatcher() {
        // Destruction must happen after publishers, dispatcher, and subscribers stop.
        if (pending_handle_.has_value()) {
            release_slot(*pending_handle_);
            pending_handle_.reset();
        }

        std::size_t slot_index = 0;
        while (ingress_.try_pop(slot_index)) {
            release_slot(slot_index);
        }

        for (auto& subscriber : subscribers_) {
            while (subscriber.queue.try_pop(slot_index)) {
                release_slot(slot_index);
            }
        }
    }

    ZeroCopyDispatcher(const ZeroCopyDispatcher&) = delete;
    ZeroCopyDispatcher& operator=(const ZeroCopyDispatcher&) = delete;

    [[nodiscard]] std::optional<std::size_t> add_subscriber() noexcept {
        for (std::size_t i = 0; i < MaxSubscribers; ++i) {
            if (!subscribers_[i].active) {
                subscribers_[i].active = true;
                ++active_subscribers_;
                return i;
            }
        }
        return std::nullopt;
    }

    template <typename... Args>
    bool try_publish(Args&&... args) noexcept(std::is_nothrow_constructible_v<T, Args...>) {
        const auto slot_index = try_acquire_slot();
        if (!slot_index.has_value()) return false;

        MessageSlot& slot = pool_[*slot_index];
        new (slot.ptr()) T(std::forward<Args>(args)...);
        slot.ref_count.store(1, std::memory_order_release); // owned by ingress/pending dispatcher path
        std::atomic_thread_fence(std::memory_order_release);

        if (!ingress_.try_push(*slot_index)) {
            slot.ptr()->~T();
            slot.ref_count.store(0, std::memory_order_release);
            return false;
        }
        return true;
    }

    bool dispatch_one() noexcept {
        if (!pending_handle_.has_value()) {
            std::size_t slot_index = 0;
            if (!ingress_.try_pop(slot_index)) return false;
            pending_handle_ = slot_index;
        }

        std::atomic_thread_fence(std::memory_order_acquire);

        for (std::size_t i = 0; i < MaxSubscribers; ++i) {
            if (subscribers_[i].active &&
                subscribers_[i].queue.size() == subscribers_[i].queue.capacity()) {
                return false;
            }
        }

        const std::size_t slot_index = *pending_handle_;
        if (active_subscribers_ != 0) {
            pool_[slot_index].ref_count.fetch_add(active_subscribers_, std::memory_order_acq_rel);
        }

        for (std::size_t i = 0; i < MaxSubscribers; ++i) {
            if (!subscribers_[i].active) continue;
            const bool pushed = subscribers_[i].queue.try_push(slot_index);
            assert(pushed && "subscriber queue had room during pre-check");
            (void)pushed;
        }

        pending_handle_.reset();
        release_slot(slot_index); // release dispatcher's own ownership
        return true;
    }

    std::size_t dispatch_available(std::size_t max_messages = static_cast<std::size_t>(-1)) noexcept {
        std::size_t count = 0;
        while (count < max_messages && dispatch_one()) {
            ++count;
        }
        return count;
    }

    [[nodiscard]] std::optional<Lease> try_consume(std::size_t subscriber_id) noexcept {
        if (subscriber_id >= MaxSubscribers || !subscribers_[subscriber_id].active) {
            return std::nullopt;
        }

        std::size_t slot_index = 0;
        if (!subscribers_[subscriber_id].queue.try_pop(slot_index)) {
            return std::nullopt;
        }

        std::atomic_thread_fence(std::memory_order_acquire);
        return Lease(this, slot_index);
    }

    [[nodiscard]] std::size_t ingress_size() const noexcept { return ingress_.size(); }
    [[nodiscard]] bool has_pending() const noexcept { return pending_handle_.has_value(); }
    [[nodiscard]] std::size_t subscriber_count() const noexcept { return active_subscribers_; }

private:
    [[nodiscard]] std::optional<std::size_t> try_acquire_slot() noexcept {
        const std::size_t start = next_slot_hint_.fetch_add(1, std::memory_order_relaxed);
        for (std::size_t offset = 0; offset < PoolCapacity; ++offset) {
            const std::size_t slot_index = (start + offset) % PoolCapacity;
            std::size_t expected = 0;
            if (pool_[slot_index].ref_count.compare_exchange_strong(
                    expected,
                    kReservedRefCount,
                    std::memory_order_acq_rel,
                    std::memory_order_relaxed)) {
                return slot_index;
            }
        }
        return std::nullopt;
    }

    void release_slot(std::size_t slot_index) noexcept {
        MessageSlot& slot = pool_[slot_index];
        const std::size_t old = slot.ref_count.fetch_sub(1, std::memory_order_acq_rel);
        if (old == 1) {
            slot.ptr()->~T();
        }
    }

    std::array<MessageSlot, PoolCapacity> pool_{};
    std::atomic<std::size_t> next_slot_hint_{0};
    MPSCQueue<std::size_t, IngressCapacity> ingress_{};
    std::array<SubscriberSlot, MaxSubscribers> subscribers_{};
    std::size_t active_subscribers_{0};              // register subscribers before concurrency starts
    std::optional<std::size_t> pending_handle_{};    // owned by single dispatch thread
};

} // namespace hft
