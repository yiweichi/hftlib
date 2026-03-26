#pragma once

#include <algorithm>
#include <cstddef>
#include <initializer_list>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace hft {

template <typename T, typename Allocator = std::allocator<T>>
class SimpleVector {
public:
    using value_type = T;
    using allocator_type = Allocator;
    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;
    using reference = T&;
    using const_reference = const T&;
    using pointer = T*;
    using const_pointer = const T*;
    using iterator = T*;
    using const_iterator = const T*;
    using reverse_iterator = std::reverse_iterator<iterator>;
    using const_reverse_iterator = std::reverse_iterator<const_iterator>;

private:
    using alloc_traits = std::allocator_traits<Allocator>;

    pointer data_ = nullptr;
    size_type size_ = 0;
    size_type capacity_ = 0;
    [[no_unique_address]] Allocator alloc_;

    pointer allocate(size_type n) {
        return n ? alloc_traits::allocate(alloc_, n) : nullptr;
    }

    void deallocate(pointer p, size_type n) {
        if (p) alloc_traits::deallocate(alloc_, p, n);
    }

    void destroy_range(pointer first, pointer last) {
        for (; first != last; ++first) {
            alloc_traits::destroy(alloc_, first);
        }
    }

    // Grow to at least `min_cap`, using 2x growth strategy
    void grow(size_type min_cap) {
        size_type new_cap = capacity_ ? capacity_ * 2 : 1;
        while (new_cap < min_cap) new_cap *= 2;

        pointer new_data = allocate(new_cap);
        size_type new_size = 0;

        try {
            for (size_type i = 0; i < size_; ++i) {
                alloc_traits::construct(alloc_, new_data + i, std::move(data_[i]));
                ++new_size;
            }
        } catch (...) {
            destroy_range(new_data, new_data + new_size);
            deallocate(new_data, new_cap);
            throw;
        }

        destroy_range(data_, data_ + size_);
        deallocate(data_, capacity_);

        data_ = new_data;
        capacity_ = new_cap;
    }

public:
    // --- Constructors ---

    SimpleVector() noexcept(noexcept(Allocator())) = default;

    explicit SimpleVector(const Allocator& alloc) noexcept
        : alloc_(alloc) {}

    explicit SimpleVector(size_type count, const Allocator& alloc = Allocator())
        : alloc_(alloc) {
        data_ = allocate(count);
        capacity_ = count;
        try {
            for (; size_ < count; ++size_) {
                alloc_traits::construct(alloc_, data_ + size_);
            }
        } catch (...) {
            destroy_range(data_, data_ + size_);
            deallocate(data_, capacity_);
            data_ = nullptr;
            size_ = capacity_ = 0;
            throw;
        }
    }

    SimpleVector(size_type count, const T& value, const Allocator& alloc = Allocator())
        : alloc_(alloc) {
        data_ = allocate(count);
        capacity_ = count;
        try {
            for (; size_ < count; ++size_) {
                alloc_traits::construct(alloc_, data_ + size_, value);
            }
        } catch (...) {
            destroy_range(data_, data_ + size_);
            deallocate(data_, capacity_);
            data_ = nullptr;
            size_ = capacity_ = 0;
            throw;
        }
    }

    SimpleVector(std::initializer_list<T> init, const Allocator& alloc = Allocator())
        : alloc_(alloc) {
        data_ = allocate(init.size());
        capacity_ = init.size();
        try {
            for (auto& v : init) {
                alloc_traits::construct(alloc_, data_ + size_, v);
                ++size_;
            }
        } catch (...) {
            destroy_range(data_, data_ + size_);
            deallocate(data_, capacity_);
            data_ = nullptr;
            size_ = capacity_ = 0;
            throw;
        }
    }

    // Copy constructor
    SimpleVector(const SimpleVector& other)
        : alloc_(alloc_traits::select_on_container_copy_construction(other.alloc_)) {
        data_ = allocate(other.size_);
        capacity_ = other.size_;
        try {
            for (; size_ < other.size_; ++size_) {
                alloc_traits::construct(alloc_, data_ + size_, other.data_[size_]);
            }
        } catch (...) {
            destroy_range(data_, data_ + size_);
            deallocate(data_, capacity_);
            data_ = nullptr;
            size_ = capacity_ = 0;
            throw;
        }
    }

    // Move constructor
    SimpleVector(SimpleVector&& other) noexcept
        : data_(other.data_)
        , size_(other.size_)
        , capacity_(other.capacity_)
        , alloc_(std::move(other.alloc_)) {
        other.data_ = nullptr;
        other.size_ = 0;
        other.capacity_ = 0;
    }

    ~SimpleVector() {
        destroy_range(data_, data_ + size_);
        deallocate(data_, capacity_);
    }

    // --- Assignment ---

    SimpleVector& operator=(const SimpleVector& other) {
        if (this != &other) {
            SimpleVector tmp(other);
            swap(tmp);
        }
        return *this;
    }

    SimpleVector& operator=(SimpleVector&& other) noexcept {
        if (this != &other) {
            destroy_range(data_, data_ + size_);
            deallocate(data_, capacity_);

            data_ = other.data_;
            size_ = other.size_;
            capacity_ = other.capacity_;
            alloc_ = std::move(other.alloc_);

            other.data_ = nullptr;
            other.size_ = 0;
            other.capacity_ = 0;
        }
        return *this;
    }

    // --- Element access ---

    reference operator[](size_type pos) { return data_[pos]; }
    const_reference operator[](size_type pos) const { return data_[pos]; }

    reference at(size_type pos) {
        if (pos >= size_) throw std::out_of_range("SimpleVector::at");
        return data_[pos];
    }
    const_reference at(size_type pos) const {
        if (pos >= size_) throw std::out_of_range("SimpleVector::at");
        return data_[pos];
    }

    reference front() { return data_[0]; }
    const_reference front() const { return data_[0]; }
    reference back() { return data_[size_ - 1]; }
    const_reference back() const { return data_[size_ - 1]; }
    pointer data() noexcept { return data_; }
    const_pointer data() const noexcept { return data_; }

    // --- Iterators ---

    iterator begin() noexcept { return data_; }
    const_iterator begin() const noexcept { return data_; }
    const_iterator cbegin() const noexcept { return data_; }
    iterator end() noexcept { return data_ + size_; }
    const_iterator end() const noexcept { return data_ + size_; }
    const_iterator cend() const noexcept { return data_ + size_; }
    reverse_iterator rbegin() noexcept { return reverse_iterator(end()); }
    reverse_iterator rend() noexcept { return reverse_iterator(begin()); }

    // --- Capacity ---

    [[nodiscard]] bool empty() const noexcept { return size_ == 0; }
    size_type size() const noexcept { return size_; }
    size_type capacity() const noexcept { return capacity_; }

    void reserve(size_type new_cap) {
        if (new_cap > capacity_) grow(new_cap);
    }

    void shrink_to_fit() {
        if (size_ == capacity_) return;

        pointer new_data = allocate(size_);
        for (size_type i = 0; i < size_; ++i) {
            alloc_traits::construct(alloc_, new_data + i, std::move(data_[i]));
        }
        destroy_range(data_, data_ + size_);
        deallocate(data_, capacity_);
        data_ = new_data;
        capacity_ = size_;
    }

    // --- Modifiers ---

    void clear() noexcept {
        destroy_range(data_, data_ + size_);
        size_ = 0;
    }

    void push_back(const T& value) {
        if (size_ == capacity_) grow(size_ + 1);
        alloc_traits::construct(alloc_, data_ + size_, value);
        ++size_;
    }

    void push_back(T&& value) {
        if (size_ == capacity_) grow(size_ + 1);
        alloc_traits::construct(alloc_, data_ + size_, std::move(value));
        ++size_;
    }

    template <typename... Args>
    reference emplace_back(Args&&... args) {
        if (size_ == capacity_) grow(size_ + 1);
        alloc_traits::construct(alloc_, data_ + size_, std::forward<Args>(args)...);
        return data_[size_++];
    }

    void pop_back() {
        --size_;
        alloc_traits::destroy(alloc_, data_ + size_);
    }

    void resize(size_type count) {
        if (count < size_) {
            destroy_range(data_ + count, data_ + size_);
            size_ = count;
        } else if (count > size_) {
            reserve(count);
            for (; size_ < count; ++size_) {
                alloc_traits::construct(alloc_, data_ + size_);
            }
        }
    }

    void resize(size_type count, const T& value) {
        if (count < size_) {
            destroy_range(data_ + count, data_ + size_);
            size_ = count;
        } else if (count > size_) {
            reserve(count);
            for (; size_ < count; ++size_) {
                alloc_traits::construct(alloc_, data_ + size_, value);
            }
        }
    }

    iterator erase(const_iterator pos) {
        auto offset = pos - cbegin();
        pointer p = data_ + offset;
        std::move(p + 1, data_ + size_, p);
        --size_;
        alloc_traits::destroy(alloc_, data_ + size_);
        return data_ + offset;
    }

    iterator erase(const_iterator first, const_iterator last) {
        auto offset = first - cbegin();
        auto count = last - first;
        if (count == 0) return data_ + offset;

        pointer dst = data_ + offset;
        pointer src = data_ + offset + count;
        std::move(src, data_ + size_, dst);

        destroy_range(data_ + size_ - count, data_ + size_);
        size_ -= count;
        return data_ + offset;
    }

    void swap(SimpleVector& other) noexcept {
        using std::swap;
        swap(data_, other.data_);
        swap(size_, other.size_);
        swap(capacity_, other.capacity_);
        if constexpr (alloc_traits::propagate_on_container_swap::value) {
            swap(alloc_, other.alloc_);
        }
    }

    allocator_type get_allocator() const noexcept { return alloc_; }

    // --- Comparison ---

    friend bool operator==(const SimpleVector& a, const SimpleVector& b) {
        if (a.size_ != b.size_) return false;
        return std::equal(a.begin(), a.end(), b.begin());
    }

    friend bool operator!=(const SimpleVector& a, const SimpleVector& b) {
        return !(a == b);
    }
};

} // namespace hft
