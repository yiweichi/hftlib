# HFTLib Practice

A small C++17 practice project for implementing low-latency and systems-style building blocks by hand.

## Components

- `SimpleVector`: a minimal `std::vector`-like container with manual memory management and iterators.
- `smart_ptr.hpp`: educational implementations of `UniquePtr`, `SharedPtr`, and `WeakPtr`.
- `SPSCRingBuffer`: a single-producer single-consumer ring buffer with atomic indices and cache-line-aware layout.

## Build

```bash
cmake -S . -B build
cmake --build build
```

## Run Tests

```bash
cd build
ctest --output-on-failure
```

## Project Goal

This repository is meant for learning modern C++ fundamentals through implementation:

- value categories, move semantics, and RAII
- templates, traits, and operator overloading
- atomic operations and basic lock-free design
- ownership models with unique, shared, and weak references

## Layout

- `include/`: library headers
- `tests/`: focused test programs for each component
- `build/`: local build output
