# CTRR (Compile-Time Region Reclamation)

**Status:** Normative memory model contract (project spec)  
**Scope:** Compiler + runtime behavior guarantees  
**Target:** **C99 + POSIX + common compiler extensions** (e.g., TLS, `__atomic` builtins)  
**Non-goals:** stop-the-world tracing GC, heap-wide garbage collection loops

OmniLisp uses **CTRR (Compile-Time Region Reclamation)** as its primary memory
model. CTRR is **not** a garbage collector: there is no runtime phase that scans
the heap searching for garbage.

CTRR is also intentionally named differently from the paper term **ASAP (As
Static As Possible)**. ASAP has a specific meaning in the literature; OmniLisp’s
CTRR contract is “regions + compile-time scheduled lifetimes + explicit escape
repair”.

---

## 1) Glossary (precise meanings)

- **Region (canonical)**: a **semantic lifetime class**: “a collection of objects with the same lifetime”.
  - Regions are inferred/scheduled by the compiler (CTRR analysis) and then **implemented** by the runtime.
  - **Important:** a Region is *not* synonymous with an Arena allocator.
  - See `runtime/docs/MEMORY_TERMINOLOGY.md` (pinned project-wide terminology).
- **Arena**: a physical bump/chunk allocator used to implement bulk allocation (not a semantic lifetime class).
- **ArenaRegion / RCB**: the runtime container that owns an Arena and implements Region‑RC + tethering (currently `struct Region` in `runtime/src/memory/region_core.h`).
- **Region lifetime**: the interval during which a Region (semantic) is valid; it is realized as “ArenaRegion storage remains valid”.
- **Escape**: a value outlives the region where it was allocated (return, closure
  capture, global store, cross-thread send, etc.).
- **Borrow**: a temporary reference bounded by a window (typically a call).
- **Tethering**: runtime pinning of a region during a borrow window.
- **Transmigration**: moving/copying an object graph from a source region to a
  destination region, rewriting pointers so the result graph contains **no
  pointers into the source region**.

---

## 2) Core guarantees (non-negotiable)

### 2.1 Region Closure Property (hard safety guarantee)

For any region `Rsrc`:

> After `region_exit(Rsrc)` and once the runtime is allowed to reclaim or reuse
> `Rsrc`’s storage, **no reachable value may contain any pointer into memory
> owned by `Rsrc`.**

This is the foundation of CTRR. Violations are use-after-free by construction.

**Enforcement note (pinned to the canonical Region definition):**
- In a real Lisp, runtime mutation and cross-thread/global structures can create new escape edges dynamically.
- Therefore, “Region = lifetime class” is only a guarantee if the implementation provides:
  1. **Region identity** (`region_of(obj)` / owning ArenaRegion metadata), and
  2. a **store barrier** (mutation-time auto-repair: transmigrate or adopt/merge).
- See `runtime/docs/MEMORY_TERMINOLOGY.md` (“Enforcement Requirements”) and `TODO.md` Issue 1 / Issue 2 amendments.

### 2.2 “Everything can escape” (language-level guarantee)

OmniLisp’s contract is:

> Any user-constructible value may escape a region boundary and remain valid.

Therefore, transmigration must be **total** over all runtime tags.

### 2.3 No stop-the-world tracing (design constraint)

CTRR prohibits collectors that scan the heap looking for garbage.
Runtime memory work must be:

- **Explicit** (triggered by compiler-inserted operations)
- **Local** (per-region / per-root / per-subgraph), not global

---

## 3) Compiler responsibilities

The compiler must:

1. **Schedule region lifetimes**
   - Emit `region_create()` at scope entry.
   - Emit `region_exit()` at the earliest safe point (based on liveness).

2. **Protect borrows**
   - Insert `region_tether_start()` / `region_tether_end()` around borrows that
     cross region boundaries and must not outlive the borrow window.

3. **Repair escapes**
   - At every escape boundary, ensure the escaping value becomes valid in an
     outliving region by emitting:
     - `transmigrate(value, src_region, dest_region)`, or
     - a defined ownership-transfer optimization (e.g., region adoption/splicing)
       that still satisfies the Region Closure Property.

**Important:** tethering is not an escape mechanism. It pins regions temporarily;
it does not rewrite pointers.

---

## 4) Runtime responsibilities

The runtime must:

1. **Reclaim regions safely**
   - A region may be reclaimed/reused only when it has exited and no longer has
     any outstanding liveness pins (external refs, tethers, etc.).

2. **Make tethering correct**
   - Between `region_tether_start(R)` and `region_tether_end(R)`, `R` must not be
     reclaimed or reused.

3. **Make transmigration correct and total**
   - After transmigration, the destination graph contains no pointers into the
     source region.
   - Missing support for a runtime tag is a bug (no shallow-copy fallback).

---

## 5) Metadata-driven transmigration (required)

Transmigration must be **metadata-driven**:

- each runtime type/tag provides:
  - a **clone** operation (allocate/copy payload into destination region)
  - a **trace** operation (enumerate all child `Obj*` slots, including inside
    payload buffers like arrays/dicts)

Missing metadata for a tag that can escape must fail loudly in debug builds.

See `runtime/docs/CTRR_TRANSMIGRATION.md` for the detailed runtime contract.
