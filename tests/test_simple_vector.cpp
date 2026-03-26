#include "simple_vector.hpp"

#include <cassert>
#include <iostream>
#include <string>

struct LifetimeTracker {
    static int alive;
    int val;
    LifetimeTracker() : val(0) { ++alive; }
    LifetimeTracker(int v) : val(v) { ++alive; }
    LifetimeTracker(const LifetimeTracker& o) : val(o.val) { ++alive; }
    LifetimeTracker(LifetimeTracker&& o) noexcept : val(o.val) { o.val = -1; ++alive; }
    LifetimeTracker& operator=(const LifetimeTracker&) = default;
    LifetimeTracker& operator=(LifetimeTracker&&) = default;
    ~LifetimeTracker() { --alive; }
    bool operator==(const LifetimeTracker& o) const { return val == o.val; }
};
int LifetimeTracker::alive = 0;

void test_default_constructor() {
    hft::SimpleVector<int> v;
    assert(v.size() == 0);
    assert(v.capacity() == 0);
    assert(v.empty());
    std::cout << "  [PASS] default constructor\n";
}

void test_push_back_and_access() {
    hft::SimpleVector<int> v;
    v.push_back(10);
    v.push_back(20);
    v.push_back(30);
    assert(v.size() == 3);
    assert(v[0] == 10);
    assert(v[1] == 20);
    assert(v[2] == 30);
    assert(v.front() == 10);
    assert(v.back() == 30);
    std::cout << "  [PASS] push_back and access\n";
}

void test_initializer_list() {
    hft::SimpleVector<int> v = {1, 2, 3, 4, 5};
    assert(v.size() == 5);
    for (int i = 0; i < 5; ++i) assert(v[i] == i + 1);
    std::cout << "  [PASS] initializer_list\n";
}

void test_copy() {
    hft::SimpleVector<int> v1 = {1, 2, 3};
    hft::SimpleVector<int> v2(v1);
    assert(v1 == v2);
    v2.push_back(4);
    assert(v1 != v2);
    std::cout << "  [PASS] copy constructor\n";
}

void test_move() {
    hft::SimpleVector<int> v1 = {1, 2, 3};
    hft::SimpleVector<int> v2(std::move(v1));
    assert(v2.size() == 3);
    assert(v1.empty());
    std::cout << "  [PASS] move constructor\n";
}

void test_emplace_back() {
    hft::SimpleVector<std::string> v;
    v.emplace_back(5, 'x');
    assert(v[0] == "xxxxx");
    v.emplace_back("hello");
    assert(v[1] == "hello");
    std::cout << "  [PASS] emplace_back\n";
}

void test_reserve_and_capacity() {
    hft::SimpleVector<int> v;
    v.reserve(100);
    assert(v.capacity() >= 100);
    assert(v.size() == 0);
    for (int i = 0; i < 100; ++i) v.push_back(i);
    assert(v.size() == 100);
    std::cout << "  [PASS] reserve\n";
}

void test_resize() {
    hft::SimpleVector<int> v = {1, 2, 3};
    v.resize(5, 42);
    assert(v.size() == 5);
    assert(v[3] == 42);
    assert(v[4] == 42);
    v.resize(2);
    assert(v.size() == 2);
    assert(v[0] == 1);
    std::cout << "  [PASS] resize\n";
}

void test_erase() {
    hft::SimpleVector<int> v = {1, 2, 3, 4, 5};
    v.erase(v.begin() + 1);
    assert(v.size() == 4);
    assert(v[1] == 3);

    v.erase(v.begin() + 1, v.begin() + 3);
    assert(v.size() == 2);
    assert(v[0] == 1);
    assert(v[1] == 5);
    std::cout << "  [PASS] erase\n";
}

void test_pop_back() {
    hft::SimpleVector<int> v = {1, 2, 3};
    v.pop_back();
    assert(v.size() == 2);
    assert(v.back() == 2);
    std::cout << "  [PASS] pop_back\n";
}

void test_at_bounds_check() {
    hft::SimpleVector<int> v = {1, 2, 3};
    bool threw = false;
    try {
        v.at(10);
    } catch (const std::out_of_range&) {
        threw = true;
    }
    assert(threw);
    std::cout << "  [PASS] at() bounds check\n";
}

void test_iterator() {
    hft::SimpleVector<int> v = {10, 20, 30};
    int sum = 0;
    for (auto& x : v) sum += x;
    assert(sum == 60);
    std::cout << "  [PASS] range-for iteration\n";
}

void test_shrink_to_fit() {
    hft::SimpleVector<int> v;
    v.reserve(100);
    v.push_back(1);
    v.push_back(2);
    v.shrink_to_fit();
    assert(v.capacity() == 2);
    assert(v[0] == 1);
    assert(v[1] == 2);
    std::cout << "  [PASS] shrink_to_fit\n";
}

void test_lifetime_tracking() {
    {
        hft::SimpleVector<LifetimeTracker> v;
        v.emplace_back(1);
        v.emplace_back(2);
        v.emplace_back(3);
        assert(LifetimeTracker::alive == 3);
        v.pop_back();
        assert(LifetimeTracker::alive == 2);
        v.clear();
        assert(LifetimeTracker::alive == 0);
    }
    assert(LifetimeTracker::alive == 0);
    std::cout << "  [PASS] lifetime tracking (no leaks)\n";
}

void test_growth_strategy() {
    hft::SimpleVector<int> v;
    for (int i = 0; i < 1000; ++i) v.push_back(i);
    assert(v.size() == 1000);
    // capacity should be a power of 2 >= 1000
    assert(v.capacity() >= 1000);
    assert((v.capacity() & (v.capacity() - 1)) == 0);
    for (int i = 0; i < 1000; ++i) assert(v[i] == i);
    std::cout << "  [PASS] growth strategy (2x, power-of-2)\n";
}

int main() {
    std::cout << "=== SimpleVector Tests ===\n";
    test_default_constructor();
    test_push_back_and_access();
    test_initializer_list();
    test_copy();
    test_move();
    test_emplace_back();
    test_reserve_and_capacity();
    test_resize();
    test_erase();
    test_pop_back();
    test_at_bounds_check();
    test_iterator();
    test_shrink_to_fit();
    test_lifetime_tracking();
    test_growth_strategy();
    std::cout << "All SimpleVector tests passed!\n\n";
    return 0;
}
