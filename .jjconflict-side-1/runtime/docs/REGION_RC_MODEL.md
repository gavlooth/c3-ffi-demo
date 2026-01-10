# Region-RC Model Specification

**Status:** Normative specification for Region-RC semantics  
**Applies to:** `runtime/src/memory/region_core.h`, `runtime/src/memory/region_core.c`, compiler code generation  
**Last Updated:** 2026-01-10  
**Related:** `docs/CTRR.md` (Region Closure Property), `runtime/docs/CTRR_TRANSMIGRATION.md` (transmigration contract)

---

## Terminology (Pinned)

This project reserves **Region** for the semantic meaning:

> **Region (canonical):** a collection of objects with the same lifetime.

This document describes **Region‑RC**, which is the runtime liveness mechanism applied to the **ArenaRegion/RCB** that implements a Region.

For unambiguous terms and a code map, see:
- `runtime/docs/MEMORY_TERMINOLOGY.md`

---

## 1) What is Region-RC?

**Region-RC** is OmniLisp's coarse-grained reference counting model applied at the **region granularity**, not per-object. It is the runtime mechanism that allows regions to outlive their lexical scope while maintaining CTRR's "everything can escape" guarantee.

### 1.1 Why coarse-grained RC?

Traditional reference counting increments/decrements on every allocation and assignment:
- High runtime overhead (every operation)
- Cache pollution from RC fields
- Still requires cycle collection for safety

Region-RC applies RC only to **regions**:
- No per-object RC overhead (objects inside regions are bulk-freed)
- RC operations only at escape boundaries (return, capture, global store)
- Cycles within regions are safe (entire cycle dies with region)

**Important implementation requirement (enforcement):**
- “RC operations only at escape boundaries” is only meaningful if the runtime can identify an object's owning ArenaRegion (`region_of(obj)`) and if mutation-time stores are mediated by a store barrier.
- This is tracked as required work in `TODO.md`:
  - Issue 1 / Amendment A: `I1-region-of-obj-mechanism` (completed 2026-01-10)
  - Issue 2 / Amendment A: `I2-store-barrier-choke-point`

---

### 1.1.1 Implementation Decision: Option A (Per-Object Owner Pointer) Chosen

**Decision (2026-01-10):** OmniLisp implemented **Option A (tooling-first, simplest)** for `region_of(obj)`.

**Rationale:**
- **ASAN/TSAN compatibility:** Option A works directly with ASAN and TSAN without requiring custom suppression rules or fighting with non-canonical pointers
- **Simplicity:** No complex masking/unmasking rules at every API boundary
- **Debuggability:** Straightforward pointer tracing in debuggers

**Implementation:**
- Added `Region* owner_region;` field to `struct Obj` in `runtime/include/omni.h`
- Set `owner_region` in `alloc_obj_typed()` for all boxed objects
- Implemented `static inline Region* omni_obj_region(Obj* o)` that returns NULL for immediates
- Verified with comprehensive test suite in `runtime/tests/test_region_of_obj.c`

**Alternatives Considered and Rejected:**
- **Option B (pointer masking):** Would encode `region_id` into high bits of pointer. Rejected due to ASAN/UBSAN compatibility issues and complexity of enforcing masking rules at all API boundaries.

### 1.2 Relationship to CTRR

Region-RC is **complementary** to CTRR, not a replacement:

```
CTRR (Compiler's Job):
  • Schedule region lifetimes at compile time
  • Inject region_exit() at scope boundaries
  • Inject transmigrate() at escape boundaries
  • Inject region_tether_start/end() for borrows

Region-RC (Runtime's Job):
  • Keep regions alive when they escape lexical scope
  • Track external references to regions
  • Reclaim regions only when safe (scope closed + no external refs)
  • Support bounded borrows via tethering
```

---

## 2) Region Liveness Rule

### 2.1 Definition

A region `R` is **alive** and must NOT be reclaimed if:

```
scope_alive(R) == true  ||  external_rc(R) > 0
```

A region `R` is **reclaimable** (may be freed or reused) if:

```
scope_alive(R) == false  &&  external_rc(R) == 0
```

### 2.2 State Transition Diagram

```
┌─────────────┐   region_create()    ┌─────────────────────┐
│  Non-existent │ ────────────────────▶ │  ALIVE            │
│             │                      │ scope_alive=true     │
│             │                      │ external_rc=0      │
└─────────────┘                      │                   │
                                     └─────┬─────────────┘
                                           │
               ┌─────────────────────────────┴─────────────────────────────┐
               │                                                   │
         retain()                                             region_exit()
               │                                                   │
               ▼                                                   ▼
┌─────────────────────┐                                   ┌─────────────────────┐
│  ALIVE            │◄──────────────────────────────────────────│  CLOSING         │
│ scope_alive=true   │        release()                     │ scope_alive=false  │
│ external_rc=N     │        (N → N-1)                    │ external_rc=0      │
│ (N ≥ 1)          │                                   │                   │
└────────┬──────────┘                                   └─────┬─────────────┘
         │                                                    │
         │ region_exit()                                         │ release()
         │ (sets scope_alive=false)                               │ (0 → -1? BUG!)
         ▼                                                    ▼
┌─────────────────────┐                                   ┌─────────────────────┐
│  CLOSING         │◄──────────────────────────────────────────│  RECLAIMABLE      │
│ scope_alive=false  │        release()                     │ scope_alive=false  │
│ external_rc=N     │        (N → N-1)                    │ external_rc=0      │
│ (N ≥ 1)          │                                   │                   │
└────────┬──────────┘                                   └─────┬─────────────┘
         │                                                    │
         │                                                    │ region_destroy()
         ▼                                                    │
┌─────────────────────┐                                           │
│  RECLAIMABLE      │──────────────────────────────────────────────────┘
│ scope_alive=false  │
│ external_rc=0      │
└────────┬──────────┘
         │
         │ region_destroy()
         ▼
┌─────────────────────┐
│  FREED            │
│  (non-existent)    │
└─────────────────────┘
```

### 2.3 Key Invariants

1. **Scope exit does NOT imply reclamation:**
   - `region_exit(r)` sets `scope_alive(r) = false`
   - But `r` stays alive if `external_rc(r) > 0`

2. **RC zero implies safe reclamation:**
   - When `external_rc(r) == 0 && scope_alive(r) == false`, no external references exist
   - Region can be freed immediately (or reused for allocation)

3. **RC never goes negative:**
   - `region_release(r)` MUST assert `external_rc(r) > 0` before decrementing
   - Negative RC indicates a bug in escape analysis or RC discipline

---

## 3) External Pointer / External Reference Definition

### 3.1 Formal Definition

An **external reference** (or **external pointer**) to region `R` is:

```
A pointer into region R that is stored OUTSIDE region R
in a way that can outlive R's lexical scope.
```

### 3.2 External vs. Internal References

| Aspect | Internal Reference | External Reference |
|--------|-------------------|-------------------|
| **Location** | Stored **inside** region `R` | Stored **outside** region `R` |
| **Lifetime** | Dies when `R` dies (bulk free) | Can outlive `R` (RC tracking) |
| **RC Operation** | None (implicit via region lifecycle) | `region_retain(R)` on creation, `region_release(R)` on destruction |
| **Example** | Pair `(A . B)` where `A` and `B` allocated in `R` | Return value stored in caller's register/global, closure capture, channel send |

### 3.3 External Reference Boundaries

The following operations **create external references** and must trigger `region_retain(R)`:

#### 3.3.1 Return to Caller / Outliving Scope

```lisp
;; Source region R (local scope)
(define (make-pair)
  (let ((region (region-create))  ; Region R created
        (x (pair 1 2)))        ; x allocated in R
    (region-exit region)           ; scope_alive(R) = false
    x))                           ; x escapes → EXTERNAL REFERENCE to R

;; Generated code (conceptual):
Region* R = region_create();
Obj* x = mk_pair_region(R, 1, 2);
region_exit(R);              // scope_alive(R) = false
region_retain(R);           // external_rc(R)++
return x;                   // External reference stored in caller
```

**RC Operation:**
- `region_retain(R)` before return
- Caller responsible for `region_release(R)` when `x` is no longer needed

#### 3.3.2 Closure Capture

```lisp
(define (make-closure)
  (let ((region (region-create))  ; Region R
        (x 42))                ; x allocated in R
    (region-exit region)
    (fn [] x)))               ; λ captures x → EXTERNAL REFERENCE to R

;; Generated code:
Region* R = region_create();
Obj* x = mk_int_region(R, 42);
region_exit(R);
region_retain(R);           // external_rc(R)++
Obj* closure = mk_closure(..., x);  // Closure stores reference to x
return closure;
```

**RC Operation:**
- `region_retain(R)` when closure captures `x`
- When closure is freed, `region_release(R)` is called

#### 3.3.3 Global / Module Store

```lisp
(define *global-var* nil)

;; In some function:
(let ((region (region-create))
      (x (pair 1 2)))
  (region-exit region)
  (set! *global-var* x))  ; x stored globally → EXTERNAL REFERENCE

;; Generated code:
Region* R = region_create();
Obj* x = mk_pair_region(R, 1, 2);
region_exit(R);
region_retain(R);           // external_rc(R)++
set_global("*global-var*", x);
```

**RC Operation:**
- `region_retain(R)` when storing to global
- When global is overwritten or program exits, `region_release(R)` is called

#### 3.3.4 Cross-Thread Channel Send/Recv

```lisp
;; Thread 1:
(let ((region (region-create))
      (x (pair 1 2)))
  (region-exit region)
  (channel-send ch x))       ; x sent across thread → EXTERNAL REFERENCE

;; Generated code (Thread 1):
Region* R = region_create();
Obj* x = mk_pair_region(R, 1, 2);
region_exit(R);
region_retain(R);           // external_rc(R)++
channel_send(ch, x);
// Thread 1 no longer owns x (transfer of ownership to receiver)

;; Generated code (Thread 2):
Obj* x = channel_recv(ch);
// Thread 2 now holds external reference to R
// Thread 2 responsible for region_release(R)
```

**RC Operation:**
- `region_retain(R)` when value enters channel
- Receiver must call `region_release(R)` when done with received value
- (Ownership transfer semantics may apply)

#### 3.3.5 Mutation Store into Older Region Containers

```lisp
;; Caller region (older, longer-lived):
(let ((older (region-create))
      (container (dict)))
  (region-exit older))
  ;; Function called with older as parameter
  (f older container))

;; Callee function creates temporary data and mutates container:
(define (f older-region container)
  (let ((region (region-create))  ; Younger region Y
        (x (pair 1 2)))        ; x allocated in Y
    (region-exit region)
    (dict-set! container "key" x)))  ; x stored in older-region → EXTERNAL REFERENCE

;; Generated code (conceptual):
Region* Y = region_create();
Obj* x = mk_pair_region(Y, 1, 2);
region_exit(Y);                    // scope_alive(Y) = false
dict_set(container, "key", x);    // Stored in older region
// This is illegal without AUTO-REPAIR!

;; Correct (with auto-repair):
Region* Y = region_create();
Obj* x = mk_pair_region(Y, 1, 2);
region_exit(Y);
// AUTO-REPAIR: transmigrate x into older-region before storing
Obj* x_copy = transmigrate(x, Y, older-region);
dict_set(container, "key", x_copy);
region_destroy(Y);  // Y is now reclaimable
```

**RC Operation with Auto-Repair:**
- Option 1: **Transmigrate** (copy data into older region)
  - No RC operations needed (data now lives in older region)
  - Preferred for small values

- Option 2: **Retain younger region** (prevent reclamation)
  - `region_retain(Y)` when storing into older region
  - Older region's `external_rc` now includes reference to Y
  - Risk: Younger region is kept alive longer than necessary (retention cliff)

---

## 4) Relationship to Transmigration

### 4.1 Transmigration is "Move", RC is "Share"

| Mechanism | Purpose | RC Effect |
|-----------|---------|-----------|
| **Transmigration** | Move/copy object graph from `src` to `dst` | `src` RC unchanged (ownership transfer if `src` reclaims) |
| **Region-RC** | Share region across scope boundaries | Increments `external_rc` when sharing, decrements when done |

### 4.2 Transmigration Repairs Escapes

When a value escapes a closing region, the compiler has two choices:

#### Choice 1: Transmigrate (Preferred for small values)

```c
// Compiler emits:
Region* local = region_create();
Obj* x = mk_pair_region(local, 1, 2);
Obj* out = transmigrate(x, local, caller_region);
region_exit(local);  // local is reclaimable NOW
return out;  // out lives in caller_region (no RC needed)
```

**RC Operations:** None (ownership transfer)

**Benefits:**
- Closing region reclaims immediately
- No RC bookkeeping overhead
- Clear ownership semantics

**When Used:**
- Small graphs (≤ threshold, e.g., few KB)
- Frequent escapes (per-call overhead acceptable)

#### Choice 2: Region-RC (Preferred for large values)

```c
// Compiler emits:
Region* local = region_create();
Obj* x = mk_large_graph_region(local, ...);
Obj* out = x;  // No transmigration
region_exit(local);  // scope_alive = false, but NOT reclaimed
region_retain(local);  // external_rc++
return out;  // Caller receives reference to local region

// Caller region uses 'out':
use_value(out);
region_release(local);  // external_rc--
// If external_rc == 0, local is now reclaimable
```

**RC Operations:**
- `region_retain(local)` before return
- `region_release(local)` when caller done with value

**Benefits:**
- No copying overhead for large graphs
- Deferred reclamation (bulk free when all references dropped)
- Natural for shared, long-lived data

**When Used:**
- Large graphs (copying too expensive)
- Shared references (multiple references to same region)
- Long-lived data structures (e.g., config, caches)

### 4.3 Region Closure Property Enforcement

Region-RC and transmigration **both** enforce the Region Closure Property:

1. **Transmigration:** Rewrites pointers so destination graph has no pointers into closing region
2. **Region-RC:** Prevents reclamation while external references exist

Both mechanisms must work together:

```
Correct scenario:
  • Value escapes → transmigrate to caller_region
  • Source region exits and is reclaimed immediately
  • Region Closure holds (no pointers into reclaimed region)

Correct scenario:
  • Value escapes → retain source region
  • Source region exits but not reclaimed (external_rc > 0)
  • Caller uses value, then releases
  • When external_rc == 0, region is reclaimed
  • Region Closure holds (reclamation delayed until safe)

Incorrect scenario (must be prevented):
  • Value escapes → NO retain, NO transmigrate
  • Source region exits and is reclaimed
  • Caller has pointer into reclaimed region → USE-AFTER-FREE
  • VIOLATION of Region Closure Property
```

---

## 5) Relationship to Mutation Auto-Repair

### 5.1 The Problem: Younger → Older Stores

CTRR forbids stores from a **younger region** into an **older region** without repair:

```c
// ILLEGAL (no repair):
Region* young = region_create();  // Region Y (young, short-lived)
Region* old = region_create();   // Region O (old, long-lived)
Obj* x = mk_pair_region(young, 1, 2);  // x in Y
dict_set(old_dict, "key", x);  // Y pointer stored in O!
region_exit(young);  // Y reclaims
// O now contains pointer into reclaimed Y → USE-AFTER-FREE
```

### 5.2 Auto-Repair Strategies

#### Strategy 1: Transmigrate on Store (Preferred)

```c
// CORRECT (transmigrate):
Region* young = region_create();
Region* old = region_create();
Obj* x = mk_pair_region(young, 1, 2);

// Compiler inserts auto-repair:
Obj* x_in_old = transmigrate(x, young, old);
dict_set(old_dict, "key", x_in_old);  // Store repaired value

region_exit(young);  // Y reclaims safely (no pointers into Y)
```

**RC Operations:** None (data copied into older region)

**When Used:**
- Small values (copying cheap)
- Older region expected to outlive younger (no retention problem)

#### Strategy 2: Retain Younger Region (Alternative)

```c
// CORRECT (retain):
Region* young = region_create();
Region* old = region_create();
Obj* x = mk_pair_region(young, 1, 2);

// Compiler inserts auto-repair:
region_retain(young);  // external_rc(young)++
dict_set(old_dict, "key", x);  // Store reference to young region

region_exit(young);  // scope_alive = false, but NOT reclaimed
// Region Y kept alive by old's external reference

// Later, when old_dict is cleared:
dict_remove(old_dict, "key");
region_release(young);  // external_rc--
// If external_rc == 0, young is now reclaimable
```

**RC Operations:**
- `region_retain(young)` on store
- `region_release(young)` when reference removed

**Risk:** **Retention cliff** - younger region kept alive longer than needed

**When Used:**
- Large values (transmigration too expensive)
- Multiple references to same young region (sharing benefit)
- Temporary: Future compiler optimization may detect this pattern and prefer transmigrate

### 5.3 Auto-Repair Boundary Inventory

The compiler must detect **all mutation boundaries** and insert auto-repair:

| Boundary | Auto-Repair Required? | Example |
|-----------|------------------------|----------|
| Dict entry insert | YES | `(dict-set! old-dict "key" young-value)` |
| Array element assignment | YES | `(array-set! old-array 0 young-value)` |
| Box content mutation | YES | `(set! old-box young-value)` |
| Tuple field mutation | YES | `(tuple-set! old-tuple 0 young-value)` |
| Direct assignment to variable in older scope | YES | `(let ([x old-binding]) (set! x young-value))` |

**Rule of thumb:**
> If the **destination** of a store lives in an older region (or global/module scope), and the **source** lives in a younger (or closing) region, auto-repair is required.

### 5.4 Compiler Decision Heuristic (TODO)

Future compiler passes will choose between transmigrate vs. retain based on:

1. **Size heuristic:**
   - Small (≤ threshold): Transmigrate
   - Large (> threshold): Retain

2. **Liveness analysis:**
   - Older region soon to exit: Transmigrate (avoid retention)
   - Older region long-lived: Retain may be acceptable

3. **Reference count tracking:**
   - Multiple references to same young region: Retain (sharing)
   - Single reference: Transmigrate (clear ownership)

---

## 6) Tethering: Bounded Borrows

### 6.1 What is Tethering?

Tethering is a **temporary liveness pin** that keeps a region alive during a borrow window:

```lisp
;; Caller region:
(let ((region (region-create))
      (x (pair 1 2)))
  (region-exit region)
  (f x))  ;; Pass x as BORROW (not escape)

;; Callee function:
(define (f borrowed-pair)
  (+ (pair-car borrowed-pair)       ;; Borrowed during call
     (pair-cdr borrowed-pair)))      ;; Borrowed during call

;; Generated code (conceptual):
// Caller:
Region* R = region_create();
Obj* x = mk_pair_region(R, 1, 2);
region_exit(R);  // scope_alive = false

// TETHER START:
region_tether_start(R);  // tether_count(R)++
call_f(x);
region_tether_end(R);    // tether_count(R)--

// Region R reclaims after tether_end (if external_rc == 0)
```

### 6.2 Tethering RC Semantics

Tethering uses a **separate counter** (`tether_count`) from `external_rc`:

```
Region liveness:
  scope_alive == true  ||
  external_rc > 0        ||
  tether_count > 0         ← Tethering keeps region alive

Region reclaimable:
  scope_alive == false  &&
  external_rc == 0       &&
  tether_count == 0       ← No active tethers
```

**Why separate?**
- `tether_count` is **thread-local** and bounded (call stack depth)
- `external_rc` is **cross-thread** and unbounded (global references)
- Tethering is for **temporary borrows**, RC is for **sharing**

### 6.3 Tethering vs. RC vs. Transmigration

| Mechanism | Scope | RC Counter | Duration | Use Case |
|-----------|--------|-------------|-----------|-----------|
| **Transmigration** | Escape | None | Permanent | Move data permanently to caller/outliving region |
| **Region-RC (external_rc)** | Escape | `external_rc` | Unbounded | Share region across threads/globals/closures |
| **Tethering (tether_count)** | Borrow | `tether_count` | Bounded (call) | Temporarily pass reference without copying or sharing |

### 6.4 Tethering Safety

Tethering MUST enforce:

1. **Bounded lifetime:**
   - Every `tether_start(R)` must have matching `tether_end(R)`
   - Compiler emits `tether_start` before call, `tether_end` after

2. **No reclamation during tether:**
   - `region_exit(R)` waits if `tether_count(R) > 0`
   - `region_destroy(R)` asserts `tether_count(R) == 0`

3. **Thread-local isolation:**
   - `tether_count` is thread-local (no atomic operations)
   - Cross-thread sharing requires `external_rc`, not `tether_count`

---

## 7) Conformance Checklist

To verify that OmniLisp correctly implements Region-RC, the following checklist must be satisfied:

### 7.1 Region Lifecycle

- [ ] `region_create()` initializes `scope_alive = true`, `external_rc = 0`, `tether_count = 0`
- [ ] `region_exit()` sets `scope_alive = false`, does NOT reclaim if `external_rc > 0` or `tether_count > 0`
- [ ] `region_retain()` increments `external_rc` atomically
- [ ] `region_release()` asserts `external_rc > 0`, then decrements, reclaims if zero and `scope_alive == false`
- [ ] `region_destroy()` asserts `scope_alive == false`, `external_rc == 0`, `tether_count == 0`

### 7.2 External Reference Boundaries

- [ ] **Return to caller:** `region_retain()` emitted before return of escaping value
- [ ] **Closure capture:** `region_retain()` emitted when closure captures variable from closing region
- [ ] **Global/module store:** `region_retain()` emitted when storing to global/module
- [ ] **Cross-thread channel:** `region_retain()` emitted on send, `region_release()` emitted on recv
- [ ] **Mutation into older:** Auto-repair (transmigrate or retain) inserted for younger→older stores

### 7.3 Transmigration Integration

- [ ] Transmigration does NOT use RC (ownership transfer or copy)
- [ ] Compiler chooses transmigrate vs. retain based on size/heuristic (not random)
- [ ] Region Closure Property verified after transmigration (debug build: `assert_no_ptrs_into_region`)

### 7.4 Mutation Auto-Repair

- [ ] All mutation boundaries (dict-set, array-set, box-set, tuple-set) have auto-repair
- [ ] Auto-repair decision (transmigrate vs. retain) uses size/liveness heuristic
- [ ] Younger→older stores are **never** silently copied without repair

### 7.5 Tethering

- [ ] `tether_start()` increments `tether_count`, `tether_end()` decrements
- [ ] `region_exit()` blocks or fails if `tether_count > 0` (must be explicit in error message)
- [ ] `tether_count` is thread-local (no atomics)
- [ ] Cross-thread sharing uses `external_rc`, not `tether_count`

### 7.6 Debug Verification

- [ ] Debug build asserts `external_rc >= 0` (negative RC is fatal error)
- [ ] Debug build checks for pointers into dead regions after `region_exit` / `region_destroy`
- [ ] Debug build logs all RC operations (retain/release) for manual inspection

### 7.7 Testing

- [ ] Unit tests for `region_retain` / `region_release` invariants
- [ ] Unit tests for external reference boundaries (return, capture, global, channel)
- [ ] Unit tests for auto-repair (younger→older dict/array/box mutations)
- [ ] Unit tests for tethering (nested tethers, cross-thread isolation)
- [ ] Integration test: "retention cliff" scenario (younger region kept alive too long)
- [ ] Integration test: Region Closure Property violation is detected and aborts

---

## 8) "Done Means" for Region-RC Implementation

Region-RC is considered complete/verified only when:

1. **All conformance checklist items pass:** Every item in Section 7 is implemented and tested
2. **No unhandled external references:** Every external reference boundary has `region_retain` / `region_release` or auto-repair
3. **No younger→older unsound stores:** Every younger→older mutation has explicit auto-repair (transmigrate or retain)
4. **Debug assertions fire:** Attempting to reclaim region with `external_rc > 0` or `tether_count > 0` aborts with clear error
5. **Tests cover edge cases:**
   - Double retain/release (should balance)
   - Negative RC (should abort)
   - Concurrent retain/release (atomic operations)
   - Tethering across thread boundaries (should use RC instead)

---

## 9) Relationship to RC Dialect Literature

The RC dialect (https://www.barnowl.org/research/rc/index.html) defines **"external pointers"** similarly to OmniLisp's definition:

**RC Dialect:**
> "A pointer into region R that is stored outside R is called an **external reference**."
> "A region R is live iff it has not exited OR it has outstanding external references."

**OmniLisp Region-RC:**
> "A pointer into region R that is stored outside R is an **external reference**."
> "Region R is reclaimable iff `scope_alive == false && external_rc == 0`."

**Key Difference:**
- RC dialect's `exited` is equivalent to OmniLisp's `!scope_alive`
- RC dialect's `outstanding external references` is equivalent to OmniLisp's `external_rc > 0`
- OmniLisp adds `tether_count` for bounded borrows (RC dialect uses explicit borrow annotations)

**Alignment:**
✅ Region-RC model is **aligned** with RC dialect's external pointer semantics

---

## 10) Open Questions / Future Work

### 10.1 Auto-Repair Heuristics (TODO)

What is the optimal threshold for choosing transmigrate vs. retain?

- **Current:** Fixed threshold (e.g., 4 KB)
- **Future:** Adaptive threshold based on:
  - Region lifetime analysis
  - Allocation frequency
  - Cache locality considerations

### 10.2 Retention Cliff Detection (TODO)

How to detect and warn about "retention cliffs" (younger regions kept alive too long by older regions)?

- **Option 1:** Runtime diagnostics
  - Log when region exits but `external_rc > 0`
  - Log when region finally reclaims after delay
  - Flag regions with high "time alive after exit" ratio

- **Option 2:** Compiler analysis
  - Track region graph: "older region holds reference to younger region"
  - Warn if older region significantly outlives younger

### 10.3 Ownership Transfer Semantics (TODO)

Can we implement true ownership transfer for cross-thread channel send?

- **Current:** Channel send retains region, receiver releases (shared ownership)
- **Future:** If sender never uses value again, transfer ownership (no RC overhead)

---

## 11) References

- `docs/CTRR.md` - CTRR contract and Region Closure Property
- `runtime/docs/CTRR_TRANSMIGRATION.md` - Transmigration implementation contract
- `runtime/docs/ARCHITECTURE.md` - Overall runtime architecture (to be updated with link to this document)
- RC Dialect: https://www.barnowl.org/research/rc/index.html
