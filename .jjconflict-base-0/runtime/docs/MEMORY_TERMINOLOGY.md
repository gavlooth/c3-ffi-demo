# OmniLisp Memory Terminology (Pinned Definitions)
**Status:** Normative terminology (project-wide)  
**Last Updated:** 2026-01-10  
**Purpose:** Remove ambiguity between *semantic lifetime regions* and *runtime allocation arenas*.

This document **pins** the meaning of the word **“Region”** to the *semantic* definition:

> **Region (canonical):** a collection of objects with the **same lifetime**.

Everything else must use distinct terms (Arena, ArenaRegion/RCB, Global Arena, etc.).

---

## 1) Canonical Terms (Use These Words)

### 1.1 Region (aka Lifetime Region, LR)

**Definition:** A **Region** is a *semantic lifetime class*: “objects that are intended to die together”.

This is the meaning used by the CTRR contract. It is not “just an arena”.

**How it exists today:**
- As an *analysis construct* in the compiler (where lifetimes and escapes are inferred).

**Code location (semantic):**
- `csrc/analysis/analysis.h` → `RegionInfo` (region_id, scope positions, escaping refs)

---

### 1.2 Arena (allocation mechanism)

**Definition:** An **Arena** is a *physical allocator* (bump allocation over linked chunks).

An Arena can be used to *implement* a Region, but an Arena is not automatically a Region.

**Code location (allocator):**
- `third_party/arena/arena.h` → `Arena`, `ArenaChunk`, `arena_alloc`, `arena_free`, etc.

---

### 1.3 ArenaRegion / Region Control Block (RCB) (runtime container)

**Definition:** An **ArenaRegion (RCB)** is the *runtime object* that:
- owns an Arena (and inline buffer),
- implements bulk allocation and reclamation,
- tracks liveness (Region‑RC external refs + tethering),
- is the unit that `region_exit()` closes and `region_destroy_if_dead()` reclaims.

**Important:** An ArenaRegion is an **implementation container**. It *should* correspond to a semantic Region (LR), but it can silently become a “mixed-lifetime arena” if we do not enforce lifetime constraints at runtime boundaries.

**Code location (runtime container):**
- `runtime/src/memory/region_core.h` → `struct Region` (this is the current ArenaRegion/RCB type)
- `runtime/src/memory/region_core.c` → `region_create`, `region_exit`, `region_destroy_if_dead`, retain/release, tethering

**Naming note (current code reality):**
- The runtime type is currently named `Region` in C. In docs, treat it as **ArenaRegion/RCB**.

---

### 1.4 Global ArenaRegion (legacy/shim pool)

**Definition:** The **Global ArenaRegion** is the long-lived runtime pool used by the interpreter/legacy APIs.

It is not a semantic Region (LR). It is a convenience allocation root.

**Code location:**
- `runtime/src/runtime.c` → `_global_region` and `_ensure_global_region()`

---

## 2) “Region is not Arena”: Why the distinction matters

If we treat “region” as “just an arena”, then:
- runtime mutation can create younger→older (shorter-lived→longer-lived) pointers,
- cross-thread structures (channels/atoms) can retain pointers into closing arenas,
- and the system degrades into “arenas with accidental lifetime entanglement”.

That breaks the *guarantee* behind the CTRR model: “a Region is a lifetime class”.

Therefore, **Region (LR) must be enforced**, not only inferred.

---

## 3) Enforcement Requirements (Non-Negotiable for the LR meaning)

To make “Region = lifetime class” true in the presence of mutation/concurrency:

### 3.1 Region identity (runtime)

The runtime must be able to answer:

> Given a boxed `Obj*`, which ArenaRegion/RCB owns it?

This is commonly called `region_of(obj)` or “owner-region metadata”.

Without region identity, we cannot correctly:
- update `external_rc` at real escape boundaries,
- implement store barrier auto-repair,
- make channels/atoms safe without forcing everything into the global pool.

Planned work:
- `TODO.md` → Issue 1 / Amendment A / `I1-region-of-obj-mechanism`

### 3.2 Store barrier (runtime)

All pointer-storing operations (mutation) must go through a single helper that enforces:
- no illegal lifetime edges remain after the store (auto-repair: transmigrate or merge/adopt).

This is the runtime analog of “ASAP inserts frees”: mutation creates new escape edges dynamically, so it must be repaired dynamically, locally, and immediately (no STW GC).

Planned work:
- `TODO.md` → Issue 2 / Amendment A / `I2-store-barrier-choke-point`

---

## 4) Where “Regions” live in the code (map)

### 4.1 Semantic Regions (LR) — compiler side

- `csrc/analysis/analysis.h`:
  - `RegionInfo` (region_id, positions, escape info)
- `csrc/analysis/analysis.c`:
  - region tracking (`omni_region_new`, region enter/exit)
- `csrc/codegen/region_codegen.c`, `csrc/codegen/codegen.c`:
  - emits `region_create/exit/destroy_if_dead`
  - emits `transmigrate(...)` at escape boundaries (today: not yet retain/release)

### 4.2 Runtime ArenaRegions (RCB) — allocation + liveness

- `runtime/src/memory/region_core.h` / `runtime/src/memory/region_core.c`:
  - ArenaRegion/RCB definition + liveness rules (`external_rc`, `tether_count`, `scope_alive`)
- `runtime/src/memory/region_value.c`:
  - allocators/constructors (place objects into an ArenaRegion)
- `runtime/src/memory/transmigrate.c`:
  - escape repair (clone/trace/remap)
- `runtime/src/runtime.c`:
  - global pool (`_global_region`) and pointer-storing primitives (arrays/dicts/atoms/channels)

---

## 5) Allowed “impure” language for discussions (but not for the canonical term)

To talk about real runtime states without redefining “Region”:

- **Lifetime-tainted ArenaRegion**: an ArenaRegion whose contents are no longer a single lifetime class due to an *unrepaired* dynamic lifetime entanglement (typically a mutation-time store that would violate the Region Closure Property when a younger lifetime ends).
  - Exclusions:
    - The **Global ArenaRegion** (`_global_region`) is not “tainted” by this definition; it is intentionally long-lived.
    - A region that has been intentionally **adopted/merged/promoted** as part of auto-repair is not “tainted”; it is a larger lifetime class by construction.
- **Retained ArenaRegion**: an ArenaRegion kept alive by `external_rc > 0`.
- **Borrowed ArenaRegion**: an ArenaRegion temporarily kept alive by `tether_count > 0`.

Use these terms instead of “impure region” if we want to keep “Region” reserved for LR.
