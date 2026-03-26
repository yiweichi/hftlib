#pragma once

#include <atomic>
#include <cstddef>
#include <type_traits>
#include <utility>

namespace hft {

// C++17 polyfill for std::is_unbounded_array_v (C++20)
template <typename T> struct is_unbounded_array : std::false_type {};
template <typename T> struct is_unbounded_array<T[]> : std::true_type {};
template <typename T> inline constexpr bool is_unbounded_array_v = is_unbounded_array<T>::value;

// ============================================================================
// UniquePtr — exclusive ownership, zero overhead over raw pointer
// ============================================================================

template <typename T>
struct DefaultDeleter {
    constexpr DefaultDeleter() noexcept = default;

    // Allow conversion from deleter of derived type
    template <typename U, typename = std::enable_if_t<std::is_convertible_v<U*, T*>>>
    DefaultDeleter(const DefaultDeleter<U>&) noexcept {}

    void operator()(T* ptr) const noexcept {
        static_assert(sizeof(T) > 0, "cannot delete incomplete type");
        delete ptr;
    }
};

template <typename T>
struct DefaultDeleter<T[]> {
    constexpr DefaultDeleter() noexcept = default;

    void operator()(T* ptr) const noexcept {
        static_assert(sizeof(T) > 0, "cannot delete incomplete type");
        delete[] ptr;
    }
};

template <typename T, typename Deleter = DefaultDeleter<T>>
class UniquePtr {
public:
    using element_type = T;
    using deleter_type = Deleter;
    using pointer = T*;

private:
    pointer ptr_ = nullptr;
    [[no_unique_address]] Deleter deleter_;

public:
    constexpr UniquePtr() noexcept = default;
    constexpr UniquePtr(std::nullptr_t) noexcept : ptr_(nullptr) {}

    explicit UniquePtr(pointer p) noexcept : ptr_(p) {}
    UniquePtr(pointer p, const Deleter& d) noexcept : ptr_(p), deleter_(d) {}
    UniquePtr(pointer p, Deleter&& d) noexcept : ptr_(p), deleter_(std::move(d)) {}

    ~UniquePtr() { reset(); }

    // Move only
    UniquePtr(const UniquePtr&) = delete;
    UniquePtr& operator=(const UniquePtr&) = delete;

    UniquePtr(UniquePtr&& other) noexcept
        : ptr_(other.release()), deleter_(std::move(other.deleter_)) {}

    UniquePtr& operator=(UniquePtr&& other) noexcept {
        if (this != &other) {
            reset(other.release());
            deleter_ = std::move(other.deleter_);
        }
        return *this;
    }

    // Converting move from derived type
    template <typename U, typename D,
              typename = std::enable_if_t<std::is_convertible_v<U*, T*>>>
    UniquePtr(UniquePtr<U, D>&& other) noexcept
        : ptr_(other.release()), deleter_(std::move(other.get_deleter())) {}

    UniquePtr& operator=(std::nullptr_t) noexcept {
        reset();
        return *this;
    }

    // --- Observers ---

    pointer get() const noexcept { return ptr_; }
    Deleter& get_deleter() noexcept { return deleter_; }
    const Deleter& get_deleter() const noexcept { return deleter_; }

    explicit operator bool() const noexcept { return ptr_ != nullptr; }

    T& operator*() const noexcept { return *ptr_; }
    pointer operator->() const noexcept { return ptr_; }

    // --- Modifiers ---

    pointer release() noexcept {
        pointer tmp = ptr_;
        ptr_ = nullptr;
        return tmp;
    }

    void reset(pointer p = nullptr) noexcept {
        pointer old = ptr_;
        ptr_ = p;
        if (old) deleter_(old);
    }

    void swap(UniquePtr& other) noexcept {
        using std::swap;
        swap(ptr_, other.ptr_);
        swap(deleter_, other.deleter_);
    }

    // --- Comparison ---

    friend bool operator==(const UniquePtr& a, const UniquePtr& b) { return a.ptr_ == b.ptr_; }
    friend bool operator!=(const UniquePtr& a, const UniquePtr& b) { return a.ptr_ != b.ptr_; }
    friend bool operator==(const UniquePtr& a, std::nullptr_t) { return !a; }
    friend bool operator!=(const UniquePtr& a, std::nullptr_t) { return bool(a); }
};

// Array specialization
template <typename T, typename Deleter>
class UniquePtr<T[], Deleter> {
public:
    using element_type = T;
    using deleter_type = Deleter;
    using pointer = T*;

private:
    pointer ptr_ = nullptr;
    [[no_unique_address]] Deleter deleter_;

public:
    constexpr UniquePtr() noexcept = default;
    constexpr UniquePtr(std::nullptr_t) noexcept : ptr_(nullptr) {}
    explicit UniquePtr(pointer p) noexcept : ptr_(p) {}

    ~UniquePtr() { reset(); }

    UniquePtr(const UniquePtr&) = delete;
    UniquePtr& operator=(const UniquePtr&) = delete;

    UniquePtr(UniquePtr&& other) noexcept
        : ptr_(other.release()), deleter_(std::move(other.deleter_)) {}

    UniquePtr& operator=(UniquePtr&& other) noexcept {
        if (this != &other) {
            reset(other.release());
            deleter_ = std::move(other.deleter_);
        }
        return *this;
    }

    T& operator[](std::size_t i) const { return ptr_[i]; }
    pointer get() const noexcept { return ptr_; }
    explicit operator bool() const noexcept { return ptr_ != nullptr; }

    pointer release() noexcept {
        pointer tmp = ptr_;
        ptr_ = nullptr;
        return tmp;
    }

    void reset(pointer p = nullptr) noexcept {
        pointer old = ptr_;
        ptr_ = p;
        if (old) deleter_(old);
    }
};

template <typename T, typename... Args>
std::enable_if_t<!std::is_array_v<T>, UniquePtr<T>>
make_unique(Args&&... args) {
    return UniquePtr<T>(new T(std::forward<Args>(args)...));
}

template <typename T>
std::enable_if_t<is_unbounded_array_v<T>, UniquePtr<T>>
make_unique(std::size_t n) {
    return UniquePtr<T>(new std::remove_extent_t<T>[n]());
}

// ============================================================================
// ControlBlock — shared reference count (intrusive)
// ============================================================================

struct ControlBlock {
    std::atomic<long> strong_count{1};
    std::atomic<long> weak_count{1}; // +1 bias while strong_count > 0

    void add_strong() noexcept {
        strong_count.fetch_add(1, std::memory_order_relaxed);
    }

    // Returns true if successfully acquired a strong ref (for weak->strong promotion)
    bool try_add_strong() noexcept {
        long cur = strong_count.load(std::memory_order_relaxed);
        while (cur > 0) {
            if (strong_count.compare_exchange_weak(cur, cur + 1,
                    std::memory_order_acq_rel, std::memory_order_relaxed)) {
                return true;
            }
        }
        return false;
    }

    void add_weak() noexcept {
        weak_count.fetch_add(1, std::memory_order_relaxed);
    }

    // Returns true if this was the last strong ref (caller should destroy managed object)
    bool release_strong() noexcept {
        return strong_count.fetch_sub(1, std::memory_order_acq_rel) == 1;
    }

    // Returns true if this call should destroy the control block itself
    bool release_weak() noexcept {
        return weak_count.fetch_sub(1, std::memory_order_acq_rel) == 1;
    }

    long use_count() const noexcept {
        return strong_count.load(std::memory_order_relaxed);
    }
};

// Type-erased control block that knows how to destroy the object
template <typename T>
struct ControlBlockImpl : ControlBlock {
    T* ptr;

    explicit ControlBlockImpl(T* p) : ptr(p) {}

    void destroy_object() {
        delete ptr;
        ptr = nullptr;
    }
};

// ============================================================================
// SharedPtr — shared ownership with reference counting
// ============================================================================

template <typename T> class WeakPtr;

template <typename T>
class SharedPtr {
    template <typename U> friend class SharedPtr;
    template <typename U> friend class WeakPtr;

public:
    using element_type = T;

private:
    T* ptr_ = nullptr;
    ControlBlockImpl<T>* cb_ = nullptr;

    // Private constructor for WeakPtr::lock()
    SharedPtr(T* p, ControlBlockImpl<T>* cb) noexcept : ptr_(p), cb_(cb) {}

public:
    constexpr SharedPtr() noexcept = default;
    constexpr SharedPtr(std::nullptr_t) noexcept {}

    explicit SharedPtr(T* p) : ptr_(p) {
        if (p) {
            try {
                cb_ = new ControlBlockImpl<T>(p);
            } catch (...) {
                delete p;
                throw;
            }
        }
    }

    SharedPtr(const SharedPtr& other) noexcept
        : ptr_(other.ptr_), cb_(other.cb_) {
        if (cb_) cb_->add_strong();
    }

    SharedPtr(SharedPtr&& other) noexcept
        : ptr_(other.ptr_), cb_(other.cb_) {
        other.ptr_ = nullptr;
        other.cb_ = nullptr;
    }

    // Converting constructors for derived-to-base
    template <typename U, typename = std::enable_if_t<std::is_convertible_v<U*, T*>>>
    SharedPtr(const SharedPtr<U>& other) noexcept
        : ptr_(other.ptr_), cb_(reinterpret_cast<ControlBlockImpl<T>*>(other.cb_)) {
        if (cb_) cb_->add_strong();
    }

    template <typename U, typename = std::enable_if_t<std::is_convertible_v<U*, T*>>>
    SharedPtr(SharedPtr<U>&& other) noexcept
        : ptr_(other.ptr_), cb_(reinterpret_cast<ControlBlockImpl<T>*>(other.cb_)) {
        other.ptr_ = nullptr;
        other.cb_ = nullptr;
    }

    ~SharedPtr() { reset(); }

    SharedPtr& operator=(const SharedPtr& other) noexcept {
        if (this != &other) {
            SharedPtr tmp(other);
            swap(tmp);
        }
        return *this;
    }

    SharedPtr& operator=(SharedPtr&& other) noexcept {
        if (this != &other) {
            reset();
            ptr_ = other.ptr_;
            cb_ = other.cb_;
            other.ptr_ = nullptr;
            other.cb_ = nullptr;
        }
        return *this;
    }

    // --- Observers ---

    T* get() const noexcept { return ptr_; }
    T& operator*() const noexcept { return *ptr_; }
    T* operator->() const noexcept { return ptr_; }
    explicit operator bool() const noexcept { return ptr_ != nullptr; }

    long use_count() const noexcept {
        return cb_ ? cb_->use_count() : 0;
    }

    bool unique() const noexcept { return use_count() == 1; }

    // --- Modifiers ---

    void reset() noexcept {
        if (cb_) {
            if (cb_->release_strong()) {
                cb_->destroy_object();
                // Remove the weak bias (the +1 held while strong_count > 0).
                // If no WeakPtrs remain, this makes weak_count hit 0 → free the cb.
                if (cb_->release_weak()) {
                    delete cb_;
                }
            }
        }
        ptr_ = nullptr;
        cb_ = nullptr;
    }

    void reset(T* p) {
        SharedPtr tmp(p);
        swap(tmp);
    }

    void swap(SharedPtr& other) noexcept {
        using std::swap;
        swap(ptr_, other.ptr_);
        swap(cb_, other.cb_);
    }

    // --- Comparison ---

    friend bool operator==(const SharedPtr& a, const SharedPtr& b) { return a.ptr_ == b.ptr_; }
    friend bool operator!=(const SharedPtr& a, const SharedPtr& b) { return a.ptr_ != b.ptr_; }
    friend bool operator==(const SharedPtr& a, std::nullptr_t) { return !a; }
    friend bool operator!=(const SharedPtr& a, std::nullptr_t) { return bool(a); }
};

template <typename T, typename... Args>
SharedPtr<T> make_shared(Args&&... args) {
    return SharedPtr<T>(new T(std::forward<Args>(args)...));
}

// ============================================================================
// WeakPtr — non-owning observer of SharedPtr
// ============================================================================

template <typename T>
class WeakPtr {
    template <typename U> friend class WeakPtr;

private:
    T* ptr_ = nullptr;
    ControlBlockImpl<T>* cb_ = nullptr;

public:
    constexpr WeakPtr() noexcept = default;

    WeakPtr(const SharedPtr<T>& sp) noexcept
        : ptr_(sp.ptr_), cb_(sp.cb_) {
        if (cb_) cb_->add_weak();
    }

    WeakPtr(const WeakPtr& other) noexcept
        : ptr_(other.ptr_), cb_(other.cb_) {
        if (cb_) cb_->add_weak();
    }

    WeakPtr(WeakPtr&& other) noexcept
        : ptr_(other.ptr_), cb_(other.cb_) {
        other.ptr_ = nullptr;
        other.cb_ = nullptr;
    }

    ~WeakPtr() {
        if (cb_ && cb_->release_weak()) {
            delete cb_;
        }
    }

    WeakPtr& operator=(const WeakPtr& other) noexcept {
        if (this != &other) {
            WeakPtr tmp(other);
            swap(tmp);
        }
        return *this;
    }

    WeakPtr& operator=(WeakPtr&& other) noexcept {
        if (this != &other) {
            if (cb_ && cb_->release_weak()) delete cb_;
            ptr_ = other.ptr_;
            cb_ = other.cb_;
            other.ptr_ = nullptr;
            other.cb_ = nullptr;
        }
        return *this;
    }

    WeakPtr& operator=(const SharedPtr<T>& sp) noexcept {
        WeakPtr tmp(sp);
        swap(tmp);
        return *this;
    }

    [[nodiscard]] SharedPtr<T> lock() const noexcept {
        if (cb_ && cb_->try_add_strong()) {
            return SharedPtr<T>(ptr_, cb_);
        }
        return SharedPtr<T>();
    }

    bool expired() const noexcept {
        return !cb_ || cb_->use_count() == 0;
    }

    long use_count() const noexcept {
        return cb_ ? cb_->use_count() : 0;
    }

    void swap(WeakPtr& other) noexcept {
        using std::swap;
        swap(ptr_, other.ptr_);
        swap(cb_, other.cb_);
    }

    void reset() noexcept {
        if (cb_ && cb_->release_weak()) delete cb_;
        ptr_ = nullptr;
        cb_ = nullptr;
    }
};

} // namespace hft
