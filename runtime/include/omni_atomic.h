/*
 * omni_atomic.h - Atomic Operations Policy Layer
 *
 * Issue 4 P3: Centralized atomic operations for Region-RC, tethering, and SMR.
 *
 * This header routes all atomic operations through a single policy layer,
 * enabling:
 * - Consistent memory ordering across codebase
 * - Easy addition of TSAN annotations
 * - Clean separation between C99+extensions vs C11 atomics
 *
 * Currently uses GCC/Clang __atomic_* builtins (C99 + extensions).
 * Future: Can be switched to C11 atomics if desired.
 */

#ifndef OMNI_ATOMIC_H
#define OMNI_ATOMIC_H

#include <stdint.h>
#include <stddef.h>   /* For size_t */
#include <stdbool.h>  /* For bool type */

#ifdef __cplusplus
extern "C" {
#endif

/* ========== Memory Ordering Policy ========== */

/*
 * Current policy: Use __atomic_* builtins with sequential consistency.
 *
 * Memory order rationale:
 * - For Region-RC external_rc/tether_count, we need sequential consistency
 *   to ensure acquire/release semantics are correct across threads.
 * - For SMR (future), we'll need relaxed/acquire/release ordering
 *   depending on QSBR vs hazard-pointer approach.
 */

/* ========== Type-Safe Atomic Operations ========== */

/*
 * Atomic load - read value with acquire semantics
 */
static inline uint32_t omni_atomic_load_u32(const volatile uint32_t* ptr) {
    return __atomic_load_n(ptr, __ATOMIC_ACQUIRE);
}

static inline uint64_t omni_atomic_load_u64(const volatile uint64_t* ptr) {
    return __atomic_load_n(ptr, __ATOMIC_ACQUIRE);
}

/*
 * Atomic store - write value with release semantics
 */
static inline void omni_atomic_store_u32(volatile uint32_t* ptr, uint32_t value) {
    __atomic_store_n(ptr, value, __ATOMIC_RELEASE);
}

static inline void omni_atomic_store_u64(volatile uint64_t* ptr, uint64_t value) {
    __atomic_store_n(ptr, value, __ATOMIC_RELEASE);
}

/*
 * Atomic fetch-and-add - increment with acquire-release semantics
 */
static inline uint32_t omni_atomic_fetch_add_u32(volatile uint32_t* ptr, uint32_t value) {
    return __atomic_fetch_add(ptr, value, __ATOMIC_ACQ_REL);
}

static inline uint64_t omni_atomic_fetch_add_u64(volatile uint64_t* ptr, uint64_t value) {
    return __atomic_fetch_add(ptr, value, __ATOMIC_ACQ_REL);
}

/*
 * Atomic add-and-fetch - increment and return new value
 */
static inline uint32_t omni_atomic_add_fetch_u32(volatile uint32_t* ptr, uint32_t value) {
    return __atomic_add_fetch(ptr, value, __ATOMIC_ACQ_REL);
}

static inline uint64_t omni_atomic_add_fetch_u64(volatile uint64_t* ptr, uint64_t value) {
    return __atomic_add_fetch(ptr, value, __ATOMIC_ACQ_REL);
}

/*
 * Atomic sub-and-fetch - decrement and return new value
 */
static inline uint32_t omni_atomic_sub_fetch_u32(volatile uint32_t* ptr, uint32_t value) {
    return __atomic_sub_fetch(ptr, value, __ATOMIC_ACQ_REL);
}

static inline uint64_t omni_atomic_sub_fetch_u64(volatile uint64_t* ptr, uint64_t value) {
    return __atomic_sub_fetch(ptr, value, __ATOMIC_ACQ_REL);
}

/* ========== 16-bit Atomics (for region_id) ========== */

/*
 * Atomic load for uint16_t
 */
static inline uint16_t omni_atomic_load_u16(const volatile uint16_t* ptr) {
    return __atomic_load_n((volatile uint16_t*)ptr, __ATOMIC_ACQUIRE);
}

/*
 * Atomic fetch-and-add for uint16_t
 */
static inline uint16_t omni_atomic_fetch_add_u16(volatile uint16_t* ptr, uint16_t value) {
    return __atomic_fetch_add(ptr, value, __ATOMIC_ACQ_REL);
}

/* ========== Pointer-Safe Atomic Operations ========== */

/*
 * Atomic load for pointers (size_t)
 */
static inline size_t omni_atomic_load_size_t(const volatile size_t* ptr) {
    return __atomic_load_n(ptr, __ATOMIC_ACQUIRE);
}

/*
 * Atomic store for pointers (size_t)
 */
static inline void omni_atomic_store_size_t(volatile size_t* ptr, size_t value) {
    __atomic_store_n(ptr, value, __ATOMIC_RELEASE);
}

/*
 * Atomic fetch-and-add for pointers (size_t)
 */
static inline size_t omni_atomic_fetch_add_size_t(volatile size_t* ptr, size_t value) {
    return __atomic_fetch_add(ptr, value, __ATOMIC_ACQ_REL);
}

/*
 * Atomic CAS for pointers (size_t)
 */
static inline bool omni_atomic_cas_size_t(volatile size_t* ptr,
                                         size_t* expected,
                                         size_t desired) {
    return __atomic_compare_exchange_n(ptr, expected, desired,
                                    false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
}

/* ========== TSAN (ThreadSanitizer) Support ========== */

/*
 * When building with -fsanitize=thread, TSAN provides its own
 * annotations. We use them here to ensure TSAN can detect
 * data races even in our atomic operations.
 *
 * Note: __atomic_* builtins are already TSAN-aware.
 */
#ifdef __has_feature
  #if __has_feature(thread_sanitizer)
    #define OMNI_TSAN_ENABLED 1
  #else
    #define OMNI_TSAN_ENABLED 0
  #endif
#else
  #define OMNI_TSAN_ENABLED 0
#endif

/* ========== Debug Assertions ========== */

/*
 * Debug builds can add runtime checks for atomic invariants.
 */
#ifdef NDEBUG
  #define OMNI_ATOMIC_ASSERT(cond) ((void)0)
#else
  #include <assert.h>
  #define OMNI_ATOMIC_ASSERT(cond) assert(cond)
#endif

/*
 * Assertion: Atomic pointer must be aligned to natural size.
 */
static inline void omni_assert_aligned(const volatile void* ptr, size_t alignment) {
    OMNI_ATOMIC_ASSERT(((uintptr_t)ptr & (alignment - 1)) == 0);
}

#ifdef __cplusplus
}
#endif

#endif /* OMNI_ATOMIC_H */
