# OmniLisp TODO

**Status:** Core runtime complete. Language surface features in progress.

**Last Updated:** 2026-01-28

---

## 🔴 TOP PRIORITY - Language Completion

These features have backend infrastructure but are NOT wired to surface syntax.

### P0: Macro System

**Status:** 0% wired | Infrastructure exists in `csrc/macro/`

| Task | Location | Status |
|------|----------|--------|
| Wire `(define [syntax name] ...)` parsing | `csrc/parser/parser.c` | TODO |
| Implement pattern matching in macro expander | `csrc/macro/macro.c` | TODO |
| Add `...` ellipsis pattern support | `csrc/macro/pattern.c` | TODO |
| Hygiene: gensym and scope tracking | `csrc/macro/hygiene.c` | TODO |
| Wire macro expansion in compiler | `csrc/compiler/compiler.c` | TODO |
| Add standard macros: `when`, `unless`, `cond`, `->`, `->>` | `lib/core.omni` | TODO |

**Blocking:** Cannot define user syntactic sugar without this.

**Design reference:** `docs/UNDOCUMENTED_FEATURES.md` section 2

### P0: Multiple Dispatch (Surface Syntax) - COMPLETE

**Status:** 100% complete | Surface syntax wired to runtime

| Task | Location | Status |
|------|----------|--------|
| ~~Wire type-annotated `define` to dispatch table~~ | `csrc/codegen/codegen.c` | **COMPLETE** - Type extraction + method tracking |
| ~~Enable `(define foo [x {Int}] ...)` + `(define foo [x {String}] ...)`~~ | `csrc/codegen/codegen.c` | **COMPLETE** - Typed methods tracked |
| ~~Generate dispatch lookup code~~ | `csrc/codegen/codegen.c:5800` | **COMPLETE** - Uses omni_generic_invoke |
| ~~Runtime method table registration~~ | `runtime/src/generic.c` | **EXISTS** |
| ~~Test multiple dispatch end-to-end~~ | `tests/test_dispatch.omni` | **COMPLETE** - Int/String dispatch working |

**Known limitation:** nil values return error (runtime check_argument_types returns false for NULL). Will be addressed in P1 type system completion.

**Reference:** `UNWIRED_FEATURES_REPORT.md` - Phase 19

### P1: Type System Completion

**Status:** 95% complete | Minor gaps

| Task | Location | Status |
|------|----------|--------|
| Wire parametric subtype checking | `csrc/analysis/type_infer.c` | TODO |
| `type?` predicate with user-defined types | `runtime/src/types.c` | TODO |
| `type-of` returning struct types | `runtime/src/types.c` | PARTIAL |
| Effect row type checking | `csrc/analysis/effect_check.c` | TODO |
| Handle nil in generic dispatch | `runtime/src/generic.c:202` | TODO - check_argument_types needs nil support |

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

#### P3: Memory (region allocation) - COMPLETE

| Task | Location | Status |
|------|----------|--------|
| ~~Use region allocation in string_join~~ | `runtime/src/string_utils.c:145` | **COMPLETE** - Now uses mk_string_region(). |
| ~~Use region allocation in string_replace~~ | `runtime/src/string_utils.c:202` | **COMPLETE** - Now uses mk_string_region(). |

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

### Core Language: 85% Complete

| Feature | Status | Notes |
|---------|--------|-------|
| Control Flow | ✅ Complete | `if`, `match`, `do` |
| Bindings | ✅ Complete | `let`, `define` |
| Functions | ✅ Complete | `lambda`, `fn`, closures |
| Data Types | ✅ Complete | List, Array, Dict, primitives |
| Type System | ⚠️ 95% | Dispatch not wired to surface |
| **Macros** | ❌ **0%** | **TOP PRIORITY** |
| **Multiple Dispatch** | ⚠️ **70%** | **TOP PRIORITY** - backend exists |
| Modules | ✅ Complete | `module`, `import`, `require` |
| Error Handling | ✅ Complete | Effects system |
| Concurrency | ✅ Complete | Fibers, effects |
| Effects System | ✅ Complete | `handle`, `perform`, `resume` |

### Standard Library: 80% Complete

| Feature | Status | Notes |
|---------|--------|-------|
| Collections | ⚠️ 80% | Missing: `sort`, `group-by`, `zip` |
| Math/Numerics | ⚠️ 40% | Missing: trig, `sqrt`, `pow`, `random` |
| I/O | ✅ Complete | File operations |
| JSON | ❌ 0% | Not implemented |
| Strings | ✅ Complete | All string ops |
| DateTime | ✅ Complete | Date/time operations |

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

- **P0 Complete: Multiple Dispatch Surface Syntax Wired**
  - Added type extraction from slot syntax `[x {Type}]`
  - Track typed methods in codegen with unique mangled names
  - Generate wrapper functions adapting static functions to ClosureFn
  - Emit `__init_generics()` to create generic functions and register methods
  - Update call sites to use `omni_generic_invoke()` for runtime dispatch
  - Test: `tests/test_dispatch.omni` - Int/String dispatch working
- Fixed `prim_take_while` buffer overflow (P0)
- Added `take-while` and `drop-while` codegen mappings
- Fixed effects system: list form, resumption calls, nested handlers
- Completed hash optimization work (P0-P2 in TODO_NAIVE_OPTIMIZATIONS.md)
- Completed P3: Region allocation for `string_join` and `string_replace`

---

## Documentation

- `docs/SYNTAX.md` - Language syntax reference
- `docs/QUICK_REFERENCE.md` - API quick reference
- `TODO_NAIVE_OPTIMIZATIONS.md` - Detailed optimization tracking
- `archived_todos.md` - Development history and completed tasks
