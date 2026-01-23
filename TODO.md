# OmniLisp TODO

**Status:** All major development complete. See `archived_todos.md` for history.

**Last Updated:** 2026-01-23

---

## Active Tasks

### REVIEWED:NAIVE Items

These items were marked during code review as naive implementations needing attention:

#### P0: Bug Fix

| Task | Location | Issue |
|------|----------|-------|
| Fix `prim_distinct` buffer overflow | `runtime/src/collections.c:932` | Fixed 1024-element buffer silently truncates larger lists. Should dynamically resize. |

#### P1: Architecture / Performance

| Task | Location | Issue |
|------|----------|-------|
| ~~Refactor regex to use Pika parser~~ | `runtime/src/regex_compile.c` | **COMPLETE** - Now uses Pika clause API and grammar parsing. All 10 regex tests pass. |
| Replace bubble sort in profiler | `runtime/src/profile.c:393` | O(n²) bubble sort. Use qsort() for O(n log n). |

#### P2: Performance (Linear search where hash would help)

| Task | Location | Issue |
|------|----------|-------|
| Hash-based condition type lookup | `runtime/src/condition.c:68,78` | Linear search through registry. Add hash table for O(1) lookup. |
| Hash-based condition slot lookup | `runtime/src/condition.c:206` | Linear search through slots. Consider hash map. |
| Optimize generic method dispatch | `runtime/src/generic.c:214,296` | Linear search for method matching. Consider dispatch table. |

#### P3: Memory (heap instead of region)

| Task | Location | Issue |
|------|----------|-------|
| Use region allocation in string_join | `runtime/src/string_utils.c:145` | Uses malloc instead of region_alloc. |
| Use region allocation in string_replace | `runtime/src/string_utils.c:202` | Uses malloc instead of region_alloc. |

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

### Core Language: 95% Complete

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

### Standard Library: 90% Complete

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

## Documentation

- `docs/SYNTAX.md` - Language syntax reference
- `docs/QUICK_REFERENCE.md` - API quick reference
- `archived_todos.md` - Development history and completed tasks
