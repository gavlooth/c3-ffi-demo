# Region Threading Model

**Status:** Normative specification for threading and region ownership in OmniLisp  
**Applies to:** All multi-threaded code, region operations, and SMR integration  
**Last Updated:** 2026-01-10  
**Related:** `runtime/docs/REGION_RC_MODEL.md` (Region-RC semantics), `runtime/docs/SMR_FOR_RUNTIME_STRUCTURES.md` (SMR for runtime structures)

**Terminology:** This document uses “region” to refer to the runtime **ArenaRegion/RCB** (currently `struct Region`),
and reserves canonical “Region” (lifetime class) terminology as specified in `runtime/docs/MEMORY_TERMINOLOGY.md`.

---

## 1) Purpose

This document defines the **threading model** for OmniLisp regions, ensuring:

1. **Region ownership** is clear (which thread owns which region)
2. **Borrowing rules** are well-defined (how regions are shared temporarily)
3. **Tethering** is thread-local (cross-thread sharing requires RC)
4. **SMR integration** respects region ownership

**Core Principle:**
> Regions have a **single owning thread** at any time. Other threads must use **explicit mechanisms** (RC, tethering, SMR) to access regions safely.

---

## 2) Thread Ownership Model

### 2.1 Single-Threaded Ownership

**Default:** A region is created and owned by **one thread**:

```lisp
;; Thread A creates region R
(let ((region (region-create))
      (x (pair 1 2)))
  ;; Thread A owns region R
  ;; No other thread can access R directly
  (do-something-with x)
  (region-exit region))
```

**Invariants:**
1. **Creator = Owner:** The thread calling `region_create()` becomes the owner
2. **Owner = Lifetime:** Only the owner can call `region_exit()` or `region_destroy()`
3. **Owner = Allocator:** Only the owner can allocate from the region

### 2.2 Cross-Thread Ownership Transfer

**Explicit transfer** of ownership is supported:

```c
// Thread A (owner of R)
Region* R = region_create();

// Transfer ownership to Thread B
transfer_region_ownership(R, thread_B);

// Thread B (now owner of R)
// Can allocate from R, call region_exit(R), etc.
```

**Invariants:**
1. **One owner at a time:** After transfer, Thread A no longer owns R
2. **Explicit:** Transfer must be explicit (not implicit via sharing)
3. **No shared access:** During transfer, no other thread accesses R

### 2.3 Ownership vs. Access

| Concept | Meaning | Mechanism |
|---------|----------|-----------|
| **Ownership** | Thread responsible for lifecycle | `region_create()`, `region_exit()`, `region_destroy()` |
| **Access** | Thread reads/writes region data | Allocation, reading objects, mutation |
| **Borrow** | Temporary, bounded access | `region_tether_start/end()` (thread-local only) |
| **Share** | Unbounded, cross-thread access | `region_retain/release()` (RC) |

**Rule:**
> Access without ownership requires **borrowing** (thread-local) or **sharing** (cross-thread RC).

---

## 3) Thread-Local Borrowing (Tethering)

### 3.1 What is Tethering?

Tethering is a **temporary, bounded, thread-local** mechanism to borrow a region:

```lisp
;; Caller (Thread A) owns region R
(let ((region (region-create))
      (x (pair 1 2)))
  (region-exit region)
  ;; Pass x as BORROW (not escape)
  (f x))  ;; Tether window: Thread A still "borrows" region R

;; Callee (Thread A, same thread)
(define (f borrowed-pair)
  (+ (pair-car borrowed-pair)
     (pair-cdr borrowed-pair)))
;; Borrow ends when f returns
```

**Generated Code:**
```c
// Caller
Region* R = region_create();
Obj* x = mk_pair_region(R, 1, 2);
region_exit(R);  // scope_alive = false

// TETHER START (Thread A still needs x)
region_tether_start(R);  // tether_count(R)++
call_f(x);

// TETHER END
region_tether_end(R);    // tether_count(R)--
// Region R is now reclaimable
```

### 3.2 Tethering Rules

**Rule 1: Thread-Local Only**
> Tethering is **only allowed within the same thread**. Cross-thread borrowing requires RC, not tethering.

**Rule 2: Bounded Lifetime**
> Every `tether_start()` must have a matching `tether_end()`. The compiler emits these around call frames.

**Rule 3: No Reclamation During Tether**
> `region_exit()` and `region_destroy()` **must check** `tether_count > 0` and fail if not zero.

### 3.3 Tethering Implementation

```c
// In Region struct
typedef struct Region {
    // ... other fields ...
    size_t tether_count;  // Active borrows (thread-local)
} Region;

// Tether operations (inline, no atomics)
void region_tether_start(Region* r) {
    r->tether_count++;
}

void region_tether_end(Region* r) {
    assert(r->tether_count > 0);  // Must have matching start
    r->tether_count--;
    
    // Check if reclaimable now
    if (r->scope_alive == false &&
        r->external_rc == 0 &&
        r->tether_count == 0) {
        region_destroy(r);
    }
}
```

---

## 4) Cross-Thread Sharing (Region-RC)

### 4.1 What is Region-RC Sharing?

When a region must be accessed by **multiple threads**, use **Region-RC**:

```lisp
;; Thread A (creates region R)
(let ((region (region-create))
      (x (pair 1 2)))
  (region-exit region)
  ;; Send x to Thread B (cross-thread)
  (channel-send ch x))

;; Thread B (receives x from Thread A)
(let ((x (channel-recv ch)))
  ;; Thread B accesses x (shares region R)
  (do-something-with x))
```

**Generated Code:**
```c
// Thread A (sender)
Region* R = region_create();
Obj* x = mk_pair_region(R, 1, 2);
region_exit(R);

// RETAIN before cross-thread transfer
region_retain(R);  // external_rc(R)++
channel_send(ch, x);

// Thread A no longer owns R (but holds RC)

// Thread B (receiver)
Obj* x = channel_recv(ch);
// Thread B now holds external reference to R
use_value(x);

// Thread B done with reference
region_release(R);  // external_rc(R)--

// If external_rc == 0, Thread A can reclaim R
```

### 4.2 Region-RC Rules

**Rule 1: Cross-Thread Requires RC**
> Any cross-thread access **must** use `region_retain()` / `region_release()`.

**Rule 2: RC is Thread-Safe**
> `region_retain()` and `region_release()` **must** be atomic (use atomics).

**Rule 3: Owner Still Calls Exit**
> The creating thread still calls `region_exit()`, but reclamation is deferred until `external_rc == 0`.

### 4.3 Region-RC Implementation

```c
// In Region struct
typedef struct Region {
    // ... other fields ...
    uint32_t external_rc;  // Cross-thread references (atomic)
} Region;

// RC operations (atomic)
void region_retain(Region* r) {
    __atomic_fetch_add(&r->external_rc, 1, __ATOMIC_SEQ_CST);
}

void region_release(Region* r) {
    uint32_t old = __atomic_fetch_sub(&r->external_rc, 1, __ATOMIC_SEQ_CST);
    assert(old > 0);  // Must not go negative
    
    // Check if reclaimable now
    if (old == 1 &&
        r->scope_alive == false &&
        r->tether_count == 0) {
        region_destroy(r);
    }
}
```

---

## 5) Quiescent Points (SMR Integration)

### 5.1 What is a Quiescent Point?

A **quiescent point** is when a thread is:
- Not holding any references to SMR-protected structures
- Not in a critical section
- Safe to advance epoch / reclaim memory

### 5.2 Quiescent Points in OmniLisp

| Location | Quiescent? | Rationale |
|-----------|-------------|-----------|
| **End of bytecode step** | **YES** | Just finished instruction, no locks held |
| **After `tether_end()`** | **YES** | Released borrow, no active region references |
| **Before blocking I/O** | **YES** | About to block, won't touch SMR structures |
| **Return to event loop** | **YES** | Returning to scheduler, no active references |
| **Inside `tether_start...tether_end`** | **NO** | Holding borrow, NOT safe |
| **Inside long computation** | **NO** | Active region access, NOT safe |
| **After `region_retain()`** | **YES** | Just acquired RC, no references yet |
| **Before `region_release()`** | **NO** | About to release RC, still has reference |

### 5.3 Quiescent Point Implementation

```c
// Per-thread QSBR state
typedef struct {
    uint64_t local_epoch;
    uint64_t tether_depth;  // Track tether nesting
} QSBR_Thread;

// After each bytecode step
void bytecode_step_done(void) {
    if (thread_state->qsbr.tether_depth == 0) {
        qsbr_quiescent(&thread_state->qsbr);
    }
}

// Tether operations track depth
void region_tether_start(Region* r) {
    r->tether_count++;
    thread_state->qsbr.tether_depth++;  // Enter tether
}

void region_tether_end(Region* r) {
    r->tether_count--;
    thread_state->qsbr.tether_depth--;  // Exit tether
    
    if (thread_state->qsbr.tether_depth == 0) {
        qsbr_quiescent(&thread_state->qsbr);  // Report quiescent
    }
    
    // Check reclaimable
    if (r->tether_count == 0 &&
        r->external_rc == 0 &&
        r->scope_alive == false) {
        region_destroy(r);
    }
}
```

### 5.4 Quiescent Point Contract

**Rule:**
> A thread may only report quiescent if `tether_depth == 0` (no active borrows).

**Rationale:**
> Active borrows imply the thread is using regions that may own SMR structures. Reporting quiescent during a borrow could reclaim memory still in use.

---

## 6) Interaction with SMR

### 6.1 SMR vs. Region Ownership

SMR is used for **internal runtime structures**, NOT for regions:

| Structure | Thread Safety Mechanism | Example |
|-----------|------------------------|----------|
| **Intern Table** | QSBR | Symbol lookups |
| **Metadata Registry** | QSBR | Type metadata queries |
| **Global Module Map** | QSBR or Lock | Module imports |
| **Region** | Ownership + RC + Tethering | Heap allocation |

### 6.2 Region Ownership of SMR Structures

Some SMR structures are **owned by regions**:

```lisp
;; Region R owns an intern table
(let ((region (region-create))
      (table (intern-table region)))  ;; Table owned by R
  (region-tether-start region)
  ;; Thread uses table (intern-table-get table "foo")
  (region-tether-end region)
  (region-exit region))  ;; R (and table) reclaimed
```

**Invariants:**
1. **Table lifetime = Region lifetime:** When R exits, table is reclaimed
2. **Quiescent points respect region:** Don't report quiescent while tethered to R
3. **SMR reclamation respects tethering:** Don't reclaim table if R is tethered

### 6.3 SMR Quiescent and Region Tethering

**Scenario:**
- Thread A is tethered to Region R (which owns Intern Table)
- Thread A wants to report quiescent for QSBR

**Correct Behavior:**
```c
// Thread A is tethered to R
region_tether_start(R);

// ... use intern table ...

// Before reporting quiescent:
if (tether_depth == 0) {
    qsbr_quiescent();  // Safe: no active region references
} else {
    // tether_depth > 0: NOT safe to report quiescent
    // Do NOT call qsbr_quiescent()
}

region_tether_end(R);

// Now tether_depth == 0, safe to report quiescent
qsbr_quiescent();
```

---

## 7) Concurrency Scenarios

### 7.1 Scenario 1: Single-Threaded Borrow

```lisp
;; Thread A
(let ((region (region-create))
      (x (pair 1 2)))
  (region-exit region)
  (f x))  ;; Borrow (same thread)

(define (f borrowed)
  (+ (pair-car borrowed)
     (pair-cdr borrowed)))
```

**Thread Safety:**
- **Owner:** Thread A (creates and destroys region)
- **Access:** Thread A (same thread)
- **Mechanism:** Tethering (thread-local, no atomics)

### 7.2 Scenario 2: Cross-Thread Share

```lisp
;; Thread A
(let ((region (region-create))
      (x (pair 1 2)))
  (region-exit region)
  (channel-send ch x))

;; Thread B
(let ((x (channel-recv ch)))
  (do-something-with x))
```

**Thread Safety:**
- **Owner:** Thread A (creates, exits region)
- **Access:** Both Thread A and Thread B
- **Mechanism:** Region-RC (cross-thread, atomic RC)

### 7.3 Scenario 3: Tether + SMR Quiescent

```lisp
;; Thread A
(let ((region (region-create))
      (table (intern-table region)))
  (region-exit region)
  (region-tether-start region)
  ;; Use SMR structure (intern table)
  (intern-get table "foo")
  (region-tether-end region)
  ;; Safe to report quiescent now
  (do-something-else))
```

**Thread Safety:**
- **Owner:** Thread A
- **Access:** Thread A
- **Mechanism:** Tethering for region, QSBR for intern table
- **Quiescent:** Reported after `tether_end()` (safe)

---

## 8) Testing Strategy

### 8.1 Unit Tests

**Test: Single-threaded ownership**
```lisp
(define (test-single-thread-ownership)
  (let ((region (region-create))
        (x (pair 1 2)))
    (region-exit region)
    ;; Should succeed: owner calls exit
    (assert (region-destroyed region))))
```

**Test: Cross-thread RC**
```lisp
(define (test-cross-thread-rc)
  (let ((region (region-create))
        (x (pair 1 2)))
    (region-exit region)
    (channel-send ch x)
    ;; Thread B receives
    (let ((y (channel-recv ch)))
      ;; Thread B should be able to access y
      (assert (= (pair-car y) 1))
      (region-release region)))))
```

**Test: Tether prevents quiescent**
```lisp
(define (test-tether-quiescent)
  (let ((region (region-create))
        (x (pair 1 2)))
    (region-exit region)
    (region-tether-start region)
    ;; Should NOT be quiescent (tether_depth > 0)
    (assert (not (qsbr-is-quiescent)))
    (region-tether-end region)
    ;; Should NOW be quiescent
    (assert (qsbr-is-quiescent)))))
```

### 8.2 Concurrency Stress Tests

**Test: Many threads sharing one region**
```lisp
(define (test-many-threads-one-region)
  (let ((region (region-create))
        (x (pair 1 2)))
    (region-exit region)
    (region-retain region)  ;; external_rc = 1
    ;; Spawn 10 threads
    (for i in (range 10)
      (spawn (lambda ()
               (let ((y (channel-recv ch)))
                 ;; Each thread accesses shared region
                 (do-something-with y)
                 (region-release region)))))))
```

**Test: SMR structure owned by region**
```lisp
(define (test-smr-owned-by-region)
  (let ((region (region-create))
        (table (intern-table region)))
    (region-exit region)
    (region-tether-start region)
    ;; Use intern table (SMR structure)
    (intern-get table "foo")
    (region-tether-end region)
    ;; Region and table reclaimed together
    (assert (region-destroyed region)))))
```

---

## 9) "Done Means" for Threading Model

The threading model is complete only when:

1. **Ownership is enforced:** Only owner can call `region_exit()` / `region_destroy()`
2. **Tethering is thread-local:** `tether_count` is thread-local (no atomics)
3. **RC is thread-safe:** `external_rc` is atomic (using `__atomic_*` builtins)
4. **Quiescent points respect tethering:** No quiescent reported while `tether_depth > 0`
5. **All tests pass:** Unit tests and concurrency stress tests pass
6. **SMR integration works:** SMR structures respect region ownership and tethering

---

## 10) Future Work

### 10.1 Explicit Ownership Transfer

Future work: Add `transfer_region_ownership()` API:

```c
// Thread A (owner of R)
Region* R = region_create();

// Transfer to Thread B
transfer_region_ownership(R, thread_B);

// Thread B (now owner)
region_exit(R);  // Can now call exit
```

**Use case:** Migrating regions between thread pools (e.g., worker threads)

### 10.2 Lock-Free Region Allocation

Future work: Make `region_alloc()` lock-free for single-threaded case:

```c
void* region_alloc(Region* r, size_t size) {
    if (r->is_single_threaded) {
        // Fast path: no atomics
        return bump_alloc(&r->arena, size);
    } else {
        // Slow path: atomics
        return atomic_bump_alloc(&r->arena, size);
    }
}
```

**Use case:** Eliminate atomic overhead for single-threaded workloads

---

## 11) References

- `runtime/docs/REGION_RC_MODEL.md` - Region-RC semantics and external pointers
- `runtime/docs/SMR_FOR_RUNTIME_STRUCTURES.md` - SMR for internal runtime structures
- `docs/CTRR.md` - CTRR contract and Region Closure Property
