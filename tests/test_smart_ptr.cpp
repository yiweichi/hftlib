#include "smart_ptr.hpp"

#include <cassert>
#include <iostream>
#include <string>

static int destructor_count = 0;

struct Tracked {
    int val;
    Tracked(int v) : val(v) {}
    ~Tracked() { ++destructor_count; }
};

struct Base {
    virtual ~Base() = default;
    int base_val = 10;
};

struct Derived : Base {
    int derived_val = 20;
};

// ============== UniquePtr Tests ==============

void test_unique_basic() {
    destructor_count = 0;
    {
        auto p = hft::make_unique<Tracked>(42);
        assert(p);
        assert(p->val == 42);
        assert((*p).val == 42);
    }
    assert(destructor_count == 1);
    std::cout << "  [PASS] UniquePtr basic\n";
}

void test_unique_move() {
    auto p1 = hft::make_unique<Tracked>(7);
    auto p2 = std::move(p1);
    assert(!p1);
    assert(p2);
    assert(p2->val == 7);
    std::cout << "  [PASS] UniquePtr move\n";
}

void test_unique_release_reset() {
    destructor_count = 0;
    auto p = hft::make_unique<Tracked>(99);

    Tracked* raw = p.release();
    assert(!p);
    assert(raw->val == 99);
    assert(destructor_count == 0);

    p.reset(raw);
    assert(p);
    assert(p->val == 99);

    p.reset();
    assert(!p);
    assert(destructor_count == 1);
    std::cout << "  [PASS] UniquePtr release/reset\n";
}

void test_unique_nullptr() {
    hft::UniquePtr<int> p1;
    assert(!p1);
    assert(p1 == nullptr);

    hft::UniquePtr<int> p2(nullptr);
    assert(!p2);

    auto p3 = hft::make_unique<int>(5);
    p3 = nullptr;
    assert(!p3);
    std::cout << "  [PASS] UniquePtr nullptr\n";
}

void test_unique_array() {
    auto arr = hft::make_unique<int[]>(5);
    for (int i = 0; i < 5; ++i) arr[i] = i * 10;
    assert(arr[3] == 30);
    std::cout << "  [PASS] UniquePtr array\n";
}

void test_unique_polymorphism() {
    destructor_count = 0;
    {
        hft::UniquePtr<Derived> d(new Derived());
        hft::UniquePtr<Base> b(std::move(d));
        assert(!d);
        assert(b);
        assert(b->base_val == 10);
    }
    std::cout << "  [PASS] UniquePtr derived-to-base conversion\n";
}

void test_unique_custom_deleter() {
    bool custom_called = false;
    auto deleter = [&custom_called](int* p) {
        custom_called = true;
        delete p;
    };

    {
        hft::UniquePtr<int, decltype(deleter)> p(new int(42), deleter);
        assert(*p == 42);
    }
    assert(custom_called);
    std::cout << "  [PASS] UniquePtr custom deleter\n";
}

// ============== SharedPtr Tests ==============

void test_shared_basic() {
    destructor_count = 0;
    {
        auto sp = hft::make_shared<Tracked>(100);
        assert(sp);
        assert(sp->val == 100);
        assert(sp.use_count() == 1);
        assert(sp.unique());
    }
    assert(destructor_count == 1);
    std::cout << "  [PASS] SharedPtr basic\n";
}

void test_shared_copy() {
    auto sp1 = hft::make_shared<int>(42);
    assert(sp1.use_count() == 1);

    auto sp2 = sp1;
    assert(sp1.use_count() == 2);
    assert(sp2.use_count() == 2);
    assert(*sp1 == *sp2);
    assert(sp1.get() == sp2.get());

    sp1.reset();
    assert(sp2.use_count() == 1);
    assert(!sp1);
    std::cout << "  [PASS] SharedPtr copy and refcount\n";
}

void test_shared_move() {
    auto sp1 = hft::make_shared<int>(7);
    auto sp2 = std::move(sp1);
    assert(!sp1);
    assert(sp2);
    assert(*sp2 == 7);
    assert(sp2.use_count() == 1);
    std::cout << "  [PASS] SharedPtr move\n";
}

void test_shared_reset() {
    destructor_count = 0;
    {
        auto sp1 = hft::make_shared<Tracked>(1);
        auto sp2 = sp1;
        assert(destructor_count == 0);
        sp1.reset();
        assert(destructor_count == 0);
        sp2.reset();
        assert(destructor_count == 1);
    }
    std::cout << "  [PASS] SharedPtr reset and destruction\n";
}

void test_shared_nullptr() {
    hft::SharedPtr<int> sp1;
    assert(!sp1);
    assert(sp1.use_count() == 0);

    hft::SharedPtr<int> sp2(nullptr);
    assert(!sp2);
    std::cout << "  [PASS] SharedPtr nullptr\n";
}

// ============== WeakPtr Tests ==============

void test_weak_basic() {
    auto sp = hft::make_shared<int>(42);
    hft::WeakPtr<int> wp(sp);

    assert(!wp.expired());
    assert(wp.use_count() == 1);

    auto locked = wp.lock();
    assert(locked);
    assert(*locked == 42);
    assert(locked.use_count() == 2);
    std::cout << "  [PASS] WeakPtr basic\n";
}

void test_weak_expiry() {
    hft::WeakPtr<int> wp;
    {
        auto sp = hft::make_shared<int>(99);
        wp = sp;
        assert(!wp.expired());
    }
    assert(wp.expired());
    auto locked = wp.lock();
    assert(!locked);
    std::cout << "  [PASS] WeakPtr expiry\n";
}

void test_weak_multiple_shared() {
    auto sp1 = hft::make_shared<int>(10);
    hft::WeakPtr<int> wp(sp1);

    {
        auto sp2 = sp1;
        assert(wp.use_count() == 2);
    }
    assert(wp.use_count() == 1);
    assert(!wp.expired());
    std::cout << "  [PASS] WeakPtr with multiple shared owners\n";
}

void test_weak_copy() {
    auto sp = hft::make_shared<int>(5);
    hft::WeakPtr<int> wp1(sp);
    hft::WeakPtr<int> wp2(wp1);

    assert(wp1.use_count() == 1);
    assert(wp2.use_count() == 1);

    auto l1 = wp1.lock();
    auto l2 = wp2.lock();
    assert(l1.get() == l2.get());
    std::cout << "  [PASS] WeakPtr copy\n";
}

int main() {
    std::cout << "=== UniquePtr Tests ===\n";
    test_unique_basic();
    test_unique_move();
    test_unique_release_reset();
    test_unique_nullptr();
    test_unique_array();
    test_unique_polymorphism();
    test_unique_custom_deleter();

    std::cout << "\n=== SharedPtr Tests ===\n";
    test_shared_basic();
    test_shared_copy();
    test_shared_move();
    test_shared_reset();
    test_shared_nullptr();

    std::cout << "\n=== WeakPtr Tests ===\n";
    test_weak_basic();
    test_weak_expiry();
    test_weak_multiple_shared();
    test_weak_copy();

    std::cout << "\nAll SmartPtr tests passed!\n\n";
    return 0;
}
