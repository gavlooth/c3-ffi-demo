# OmniLisp TODO

**Status:** All major development complete. See `archived_todos.md` for history.

**Last Updated:** 2026-01-28

---

## Active Tasks

### REVIEWED:NAIVE Items

All P0-P2 items have been addressed. See commit history for details.

#### P0: Bug Fix - COMPLETE

| Task | Location | Status |
|------|----------|--------|
| ~~Fix buffer overflow in `prim_take_while`~~ | `runtime/src/collections.c` | **COMPLETE** - Changed from fixed 1024-element buffer to dynamic resizing. |

#### P1: Architecture / Performance - COMPLETE

| Task | Location | Status |
|------|----------|--------|
| ~~Refactor regex to use Pika parser~~ | `runtime/src/regex_compile.c` | **COMPLETE** - Now uses Pika clause API and grammar parsing. |
| ~~Replace bubble sort in profiler~~ | `runtime/src/profile.c` | **COMPLETE** - Using qsort() for O(n log n). |

#### P2: Performance (Hash optimizations) - COMPLETE

| Task | Location | Status |
|------|----------|--------|
| ~~Hash-based condition type lookup~~ | `runtime/src/condition.c` | **COMPLETE** - Added name/ID hash maps. |
| ~~Hash-based condition slot lookup~~ | `runtime/src/condition.c` | **COMPLETE** - Added per-condition slot maps. |
| ~~Optimize generic method dispatch~~ | `runtime/src/generic.c` | **COMPLETE** - Added dispatch cache + arity-grouped methods. |

See `TODO_NAIVE_OPTIMIZATIONS.md` for full optimization history.

#### P3: Memory (heap instead of region) - Deferred

| Task | Location | Issue |
|------|----------|-------|
| Use region allocation in string_join | `runtime/src/string_utils.c:145` | Uses malloc instead of region_alloc. Low priority - working correctly. |
| Use region allocation in string_replace | `runtime/src/string_utils.c:202` | Uses malloc instead of region_alloc. Low priority - working correctly. |

---

## Deferred Enhancements (Optional)

These have working fallbacks and are not blocking:

| Item | Location | Reason Deferred |
|------|----------|-----------------|
| Specialization system enhancements | `csrc/codegen/codegen.c` | Working fallbacks exist |
| Type inference enhancements | `csrc/analysis/type_infer.c` | Working fallbacks exist |
| ~~Channel select blocking~~ | `runtime/src/memory/continuation.c` | **COMPLETE** - Multi-channel fiber_select implemented |
| Module auto-loading | `runtime/src/modules.c:540` | Requires file loading pipeline |
| Runtime symbol lookup | `runtime/src/debug.c:736` | Requires environment access |

---

## Completion Summary

### Core Language: 100% Complete

| Feature | Status |
|---------|--------|
| Control Flow | Complete |
| Bindings | Complete |
| Functions | Complete |
| Data Types | Complete |
| Type System | Complete |
| Macros | Complete |
| Modules | Complete |
| Error Handling | Complete |
| Concurrency | Complete |
| Sets | Complete |
| DateTime | Complete |
| Strings | Complete |
| Effects System | Complete |

### Standard Library: 95% Complete

| Feature | Status |
|---------|--------|
| Collections | Complete |
| Math/Numerics | Complete |
| I/O | Complete |
| JSON | Complete |
| Debug/Testing | Complete |
| Profiling | Complete |

### Runtime: 100% Complete

| Feature | Status |
|---------|--------|
| Region-based Memory | Complete |
| Reference Counting | Complete |
| Transmigration | Complete |
| Fibers/Promises | Complete |
| Channels | Complete |
| FFI (ccall) | Complete |

---

## Recent Changes (2026-01-28)

- Fixed `prim_take_while` buffer overflow (P0)
- Added `take-while` and `drop-while` codegen mappings
- Fixed effects system: list form, resumption calls, nested handlers
- Completed hash optimization work (P0-P2 in TODO_NAIVE_OPTIMIZATIONS.md)

---

## Documentation

- `docs/SYNTAX.md` - Language syntax reference
- `docs/QUICK_REFERENCE.md` - API quick reference
- `TODO_NAIVE_OPTIMIZATIONS.md` - Detailed optimization tracking
- `archived_todos.md` - Development history and completed tasks
