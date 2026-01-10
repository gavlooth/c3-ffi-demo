# CTRR Transmigration Remap: Forwarding Table Strategy (Phase 35 Design)

**Status:** Design note for a non-GC performance upgrade  
**Applies to:** `runtime/src/memory/transmigrate.c` remap implementation  
**Non-goals:** stop-the-world GC, heap scanning, language-visible sharing primitives

This document describes a **runtime-only** optimization to reduce the constant
factor of CTRR transmigration in large, mostly-acyclic graphs (notably linked
lists), without changing correctness semantics:

- preserve cycles
- preserve sharing
- ensure Region Closure (no pointers into closing `src_region`)

---

## 1) Why the current remap is still expensive

The current transmigration loop uses:

- a bitmap for **visited** checks (fast O(1))
- a hash table for `old_ptr -> new_ptr` mapping (needed to preserve sharing/cycles)

Even when the graph is a simple acyclic list (no sharing), we still do:

- one remap insertion per node
- occasional remap lookups (for revisits / cycles)

Hashing + probing is a significant constant-factor tax for the “list wall”
benchmark (10k nodes and up), even with robin-hood probing.

---

## 2) Key observation: src_region is an address domain we can index

Transmigration already computes a `RegionBitmap` with:

- `start`: min address in `src_region` allocation domain
- `size_words`: number of machine words covered by the domain

We can reuse the same address-to-index calculation to build a **forwarding
table**:

- `forward[word_offset] = new_ptr` for the **word offset of the old object**

This replaces a hash lookup with:

- a couple of integer ops + one array load/store

---

## 3) Dense forwarding table (fast path)

### 3.1 Layout

If the src region address domain is not too large, allocate:

```c
void** forward = arena_alloc(tmp_arena, b->size_words * sizeof(void*));
memset(forward, 0, b->size_words * sizeof(void*));
```

Index:

```c
size_t word_offset = ((uintptr_t)old_ptr - b->start) / sizeof(uintptr_t);
```

Contract:

- Only store forwarding pointers at **object start addresses**.
- It’s OK that many word offsets do not correspond to object starts; those
  entries remain NULL and are never queried unless the traversal sees that exact
  pointer as a child slot value.

### 3.2 Get/put

```c
static inline void* forward_get(RegionBitmap* b, void** fwd, void* old_ptr) {
  if (!b || !fwd || !old_ptr) return NULL;
  size_t off = ((uintptr_t)old_ptr - b->start) / sizeof(uintptr_t);
  if (off >= b->size_words) return NULL;
  return fwd[off];
}

static inline void forward_put(RegionBitmap* b, void** fwd, void* old_ptr, void* new_ptr) {
  size_t off = ((uintptr_t)old_ptr - b->start) / sizeof(uintptr_t);
  fwd[off] = new_ptr;
}
```

### 3.3 Thresholding

Dense forwarding is only reasonable if memory use is bounded:

- memory = `size_words * sizeof(void*)`

Example (64-bit):
- 1M words ⇒ 8MB forwarding table (often acceptable for transmigrate temp arena)
- 16M words ⇒ 128MB (maybe too large)

Therefore, use a threshold like:

- `if (size_words * sizeof(void*) <= OMNI_FWD_MAX_BYTES)` then use dense forwarding
- else fall back to chunked forwarding or robin-hood hash

### 3.4 Lazy allocation (important practical refinement)

A dense forwarding table has a **fixed, one-time memset cost** per transmigrate
call. For small graphs, this cost can dominate and make performance worse.

Therefore, in practice it is better to allocate the forwarding table **lazily**:

- Start with the existing robin-hood hash remap (cheap for small graphs).
- After `N` distinct clones have occurred (graph is “large enough”), allocate and
  zero the dense forwarding table once and then prefer forwarding for subsequent
  remap writes/reads.

This preserves semantics and targets the list “wall” without regressing the
small-case constant factor.

---

## 4) Chunked forwarding table (sparse domains)

Dense forwarding can be too large when the src address range is sparse (e.g.,
many arena chunks far apart, or large inline gaps).

Chunked strategy:

1. Split the forwarding table into fixed-size pages (e.g., 4096 word entries).
2. Maintain a small hash map: `page_index -> page_ptr`.
3. Allocate pages lazily when inserting a forwarding pointer.

This keeps O(1) access for most operations and bounds memory usage to “pages
touched”, while avoiding full hash lookups per remap operation.

This is still not GC; it’s a temporary remap index that is freed when transmigrate
returns.

---

## 5) Correctness constraints (must not regress)

Forwarding table is a pure remap implementation detail. It must preserve:

- **Cycle handling:** if an object is revisited, rewriting must use the same new pointer
- **Sharing preservation:** multiple old references to the same object map to one new object
- **External pointer filtering:** pointers outside `src_region` must remain unchanged
- **No shallow-copy fallback:** missing metadata is still fatal

---

## 6) Verification (bench + tests)

### 6.1 Bench

- Run `make -C runtime/tests bench` and compare list benchmarks:
  - 10k list transmigrate ns/op should meaningfully decrease

### 6.1.1 Measured impact (local run, 2026-01-09)

On this repository, with Phase 35 P0 enabled (lazy dense forwarding; default
threshold `OMNI_REMAP_FORWARDING_MIN_CLONES=2048`), `make -C runtime/tests bench`
showed:

- 10k list transmigrate improved from ~0.82 ms/op to ~0.54 ms/op (≈1.5× faster).

Exact numbers vary by machine and run; treat the measurement as directional, not
as a stable contract.

### 6.2 Tests

Add tests that:

- Force the forwarding table path (compile-time option) and ensure:
  - cycles are preserved
  - sharing is preserved
  - boxed scalars and external pointers still behave correctly

---

## 7) Why this is still within CTRR / Region-RC constraints

- No heap scanning beyond the reachable graph root
- No background collector
- No global runtime phase
- No language-visible sharing primitive

This is purely an implementation choice for `old_ptr -> new_ptr` mapping during
an explicit escape repair operation.
