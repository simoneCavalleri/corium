# Contributing to Corium

Thank you for your interest in contributing to **Corium**! Corium is an open-source, high-performance, header-only C++20 framework built for deterministic, zero-heap real-time systems.

---

## 1. Code Standards & Guarantees

Every contribution to Corium must strictly preserve the following architectural guarantees:

1. **Zero Dynamic Memory Allocation**:
   - Never call `new`, `malloc`, `std::make_unique`, or `std::make_shared` in hot-path library headers.
   - Use fixed-capacity arrays (`std::array`), SBO buffers, and static placement.
   - All tests must pass `corium_zero_heap_test` (zero heap allocations verified via overloaded global `new`/`delete` hooks).

2. **Zero RTTI & Zero Exceptions**:
   - All code must compile cleanly with `-fno-rtti` and `-fno-exceptions`.
   - Never use `dynamic_cast`, `typeid`, `throw`, `try`, or `catch`.

3. **C++20 Compliance**:
   - Use standard C++20 features: concepts, `std::variant`, `std::span`, coroutines (`co_await`, `co_yield`, `co_return`), `std::jthread`, `std::stop_token`.

4. **Naming Conventions**:
   - `PascalCase` for types, classes, structs, and concepts (`BasicEventBus`, `FlightRecorder`).
   - `camelCase` for methods, functions, and member variables (`postDelayed()`, `processOne()`).
   - `_leadingUnderscore` for private member variables (`_eventQueue`, `_profilerPolicy`).
   - `ALL_CAPS` for compile-time constants and macro guards (`CORIUM_WIRE_MAGIC`).

---

## 2. Building & Testing

### Configure with Sanitizers
```bash
# ASan / UBSan Build
cmake -B build-san -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer" -DCORIUM_BUILD_TESTS=ON -DCORIUM_BUILD_SAMPLES=ON
cmake --build build-san -j$(nproc)
ctest --test-dir build-san --output-on-failure

# ThreadSanitizer (TSan) Build
cmake -B build-tsan -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_CXX_FLAGS="-fsanitize=thread -fno-omit-frame-pointer" -DCORIUM_BUILD_TESTS=ON
cmake --build build-tsan -j$(nproc)
ctest --test-dir build-tsan --output-on-failure
```

---

## 3. Single-Header Distribution Synchronization

Whenever you add or modify public headers in `include/corium/`:
```bash
python3 tools/amalgamate.py
python3 tools/amalgamate.py --check
```

---

## 4. Documentation Standards

All public headers must include:
- Doxygen `@file`, `@ingroup <group>`, and `@brief` file-level comment block.
- `@tparam`, `@param`, `@return`, `@note`, `@warning` on public API methods.
- Verify documentation coverage:
```bash
python3 tools/doc_coverage.py --strict
```

---

## 5. Commit Convention

We adhere to **Conventional Commits**:
- `feat(module): add feature description`
- `fix(module): fix issue description`
- `docs(module): update documentation`
- `refactor(module): clean up code without altering behavior`
- `test(module): add unit tests`
