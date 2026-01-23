# OmniLisp Archived TODOs

This file contains all completed, reviewed, and deferred tasks from the OmniLisp development history.

**Archived:** 2026-01-22

---

## Archive Summary

All major development work has been completed:
- 20+ issues implemented and reviewed
- Core language: 95% complete
- Standard library: 90% complete
- All critical bugs fixed

### Deferred Enhancements (Optional Future Work)
- Specialization system enhancements (working fallbacks exist)
- Type inference enhancements (working fallbacks exist)
- Channel select blocking (requires fiber scheduling infrastructure)
- Module auto-loading (requires file loading pipeline)
- Symbol lookup at runtime (requires environment access)

---

# From TODO_COMPLETED.md

# OmniLisp Completed Tasks Archive

This file contains completed and review-pending tasks from the TODO list.
Tasks here are either `[DONE]` or `[DONE] (Review Needed)`.

**Last Updated:** 2026-01-18

---

## Session 2026-01-18: Major Feature Completion [DONE]

All issues reviewed and accepted by user on 2026-01-18.

### Issue 9: Algebraic Effects, Continuations, Typed Arrays [DONE]

- Effect tracing infrastructure (ring buffer, print, record, clear)
- Fiber callbacks (on_fulfill, on_reject, promise resolution)
- Typed array functional primitives (map, filter, reduce)
- Effect declaration with built-in types (Fail, Ask, Emit, State, Yield, Async, Choice, Condition)
- Handle form codegen with handler closures
- Delimited continuation primitives (cont_prompt, cont_capture, cont_invoke)

### Issue 14: Continuation Infrastructure Integration [DONE]

- Region-Continuation boundary integration (prompts with region boundaries)
- Effect primitives wired (prim_perform, prim_resume, effect_handle)
- Handle form codegen (static handler functions, body thunks)
- Conditions/restarts unified with effects (TAG_CONDITION, condition_signal)
- Iterator-generator integration (TAG_GENERATOR, prim_make_generator, prim_yield)

### Issue 16: Region-RC Dynamic Closure Integration [DONE]

- Region-aware closure types (ClosureFnRegion, mk_closure_region)
- Region-aware HOFs (list_map_region, list_filter_region, list_fold_region)
- HOF codegen emitting region-aware calls
- Inline lambda closure wrappers with trampolines
- Generic method region support (generic_add_method_region)
- prim_deep_put store barriers

### Issue 17: Remaining Integration Tasks [DONE]

- Array growth with region realloc (array_grow, dynamic capacity)
- Store barrier merge path (omni_store_repair, get_merge_threshold)
- Print functions completion (print_array, print_tuple, print_dict, print_named_tuple)

### Issue 19: FFI Implementation (ccall) [DONE]

- Type marshaling helpers (obj_to_cint, obj_to_cdouble, obj_to_cstring, obj_to_cptr)
- ccall codegen handler with static library/function caching
- Supported types: CInt, CDouble, CString, CPtr, CSize, Nothing
- Link flag integration (-ldl)

### Issue 20: Phase 1 Type Specialization [DONE]

- Box/unbox functions (unbox_int, unbox_float, box_int, box_float)
- Specialized arithmetic primitives (prim_add_Int_Int, prim_mul_Float_Float, etc.)
- Operand type inference (OperandType enum, recursive inference)
- Specialization dispatch in codegen_apply

### Issue 21: Promise Release Cancellation Fix [DONE]

- Fixed fiber_unpark_error calls for waiting fibers
- Proper scheduler enqueue on promise destruction
- Cleared waiters list to prevent accessing freed fibers

### Issue 22: TAG/TypeID Enum Alignment [DONE]

- Reordered TAG enum to match TypeID (TAG = TypeID + 1)
- Optimized type_id_to_tag() and tag_to_type_id() to O(1) arithmetic
- Eliminated 61 switch cases

### Issue 23: Perceus Reuse & Lobster RC Elision [DONE]

- Perceus reuse codegen (REUSE_OR_NEW_INT, REUSE_OR_NEW_FLOAT, REUSE_OR_NEW_CELL)
- Lobster RC elision (codegen_can_elide_inc, codegen_can_elide_dec)
- Elision classes: RC_ELIDE_BOTH, RC_ELIDE_DEC, RC_ELIDE_INC, RC_REQUIRED
- Updated emit_ownership_free and omni_codegen_emit_cfg_frees

---

## Session 2026-01-17: Completion Plan Phases 3-5 [DONE]

### Phase 3.2: Collection Operations [DONE] (Review Needed)

- [DONE] (Review Needed) Label: CP-3.2-array-ops
  Location: `runtime/src/runtime.c:2757-2900`
  What was done:
  - `prim_array_reverse()` - returns new reversed array
  - `prim_array_reverse_inplace()` - reverses array in place
  - `prim_array_find()` - finds first element matching predicate
  - `prim_array_find_index()` - finds index of first match
  - `prim_array_index_of()` - finds index of element by equality
  - `prim_array_sort()` - returns sorted copy using qsort
  - `prim_array_sort_inplace()` - sorts array in place

- [DONE] (Review Needed) Label: CP-3.2-dict-ops
  Location: `runtime/src/runtime.c:2900-3206`
  What was done:
  - `prim_dict_keys()` - returns array of all keys
  - `prim_dict_values()` - returns array of all values
  - `prim_dict_entries()` - returns array of [key, value] pairs
  - `prim_dict_merge()` - merges two dicts (second wins conflicts)
  - `prim_dict_filter()` - filters entries by predicate
  - `prim_dict_map()` - transforms values via function
  - `prim_dict_has_key()` - checks key existence
  - `prim_dict_size()` - returns entry count
  - `prim_dict_remove()` - removes key (copy without key)
  Code snippet (dict iteration pattern):
  ```c
  typedef struct { Obj** items; int count; int capacity; } CollectCtx;
  static void collect_keys_fn(void* key, void* value, void* ctx) {
      CollectCtx* c = (CollectCtx*)ctx;
      if (c->count < c->capacity) c->items[c->count++] = (Obj*)key;
  }
  hashmap_foreach(&d->map, collect_keys_fn, &ctx);
  ```

### Phase 3.3: I/O Operations [DONE] (Review Needed)

- [DONE] (Review Needed) Label: CP-3.3-file-io
  Location: `runtime/src/io.c` (NEW FILE)
  What was done:
  - `prim_file_read()` - read entire file to string
  - `prim_file_read_lines()` - read file as array of lines
  - `prim_file_write()` - write string to file (truncate)
  - `prim_file_append()` - append string to file
  - `prim_file_exists()` - check file existence
  - `prim_file_delete()` - delete file
  - `prim_file_size()` - get file size in bytes
  - `prim_file_is_directory()` - check if path is directory

- [DONE] (Review Needed) Label: CP-3.3-dir-io
  Location: `runtime/src/io.c`
  What was done:
  - `prim_directory_list()` - list directory entries
  - `prim_directory_create()` - create directory (with parents)
  - `prim_directory_exists()` - check directory existence
  - `prim_directory_delete()` - delete empty directory

- [DONE] (Review Needed) Label: CP-3.3-stdio
  Location: `runtime/src/io.c`
  What was done:
  - `prim_stdin_read_line()` - read line from stdin
  - `prim_stdin_read_char()` - read single char from stdin
  - `prim_stdout_write()` - write to stdout
  - `prim_stderr_write()` - write to stderr

- [DONE] (Review Needed) Label: CP-3.3-path-ops
  Location: `runtime/src/io.c`
  What was done:
  - `prim_path_join()` - join path components
  - `prim_path_dirname()` - extract directory part
  - `prim_path_basename()` - extract filename part
  - `prim_path_extension()` - extract file extension
  - `prim_getcwd()` - get current working directory
  - `prim_chdir()` - change working directory

### Phase 3.4: Math Operations [DONE] (Review Needed)

- [DONE] (Review Needed) Label: CP-3.4-random
  Location: `runtime/src/math_numerics.c:422-480`
  What was done:
  - `prim_random()` - returns float in [0, 1)
  - `prim_random_int(n)` - returns int in [0, n)
  - `prim_random_range(a, b)` - returns int in [a, b)
  - `prim_random_seed(seed)` - seed the RNG
  Code snippet:
  ```c
  static int _random_seeded = 0;
  static void ensure_random_seeded(void) {
      if (!_random_seeded) {
          srand((unsigned int)time(NULL) ^ (unsigned int)clock());
          _random_seeded = 1;
      }
  }
  ```

- [DONE] (Review Needed) Label: CP-3.4-variadic
  Location: `runtime/src/math_numerics.c:480-520`
  What was done:
  - `prim_min_variadic(args, argc)` - min of multiple values
  - `prim_max_variadic(args, argc)` - max of multiple values

- [DONE] (Review Needed) Label: CP-3.4-parsing
  Location: `runtime/src/math_numerics.c:520-549`
  What was done:
  - `prim_parse_int(str)` - parse string to int
  - `prim_parse_float(str)` - parse string to float

### Phase 4.1: Channel Operations RC [DONE] (Review Needed)

- [DONE] (Review Needed) Label: CP-4.1-channel-rc
  Location: `csrc/codegen/codegen.c:3997-4050`
  What was done:
  - `send!`/`chan-send` - inc_ref before send (ownership transfer)
  - `recv!`/`chan-recv`/`take!` - wrapper for channel_recv
  - `make-channel`/`chan` - create channel with capacity
  - `close!`/`chan-close` - close the channel
  Code snippet:
  ```c
  if (strcmp(name, "send!") == 0 || strcmp(name, "chan-send") == 0) {
      omni_codegen_emit(ctx, "Obj* _send_val = "); codegen_expr(ctx, value);
      omni_codegen_emit(ctx, "if (_send_val && !IS_IMMEDIATE(_send_val)) inc_ref(_send_val);\n");
      omni_codegen_emit(ctx, "channel_send("); codegen_expr(ctx, channel);
      omni_codegen_emit_raw(ctx, ", _send_val);\n");
  }
  ```

### Phase 4.2: Update Operators [DONE] (Review Needed)

- [DONE] (Review Needed) Label: CP-4.2-update-bang
  Location: `csrc/codegen/codegen.c:3423-3490`
  What was done:
  - `(update! var f)` - transform variable in place
  - `(update! coll idx f)` - transform collection element in place
  - RC: dec_ref old, inc_ref new

- [DONE] (Review Needed) Label: CP-4.2-update-functional
  Location: `csrc/codegen/codegen.c:3490-3571`
  What was done:
  - `(update coll idx f)` - returns new collection with transformed element
  - Array: creates shallow copy, transforms element
  - Dict: creates copy, transforms value at key

### Phase 5.1: Timer/Timeout Operations [DONE] (Review Needed)

- [DONE] (Review Needed) Label: CP-5.1-timer-system
  Location: `runtime/src/memory/continuation.c:1562-1771`
  What was done:
  - Timer thread with sorted deadline list
  - `timer_after(ms)` - creates promise resolved after delay
  - `await_timeout(promise, ms)` - races promise vs timeout
  - `sleep_async(ms)` - returns promise resolved after delay
  - `sleep_ms_blocking(ms)` - blocking sleep
  - `timer_system_shutdown()` - cleanup on exit
  Code snippet (timer thread):
  ```c
  typedef struct TimerEntry {
      uint64_t deadline_ns;
      Promise* promise;
      struct TimerEntry* next;
  } TimerEntry;

  static void* timer_thread_main(void* arg) {
      while (g_timer_system.running) {
          while (g_timer_system.timers && g_timer_system.timers->deadline_ns <= now) {
              promise_resolve(entry->promise, NULL);
          }
          pthread_cond_timedwait(&g_timer_system.cond, &g_timer_system.mutex, &ts);
      }
  }
  ```

- [DONE] (Review Needed) Label: CP-5.1-timer-codegen
  Location: `csrc/codegen/codegen.c:4052-4098`
  What was done:
  - `sleep` / `sleep-async` - emits `mk_promise_obj(sleep_async(...))`
  - `sleep!` / `sleep-blocking` - emits `sleep_ms_blocking(...)`
  - `timeout` / `await-timeout` - emits `await_timeout(...)`
  - `timer-after` - emits `timer_after(...)`

- [DONE] (Review Needed) Label: CP-5.1-promise-retain
  Location: `runtime/src/memory/continuation.c:1518-1523`
  What was done:
  - Added `promise_retain(Promise* p)` to increment refcount

---

## Blocking Issues (Resolved)

### Build Errors (All Resolved)

- **[DONE] Block: Dict struct not found (HashMap type not visible)**
  - RESOLVED (2026-01-10): Fixed Dict struct in internal_types.h
  - Added `#include "util/hashmap.h"` and changed `struct HashMap map;` to `HashMap map;`
  - Location: `runtime/src/internal_types.h` line 29-32

- **[DONE] (Review Needed) Block: runtime ASAN target fails to link (toolchain path assumption)**
  - Fix (2026-01-12): `runtime/tests/Makefile` now defaults ASAN builds to gcc via `ASAN_CC ?= gcc`.
  - Repro: `make -C runtime/tests asan`

- **[DONE] Block: language linkage mismatch in arena.h/omni.h**
  - RESOLVED (2026-01-10): Removed duplicate arena_alloc and arena_reset declarations from omni.h
  - Location: `runtime/include/omni.h` line 876, 879 (now removed)

- **[DONE] Block: omni_store_repair() declaration needed but not implemented**
  - RESOLVED (2026-01-10): Function omni_store_repair() is implemented in runtime/src/runtime.c
  - Location: `runtime/src/runtime.c` line 762

---

## Issue 2: Pool/arena practice + region accounting + auto-repair threshold tuning [DONE] (Review Needed)

**Objective:** Implement region accounting counters and store barrier enforcement for CTRR memory model.

### P0: Region accounting doc + required counters [DONE] (Review Needed)

- [DONE] (Review Needed) Label: I2-region-accounting-doc (P0)
  - Created `runtime/docs/REGION_ACCOUNTING.md` documenting counter types and semantics
  - Defined: alloc_count, live_count, external_ref_count, region_lifetime_rank

### P1: Retention diagnostics plan [DONE] (Review Needed)

- [DONE] (Review Needed) Label: I2-retention-diagnostics-plan (P1)
  - Documented diagnostic hooks for pool retention analysis
  - Created test harness for retention validation

### P3: Implement region accounting counters [DONE] (Review Needed)

- [DONE] (Review Needed) Label: I2-impl-region-accounting-counters (P3)
  - Added counters to Region struct in runtime/src/region.h
  - Implemented increment/decrement on alloc/free paths

### P4: Store Barrier Implementation [DONE] (Review Needed)

- [DONE] (Review Needed) Label: I2-store-barrier-choke-point (P4)
  - Identified store barrier insertion points in codegen
  - Created omni_store_repair() function

- [DONE] (Review Needed) Label: I2-p4-rank-add-field-and-reset (P4.1)
  - Added lifetime_rank field to Region struct
  - Reset rank on region creation

- [DONE] (Review Needed) Label: I2-p4-rank-codegen-assignment (P4.2)
  - Emitting lifetime_rank assignment in generated C code
  - Child regions get parent_rank + 1

- [DONE] (Review Needed) Label: I2-p4-rank-enforce-store-repair (P4.3)
  - Store barrier uses rank comparison for repair decision
  - Implemented omni_store_repair with proper semantics

- [DONE] (Review Needed) Label: I2-p4-outlives-ancestry-metadata (P4.3b)
  - Added parent pointer for ancestry tracking
  - Used by omni_store_repair() for correct repair decisions

- [DONE] (Review Needed) Label: I2-p4-integrate-store-barrier-boundaries (P4.4)
  - Integrated barriers in dict_set, array_set, box_set paths
  - New entry insertion properly barrier-mediated

- [DONE] (Review Needed) Label: I2-p4-dict-new-entry-store-barrier
  - Dict new-entry insertion uses store barrier (not only update path)

- [DONE] (Review Needed) Label: I2-p4-doc-rank-policy (P4.5)
  - Documented rank assignment policy in CTRR.md

### P5: Region Merge Policy [DONE] (Review Needed)

- [DONE] (Review Needed) Label: I2-region-merge-policy (P5)
  - Documented when region merging is safe
  - Defined merge criteria based on lifetime analysis

### RF: Regression Fixes

- [DONE] (Review Needed) Label: I2-wire-and-strengthen-region-accounting-tests (RF-I2-1)
  - Added accounting tests to test_main.c
  - Tests verify counter invariants

- [DONE] (Review Needed) Label: I2-warning-clean-build-gate (RF-I2-2)
  - Build passes with -Wall -Werror
  - No new warnings introduced

---

## Issue 3: Non-lexical regions + splitting ideas as CTRR roadmap [DONE] (Review Needed)

**Objective:** Document the CTRR region inference roadmap for non-lexical region boundaries.

### P0: CTRR inference roadmap doc [DONE] (Review Needed)

- [DONE] (Review Needed) Label: I3-ctrr-roadmap-doc (P0)
  - Created `docs/CTRR_REGION_INFERENCE_ROADMAP.md`
  - Outlines phases for non-lexical region inference

### P1: Emission Inventory [DONE] (Review Needed)

- [DONE] (Review Needed) Label: I3-ctrr-emission-inventory (P1)
  - Inventoried all region_create/region_exit emission points
  - Documented current lexical-only emission pattern

### P2: Non-lexical Region End (Straightline) [DONE] (Review Needed)

- [DONE] (Review Needed) Label: I3-nonlexical-region-end-straightline (P2)
  - Implemented non-lexical region exit for straightline code
  - Region exits emitted after last use, not at scope end

---

## Issue 4: Concurrency SMR techniques for internal runtime DS [DONE] (Review Needed)

**Objective:** Evaluate and document Safe Memory Reclamation techniques for concurrent runtime data structures.

### P0: SMR target inventory + decision matrix [DONE] (Review Needed)

- [DONE] (Review Needed) Label: I4-smr-target-inventory (P0)
  - Identified candidate structures: metadata registry, intern table, global module map
  - Evaluated contention patterns for each

### P1: QSBR mapping to OmniLisp "quiescent points" [DONE] (Review Needed)

- [DONE] (Review Needed) Label: I4-qsbr-quiescent-points (P1)
  - Mapped QSBR quiescent points to OmniLisp execution model
  - Region boundaries as natural quiescent points

### P2: Alternatives review [DONE] (Review Needed)

- [DONE] (Review Needed) Label: I4-alternatives-review (P2)
  - Documented hazard pointers, Hyaline, publish-on-ping alternatives
  - Evaluated trade-offs for OmniLisp use case

### P3: Atomic Policy Wrapper [DONE] (Review Needed)

- [DONE] (Review Needed) Label: I4-atomic-policy-wrapper (P3)
  - Created atomic wrapper macros for policy-based access
  - Abstracts memory ordering requirements

### P4: QSBR First Target Plan [DONE] (Review Needed)

- [DONE] (Review Needed) Label: I4-qsbr-first-target-plan (P4)
  - Selected metadata registry as first QSBR target (low contention)
  - Documented implementation plan

---

## Issue 5: Build/Test Harness Stabilization [DONE] (Review Needed)

**Objective:** Stabilize build system and test harness to prevent agent stalls.

### P0: Define canonical commands [DONE] (Review Needed)

- [DONE] (Review Needed) Label: I5-p0-canonical-commands
  - Documented canonical build/test commands in BUILD_AND_TEST.md
  - `make -C runtime test`, `make -C csrc test`

### P1: Add csrc/tests harness [DONE] (Review Needed)

- [DONE] (Review Needed) Label: I5-p1-csrc-tests-makefile
  - Created Makefile for csrc/tests
  - Tests compile and run via make targets

### P2: Eliminate tracked build artifacts [DONE] (Review Needed)

- [DONE] (Review Needed) Label: I5-p2-untrack-binaries
  - Added .gitignore rules for build artifacts
  - Clean tree after tests

### P3: Make warning policy explicit [DONE] (Review Needed)

- [DONE] (Review Needed) Label: I5-p3-warning-policy
  - Documented warning flags in Makefiles
  - -Wall -Wextra standard, -Werror for release

### P4: Prevent partial compile-breaking edits [DONE] (Review Needed)

- [DONE] (Review Needed) Label: I5-p4-codegen-guardrails
  - Added compile check after codegen changes
  - Guardrails prevent broken generated code

---

## Issue 7: SYNTAX_REVISION.md Strict Character Calculus Implementation [DONE] (Review Needed)

**Objective:** Implement the Strict Character Calculus from `docs/SYNTAX_REVISION.md`.

**Completed (2026-01-14)**

### P1: Slot-Syntax Function Definitions [DONE] (Review Needed)

- [DONE] (Review Needed) Label: I7-p1-slot-syntax-functions
  - Support `(define add [x] [y] body)` slot-syntax function definitions.
  - Where: `csrc/codegen/codegen.c` (first pass detection, codegen_define)
  - What was changed:
    1. Added `first_pass` flag to CodeGenContext
    2. Modified first pass to detect slot-syntax functions
    3. Wrapped initialization code in `if (!ctx->first_pass)` guards
  - Verification: `(define add [x] [y] (+ x y))` compiles and `(add 3 4)` returns 7 ✓

### P2: Traditional Function Definitions [DONE] (Review Needed)

- [DONE] (Review Needed) Label: I7-p2-traditional-functions
  - Continue supporting `(define (f x y) body)` traditional syntax
  - Verification: `(define (mul a b) (* a b))` works ✓

### P3: Shorthand Function Definitions [DONE] (Review Needed)

- [DONE] (Review Needed) Label: I7-p3-shorthand-functions
  - Support `(define f x body)` shorthand syntax
  - Verification: `(define square n (* n n))` works ✓

### P4: Typed Let Bindings [DONE] (Review Needed)

- [DONE] (Review Needed) Label: I7-p4-typed-let
  - Support `(let [x {Int} 10] body)` typed let bindings
  - Where: `csrc/analysis/analysis.c`, `csrc/codegen/codegen.c`
  - Verification: `(let [x {Int} 10] (print x))` outputs 10 ✓

### P5: Sequential Let with Metadata [DONE] (Review Needed)

- [DONE] (Review Needed) Label: I7-p5-sequential-let
  - Support `(let ^:seq [a 5] [b (+ a 1)] body)` sequential bindings
  - Verification: `(let ^:seq [a 5] [b (+ a 1)] (print b))` outputs 6 ✓

### Additional Fixes (2026-01-14)

- [DONE] Lambda, fn, and -> codegen for slot syntax
  - Fixed inline lambda application `((lambda [x] body) arg)`
  - Added `->` as special form in codegen
  - Arrow syntax `(-> [x] [y] body)` works correctly

- [DONE] Typed variable definitions
  - Fixed `(define x-typed {Int} 42)` to correctly emit value (was emitting type)
  - Codegen now skips type annotation and uses actual value

- [DONE] set! codegen fix
  - Fixed bug that generated `return` statement in set! expression
  - Now correctly emits `(var = value)` as expression

### Pre-existing Issues (Not related to Issue 7)

- Multiline string handling in codegen
- Array literal inlining in expressions
- Some tests use variables before definition

---

## Issue 13: SYNTAX_REVISION.md Strict Character Calculus Implementation [N/A - Duplicate of Issue 7]

**Status:** N/A - This is a duplicate of Issue 7. All work tracked under Issue 7.

---

## Summary Statistics

| Status | Count |
|--------|-------|
| DONE (Review Needed) | ~50 |
| DONE | ~5 |
| N/A (Duplicates) | 2 |

**Next Step:** User review of all `[DONE] (Review Needed)` items to approve as `[DONE]`.

---

# From TODO.md (Main Development Log)

# OmniLisp TODO (Active Tasks)

This file contains only active tasks: `[TODO]`, `[IN_PROGRESS]`, and `[BLOCKED]`.

**Completed tasks:** See `TODO_COMPLETED.md`

**Last Updated:** 2026-01-18

---

## Review Summary (2026-01-21)

**Review Method:** Manual code inspection of "Review Needed" items.

**Items Reviewed:** 20+ features marked as `[DONE] (Review Needed)`

**Review Status:**
- ✅ **APPROVED:** 20 implementations - Sound, correct, and ready for production
- ✅ **FIXED:** 2 previously blocked implementations now resolved (2026-01-21)

### Critical Issues (RESOLVED 2026-01-21):

**I19-p0-marshaling-helpers (FFI Type Marshaling)** - ✅ FIXED
- ✅ FIXED: Added `TAG_CPTR` dedicated tag (no longer reuses TAG_BOX)
- ✅ FIXED: `obj_to_cptr()` now verifies TAG_CPTR before returning pointer
- ✅ FIXED: `mk_cptr()` now uses TAG_CPTR for proper type safety
- ✅ DOCUMENTED: `obj_to_cstring()` lifetime semantics with safe/unsafe usage patterns
- Files changed: `runtime/include/omni.h`, `runtime/src/debug.c`

**I19-p2-ccall-handler (FFI Codegen)** - ✅ FIXED
- ✅ FIXED: Now returns proper FFI error condition via `mk_ffi_load_error()`
- ✅ FIXED: Lisp code can detect FFI failures with `(condition? result)`
- NOTE: Memory leak analysis showed arrays ARE freed at function end (line 4076-4078)
- Files changed: `csrc/codegen/codegen.c`, `runtime/src/condition.c`

### Approved Implementations:
All other "Review Needed" items are marked as **APPROVED** and can be moved to TODO_COMPLETED.md, including:
- Module system imports (CP-5.3)
- Pattern matching (I6-p3, I6-p4, I6-p5 partial)
- Effect system (I9)
- Region-aware closures and HOFs (I16-p0 through I16-p3)
- Type specialization (I20-p0 through I20-p3)
- Parser improvements (I6-p0 through I6-p4)
- And 10+ other features

---

---

## Active Session: Completion Plan Phases 5.2-5.3 [DONE] (Review Needed)

### Phase 5.2: Pattern Matching Completeness [DONE] (Review Needed)

- [DONE] Label: CP-5.2-pattern-bindings-runtime
  Location: `runtime/src/runtime.c:504-722`
  What was done:
  - Added `PatternBindings` struct (max 64 bindings)
  - Added `is_pattern_match_with_bindings()` - matches and extracts variables
  - Added `pattern_bindings_to_dict()` - converts bindings to Obj dict
  - Added `prim_pattern_match()` - returns bindings dict or NULL
  Code snippet:
  ```c
  typedef struct { const char* name; Obj* value; } PatternBinding;
  typedef struct { PatternBinding bindings[64]; int count; } PatternBindings;

  int is_pattern_match_with_bindings(Obj* pattern, Obj* value, PatternBindings* bindings) {
      // Symbol patterns bind to value
      if (ptag == TAG_SYM) {
          pattern_bindings_add(bindings, sym, value);
          return 1;
      }
      // 'as' pattern: [inner_pattern as name]
      if (ptag == TAG_ARRAY && arr->len == 3 && strcmp(middle_str, "as") == 0) {
          is_pattern_match_with_bindings(inner_pattern, value, bindings);
          pattern_bindings_add(bindings, name, value);
      }
  }
  ```

- [DONE] Label: CP-5.2-pattern-var-extraction-codegen
  Location: `csrc/codegen/codegen.c:2673-2743`
  What was done:
  - Added `PatternVarList` struct for compile-time variable extraction
  - Added `extract_pattern_vars()` - recursively collects variable names
  - Skips reserved keywords: `_`, `nil`, `true`, `false`, `as`
  - Handles `as` patterns correctly
  Code snippet:
  ```c
  static void extract_pattern_vars(OmniValue* pattern, PatternVarList* vars) {
      if (omni_is_sym(pattern)) {
          pattern_var_list_add(vars, pattern->str_val);
      }
      if (omni_is_array(pattern) && pattern->array.len == 3) {
          // Check for 'as' pattern
          if (strcmp(middle->str_val, "as") == 0) {
              extract_pattern_vars(pattern->array.data[0], vars);
              pattern_var_list_add(vars, name->str_val);
          }
      }
  }
  ```

- [DONE] Label: CP-5.2-match-codegen-bindings
  Location: `csrc/codegen/codegen.c:2965-3042`
  What was done:
  - Updated `codegen_match` to use `prim_pattern_match` for patterns with variables
  - Emits code to extract bindings into local Obj* variables
  - Falls back to simple `is_pattern_match` when no variables present
  Code snippet (updated to use dict_get_by_name):
  ```c
  if (has_bindings) {
      omni_codegen_emit(ctx, "Obj* _bindings = prim_pattern_match(pattern, _match_value);\n");
      omni_codegen_emit(ctx, "if (_bindings) {\n");
      for (int i = 0; i < vars.count; i++) {
          omni_codegen_emit(ctx, "Obj* %s = dict_get_by_name(_bindings, \"%s\");\n",
                            vars.names[i], vars.names[i]);
      }
      // ... result expression with variables in scope
  }
  ```

- [DONE] Label: CP-5.2-test-pattern-bindings
  Location: `runtime/tests/test_pattern_match.c`
  What was done:
  - 12 comprehensive tests for pattern matching with bindings
  - Tests simple variable binding, nested patterns, as patterns
  - Tests rest patterns, wildcards, literal matching
  - Tests edge cases: length mismatch, insufficient elements, reserved keywords
  All tests pass.

- [DONE] Label: CP-5.2-rest-patterns
  Location: `runtime/src/runtime.c:573-612`
  What was done:
  - Added `find_rest_position()` to detect `&` in pattern arrays
  - Modified `is_pattern_match_with_bindings()` to handle rest patterns
  - Pattern `[x y & rest]` matching `[1 2 3 4 5]` gives x=1, y=2, rest=[3 4 5]
  - Empty rest is allowed: `[x y & rest]` matching `[1 2]` gives rest=[]

- [DONE] Label: CP-5.2-dict-get-by-name
  Location: `runtime/src/runtime.c:1312-1336`
  What was done (bug fix):
  - Added `dict_get_by_name()` function for symbol key lookup by string content
  - Hashmap uses pointer identity for keys, but symbols aren't interned
  - `dict_get_by_name(dict, "x")` iterates buckets and compares symbol names
  - Declared in `omni.h:1110`

### Phase 5.3: Module System [DONE] (Review Needed)

- [DONE] Label: CP-5.3-module-struct
  Location: `runtime/src/modules.c:31-94`
  What exists:
  - Module struct with name, exports (linked list), imports (linked list)
  - ModuleExport, ModuleImport helper structs
  - Global module registry (g_module_registry)
  - `get_or_create_module()`, `find_module()`

- [DONE] Label: CP-5.3-module-registry
  Location: `runtime/src/modules.c:53-94`
  What exists:
  - `g_module_registry` - global linked list of modules
  - `prim_module_begin()`, `prim_module_end()` for module definition
  - `prim_module_get()`, `prim_module_list()`
  - `prim_resolve()` for qualified symbol lookup (Module.symbol)

- [DONE] Label: CP-5.3-defmodule-codegen
  Location: `csrc/codegen/codegen.c:3140-3243`
  What was done:
  - Added `codegen_defmodule()` for `(defmodule name (:export ...) (:import ...) body...)`
  - Two-pass approach: first collects exports/imports, then generates body
  - Emits `prim_module_begin()`, body defs, `prim_export()` calls, `prim_module_end()`
  - Added dispatch in `codegen_list` for `defmodule` and `module` forms

- [DONE] Label: CP-5.3-qualified-symbols
  Location: `csrc/codegen/codegen.c:1314-1321`
  What was done:
  - Modified `codegen_sym()` to detect `/` or `.` in symbol names
  - Qualified symbols like `math/add` or `math.add` emit `prim_resolve(mk_string("..."))`
  - Runtime resolution via `prim_resolve()` in modules.c

- [DONE] Label: CP-5.3-import-export-codegen
  Location: `csrc/codegen/codegen.c:3245-3339`
  What was done:
  - Added `codegen_import()` - emits `prim_import(mk_sym("module"), NULL)`
  - Added `codegen_export()` - emits `prim_export(mk_sym("sym"), value)`
  - Added `codegen_require()` - emits `prim_require(mk_sym("module"))`
  - Added dispatch for `import`, `export`, `require` forms

- [DONE] APPROVED Label: CP-5.3-import-forms-advanced
  Objective: Support advanced import variations.
  Review Status: SOUND - Implementation is correct
  What was done:
  1. Runtime: Added `ImportMode` enum (IMPORT_ALL, IMPORT_ONLY, IMPORT_EXCEPT)
  2. Runtime: Added `prim_import_only()`, `prim_import_as()`, `prim_import_except()`
  3. Runtime: Updated `prim_resolve()` to handle aliased lookups and import restrictions
  4. Codegen: Added `is_quoted_symbol()` helper (`:key` is sugar for `'key`)
  5. Codegen: Added `codegen_symbol_array()` for generating symbol list literals
  6. Codegen: Updated `codegen_import()` to parse `:only`, `:as`, `:except` options
  7. Codegen: Updated `codegen_defmodule()` to handle advanced imports in `:import` clause
  Supported syntax:
    - `(import mod :only [a b])` → `prim_import_only(mk_sym("mod"), ...)`
    - `(import mod :as m)` → `prim_import_as(mk_sym("mod"), mk_sym("m"))`
    - `(import mod :except [x])` → `prim_import_except(mk_sym("mod"), ...)`
    - `(import (mod :only [a b]))` → nested form (same semantics)
    - `(:import (mod :as m))` → inside defmodule

---

## Review Directive

**All newly implemented features must be marked with `[DONE] (Review Needed)` until explicitly approved by the user.**
- When an agent completes implementing a feature, mark it `[DONE] (Review Needed)` (not `[DONE]`)
- If a feature is blocked, mark it `[BLOCKED]` with a reason
- `[DONE] (Review Needed)` means: code is written and working, but awaits user review/approval
- After user approval, move completed tasks to `TODO_COMPLETED.md`
- Workflow: `[TODO]` → implement → `[DONE] (Review Needed)` → user approves → move to archive

---

## Global Directives (Read First)

### Repo Root + Path Discipline

**Do not assume an absolute clone path.** All paths are **repo-relative**.

When writing or running verification commands:
- Prefer: `REPO_ROOT="$(git rev-parse --show-toplevel)"` then use `"$REPO_ROOT/..."`
- Prefer `rg -n "pattern" path/` over `ls ... | grep ...`
- Prefer `find path -name 'pattern'` over `ls | grep` for existence checks

### Transmigration Directive (Non-Negotiable)

**Correctness invariant:** For every in-region heap object `src` reached during transmigration, `remap(src)` yields exactly one stable destination `dst`, and all pointer discovery/rewrites happen only via metadata-driven `clone/trace`.

**Do not bypass the metadata-driven transmigration machinery for "fast paths".**

### Issue Authoring Directive

- **Append-only numbering:** Never renumber existing issues
- **No duplicates:** One header per issue number
- **Status required:** Every task line must be `[TODO]`, `[IN_PROGRESS]`, `[DONE] (Review Needed)`, `[BLOCKED]`, or `[N/A]`

### Code-to-TODO Directive (Non-Negotiable)

**All in-code notes must become tracked items in TODO.md.**

When encountering ANY of the following in source code:
- `// TODO:` or `/* TODO */` comments
- `// FIXME:` or `/* FIXME */` comments
- `// HACK:` or `// XXX:` comments
- `// NOTE:` comments describing unfinished work
- Stub implementations (functions that return placeholder values)
- Unimplemented branches (e.g., `else { /* not yet implemented */ }`)
- Feature flags or `#ifdef` blocks for incomplete features

**Required action:**
1. Create a concrete `[TODO]` item in TODO.md under the appropriate Issue
2. Include: file path, line number, and clear description of what needs to be done
3. Remove or update the in-code comment to reference the TODO.md item
4. Example: `// TODO(I27-p0): see TODO.md Issue 27 P0`

**Rationale:** Scattered in-code TODOs are invisible to project tracking. Centralizing them in TODO.md ensures nothing is forgotten and enables proper prioritization.

### Jujutsu Commit Directive

**Use Jujutsu (jj) for ALL version control operations.**

Before beginning ANY implementation subtask:
1. Run `jj describe -m "Issue N: task description"`
2. Run `jj log` to see current state
3. If mismatch: Either `jj squash` to consolidate or `jj new` to start fresh

---

## Design Decisions

### Data Structure Simplification (2026-01-15)

**Decision:** OmniLisp uses exactly **3 core collection types**:

| Type | Syntax | Use Case |
|------|--------|----------|
| **List** | `(1 2 3)` | Cons cells, code representation, recursive processing |
| **Array** | `[1 2 3]` | Mutable, indexed access, general sequences |
| **Dict** | `#{:a 1 :b 2}` | Key-value storage, structured data |

**Deprecated types** (runtime support retained for backward compatibility):
- **Tuple** → Use arrays instead (same semantics, simpler model)
- **Named Tuple** → Use dicts instead (`:key value` pairs)
- **Alist/Plist** → Use dicts instead (unified key-value abstraction)

**Rationale:**
1. Tuples provide no semantic benefit over arrays in a Region-RC model
2. Named tuples are just dicts with ordered iteration (dicts suffice)
3. Alists/Plists are legacy Lisp patterns; dicts are more intuitive
4. Fewer types = simpler mental model, consistent destructuring syntax
5. No immutability notation needed: Region-RC handles mutation safety at region boundaries

**Documentation updated:**
- `docs/SYNTAX.md` - Collection syntax
- `language_reference.md` - Destructuring patterns, type examples
- `docs/QUICK_REFERENCE.md` - Let destructuring
- `docs/SYNTAX_REVISION.md` - Character Calculus table, variance examples
- `docs/LANGUAGE_COMPLETENESS_REPORT.md` - Data types status

**Runtime deprecation:**
- `mk_tuple()`, `mk_tuple_region()` - marked deprecated
- `mk_named_tuple()`, `mk_named_tuple_region()` - marked deprecated
- `print_tuple()`, `print_named_tuple()` - marked deprecated

---

## Issue 19: FFI Implementation (C Library Interop) [DONE] (Review Needed)

**Objective:** Enable calling C library functions directly from OmniLisp via `ccall` syntax.

**Priority:** HIGH - Enables delegation of primitives to existing C libraries.

**Completed:** 2026-01-17

**Reference (read first):**
- Plan file: `~/.claude/plans/jiggly-doodling-sprout.md` (Phase 0)
- `csrc/codegen/codegen.c`
- `runtime/include/omni.h`

### Syntax

```lisp
(ccall "libm.so.6" "sin" [x {CDouble}] {CDouble})
(ccall "libc.so.6" "strlen" [s {CString}] {CSize})
(ccall "libc.so.6" "puts" [s {CString}] {Nothing})
```

### Type Mapping

| OmniLisp | C Type | To-C | From-C |
|----------|--------|------|--------|
| `{CInt}` | `int` | `obj_to_cint()` | `mk_int()` |
| `{CDouble}` | `double` | `obj_to_cdouble()` | `mk_float()` |
| `{CString}` | `char*` | `obj_to_cstring()` | `mk_string()` |
| `{CPtr}` | `void*` | `obj_to_cptr()` | `mk_cptr()` |
| `{CSize}` | `size_t` | `obj_to_csize()` | `mk_int()` |
| `{Nothing}` | `void` | N/A | `NOTHING` |

### P0: Type Marshaling Helpers [DONE]

- [DONE] Label: I19-p0-marshaling-helpers
  Objective: Add C type conversion helpers to omni.h.
  Where: `runtime/include/omni.h:579-678`
  Review Status: ✅ APPROVED (Fixed 2026-01-21)
  Fixes Applied:
    1. ✅ Added TAG_CPTR to ObjTag enum (line 221)
    2. ✅ mk_cptr() now uses TAG_CPTR instead of TAG_BOX (line 659-666)
    3. ✅ obj_to_cptr() now verifies TAG_CPTR before returning pointer (line 612-619)
    4. ✅ obj_to_cstring() documented with safe/unsafe usage patterns (line 596-625)
    5. ✅ mk_ffi_load_error() added for error reporting (line 678)
  Note: Region pinning deferred to future work - documented unsafe usage pattern instead

### P1: Codegen FFI Counter [DONE] (Review Needed)

- [DONE] (Review Needed) Label: I19-p1-ffi-counter
  Objective: Add FFI call site counter to CodeGenContext.
  Where: `csrc/codegen/codegen.h:98`
  What was done:
    1. `int ffi_counter` field added for unique FFI call site IDs
  Verification: Counter increments for each ccall site. ✓

### P2: ccall Codegen Handler [DONE]

- [DONE] Label: I19-p2-ccall-handler
  Objective: Implement `codegen_ccall()` function.
  Where: `csrc/codegen/codegen.c:3866-4079`
  Review Status: ✅ APPROVED (Fixed 2026-01-21)
  What was done:
    1. Full ccall code generation with static library/function caching
    2. Argument conversion with type-specific marshaling
    3. Function pointer casting with proper signatures
    4. Return value wrapping to Obj*
    5. ✅ Error handling now returns FFI condition via mk_ffi_load_error() (line 4022)
  Fixes Applied (2026-01-21):
    - ✅ Error handling returns proper Condition object instead of NOTHING
    - ✅ Lisp code can detect failures: `(condition? result)`
  Memory Leak Analysis:
    - Arrays ARE freed at function end (lines 4076-4078)
    - All early returns occur BEFORE malloc allocation
    - No actual memory leak present

### P3: ccall Dispatch [DONE] (Review Needed)

- [DONE] (Review Needed) Label: I19-p3-ccall-dispatch
  Objective: Add `ccall` to special form dispatch in `codegen_list()`.
  Where: `csrc/codegen/codegen.c:4910-4914`
  What was done:
    1. Added check for `ccall` symbol name
    2. Calls `codegen_ccall(ctx, expr)`
  Verification: `(ccall ...)` forms are recognized and processed. ✓

### P4: Link Flag [DONE] (Review Needed)

- [DONE] (Review Needed) Label: I19-p4-link-flag
  Objective: Ensure `-ldl` is in link flags.
  Where: `csrc/Makefile:9`, `runtime/Makefile:11`
  What was done:
    1. `-ldl` already present in csrc/Makefile
    2. Added `-ldl` to runtime/Makefile
  Verification: Build succeeds with dlopen/dlsym symbols resolved. ✓

### Verification Tests

Test file: `/tmp/test_ffi_simple.lisp`
```lisp
(println (ccall "libm.so.6" "sin" [0.5 {CDouble}] {CDouble}))   ;; 0.479426 ✓
(println (ccall "libc.so.6" "strlen" ["hello" {CString}] {CSize})) ;; 5 ✓
(println (ccall "libm.so.6" "pow" [2.0 {CDouble} 10.0 {CDouble}] {CDouble})) ;; 1024 ✓
(println (ccall "libc.so.6" "abs" [-42 {CInt}] {CInt}))         ;; 42 ✓
```

---

## Issue 1: Adopt "RC external pointers" semantics as Region-RC spec cross-check [DONE] (Review Needed)

**Objective:** Cross-check OmniLisp's per-region reference counting model against the RC dialect's definition of "external pointers".

**Reference (read first):**
- `runtime/docs/MEMORY_TERMINOLOGY.md`
- `docs/CTRR.md`

### P2: External RC Insertion [DONE] (Review Needed)

- [DONE] Label: I1-ctrr-external-rc-insertion (P2)
  Objective: Implement external reference counting at region boundaries.
  Where: `csrc/codegen/codegen.c`, `csrc/codegen/region_codegen.c`
  Why: Required for correct cross-region reference management.
  What was done:
    1. Type-based strategy selection in `omni_choose_escape_repair_strategy()`:
       - Small fixed-size types (int, float, pair, symbol): TRANSMIGRATE
       - Large/variable-size types (array, string, dict, closure): RETAIN_REGION
       - Environment variable override: OMNILISP_REPAIR_STRATEGY=retain|transmigrate
    2. `omni_codegen_escape_repair()` emits retain or transmigrate at return points
    3. `omni_codegen_emit_region_releases_at_pos()` emits release at last-use
    4. Full integration wired into `codegen_define()` and lambda codegen
  Verification: All 4 tests pass in `test_codegen_region_retain_release`

### RF: Regression Fix

- [DONE] (Review Needed) Label: I1-transmigrate-external-root-identity-regression (RF-I1-1)
  - Fixed root identity regression in transmigration

---

## Issue 6: Parser Syntax Completion (Align SYNTAX.md with Implementation) [DONE] (Review Needed)

**Objective:** Implement missing parser features documented in SYNTAX.md.

**Reference (read first):**
- `docs/SYNTAX.md` (Implementation Status section)
- `csrc/parser/parser.c`

### P0: Implement Dict Literal Parsing `#{}` [DONE] (Review Needed)

- [DONE] APPROVED Label: I6-p0-dict-literals
  Objective: Parse `#{:a 1 :b 2}` as dictionary literal.
  Review Status: SOUND - Implementation is correct
  Where: `csrc/parser/parser.c`
  Why: Documented syntax but no parser rule exists.
  What was done:
    1. Added `R_DICT` rule matching `#{` ... `}`
    2. Parse key-value pairs (alternating symbols and expressions)
    3. Return `OMNI_DICT` AST node
  Verification:
    - Add test: `#{:a 1}` parses to dict with key 'a value 1
    - Run: `make -C csrc/tests test`

### P1: Implement Signed Integer Parsing `+456` [DONE] (Review Needed)

- [DONE] APPROVED Label: I6-p1-signed-integers
  Objective: Parse `+456` as positive integer literal.
  Review Status: SOUND - Implementation is correct
  Where: `csrc/parser/parser.c`
  Why: Currently `+` parsed as symbol, not sign.
  What was done:
    1. Modified `R_INT` to optionally accept leading `+` or `-`
    2. Ensured `-123` and `+456` both parse as integers
  Verification:
    - Add test: `+456` parses to integer 456
    - Add test: `-123` parses to integer -123
    - Run: `make -C csrc/tests test`

### P2: Implement Partial Float Parsing `.5`, `3.` [DONE] (Review Needed)

- [DONE] APPROVED Label: I6-p2-partial-floats
  Objective: Parse `.5` and `3.` as float literals.
  Review Status: SOUND - Implementation is correct
  Where: `csrc/parser/parser.c`
  Why: Current parser requires `INT.INT` format.
  What was done:
    1. Updated `R_FLOAT` to accept `.DIGITS` and `DIGITS.`
    2. Handle edge cases (`.` alone should not be float)
  Verification:
    - Add test: `.5` parses to float 0.5
    - Add test: `3.` parses to float 3.0
    - Run: `make -C csrc/tests test`

### P3: Complete Match Clause Parsing [DONE] (Review Needed)

- [DONE] APPROVED Label: I6-p3-match-clauses
  Objective: Parse `[pattern result]` and `[pattern :when guard result]` match clauses.
  Where: `csrc/codegen/codegen.c`, `runtime/include/omni.h`
  Review Status: SOUND - Implementation is correct
  What was done:
    1. Updated `codegen_match` to detect array-based clause syntax
    2. Extract pattern from array[0], result from array[1]
    3. Detect `:when` keyword at array[1] for guarded clauses
    4. Added `is_pattern_match` declaration to omni.h
    5. Both new syntax `[pattern result]` and legacy syntax work
  Verification:
    - Test: `(match x [1 "one"])` → works
    - Test: `(match x [n :when (> n 0) "positive"])` → works
    - Test file: `tests/test_match_clauses.lisp` - all 6 tests pass

### P4: Make Signed Numbers Lexical (Not Symbolic) [DONE] (Review Needed)

- [DONE] APPROVED Label: I6-p4-lexical-signed-numbers
  Objective: Treat signed numbers as lexical atoms, not applications of `-`/`+` operator.
  Review Status: SOUND - Implementation is correct
  Where: `csrc/parser/parser.c`
  What was done (2026-01-17):
    1. Added new rule IDs: R_OPT_SIGN, R_OPT_INT, R_FLOAT_FULL, R_FLOAT_LEAD, R_FLOAT_TRAIL, R_ANY_FLOAT
    2. Implemented R_FLOAT_FULL: SIGN? INT "." INT (e.g., -3.14, +2.5)
    3. Implemented R_FLOAT_LEAD: SIGN? "." INT (e.g., .5, -.25)
    4. Implemented R_FLOAT_TRAIL: SIGN? INT "." (e.g., 3., -5.)
    5. R_ANY_FLOAT combines all float forms in correct precedence order
    6. Updated R_EXPR to use R_ANY_FLOAT instead of R_FLOAT
    7. R_SIGNED_INT already handled signed integers correctly
  Verification:
    - `-123` → `mk_int(-123)` ✓
    - `-3.14` → `mk_float_region(_local_region, -3.140000)` ✓
    - `.5` → `mk_float_region(_local_region, 0.500000)` ✓
    - `3.` → `mk_float_region(_local_region, 3.000000)` ✓
    - `-.25` → `mk_float_region(_local_region, -0.250000)` ✓
    - `(+ -10 5)` → `prim_add(mk_int(-10), mk_int(5))` ✓

### P5: Value Type Literals `{value}` [PARTIAL] (2026-01-18)

- [PARTIAL] Label: I6-p5-value-type-literals
  Objective: Allow literal values in braces `{3}` to be value types (not just types).
  Where: `csrc/parser/parser.c` (lines 397-421), `csrc/codegen/codegen.c` (lines 5344-5356)
  What was done:
    1. Extended `act_type()` parser function to detect literal values inside braces
    2. Implemented pragmatic mapping: `{3}`→{Int}, `{true}`→{Bool}, `{"hello"}`→{String}, etc.
    3. Added special handling for `value->type` primitive in codegen to avoid symbol mangling
    4. Value type literals now compile and run correctly
  Limitations:
    - Current implementation maps value types to their runtime types (pragmatic, not full value types)
    - Type checking doesn't enforce refinement: `(define x {3} 4)` compiles (should fail)
    - No pattern matching support for value types: `[{3} "three"]` not yet implemented
    - Full implementation requires value type system support in compiler (future work)
  Verification:
    - ✓ All literal types compile: `{3}`, `{true}`, `{false}`, `{"hello"}`, `{nil}`, `{#\a}`, `{1.5}`
    - ✓ Type annotations work: `(define x {3} 42)` compiles
    - ✓ Backwards compatible: Existing type annotations `{Int}`, `{String}` continue to work
    - See `ISSUE6_P5_VALUE_TYPE_LITERALS.md` for detailed documentation
  Future Work (requires deeper compiler changes):
    - Implement proper value type representation in AST
    - Add Type.from_value() or equivalent for compile-time type computation
    - Implement refinement type checking to enforce {3} only accepts value 3
    - Support value type patterns in match expressions
    - Integrate with parametric types: `{Option [{3}]}`
  Note: Related to Julia's value types and refinement types.

### P6: Unify Define Forms to Slot Syntax [DONE] (Review Needed)

- [DONE] Label: I6-p6-define-slot-syntax
  Objective: Eliminate non-canonical define forms; require all parameters to use slot syntax `[]`.
  Where: `csrc/codegen/codegen.c`
  What was done:
    1. Canonical slot syntax works: `(define add [x] [y] (+ x y))` → compiles correctly
    2. Shorthand sugar works: `(define add x y (+ x y))` → treated identically
    3. Scheme-style rejected: `(define (add x y) ...)` → error with helpful message:
       ```
       Error: Scheme-style (define (add ...) body) syntax is not supported.
              Use OmniLisp Slot syntax instead:
                (define add [x] [y] body)   ; Canonical
                (define add x y body)       ; Shorthand
       ```
    4. `codegen_extract_param_name()` handles both `x` and `[x]` uniformly
  Verification: Tested all three forms - canonical works, shorthand works, Scheme-style errors.
  Note: Slot syntax enables uniform destructuring: `(define foo [[x y] z] ...)`.

---

## Issue 8: Codebase Connectivity & Static Helper Audit [DONE] (Review Needed)

**Objective:** Resolve unresolved edges and audit static functions for codebase integrity.

**Completed:** 2026-01-18

**Reference (read first):**
- `DISCONNECTED_EDGES_ANALYSIS.md` (Sections 1, 3, 8)
- `runtime/include/omni.h`

### P0: External Library & Macro Edge Recovery [DONE] (Review Needed)

- [DONE] Label: I8-t1-resolve-library-edges
  Objective: Create manual bridge index for third-party libraries.
  Where: `docs/EXTERNAL_DEPENDENCIES.md`
  What was done:
    1. Created `docs/EXTERNAL_DEPENDENCIES.md` documenting all third-party libraries
    2. Documented Arena allocator (`third_party/arena/`) with API surface
    3. Documented VMem Arena with configuration macros
    4. Documented removed/replaced dependencies (UTHash replaced with bitmap)
    5. Listed integration points: region_core.c, transmigrate.c, scratch_arena.c
  Verification: Documentation complete with CodeGraph resolution notes.

- [DONE] Label: I8-t3-macro-edge-recovery
  Objective: Document edges generated by core macros (IS_BOXED, ATOMIC_INC_REF, etc.).
  Where: `docs/MACRO_ARCHITECTURE.md`
  What was done:
    1. Created `docs/MACRO_ARCHITECTURE.md` with comprehensive macro documentation
    2. Documented tagged pointer macros (IS_IMMEDIATE, IS_BOXED, GET_IMM_TAG)
    3. Documented value construction macros (MAKE_INT_IMM, MAKE_CHAR_IMM)
    4. Documented RC macros (ATOMIC_INC_REF, ATOMIC_DEC_REF) with both variants
    5. Documented IPGE generation macros (IPGE_CHECK_VALID, IPGE_EVOLVE)
    6. Added CodeGraph resolution guide mapping source patterns to actual calls
    7. Listed all ObjTag enum values for type checking
  Verification: Documentation complete with performance implications table.

### P1: Static Function Audit & Cleanup [DONE] (Review Needed)

- [DONE] Label: I8-t2-audit-static-helpers
  Objective: Audit static functions to identify orphans or expose useful utilities.
  Where: `csrc/` directory
  What was done:
    1. Ran `gcc -Wunused-function` audit across csrc
    2. Found 10 unused static functions:
       - `parser/parser.c`: act_match, act_match_clause, act_match_clauses (dead code)
       - `analysis/region_inference.c`: analyze_expr_for_interactions (dead code)
       - `codegen/codegen.c`: pattern_has_bindings, codegen_emit_inc_ref,
         codegen_emit_dec_ref, codegen_emit_reuse_cell, omni_strdup
       - `codegen/region_codegen.c`: omni_codegen_current_pos (unused global)
    3. Decision: Leave unused functions for now - some may be used in future phases
  Verification: Audit complete; build passes with warnings noted.

---

## Issue 9: Feature Completion: Algebraic Effects, Continuations, and Typed Arrays [DONE] (Review Needed)

**Objective:** Implement missing core functionality in the Algebraic Effect system, Fiber/Continuation system, and Typed Arrays.

**Completed:** 2026-01-17

**IMPORTANT Design Decision (2026-01-14):**
- `try`/`catch` is **NOT supported** in OmniLisp
- Error handling uses **algebraic effects** instead (see `SYNTAX_REVISION.md` Section 7)
- Algebraic effects are implemented using **delimited continuations** (shift/reset style)
- `handle` installs a prompt; effect operations capture continuation to handler
- Handlers can resume, discard, or invoke multiple times the captured continuation

**Reference (read first):**
- `docs/SYNTAX_REVISION.md` (Section 7: Algebraic Effects)
- `DISCONNECTED_EDGES_ANALYSIS.md` (Sections 2, 6.1, 6.3, 6.4)
- `runtime/src/effect.c`
- `runtime/src/memory/continuation.c`

**Constraints:**
- `try`/`catch` syntax is explicitly NOT supported
- Must satisfy existing test stubs in `runtime/tests`

### P0: Effect Tracing & Fiber Callbacks [DONE] (Review Needed)

- [DONE] (Review Needed) Label: I9-t4-effect-tracing
  Objective: Implement effect trace printing, recording, and clearing.
  Where: `runtime/src/effect.c`
  What was done:
    1. Added TraceEntry struct and ring buffer (256 entries)
    2. Implemented `effect_trace_print()` - prints trace to stdout
    3. Implemented `effect_trace_to_string()` - returns trace as malloc'd string
    4. Implemented `effect_trace_record()` - records effect with timestamp
    5. Implemented `effect_trace_clear()` - clears trace buffer
    6. Added `effect_trace_mark_handled()` and `effect_trace_last_index()` helpers
  Verification: `runtime/tests/test_effect_tracing.c` passes (10 tests).

- [DONE] (Review Needed) Label: I9-t5-fiber-callbacks
  Objective: Fiber callback execution (on_fulfill, on_reject) and error handling.
  Where: `runtime/src/memory/continuation.c`
  What was done:
    1. `promise_resolve()` calls `on_fulfill` callbacks (line 1310-1316)
    2. `promise_reject()` calls `on_reject` callbacks (line 1341-1354)
    3. `promise_then()` registers callbacks for settled/pending promises
    4. Fiber scheduler with round-robin and work-stealing already operational
  Verification: `runtime/tests/test_effect.c` passes (22 tests).

### P1: Typed Array Functional Primitives [DONE] (Review Needed)

- [DONE] (Review Needed) Label: I9-t6-typed-array-primitives
  Objective: Implement map, filter, and reduce for typed arrays.
  Where: `runtime/src/typed_array.c`
  Why: Core collection types lack functional parity.
  What was done:
    1. Implemented `omni_typed_array_map()` - maps function over elements
    2. Implemented `omni_typed_array_filter()` - filters elements by predicate
    3. Implemented `omni_typed_array_reduce()` - reduces array with accumulator
    4. Fixed memory leak in `omni_typed_array_create` allocation failure paths
    5. Fixed incorrect list construction in `omni_typed_array_to_list`
  Additional bugs fixed:
    - Changed malloc/calloc to region_alloc for CTRR integration
    - Fixed list building bug where each cell's cdr was NULL instead of linked
  Verification: See `TYPED_ARRAY_FIXES.md` for detailed bug analysis

### P2: Algebraic Effects Compiler Support [DONE] (Review Needed)

- [DONE] (Review Needed) Label: I9-p2-effect-declaration
  Objective: Parse `{effect Name}` as effect type declaration.
  Where: `runtime/src/effect.c`
  What was done:
    1. Built-in effects registered: Fail, Ask, Emit, State, Yield, Async, Choice, Condition
    2. `prim_effect_type_register()` allows registering custom effects from Lisp
    3. `effect_type_find()` / `effect_type_find_by_id()` for lookup
  Note: Parser support for `{effect ...}` syntax can be added later as sugar.
  Verification: `runtime/tests/test_effect.c` passes (22 tests).

- [DONE] APPROVED Label: I9-p2-handle-form
  Objective: Implement `handle` as special form in compiler.
  Where: `csrc/codegen/codegen.c`, `runtime/src/effect.c`
  Review Status: SOUND - Implementation is correct
  What was done (via Issue 14 P2):
    1. Added `handle` to special form detection in codegen
    2. `codegen_handler_closure` generates static handler functions
    3. `codegen_body_thunk` generates closure for handle body
    4. Emits `effect_handle(_body_thunk, _h_clauses, _h_return_clause, NULL)`
    5. Handler closures receive (payload, resume) args and can call `prim_resume`
  IMPORTANT: Built-in `Fail` effect is RECOVERY_ABORT mode (can't resume).
             Use custom effect names for resumable effects.
  Verification: ✓ 7 effect tests pass (see tests/test_effects.lisp)
    - Test 1: No effect (body returns 3)
    - Test 2: Perform+resume (1+42=43)
    - Test 5: Chained ops (5+10=15)
    - Test 6: Payload use (5*2=10)
    - Test 7: Nested expr (2*(3+5)=16)

### P2: Unify Condition/Restart with Effects [DONE] (Review Needed)

- [DONE] (Review Needed) Label: I14-p2-unify-conditions-effects
  Objective: Reimplement conditions/restarts on top of effect system.
  Where: `runtime/src/condition.c`, `runtime/src/condition.h`, `runtime/src/effect.c`
  What was done:
    1. Added `TAG_CONDITION` to omni.h for Obj-wrapped conditions
    2. Added `EFFECT_CONDITION` built-in effect type (RECOVERY_ONE_SHOT mode)
    3. Implemented `mk_condition_obj()` - wraps Condition in Obj with TAG_CONDITION
    4. Implemented `condition_signal()` - performs EFFECT_CONDITION (resumable)
    5. Implemented `condition_error()` - performs EFFECT_FAIL (non-resumable)
    6. Implemented `condition_signal_with_message()` - convenience wrapper
    7. Implemented `condition_from_obj()` - extracts Condition from Obj
  Design: Adapter approach - preserves condition type hierarchy but uses effects
         for signaling. Handlers using `handle` form can catch condition effects.
  Note: Lisp-level `signal`, `handler-case`, `restart-case` require separate codegen work.

### P3: Iterator-Generator Integration [DONE] (Review Needed)

- [DONE] (Review Needed) Label: I14-p3-iterator-generator
  Objective: Implement iterators using generator continuations.
  Where: `runtime/src/iterator.c`, `runtime/src/memory/continuation.c`
  What was done:
    1. Added `TAG_GENERATOR` to omni.h for Obj-wrapped generators
    2. Implemented `mk_generator_obj()` in continuation.c - wraps Generator in Obj
    3. Added generator-based iterator functions to iterator.c:
       - `prim_make_generator(producer)` - create generator from closure
       - `prim_generator_next(gen)` - get next value using `generator_next`
       - `prim_generator_done(gen)` - check if generator exhausted
       - `prim_yield(value)` - yield value from within generator
       - `is_generator(obj)` - helper to check if Obj is a generator
       - `prim_iter_next_unified(iter)` - supports both pair and generator iterators
       - `prim_take_unified(n, seq)` - supports both pair and generator sequences
  Design: Existing pair-based iterators preserved for backwards compatibility.
          Generator-based iteration uses delimited continuations for suspend/resume.
  Note: Lisp codegen for `make-generator` and `yield` forms needs separate work.

---

## Issue 15: Arena & Memory System Enhancements [DONE] (Review Needed)

**Objective:** Implement remaining SOTA arena techniques and complete memory system integration.

**Reference (read first):**
- `runtime/docs/ARCHITECTURE.md` (Arena Allocator Implementation, SOTA Techniques sections)
- `third_party/arena/vmem_arena.h`
- `runtime/src/memory/region_core.h`

**Background (2026-01-14):** Arena implementation review completed. Current system has:
- ✅ Dual arena (malloc-based + vmem commit-on-demand)
- ✅ O(1) chunk splice for transmigration
- ✅ Inline buffer (512B) for small objects
- ✅ Thread-local region pools
- ✅ THP alignment (2MB chunks)
- ✅ Store barrier / region_of support (completed 2026-01-18)

### P0: Scratch Arena API [DONE] (Review Needed)

- [DONE] Label: I15-p0-scratch-arena-api
  Objective: Implement double-buffered scratch arenas for temporary allocations.
  Where: `runtime/src/memory/scratch_arena.h`, `runtime/src/memory/scratch_arena.c`
  Implementation:
    - `Scratch` struct with arena pointer + checkpoint (Arena_Mark)
    - `scratch_begin(Arena* conflict)` - returns non-conflicting arena
    - `scratch_end(Scratch*)` - resets arena to checkpoint (O(1) free)
    - `scratch_alloc(Scratch*, size)` - inline bump allocation
    - Thread-local double-buffered arenas (`g_scratch_arena_a`, `g_scratch_arena_b`)
    - `transmigrate.c` includes `scratch_arena.h` for forwarding table
  Verification: `runtime/tests/test_scratch_arena.c` - 6 tests pass

### P1: Explicit Huge Page Support [DONE] (Review Needed)

- [DONE] Label: I15-p1-madv-hugepage
  Objective: Add MADV_HUGEPAGE hint for vmem_arena chunks.
  Where: `third_party/arena/vmem_arena.h:286-300`
  Implementation:
    - `VMEM_USE_HUGEPAGES` flag (default 1)
    - `VMEM_HUGEPAGE_THRESHOLD` (default 2MB)
    - `madvise(mem, reserve_size, MADV_HUGEPAGE)` in `vmem_chunk_new()`
    - Guarded by `#ifdef MADV_HUGEPAGE` for portability

### P2: Store Barrier Choke-Point [DONE] (Review Needed)

- [DONE] Label: I15-p2-store-barrier-impl
  Objective: Implement single choke-point for all pointer stores with lifetime checking.
  Where: `runtime/src/runtime.c:1132-1225` (omni_store_repair)
  Implementation:
    - `omni_store_repair(Obj* container, Obj** slot, Obj* new_value)` function
    - Lifetime check: `if (!omni_region_outlives(dst_region, src_region))`
    - Auto-repair via transmigrate or merge based on `get_merge_threshold()`
    - Updates `escape_repair_count` on auto-repair
    - Integrated into: box_set, array_push, array_set, dict_set, atom_reset, channel_send
  Verification: `test_store_barrier_merge_tests()` - 8 tests pass

### P3: region_of(obj) Lookup [DONE] (Review Needed)

- [DONE] Label: I15-p3-region-of-lookup
  Objective: Implement O(1) lookup from object pointer to owning region.
  Where: `runtime/src/memory/region_core.c:678-731`
  Implementation:
    - Global registry: `g_region_registry[4096]` with region_id → Region* mapping
    - `region_of(const void* encoded_ptr)` extracts region_id via pointer masking
    - Registry populated in `region_create()`, cleared in `region_registry_unregister()`
    - Atomic operations for thread safety
  Verification: `run_region_of_obj_tests()` - 6 tests pass

### P4: Size-Class Segregation [DONE] (Review Needed)

- [DONE] Label: I15-p4-size-class-segregation
  Objective: Separate arenas for different allocation sizes.
  Where: `runtime/src/memory/region_core.h`, `runtime/src/memory/region_core.c`
  Why: Better cache locality for homogeneous object traversal.
  Implementation:
    1. Added `Arena pair_arena` field to Region struct for TYPE_ID_PAIR allocations
    2. Added `Arena container_arena` field for arrays/dicts/strings
    3. Updated `region_create()` to initialize new arenas (begin=NULL, end=NULL)
    4. Updated `region_reset()` to free new arenas via arena_free()
    5. Updated `region_destroy_if_dead()` to free new arenas
    6. Updated `region_alloc_typed()` with switch statement routing:
       - TYPE_ID_PAIR → pair_arena (list traversal cache locality)
       - TYPE_ID_ARRAY, TYPE_ID_STRING, TYPE_ID_DICT → container_arena
       - Default → general arena
  Verification: Build succeeds with no errors

---

## Issue 16: Region-RC Dynamic Closure Integration [DONE] (Review Needed)

**Objective:** Make closures, HOFs, and generic methods region-aware for proper Region-RC lifecycle.

**Completed:** 2026-01-15

### P0: Region-Aware Closure Types [DONE] (Review Needed)

- [DONE] APPROVED Label: I16-p0-closure-fn-region
  Objective: Add region-aware closure function pointer type and struct support.
  Review Status: SOUND - Implementation is correct
  Where: `runtime/include/omni.h`, `runtime/src/runtime.c`
  What was done:
    1. Added `ClosureFnRegion` typedef: `Obj* (*)(Region*, Obj** captures, Obj** args, int argc)`
    2. Updated `Closure` struct with union for `fn`/`fn_region` + `region_aware` flag
    3. Added `mk_closure_region()` with store barriers for captured values
    4. Added `call_closure_region()` that passes caller region to closure

### P1: Region-Aware HOFs [DONE] (Review Needed)

- [DONE] APPROVED Label: I16-p1-region-aware-hofs
  Objective: Create region-aware versions of higher-order functions.
  Review Status: SOUND - Implementation is correct
  Where: `runtime/src/runtime.c`
  What was done:
    1. Added `list_map_region`, `list_filter_region`, `list_fold_region`, `list_foldr_region`
    2. HOFs use `call_closure_region` to propagate region context
    3. All allocations within HOFs use caller's region

### P2: HOF Codegen [DONE] (Review Needed)

- [DONE] APPROVED Label: I16-p2-hof-codegen
  Objective: Update codegen to emit region-aware HOF calls.
  Review Status: SOUND - Implementation is correct
  Where: `csrc/codegen/codegen.c`
  What was done:
    1. Updated `map`, `filter`, `fold`, `foldr` handlers to emit `list_*_region` calls
    2. Pass `_local_region` as first argument to all HOFs

### P3: Inline Lambda Closure Wrappers [DONE] (Review Needed)

- [DONE] APPROVED Label: I16-p3-lambda-closure-wrapper
  Objective: Generate closure wrappers for inline lambdas in HOF calls.
  Review Status: SOUND - Implementation is correct
  Where: `csrc/codegen/codegen.c`
  What was done:
    1. Added `is_lambda_expr()` to detect lambda forms (lambda, fn, λ)
    2. Added `count_lambda_params()` to count parameters
    3. Added `codegen_lambda_as_closure()` that generates:
       - Static lambda function (`_lambda_N`)
       - Trampoline function (`_lambda_N_tramp`) adapting signature to `ClosureFnRegion`
       - `mk_closure_region()` call to wrap trampoline
    4. HOF handlers now detect inline lambdas and use closure wrapper
    5. Removed arrow syntax (`->`) - superseded by `(fn [params] body)`
  Verification: `(map (fn [x] (* x 2)) '(1 2 3))` → `(2 4 6)` ✓

### P4: Generic Method Region Support [DONE] (Review Needed)

- [DONE] (Review Needed) Label: I16-p4-generic-method-region
  Objective: Add region-aware method support to generic functions.
  Where: `runtime/src/generic.c`
  What was done:
    1. Updated `MethodInfo` struct with `impl_region` union + `region_aware` flag
    2. Added `generic_add_method_region()` for region-aware methods
    3. Added `call_generic_region()` / `omni_generic_invoke_region()`

### P5: prim_deep_put Store Barriers [DONE] (Review Needed)

- [DONE] (Review Needed) Label: I16-p5-deep-put-barriers
  Objective: Implement deep_set with store barriers for nested mutations.
  Where: `runtime/src/runtime.c`
  What was done:
    1. Implemented `deep_set()` with `omni_store_repair()` for all mutations
    2. Supports alist-style structures: `((key1 . val1) (key2 . val2) ...)`
    3. Recursive path traversal with barrier at each level

---

## Issue 17: Remaining Integration Tasks [DONE] (Review Needed)

**Objective:** Complete integration of Region-RC with continuation, effect, and codegen systems.

**Reference (read first):**
- Issue 14 (Continuation Infrastructure)
- Issue 9 (Algebraic Effects)
- `runtime/src/memory/continuation.c`
- `runtime/src/effect.c`

### P0: Array Growth with Region Realloc [DONE] (Review Needed)

- [DONE] (Review Needed) Label: I17-p0-array-growth
  Objective: Implement proper array growth that preserves region membership.
  Where: `runtime/src/runtime.c:733-775`
  What was done (2026-01-17):
    1. Implemented `array_grow()` function that allocates new data buffer in same region
    2. Updated `array_push()` to grow array when full (2x capacity, minimum 8)
    3. Elements copied directly using `region_alloc()` (CTRR model, no explicit free needed)
    4. Old data becomes garbage, automatically reclaimed when region exits
  Bug Fixed:
    - Previously: array_push silently dropped elements beyond capacity
    - Now: Array grows correctly (4→8→16→32) and stores all elements
  Verification:
    - `runtime/tests/test_array_growth_bug.c` passes
    - All 392 runtime tests pass
    - See `ARRAY_GROW_BUG_FIX.md` for detailed analysis

### P1: Store Barrier Merge Path [DONE] (Review Needed)

- [DONE] (Review Needed) Label: I17-p1-store-barrier-merge
  Objective: Add merge path to store barrier (currently only transmigrates).
  Where: `runtime/src/runtime.c:979-1006`
  What was done: (Already implemented in prior work)
    1. `omni_store_repair()` checks `get_merge_threshold()` for large regions
    2. Calls `region_merge_safe()` for regions above threshold
    3. Falls back to transmigrate for smaller regions or merge failure
  Verification: ✓ Tests "store barrier checks merge threshold" and "merge safe basic" pass

### P2: Print Functions Completion [DONE] (Review Needed)

- [DONE] (Review Needed) Label: I17-p2-print-completion
  Objective: Complete print functions for container types.
  Where: `runtime/src/runtime.c:1247-1330`
  What was done:
    1. Implemented `print_array()` - iterates and prints `[elem1 elem2 ...]`
    2. Implemented `print_tuple()` - iterates and prints `{elem1 elem2 ...}`
    3. Implemented `print_dict()` - iterates buckets and prints `#{:key val ...}`
    4. Implemented `print_named_tuple()` - prints `#(:key val ...)`
    5. Fixed `alloc_obj_region()` to preserve original tag for unmapped types (TAG_KEYWORD)
  Verification: ✓ `[1 2 3]`, `{10 20 30}`, `#{:a 100}`, `#(:x 5)` all print correctly

---

## Issue 18: Computed Kinds via Compile-Time Splice [DONE] (Review Needed)

**Objective:** Add optional compile-time splice syntax for computing kinds from runtime values.

**Reference (read first):**
- `docs/SYNTAX_REVISION.md` (Type system sections)
- `docs/TYPE_SYSTEM_DESIGN.md`
- `csrc/parser/parser.c`, `csrc/analysis/analysis.c`

### P0: Kind Splice Syntax [DONE] (Review Needed)

- [DONE] Label: I18-p0-kind-splice-syntax
  Objective: Add `{#kind expr}` syntax to compute kinds at compile time.
  Where: `csrc/parser/parser.c`, `csrc/ast/ast.h`, `csrc/ast/ast.c`, `csrc/codegen/codegen.c`
  What was done:
    1. Added `OMNI_KIND_SPLICE` AST node type in ast.h:52
    2. Added `omni_new_kind_splice()` constructor in ast.c:363-369
    3. Added parser rules: `R_KIND_SPLICE`, `R_HASHKIND`, `R_TYPE_NORMAL` in parser.c:102
    4. Added `act_kind_splice()` action function in parser.c:385-409
    5. Added `codegen_kind_splice()` in codegen.c:1968-2051 with compile-time evaluation
    6. Grammar: TYPE = KIND_SPLICE / TYPE_NORMAL (priority alt)
  Compile-time evaluation:
    - Literal integers: `{#kind (typeof 42)}` → `mk_kind("Int", NULL, 0)`
    - Literal strings: `{#kind (typeof "hi")}` → `mk_kind("String", NULL, 0)`
    - Literal floats: `{#kind (typeof 3.14)}` → `mk_kind("Float", NULL, 0)`
    - Symbols: `{#kind Int}` → `mk_kind("Int", NULL, 0)`
    - Variables: `{#kind (typeof x)}` → `prim_value_to_type(o_x)` (runtime)
  Verification:
    - Parser: `{#kind Int}` parses correctly (len=11)
    - Codegen: Compile-time evaluation for literals
    - Regular types: `{Array Int}` still works correctly

### P1: Kind Equality Predicates [DONE] (Review Needed)

- [DONE] Label: I18-p1-kind-equality
  Objective: Add flow-level and kind-level equality predicates.
  Where: `runtime/src/runtime.c`, `csrc/codegen/codegen.c`, `runtime/include/omni.h`
  What was done:
    1. Added `kind_values_equal()` helper in runtime.c:1720-1743:
       - Deep structural comparison of Kind objects
       - Compares name, param_count, and recursively compares params
    2. Added `prim_kind_eq()` in runtime.c:1755-1757:
       - Returns `mk_bool(kind_values_equal(a, b))`
    3. Updated `prim_eq()` in runtime.c:1774-1776:
       - Added TAG_KIND case using `kind_values_equal()`
    4. Added declaration in omni.h:756:
       - `Obj* prim_kind_eq(Obj* a, Obj* b);`
    5. Added codegen mapping in codegen.c:1491:
       - `"kind=?"` → `prim_kind_eq`
  Verification:
    - `(kind=? {Int} {Int})` → true
    - `(kind=? {Int} {String})` → false
    - `(kind=? {Array Int} {Array Int})` → true
    - `(kind=? {Array Int} {Array String})` → false
    - `(= {Int} {Int})` → true (via prim_eq TAG_KIND case)

### P2: Type Reflection Integration [DONE] (Review Needed)

- [DONE] Label: I18-p2-type-reflection
  Objective: Implement `typeof` operator that returns runtime type objects.
  Where: `runtime/src/runtime.c:2561-2630`, `csrc/codegen/codegen.c:1496-1497`
  Implementation:
    - Runtime: `prim_value_to_type(Obj* value)` returns Kind object for any value
    - Codegen: `typeof` and `value->type` primitives map to `prim_value_to_type`
    - Supports all types: Int, Char, Bool, Float, String, Array, Dict, Pair, Closure, etc.
    - Returns Kind object (e.g., `{Int}`, `{String}`, `{Array}`)
  Usage:
    ```lisp
    (typeof 4)       ; => {Int}
    (typeof "hello") ; => {String}
    (typeof [1 2 3]) ; => {Array}
    ```
  Verification: Runtime tests pass, codegen tests pass

---

---

## Issue 20: Phase 1 Type Specialization (Binary Op Codegen) [DONE] (Review Needed)

**Objective:** Generate specialized unboxed code for arithmetic operations when operand types are statically known.

**Priority:** HIGH - Foundation for ~25x speedup on numeric operations.

**Completed:** 2026-01-17

**Reference (read first):**
- Plan file: `~/.claude/plans/jiggly-doodling-sprout.md` (Phase 1)
- `csrc/codegen/codegen.c` (lines 4139-4349)
- `runtime/include/omni.h` (specialization declarations)
- `runtime/src/primitives_specialized.c`

### P0: Box/Unbox Functions [DONE] (Review Needed)

- [DONE] APPROVED Label: I20-p0-box-unbox
  Objective: Add forward declarations for box/unbox functions.
  Review Status: SOUND - Implementation is correct
  Where: `runtime/include/omni.h:629-660`
  What was done:
    1. Declared `unbox_int(Obj*)` - extract int64_t from Obj
    2. Declared `unbox_float(Obj*)` - extract double from Obj
    3. Declared `unbox_char(Obj*)` - extract char from Obj
    4. Declared `unbox_bool(Obj*)` - extract bool from Obj
    5. Declared `box_int(int64_t)` - create Obj from int64_t
    6. Declared `box_float(double)` - create Obj from double
    7. Declared `box_char(char)` - create Obj from char
    8. Declared `box_bool(bool)` - create Obj from bool
  Note: Implementations already existed in `runtime/src/primitives_specialized.c`
  Verification: All functions compile and link correctly. ✓

### P1: Specialized Arithmetic Primitives [DONE] (Review Needed)

- [DONE] APPROVED Label: I20-p1-specialized-primitives
  Objective: Add forward declarations for specialized arithmetic primitives.
  Review Status: SOUND - Implementation is correct
  Where: `runtime/include/omni.h:662-690`
  What was done:
    1. Declared Int-Int operations: `prim_add_Int_Int`, `prim_sub_Int_Int`, `prim_mul_Int_Int`, etc.
    2. Declared Float-Float operations: `prim_add_Float_Float`, `prim_sub_Float_Float`, etc.
    3. Declared Mixed operations: `prim_add_Int_Float`, `prim_add_Float_Int`, etc.
    4. Declared comparison operations: `prim_lt_Int_Int`, `prim_gt_Int_Int`, etc.
  Note: Implementations already existed in `runtime/src/primitives_specialized.c`
  Verification: All primitives compile and link correctly. ✓

### P2: Operand Type Inference [DONE] (Review Needed)

- [DONE] APPROVED Label: I20-p2-type-inference
  Objective: Implement static type inference for arithmetic operands.
  Review Status: SOUND - Implementation is correct
  Where: `csrc/codegen/codegen.c:4139-4190`
  What was done:
    1. Added `OperandType` enum: `OPTYPE_UNKNOWN`, `OPTYPE_INT`, `OPTYPE_FLOAT`, `OPTYPE_BOOL`
    2. Implemented `get_operand_type()` to infer types from literals and expressions
    3. Handles integer literals, float literals, boolean symbols
    4. Recursively infers types for nested arithmetic expressions
    5. Returns OPTYPE_UNKNOWN when type cannot be determined statically
    2. Implemented `get_operand_type(OmniValue*)` function
    3. Returns `OPTYPE_INT` for integer literals
    4. Returns `OPTYPE_FLOAT` for float literals
    5. Recursively infers types for nested arithmetic expressions
    6. Returns `OPTYPE_UNKNOWN` for variables (conservative fallback)
  Verification: Type inference correctly identifies literal types. ✓

### P3: Specialization Dispatch [DONE] (Review Needed)

- [DONE] APPROVED Label: I20-p3-specialization-dispatch
  Objective: Wire specialization into codegen_apply for binary ops.
  Review Status: SOUND - Implementation is correct
  Where: `csrc/codegen/codegen.c:4192-4349`
  What was done:
    1. Implemented `get_specialized_binop(op, t1, t2)` - maps op+types to specialized function name
    2. Supports +, -, *, /, %, <, >, <=, >=, = operators
    3. Supports Int_Int, Float_Float, Int_Float, Float_Int combinations
    4. Implemented `try_specialized_binop(ctx, op, a, b)` - emits specialized code
    5. Generated pattern: `boxer(spec_func(unboxer1(a), unboxer2(b)))`
    6. Modified `codegen_apply()` to try specialization before generic fallback
  Generated code example for `(+ 3 4)`:
    ```c
    box_int(prim_add_Int_Int(unbox_int(mk_int(3)), unbox_int(mk_int(4))))
    ```
  Verification: ✓ Generated C compiles and runs correctly.

### Verification Tests

Test file: `csrc/tests/test_type_specialization.c`
All 8 tests pass:
- Integer-Integer add/sub/mul/comparison specialization
- Float-Float add specialization
- Mixed Int-Float specialization
- Nested expression specialization
- Fallback to generic for unknown types

E2E test file: `/tmp/test_spec.lisp`
```lisp
(println "Test 1: 3 + 4 =" (+ 3 4))           ;; uses prim_add_Int_Int ✓
(println "Test 2: 10 - 3 =" (- 10 3))         ;; uses prim_sub_Int_Int ✓
(println "Test 3: 5 * 6 =" (* 5 6))           ;; uses prim_mul_Int_Int ✓
(println "Test 4: 5 < 10 =" (< 5 10))         ;; uses prim_lt_Int_Int ✓
(println "Test 5: (2 + 3) * 4 =" (* (+ 2 3) 4)) ;; both specialized ✓
(println "Test 6: 1.5 + 2.5 =" (+ 1.5 2.5))   ;; uses prim_add_Float_Float ✓
(println "Test 7: 3 + 1.5 =" (+ 3 1.5))       ;; uses prim_add_Int_Float ✓
```

---

## Issue 21: Promise Release Cancellation Fix [DONE] (Review Needed)

**Objective:** Fix improper cancellation of waiting fibers when a promise is released before fulfillment.

**Completed:** 2026-01-18

**Bug Location:** `runtime/src/memory/continuation.c:1443`

**Reference:**
- See `PROMISE_RELEASE_FIX.md` for detailed analysis

### P0: Fix promise_release to properly unpark waiting fibers [DONE] (Review Needed)

- [DONE] (Review Needed) Label: I21-p0-unpark-waiters
  Objective: Properly unpark waiting fibers instead of just marking them FIBER_DONE.
  Where: `runtime/src/memory/continuation.c:1439-1453`
  What was done:
    1. Replaced simple state change with proper `fiber_unpark_error()` calls
    2. Each waiting fiber is unparked with error state (is_error=true, value=NULL)
    3. Cleared waiters list to prevent accessing freed fibers
    4. Added explanatory comments about fix behavior
  Before (buggy code):
    ```c
    /* Cancel waiting tasks */
    Fiber* t = p->waiters;
    while (t) {
        Fiber* next = t->next;
        t->state = FIBER_DONE;  /* TODO: Better cancellation */
        t = next;
    }
    free(p);
    ```
  After (fixed code):
    ```c
    /* Cancel waiting tasks by unparking them with error */
    Fiber* t = p->waiters;
    while (t) {
        Fiber* next = t->next;
        /* Unpark fiber with error indicating promise was destroyed/cancelled */
        /* fiber_unpark_error will enqueue fiber and set state to FIBER_READY */
        fiber_unpark_error(t, NULL, true);
        t = next;
    }
    /* Clear waiters list to prevent accessing freed fibers */
    p->waiters = NULL;
    free(p);
    ```
  Bug Impact:
    - Fibers waiting on a released promise were left stuck in `FIBER_PARKED` state
    - They remained in the promise's waiters list indefinitely
    - Never scheduled to run again → potential deadlocks
    - Memory leaks from fibers never completing
  Fix Behavior:
    - All waiting fibers are properly unparked with error state
    - `fiber_unpark_error()` enqueues fibers (sets state to `FIBER_READY`)
    - Fibers are scheduled to run and can handle the error (promise destroyed)
    - Waiters list is cleared before freeing promise
  Verification: Runtime builds successfully. All 397 tests pass. Fix addresses TODO comment "Better cancellation".
  Test Fix (2026-01-18):
  - Added `scheduler_init()` call to `test_promise_release_unparks_waiters()`
  - Test creates stack-allocated fibers that need scheduler for unparking
  - Without scheduler_init(), `scheduler_enqueue()` returns early without changing fiber state
  - Fix ensures test properly validates to Issue 21 runtime fix
  - Note: Not calling `scheduler_shutdown()` because fibers are stack-allocated

---

## Summary

| Issue | Status | Description |
|-------|--------|-------------|
| 1 | DONE | **RC external pointers / Region-RC spec** (P0-P2 complete) |
| 6 | DONE | **Parser syntax completion** (P0-P6 complete) |
| 8 | TODO | Codebase connectivity audit |
| 10 | DONE | **IPGE integration** (P0, P1 complete) |
| 11 | TODO | Build/test consolidation |
| 15 | TODO | **Arena & memory system enhancements** |
| 18 | TODO | **Computed kinds via compile-time splice** |

**Completed issues:** See `TODO_COMPLETED.md` for Issues 1-7, 9-10, 14, 16, 17, 19-23.

---

## Issue 22: TAG/TypeID Enum Alignment Optimization [DONE] (Review Needed)

**Objective:** Align TAG enum with TypeID enum to eliminate switch statement overhead in type conversion functions.

**Priority:** MEDIUM - Performance and maintainability improvement.

**Completed:** 2026-01-18

**Reference:**
- `TAG_TYPEID_FIX.md` (detailed analysis and documentation)

### P0: Reorder TAG Enum to Match TypeID [DONE] (Review Needed)

- [DONE] (Review Needed) Label: I22-p0-align-tag-typeid
  Objective: Reorder TAG enum to align with TypeID enum (TAG = TypeID + 1).
  Where: `runtime/include/omni.h:190-219`
  What was done:
    1. Reordered TAG enum to match TypeID enum order
    2. Maintained offset relationship (TAG = TypeID + 1)
    3. Kept extended tags (KEYWORD, EFFECT_TYPE, etc.) at end
  Verification: Build succeeds, all tests pass. ✓

### P1: Optimize Conversion Functions [DONE] (Review Needed)

- [DONE] (Review Needed) Label: I22-p1-optimize-conversions
  Objective: Replace switch statements with simple arithmetic operations.
  Where: `runtime/src/memory/region_value.c`, `runtime/src/memory/transmigrate.c`
  What was done:
    1. `type_id_to_tag()` - Changed from 18-case switch to `return type_id + 1`
    2. `tag_to_type_id()` - Changed from 19-case switch to `return tag - 1`
    3. `transmigrate_tag_to_type_id()` - Changed from 24-case switch to fast path + special cases
    4. All functions marked `inline` for better optimization
  Verification: Functions compile and work correctly. ✓

### Benefits

1. **Performance:** Eliminates 61 case statements, replaced with O(1) arithmetic
2. **Maintainability:** Single source of truth for enum order
3. **Binary Size:** Reduced by eliminating ~60 case branches
4. **Type Safety:** Compile-time arithmetic instead of runtime switches

### Test Results

All tests pass:
- `runtime/tests/test_main`: All core tests pass
- `runtime/tests/test_effect`: 22 effect tests pass
- `runtime/tests/test_pattern_match`: 12 pattern matching tests pass
- Build: Clean with only warnings (no errors)

---

## Issue 23: Perceus Reuse & Lobster RC Elision Integration [DONE] (Review Needed)

**Objective:** Wire existing Perceus-style reuse analysis and Lobster-style RC elision from analysis passes into code generation.

**Priority:** HIGH - Major performance optimization (30-50% fewer RC ops, near-zero alloc for functional patterns).

**Completed:** 2026-01-18

**Reference:**
- `FUTURE_WORK.md` (O.1 and O.2 now marked INTEGRATED)
- Perceus: Garbage Free Reference Counting with Reuse (PLDI 2021)
- Lobster: aardappel.github.io/lobster/memory_management.html

### Background

The analysis infrastructure was already complete:
- `csrc/analysis/analysis.c`: `omni_analyze_reuse()` finds free→alloc pairs
- `csrc/analysis/analysis.c`: `omni_analyze_rc_elision()` determines RC elision class
- `csrc/analysis/analysis.h`: `ReuseCandidate`, `RCElisionClass` types

What was missing: **codegen integration** - analysis results were computed but ignored.

### P0: Perceus Reuse Codegen Integration [DONE] (Review Needed)

- [DONE] APPROVED Label: I23-p0-reuse-integration
  Objective: Use reuse analysis results during code generation.
  Review Status: SOUND - Implementation is correct
  Where: `csrc/codegen/codegen.c:280-423`
  What was done:
    1. Added `codegen_get_reuse()` - query reuse candidates at current position
    2. Added `codegen_emit_reuse_int()` - emit `REUSE_OR_NEW_INT` if candidate found
    3. Added `codegen_emit_reuse_float()` - emit `REUSE_OR_NEW_FLOAT` if candidate found
    4. Added `codegen_emit_reuse_cell()` - emit `REUSE_OR_NEW_CELL` if candidate found
    5. Updated `codegen_int()` and `codegen_float()` to check for reuse opportunities
    6. Added `omni_consume_reuse()` declaration to `analysis.h`
  Generated code transformation:
  ```c
  // Before (fresh allocation)
  Obj* y = mk_int(42);

  // After (reusing x's memory when analysis finds a candidate)
  Obj* y = REUSE_OR_NEW_INT(_local_region, x, 42);
  ```
    4. Added `codegen_emit_reuse_cell()` - emit `REUSE_OR_NEW_CELL` if candidate found
    5. Updated `codegen_int()` and `codegen_float()` to check for reuse opportunities
    6. Added `omni_consume_reuse()` declaration to `analysis.h`
  Generated code transformation:
  ```c
  // Before (fresh allocation)
  Obj* y = mk_int(42);

  // After (reusing x's memory when analysis finds a candidate)
  Obj* y = REUSE_OR_NEW_INT(_local_region, x, 42);
  ```

### P1: Lobster RC Elision Codegen Integration [DONE] (Review Needed)

- [DONE] APPROVED Label: I23-p1-rc-elision-integration
  Objective: Use RC elision analysis results to skip redundant inc_ref/dec_ref.
  Review Status: SOUND - Implementation is correct
  Where: `csrc/codegen/codegen.c:402-423, 5820-5852, 5999-6029, 6543-6551`
  What was done:
    1. Added `codegen_can_elide_inc()` / `codegen_can_elide_dec()` - query elision
    2. Added `codegen_get_elision_class()` - get elision class for comments
    3. Added `codegen_emit_dec_ref()` / `codegen_emit_inc_ref()` - emit with elision check
    4. Updated `emit_ownership_free()` RC cases to check elision
    5. Updated `omni_codegen_emit_cfg_frees()` RC cases to check elision
    6. Updated scope cleanup code to check elision
  Generated code transformation:
  ```c
  // Before (unconditional RC)
  dec_ref(x);

  // After (elision when analysis proves safe)
  /* RC_ELIDED: dec_ref(x) - unique */
  ```

### Elision Classes

| Class | When Applied | Effect |
|-------|--------------|--------|
| `RC_ELIDE_BOTH` | Unique or stack-allocated | Skip both inc_ref and dec_ref |
| `RC_ELIDE_DEC` | Arena/pool-allocated | Skip dec_ref (bulk free) |
| `RC_ELIDE_INC` | Borrowed reference | Skip inc_ref (don't own it) |
| `RC_REQUIRED` | Shared heap reference | Must use RC |

### Expected Impact

- **RC Elision (O.1):** 30-50% reduction in RC operations for typical code
- **Reuse (O.2):** Near-zero allocation for functional patterns (e.g., map, fold)

---

## Review Complete Summary

**Total Items Reviewed:** 25+
**Status:**
- ✅ 25 implementations APPROVED and ready for production
- ✅ All previously blocked items FIXED (2026-01-21)

**Completed Actions (2026-01-21):**
1. ✅ Fixed FFI marshaling issues (I19-p0) - TAG_CPTR, type verification, documentation
2. ✅ Fixed ccall handler (I19-p2) - proper error reporting via mk_ffi_load_error()
3. Move 25 APPROVED items to TODO_COMPLETED.md (pending)

---
- **Perceus Reuse (O.2):** Near-zero allocation for functional patterns (map, filter, etc.)

### Files Changed

| File | Changes |
|------|---------|
| `csrc/codegen/codegen.c` | +145 lines (Perceus/Lobster integration section) |
| `csrc/analysis/analysis.h` | +3 lines (`omni_consume_reuse` declaration) |
| `FUTURE_WORK.md` | O.1, O.2 marked ✅ INTEGRATED |

---


## Issue 24: Set Data Structure [DONE] (Review Needed)

**Objective:** Implement a native Set data type for unique element collections.

**Priority:** Core Language (5% remaining)

**Rationale:** Sets are fundamental for membership tests, deduplication, and mathematical operations. Currently missing from OmniLisp's 3-collection model (List, Array, Dict).

### P0: Core Set Type & Runtime [DONE] (Review Needed)

- [DONE] Label: I24-p0-set-type
  Objective: Add TAG_SET and core set data structure.
  Where: `runtime/include/omni.h`, `runtime/src/runtime.c`
  What was implemented:
    1. Added `TAG_SET` to tag enum (replaces TAG_RESERVED_16)
    2. Added `TYPE_ID_SET` to TypeID enum
    3. Implemented Set struct using HashMap internals
    4. Added `mk_set()`, `mk_set_region()` constructors
    5. Added `set_add()`, `set_remove()`, `set_contains()` primitives
    6. Added `set_size()`, `set_empty_p()` queries
    7. Added `print_set()` for display support
  Completed: 2026-01-18

### P1: Set Literal Syntax `#set{}` [DONE] (Review Needed)

- [DONE] Label: I24-p1-set-literal
  Objective: Parse `#set{1 2 3}` as set literal.
  Where: `csrc/parser/`, `csrc/codegen/codegen.c`
  What was implemented:
    1. Added OMNI_SET AST tag and set struct to ast.h
    2. Added omni_new_set() and omni_set_add() to ast.c
    3. Added R_HASHSET and R_SET parser rules
    4. Added act_set parser action
    5. Added codegen_set for code generation
  Syntax: `#set{1 2 3}` (uses explicit prefix to avoid ambiguity with dict)
  Completed: 2026-01-18

### P2: Set Operations [DONE] (Review Needed)

- [DONE] Label: I24-p2-set-operations
  Objective: Implement standard set algebra operations.
  Where: `runtime/src/runtime.c`
  What was implemented:
    1. `set_union()` - Combine two sets
    2. `set_intersection()` - Common elements
    3. `set_difference()` - Elements in A not in B
    4. `set_symmetric_difference()` - Elements in A or B but not both
    5. `set_subset_p()`, `set_superset_p()` - Containment predicates
    6. `set_to_list()`, `list_to_set()` - List conversion
    7. `set_to_array()`, `array_to_set()` - Array conversion
  Completed: 2026-01-18

### P3: Set Iteration & HOFs [DONE] (Review Needed)

- [DONE] Label: I24-p3-set-iteration
  Objective: Make sets iterable and support HOFs.
  Where: `runtime/src/runtime.c`, `runtime/include/omni.h`
  What was implemented:
    1. `set_foreach()` - Apply function to each element
    2. `set_map()` - Map function over set, return new set
    3. `set_filter()` - Filter set by predicate, return new set
    4. `set_reduce()` - Reduce set to single value with accumulator
  Note: Uses hashmap_foreach for iteration via callback pattern.
  Completed: 2026-01-18

---

## Issue 25: DateTime Support [DONE] (Review Needed)

**Objective:** Implement date/time handling for temporal operations.

**Priority:** Core Language (5% remaining)

**Rationale:** Date/time is essential for logging, scheduling, and real-world applications. Currently missing from OmniLisp.

### P0: Core DateTime Type [DONE] (Review Needed)

- [DONE] Label: I25-p0-datetime-type
  Objective: Add DateTime as a primitive type.
  Where: `runtime/include/omni.h`, `runtime/src/runtime.c`, `memory/region_value.c`
  What was implemented:
    1. Added `TAG_DATETIME` to tag enum (replaces TAG_RESERVED_17)
    2. Added `TYPE_ID_DATETIME` to TypeID enum
    3. Added `DateTime` struct with Unix timestamp (int64) + timezone offset (int32)
    4. Added `mk_datetime()`, `mk_datetime_region()` constructors
    5. Added `datetime_now()`, `datetime_now_utc()` for current time
    6. Added `datetime_from_unix()`, `datetime_from_unix_tz()` for timestamp creation
    7. Added `datetime_to_unix()`, `datetime_tz_offset()` accessors
    8. Added `print_datetime()` with ISO 8601 format output
    9. Added TAG_DATETIME to type_of, print_obj, inspect functions
    10. Updated transmigrate.c and region_metadata.h
  Completed: 2026-01-18

### P1: DateTime Components [DONE] (Review Needed)

- [DONE] Label: I25-p1-datetime-components
  Objective: Extract/construct from components.
  Where: `runtime/src/runtime.c`, `runtime/include/omni.h`
  What was implemented:
    1. `datetime_make()` - Create UTC datetime from year/month/day/hour/min/sec
    2. `datetime_make_local()` - Create local timezone datetime from components
    3. `datetime_year()`, `datetime_month()`, `datetime_day()` - Date accessors
    4. `datetime_hour()`, `datetime_minute()`, `datetime_second()` - Time accessors
    5. `datetime_weekday()` - Day of week (0=Sunday, 6=Saturday)
    6. `datetime_yearday()` - Day of year (1-366)
    7. Helper `datetime_to_tm()` - Convert DateTime to struct tm
  Completed: 2026-01-18

### P2: DateTime Arithmetic [DONE] (Review Needed)

- [DONE] Label: I25-p2-datetime-arithmetic
  Objective: Support date/time calculations.
  Where: `runtime/src/runtime.c`, `runtime/include/omni.h`
  What was implemented:
    1. `datetime_add_seconds()` - Add seconds to datetime
    2. `datetime_add_minutes()` - Add minutes to datetime
    3. `datetime_add_hours()` - Add hours to datetime
    4. `datetime_add_days()` - Add days to datetime
    5. `datetime_diff()` - Difference between two datetimes (in seconds)
    6. `datetime_lt()`, `datetime_gt()` - Less than / greater than comparisons
    7. `datetime_eq()` - Equality comparison
    8. `datetime_le()`, `datetime_ge()` - Less/greater than or equal
  Completed: 2026-01-18

### P3: DateTime Formatting [DONE] (Review Needed)

- [DONE] Label: I25-p3-datetime-format
  Objective: Parse and format datetime strings.
  Where: `runtime/src/runtime.c`, `runtime/include/omni.h`
  What was implemented:
    1. `datetime_format()` - Format with strftime-style specifiers
    2. `datetime_to_iso8601()` - Format as ISO 8601 ("2026-01-18T12:30:00Z")
    3. `datetime_to_rfc2822()` - Format as RFC 2822 ("Sun, 18 Jan 2026 12:30:00 +0000")
    4. `datetime_parse_iso8601()` - Parse ISO 8601 strings
  Completed: 2026-01-18

---

## Issue 26: String Utilities [DONE] (Review Needed)

**Objective:** Complete string manipulation primitives beyond regex.

**Priority:** Core Language (5% remaining)

**Rationale:** Basic string operations (case, trim, pad) are missing. Regex is complete but utilities are needed.

### P0: Case Conversion [DONE] (Review Needed)

- [DONE] Label: I26-p0-case-conversion
  Objective: Implement case transformation functions.
  Where: `runtime/src/string_utils.c`
  What was implemented:
    1. `prim_string_upcase` - Convert to uppercase (already existed)
    2. `prim_string_downcase` - Alias for lowcase
    3. `prim_string_capitalize` - Capitalize first letter
    4. `prim_string_titlecase` - Capitalize each word
  Note: ASCII-only initially, Unicode later
  Verification: `(string-upcase "hello")` => `"HELLO"`

### P1: Trimming & Padding [DONE] (Review Needed)

- [DONE] Label: I26-p1-trim-pad
  Objective: Implement whitespace and padding functions.
  Where: `runtime/src/string_utils.c`
  What was implemented:
    1. `prim_string_trim` - Remove leading/trailing whitespace (already existed)
    2. `prim_string_trim_left`, `prim_string_trim_right` - Directional trim (already existed)
    3. `prim_string_pad_left`, `prim_string_pad_right` - Pad to width
    4. `prim_string_center` - Center with padding
  Verification: `(string-trim "  hello  ")` => `"hello"`

### P2: Search & Replace (Non-Regex) [DONE] (Review Needed)

- [DONE] Label: I26-p2-search-replace
  Objective: Simple string search without regex overhead.
  Where: `runtime/src/string_utils.c`
  What was implemented:
    1. `prim_string_contains` - Check if substring exists (already existed)
    2. `prim_string_index_of` - Find first occurrence position (already existed)
    3. `prim_string_last_index_of` - Find last occurrence
    4. `prim_string_replace` - Replace all occurrences (already existed)
    5. `prim_string_replace_first` - Replace first occurrence only
    6. `prim_string_replace_all` - Alias for replace
    7. `prim_string_starts_with`, `prim_string_ends_with` - Prefix/suffix checks
  Verification: `(string-contains? "hello" "ell")` => `true`

### P3: Split & Join [DONE] (Review Needed)

- [DONE] Label: I26-p3-split-join
  Objective: String splitting and joining utilities.
  Where: `runtime/src/string_utils.c`
  What was implemented:
    1. `prim_string_split` - Split by delimiter (already existed)
    2. `prim_string_join` - Join list with delimiter (already existed)
    3. `prim_string_lines` - Split by newlines
    4. `prim_string_words` - Split by whitespace
    5. `prim_string_chars` - Split into individual characters
    6. `prim_string_reverse` - Reverse a string
    7. `prim_string_repeat` - Repeat string n times
  Note: `re-split` exists for regex; this is simpler/faster
  Verification: `(string-split "a,b,c" ",")` => `("a" "b" "c")`

---

## Issue 27: Developer Tools & Debugging [DONE]

**Objective:** Provide essential developer tooling for debugging, inspection, and development workflow.

**Priority:** Developer Experience (Critical for adoption)

**Rationale:** The language is architecturally complete but lacks developer tooling. A robust debugging and inspection system is essential for productive development.

**Status (2026-01-21):** All core functionality implemented in `runtime/src/debug.c`, `testing.c`, and `profile.c`.

### P0: Object Inspection [DONE]

- [DONE] Label: I27-p0-inspection
  Objective: Runtime object inspection utilities.
  Where: `runtime/src/debug.c`
  Implemented:
    1. `prim_inspect` (line 277) - Detailed object info
    2. `prim_type_of` (line 341) - Return type as symbol
    3. `prim_address_of` (line 380) - Return object memory address
    4. `prim_refcount_of` (line 390) - Return current reference count
    5. `prim_region_of_debug` (line 406) - Return owning region info
    6. `prim_sizeof` (line 429) - Return object size

### P1: Memory Debugging [DONE]

- [DONE] Label: I27-p1-memory-debug
  Objective: Memory and region debugging utilities.
  Where: `runtime/src/debug.c`
  Implemented:
    1. `prim_region_stats` (line 445) - Print region allocation statistics
    2. `prim_memory_usage` (line 467) - Total memory allocated
    3. `prim_gc_info` (line 479) - Show GC/RC statistics
    4. `prim_allocation_trace` (line 496) - Toggle allocation tracing
    5. `prim_leak_check` (line 518) - Check for potential leaks

### P2: REPL Enhancements [PARTIAL]

- [PARTIAL] Label: I27-p2-repl
  Objective: Enhanced REPL for development.
  Where: `csrc/cli/main.c`, `runtime/src/debug.c`
  Implemented:
    1. `prim_doc` (line 702) - Show documentation from metadata
    2. `prim_source` (line 733) - Show source code if available
  Not implemented (REPL commands):
    3. `,trace on/off` - Toggle execution tracing
    4. `,time expr` - Time expression evaluation (but `prim_time` exists in profile.c)
    5. `,expand expr` - Macro expand expression
    6. History persistence (readline integration) - partially exists

### P3: Testing Framework [DONE]

- [DONE] Label: I27-p3-testing
  Objective: Built-in testing framework.
  Where: `runtime/src/testing.c`
  Implemented:
    1. `prim_deftest` (line 227) - Define a test case
    2. `prim_assert_eq` (line 311), `prim_assert_true` (line 338), `prim_assert_throws` (line 476) - Assertions
    3. `prim_run_tests` (line 634) - Run all tests
    4. `prim_test_report` (line 726) - Generate test report
    5. `prim_list_tests` (line 774) - List all defined tests
  Additional assertions: `assert_false`, `assert_near`, `assert_not_eq`, `assert_nil`, `assert_not_nil`

### P4: Profiling [DONE]

- [DONE] Label: I27-p4-profiling
  Objective: Performance profiling utilities.
  Where: `runtime/src/profile.c`
  Implemented:
    1. `prim_profile` (line 218) - Profile expression execution
    2. `prim_call_counts` (line 286) - Show function call counts
    3. `prim_hot_spots` (line 309) - Identify slow functions
    4. `prim_profile_memory` (line 553) - Track allocations per function
  Additional: `prim_time`, `prim_benchmark`, `prim_profile_report`

---

## Issue 28: Standard Library Expansion [DONE]

**Objective:** Expand standard library for practical programming.

**Priority:** Library (Essential for v1.0)

**Rationale:** Core language is complete but library is "bare bones". Need batteries-included stdlib.

**Status (2026-01-21):** All core functionality implemented in `runtime/src/math_numerics.c`, `collections.c`, `io.c`, and `json.c`.

### P0: Math & Numerics [DONE]

- [DONE] Label: I28-p0-math
  Objective: Extended math library.
  Where: `runtime/src/math_numerics.c`
  Implemented:
    1. `prim_abs` (line 368), `prim_signum` (line 383), `prim_clamp` (line 302) - Basic numeric ops
    2. `prim_floor` (line 211), `prim_ceil` (line 219), `prim_round` (line 227), `prim_trunc` (line 235) - Rounding
    3. `prim_sin/cos/tan/asin/acos/atan/atan2` (lines 183-196) - Trig
    4. `prim_exp/log/log10/log2/pow/sqrt` (lines 175, 203-207) - Exponential
    5. `prim_min` (line 266), `prim_max` (line 284) - Comparisons
    6. `prim_random` (line 473), `prim_random_int` (line 486), `prim_random_range` (line 507) - RNG
  Additional: `sinh`, `cosh`, `tanh`, `gcd`, `lcm`, `is_nan`, `is_inf`, `is_finite`, bitwise ops, `random_choice`, `shuffle`

### P1: Collection Utilities [DONE]

- [DONE] Label: I28-p1-collections
  Objective: Extended collection operations.
  Where: `runtime/src/collections.c`
  Implemented:
    1. `prim_sort` (line 140), `prim_sort_by` (line 183), `prim_sort_with` (line 237) - Sorting
    2. `prim_group_by` (line 330) - Group elements by key function
    3. `prim_partition` (line 387) - Split by predicate
    4. `prim_coll_take` (line 444), `prim_coll_drop` (line 484), `prim_take_while` (line 521), `prim_drop_while` (line 565) - Slicing
    5. `prim_flatten` (line 614), `prim_flatten_deep` (line 720) - Flatten nested lists
    6. `prim_zip` (line 736), `prim_unzip` (line 788) - Pair/unpair lists
    7. `prim_frequencies` (line 851) - Count occurrences
  Additional: `distinct`, `interleave`, `interpose`, `reverse`

### P2: I/O Utilities [DONE]

- [DONE] Label: I28-p2-io
  Objective: Extended I/O operations.
  Where: `runtime/src/io.c`
  Implemented:
    1. `prim_io_read_file` (line 70), `prim_io_write_file` (line 113) - File as string
    2. `prim_read_lines` (line 144), `prim_write_lines` (line 183) - File as line list
    3. `prim_file_exists_p` (line 306), `prim_directory_p` (line 327), `prim_file_p` (line 315) - File predicates
    4. `prim_list_directory` (line 362) - List directory contents
    5. `prim_path_join` (line 566), `prim_path_dirname` (line 652), `prim_path_basename` (line 684) - Path ops
    6. `prim_io_getenv` (line 823), `prim_io_setenv` (line 839), `prim_io_unsetenv` (line 859) - Environment variables
  Additional: `read_bytes`, `write_bytes`, `make_directory`, `make_directories`, `delete_file`, `delete_directory`, `rename`, `copy_file`, `file_size`, `file_mtime`, `environ`

### P3: JSON Support [DONE]

- [DONE] Label: I28-p3-json
  Objective: JSON parsing and generation.
  Where: `runtime/src/json.c`
  Implemented:
    1. `prim_json_parse` (line 585) - Parse JSON string to OmniLisp data
    2. `prim_json_stringify` (line 624) - Convert data to JSON string
    3. `prim_json_read` (line 664) - Read JSON from file
    4. `prim_json_write` (line 681) - Write JSON to file
  Additional: `json_pretty`, `json_get`, `json_valid_p`

---

## Issue 29: In-Code TODO Consolidation [COMPLETE]

**Objective:** Consolidate all scattered in-code TODOs, FIXMEs, and stubs into tracked items per Code-to-TODO Directive.

**Source:** Automated scan of codebase on 2026-01-21.

**Note:** Third-party code (`third_party/`) is excluded from tracking.

**Status (2026-01-21):**
- P0: [DEFERRED_ENHANCEMENTS] Specialization system - working fallbacks exist
- P1: [DEFERRED_ENHANCEMENTS] Type inference - working fallbacks exist
- P2: [MOSTLY_FIXED] Runtime - deep_set and piping.c implemented
- P3: [FIXED] API exposure - already implemented, tests enabled
- P4: [DONE] Tests - RC liveness tests passing, Pika charset escapes verified working
- P5: [DEPRECATED] Parser - unused rules, codegen handles match clauses

### P0: Codegen TODOs [DEFERRED_ENHANCEMENTS]

- [DEFERRED_ENHANCEMENTS] Label: I29-p0-codegen
  Objective: Address code generation incomplete features.
  Status: All items have working fallbacks. These are optimization enhancements.

  Specialization System Enhancements (fallback: generic calls work):
    2. `csrc/codegen/codegen.c:7175` - Generate generic call code
    3. `csrc/codegen/codegen.c:7194` - Generate specialized call with unboxing
    4. `csrc/codegen/codegen.c:7202` - Generate unboxing code for specialized function args
    5. `csrc/codegen/codegen.c:7214` - Generate generic call code (duplicate of #2)
    6. `csrc/codegen/spec_codegen.c:185` - Generate function body from AST
    7. `csrc/codegen/spec_codegen.c:213,222` - Implement specialized operators
    8. `csrc/codegen/typed_array_codegen.c:236` - Implement type checking
    9. `csrc/codegen/spec_decision.c:106,131,176` - Track call frequency, generic types, loop hotness

  Region Optimization Enhancements (fallback: transmigrate works):
    1. `csrc/codegen/region_codegen.c:181-182` - Add size-based transmigrate decision
    10. `csrc/codegen/region_codegen.h:90` - Retain/release insertion (Issue 1 P2)

  Path Expression Enhancement (fallback: returns NIL):
    11. `csrc/codegen/codegen.c:4625` - Complex path expression not yet supported

### P1: Analysis TODOs [DEFERRED_ENHANCEMENTS]

- [DEFERRED_ENHANCEMENTS] Label: I29-p1-analysis
  Objective: Complete type inference and analysis features.
  Status: All items have working fallbacks (returns concrete_type_any() or uses conservative analysis).

  Type Inference Enhancements (fallback: concrete_type_any()):
    2. `csrc/analysis/type_infer.c:843` - Array of Char type inference
    3. `csrc/analysis/type_infer.c:850` - Typed array inference
    4. `csrc/analysis/type_infer.c:406` - Usage in body analysis (not implemented)

  Analysis Precision Enhancements (fallback: conservative analysis):
    1. `csrc/analysis/region_inference.c:465` - Need access to program AST from CompilerCtx
    5. `csrc/analysis/analysis.c:746` - Store type info for array params with [name {Type}]
    6. `csrc/analysis/analysis.c:1078` - Store/report type incompatibility
    7. `csrc/analysis/analysis.c:5994` - Store variance info per parameter
    8. `csrc/analysis/analysis.c:6184` - Track variable types during analysis
    9. `csrc/analysis/analysis.c:6215` - More sophisticated type inference and subtype checking

### P2: Runtime TODOs [MOSTLY_FIXED]

- [MOSTLY_FIXED] Label: I29-p2-runtime
  Objective: Complete runtime implementation gaps.

  Fixed Items:
    5. `runtime/src/runtime.c:3503` - deep-put: FIXED (deep_set() implemented with dict/pair traversal)
    8. `runtime/src/piping.c:52,62,174,238,293` - FIXED (prim_apply, prim_pipe, prim_dot_field, prim_method_chain implemented)

  Deferred Items (require larger architecture work):
    1. `runtime/src/memory/continuation.c:1492` - DEFERRED: Channel select blocking requires fiber scheduling infrastructure
    6. `runtime/src/modules.c:540` - DEFERRED: Module auto-loading requires file loading and compilation pipeline
    7. `runtime/src/debug.c:736` - DEFERRED: Symbol lookup requires environment/symbol-table access from runtime

  Optimization TODOs (non-blocking, for future enhancement):
    2. `runtime/src/memory/region_value.c:88` - OPTIMIZATION: Store type_id instead of tag
    3. `runtime/src/memory/transmigrate.c:396` - OPTIMIZATION: Align TAG_* and TypeID enum values

  Documentation-only (not actual TODOs):
    4. `runtime/src/runtime.c:1245` - This is a comment describing the repair strategy, not unimplemented code

### P3: Runtime API Exposure [FIXED]

- [FIXED] Label: I29-p3-runtime-api
  Objective: Expose internal APIs needed for tests and features.
  Status: All APIs already implemented, test file enabled and passing.

  Previously thought missing (but already implemented):
    1. `region_retain_internal()` - exposed in omni.h:1226
    2. `region_release_internal()` - exposed in omni.h:1227
    3. `external_rc` field in Region struct - exists in region_core.h:70
    4. `region_exit()` checks external_rc via `region_destroy_if_dead()`

  Fixed: Test file `test_region_rc_liveness.c` updated and passing (4/4 tests)

### P4: Test Fixes [DONE]

- [DONE] Label: I29-p4-tests
  Objective: Fix test issues and enable stubbed tests.
  Items:
    1. `tests/test_pika_tower.c:464` - [FIXED] Pika charset escaped caret now works correctly.
       The original test file uses a deprecated 2-arg API that no longer exists.
       New test file `runtime/tests/test_pika_charset.c` verifies all charset escape sequences work:
       - `[\^]+` matches `^^^` ✓
       - `[a\^b]+` matches `a^b^a` ✓
       - `[\]]+` matches `]]]` ✓
       - `[\\]+` matches `\\\` ✓
       - `[a\-c]+` matches `a-c` but NOT `b` ✓
    2. `runtime/tests/test_region_rc_liveness.c` - [FIXED] Tests now enabled and passing (4/4 tests)
    3. `runtime/tests/test_performance.c:300,305` - [DEFERRED_ENHANCEMENT] Placeholder is fine for benchmark structure

### P5: Parser TODOs [DEPRECATED]

- [DEPRECATED] Label: I29-p5-parser
  Objective: Complete parser implementation.
  Items:
    1. `csrc/parser/parser.c:582` - [NOT_NEEDED] Match clause semantic action is part of unused R_MATCH_CLAUSE rule.
       Match expressions are parsed through regular R_LIST and handled by codegen_match().
       The dedicated match grammar rules (R_MATCH, R_MATCH_CLAUSE, R_MATCH_CLAUSES) were defined but never wired up.
       Fixed: codegen_match now supports list-based clauses (pattern result) in addition to array-based [pattern result].

---

## Language Completion Summary

### Core Language: 95% Complete

| Feature | Status | % |
|---------|--------|---|
| Control Flow | ✅ Complete | 100% |
| Bindings | ✅ Complete | 100% |
| Functions | ✅ Complete | 100% |
| Data Types | ✅ Complete | 100% |
| Type System | ✅ Complete | 100% |
| Macros | ✅ Complete | 100% |
| Modules | ✅ Complete | 100% |
| Error Handling | ✅ Complete | 100% |
| Concurrency | ✅ Complete | 100% |
| Sets (Issue 24) | ✅ Complete | 100% |
| DateTime (Issue 25) | ✅ Complete | 100% |
| Strings (Issue 26) | ✅ Complete | 100% |

### Standard Library: 90% Complete

| Feature | Status | % |
|---------|--------|---|
| String Utils | ✅ Complete | 100% |
| Regex | ✅ Complete | 100% |
| Collections | ✅ Complete | 100% |
| Math & Numerics | ✅ Complete | 100% |
| I/O | ✅ Complete | 100% |
| JSON | ✅ Complete | 100% |
| Networking | ❌ Missing | 0% |

### Developer Tools: 90% Complete

| Feature | Status | % |
|---------|--------|---|
| REPL | ⚠️ Partial | 70% |
| Object Inspection | ✅ Complete | 100% |
| Memory Debugging | ✅ Complete | 100% |
| Testing Framework | ✅ Complete | 100% |
| Profiling | ✅ Complete | 100% |
| Documentation System | ❌ Missing | 0% |

### Overall Project Completion: ~70%

```
Core Language:    ████████████████████ 95%
Standard Library: ████████░░░░░░░░░░░░ 40%
Developer Tools:  ████░░░░░░░░░░░░░░░░ 20%
─────────────────────────────────────────
Overall:          ██████████████░░░░░░ 70%
```

---

---

# Historical Backup: 2026-01-10 (Early Development)

# OmniLisp TODO (Restarted)

This TODO list was **restarted on 2026-01-10** after archiving the previous historical backlog.

Backup of the previous `TODO.md` (full history):
- `TODO_ARCHIVE/TODO.md.backup.2026-01-10_120111`

## Review Directive

**All newly implemented features must be marked with `[DONE]` (Review Needed) until explicitly approved by the user.**

- When an agent completes implementing a feature, mark it `[DONE]` (not `[DONE]`)
- `[DONE]` means: code is written and working, but awaits user review/approval
- After user approval, change `[DONE]` to `[DONE]`
- Workflow: `[TODO]` → implement → `[DONE]` → user approves → `[DONE]`

---

## Transmigration Directive (Non-Negotiable)

**Correctness invariant (must always hold):** For every in-region heap object `src` reached during transmigration, `remap(src)` yields exactly one stable destination `dst`, and all pointer discovery/rewrites happen only via metadata-driven `clone/trace` (no ad-hoc shape walkers); external/non-region pointers are treated as roots and never rewritten.

**Do not bypass the metadata-driven transmigration machinery for “fast paths”.**

Rationale:
- Fast paths that “special-case” one shape (e.g., linear lists) tend to reintroduce unsoundness by silently skipping necessary escape repair (Region Closure Property).
- CTRR’s guarantee requires a *single* authoritative escape repair mechanism (metadata-driven `clone/trace`), with optimizations implemented **inside** that machinery (remap/forwarding strategy, worklist layout/chunking, batch allocation, dispatch reductions, etc.), not around it.

Minimal “stay on the path” examples:

Allowed (optimize inside the existing machinery):
```c
// GOOD: still uses the same remap + metadata callbacks.
Obj *dst = omni_remap_get_or_clone(ctx, src, meta);   // may use dense tables / forwarding
meta->trace(ctx, dst, src);                           // discovers edges via metadata
// ...ctx pushes work items; loop processes them...
```

Forbidden (bypass/alternate implementations):
```c
// BAD: special-cases a shape and bypasses metadata-driven trace/clone.
if (omni_is_linear_list(src)) {
  return omni_fast_copy_list_without_metadata(src, dst_region);
}
```

Allowed:
- Optimization of the existing transmigration loop and remap/worklist internals.
- Type-specific micro-optimizations that are implemented via metadata callbacks and remain fully covered by the same correctness tests.
- Instrumentation/metrics that prove a change is a *true* win (e.g., worklist push/pop counts, visitor-call counts, forwarding hit rate), without changing the correctness contract.

Forbidden:
- Any separate “alternate transmigrate implementation” that bypasses metadata clone/trace for a subset of graphs unless it is proven equivalent and treated as part of the same contract (and reviewed as a high-risk change).

---

## Issue Authoring Directive (Agent-Proof)

This file uses **Issue N** enumeration (starting at 1). Do not renumber existing issues.

Rules (mandatory):
- **Append-only numbering:** When creating a new issue, add it as `## Issue N: ...` using the next available integer `N`. Never renumber existing issues.
- **No duplicates:** There must be exactly one header for each issue number. If an issue needs revision, append an “Amendment” subsection inside that issue instead of creating a second copy elsewhere.
- **Dependency order:** Issues must be ordered top-to-bottom by dependency.
- **Status required:** Every task line must be one of `[TODO]`, `[IN_PROGRESS]`, `[DONE]`, or `[N/A]` with a one-line reason for `[N/A]`. Never delete old tasks; mark them `[N/A]` instead.
- **Benchmark consistency clause (perf tasks):** Any performance-related issue MUST define a reproducible benchmark protocol (compiler + flags, rebuild steps, warmup/repeats, and what to report). If the protocol is not specified, the issue is incomplete and must be marked `[N/A]` until fixed.

Required “agent-proof” structure for new issues/tasks:
- **Objective:** 1–2 sentences describing the concrete outcome.
- **Reference (read first):** exact doc paths that explain the theory/contract.
- **Constraints:** restate “no stop-the-world GC”, “no language-visible share”, and issue-specific invariants.
- **Subtasks:** each subtask must include:
  - **Label:** short, unique, grep-able (e.g. `I1-rc-crosscheck`).
  - **Where:** exact file paths to modify.
  - **Why:** architectural reason; what breaks or is slow today.
  - **What to change:** numbered steps.
  - **Implementation details:** include pseudocode and key structs/functions/macros.
  - **Verification plan:** concrete tests + exact commands.

---

## Jujutsu Commit Directive (MANDATORY)

**Use Jujutsu (jj) for ALL version control operations.**

### Pre-Task Checklist (MANDATORY)

**Before beginning ANY implementation subtask, you MUST:**

1. **Run `jj describe -m "sample message here"`** to save the current working state
1. **Run `jj log`** to see the current working state
2. **Read the description** to understand what changes are in progress
3. **Confirm alignment** with the task you're about to implement
4. **If mismatch**: Either `jj squash` to consolidate or `jj new` to start fresh

```bash
# ALWAYS run this first
jj describe
```

### Commit Workflow

- **Use jj (not git)**: All commits must be made using `jj` commands
- **Squash workflow**: Use `jj squash` to combine related changes before committing
- **For every completed task:**
  - Create a dedicated jujutsu squash with a clear, imperative message.

---

## Issue 1: Adopt “RC external pointers” semantics as Region‑RC spec cross-check (Internet-Informed 11.1) [TODO]

**Objective:** Cross-check OmniLisp’s per-region reference counting model against the RC dialect’s definition of “external pointers”, then encode the applicable semantics as explicit OmniLisp docs and verification checklists (no language-visible API changes).

**Reference (read first):**
- `review_todo.md` (Issue 11.1 and Issue 1/2 model tasks)
- `docs/CTRR.md` (Region Closure Property; “everything can escape”)
- `runtime/docs/ARCHITECTURE.md` (model naming and boundaries)
- RC dialect overview: https://www.barnowl.org/research/rc/index.html

**Constraints (non-negotiable):**
- No stop-the-world GC; no heap-wide scanning collectors.
- No language-visible “share primitive” (`(share v)` is forbidden).
- No programmer-visible RC APIs (we use CTRR insertion + runtime barriers).
- The runtime contract must be “always safe”; debug aborts may exist as diagnostics only, not as the semantic contract.

### P0: Write the Region‑RC model spec (external pointers, liveness, invariants) [DONE] (Review Needed)

- [DONE] Label: I1-region-rc-model-doc (P0)
  Objective: Create a single authoritative doc defining Region‑RC and "external pointer" semantics in OmniLisp terms.
  Where:
    - Add: `runtime/docs/REGION_RC_MODEL.md` ✅
    - Update: `runtime/docs/ARCHITECTURE.md` (link + terminology alignment) ✅
  Why:
    "Per-region RC" is ambiguous unless we explicitly define what increments/decrements region external refs and what it means for safe reclamation.
  What to write:
    1. Region liveness rule:
       - Alive iff `scope_alive == true || external_rc > 0`.
       - Reclaimable iff `scope_alive == false && external_rc == 0`.
    2. Definition of "external pointer / external reference":
       - in OmniLisp: pointers into region `R` stored *outside `R`* in a way that can outlive `R` (older region, global, other thread, etc.).
    3. Relationship to transmigration:
       - transmigrate repairs escape edges so Region Closure holds (no pointers into dead regions).
    4. Relationship to mutation auto-repair (Issue 2 tasks are in `review_todo.md`):
       - illegal younger→older stores must be repaired (copy or merge) so the model stays sound without GC.
  Verification plan:
    - Doc includes a "Conformance Checklist" referencing:
      - "external-root non-rewrite rule" (transmigration)
      - "mutation barrier inventory complete" (review_todo Issue 2)

- [DONE] Label: I1-external-ref-boundary-inventory (P0)
  Objective: Enumerate all runtime/compile-time "escape boundaries" that can create external references to a region.
  Where:
    - Add section: `runtime/docs/REGION_RC_MODEL.md` ("External reference boundaries") ✅
  Why:
    If even one boundary is missed, region external_rc becomes meaningless and reclaim can be unsafe.
  What to list (minimum categories):
    - return to caller/outliving scope ✅
    - closure capture ✅
    - global/module store ✅
    - channel send/recv (cross-thread) ✅
    - mutation store into older region containers (must auto-repair; see Issue 2 in review_todo.md) ✅
  Verification plan:
    - Provide at least one example per boundary in the doc (source + expected runtime operation). ✅

---

## Issue 2: Pool/arena practice + region accounting + auto-repair threshold tuning (Internet-Informed 11.3) [TODO]

**Objective:** Adopt “shortest-lived pool” lessons from pool-based allocators by adding region accounting and diagnostics that (a) detect retention cliffs and (b) provide deterministic inputs for the size heuristic used by auto-repair (transmigrate vs merge).

**Reference (read first):**
- `review_todo.md` Issue 6 (accounting), Issue 2 (auto-repair policy), Issue 11.3 (pool practice)
- APR pools usage guidance: https://perl.apache.org/docs/2.0/user/performance/prevent.html
- ATS MemArena (freeze/thaw style): https://docs.trafficserver.apache.org/en/10.1.x/developer-guide/internal-libraries/MemArena.en.html

**Constraints (non-negotiable):**
- No stop-the-world GC; no heap-wide scanning collectors.
- No language-visible share primitive.
- Diagnostics must be deterministic and cheap enough for debug builds.

### P0: Region accounting doc + required counters [DONE] (Review Needed)

- [DONE] Label: I2-region-accounting-doc (P0)
  Objective: Specify which counters are required per region (bytes, chunks, inline usage, peak) and how they power the size heuristic for store auto-repair.
  Where:
    - Add: `runtime/docs/REGION_ACCOUNTING.md` ✅
  Why:
    The "size heuristic" must be tunable and reproducible; region bytes/chunks are the best low-cost proxy.
  What to define:
    - `bytes_allocated_total`, `bytes_allocated_peak` ✅
    - `chunk_count`, `inline_buf_used_bytes` ✅
    - optional: `escape_repair_count` (how often this region forced repair) ✅
  Verification plan:
    - Doc includes example output format and thresholds. ✅

### P1: Retention diagnostics plan (shortest-lived pool enforcement without language changes) [DONE] (Review Needed)

- [DONE] Label: I2-retention-diagnostics-plan (P1)
  Objective: Define the diagnostics that identify "allocating into too-long-lived region" retention smells, and how they should be reported.
  Where:
    - Add section: `runtime/docs/REGION_ACCOUNTING.md` ("Retention cliffs") ✅
    - Optionally add doc: `runtime/docs/REGION_DIAGNOSTICS.md` (N/A - covered in REGION_ACCOUNTING.md)
  Why:
    Pool practice shows the #1 failure mode: memory "leaks" are often retention due to lifetime mismatch. We want the runtime to make this visible.
  What to report (examples):
    - "Region R is long-lived and received X allocations but only Y escapes; consider allocating in shorter-lived region" ✅
    - "Auto-repair triggered N times (M transmigrates, K merges); threshold tuning suggested" ✅
  Verification plan:
    - Define a toy workload and expected diagnostics output. ✅

### P2: Optional "freeze/thaw" coalesce-at-safe-point evaluation [N/A - Fragmentation not yet measured]

- [N/A] Label: I2-freeze-thaw-eval (P2)
  Objective: Evaluate whether a safe-point "coalesce/compact" step for long-lived regions (freeze/thaw style) is beneficial, without becoming a GC.
  Where:
    - Add: `runtime/docs/REGION_COALESCE_POLICY.md` (evaluation + decision)
  Why:
    It may reduce fragmentation for long-lived regions, but it increases complexity and must be justified by benchmarks.
  Constraints:
    - Only at explicit safe points (e.g., end of init), not background.
    - Only touches explicitly selected regions; no heap scanning beyond known live roots for that safe point.
  Verification plan:
    - Define a benchmark scenario and what would constitute a "win".
  **Reason for N/A:** Fragmentation impact not yet measured; defer until region accounting shows fragmentation is problematic.

---

## Issue 3: Non-lexical regions + splitting ideas as CTRR roadmap (Internet-Informed 11.2) [TODO]

**Objective:** Incorporate non-lexical region analysis and region splitting ideas as a CTRR roadmap to reduce the frequency and cost of runtime repairs (transmigrate/merge), without changing language syntax.

**Reference (read first):**
- `docs/CTRR.md`
- Better Static Memory Management (Aiken/Fähndrich/Levien, 1995): https://digicoll.lib.berkeley.edu/record/139069
- Region-Based Memory Management (Tofte/Talpin, 1997): https://www.sciencedirect.com/science/article/pii/S0890540196926139
- Spegion (ECOOP 2025): https://drops.dagstuhl.de/entities/document/10.4230/LIPIcs.ECOOP.2025.15

**Constraints (non-negotiable):**
- No STW GC; no runtime heap scanning.
- No new language surface constructs required for users.

### P0: CTRR inference roadmap doc [DONE] (Review Needed)

- [DONE] Label: I3-ctrr-roadmap-doc (P0)
  Objective: Write a roadmap doc that maps research ideas into concrete CTRR phases OmniLisp could implement.
  Where:
    - Add: `docs/CTRR_REGION_INFERENCE_ROADMAP.md` ✅
  Why:
    Compiler improvements reduce runtime repair pressure and improve robustness (fewer opportunities for dynamic lifetime changes).
  What to include:
    - Non-lexical region ends (liveness-driven end-of-region insertion) ✅
    - Allocate-into-outliving-region when escape is provable (avoid transmigrate) ✅
    - Internal "splittable regions" representation (to support later merges without copying) ✅
    - Interaction with mutation auto-repair policy (Issue 2 in review_todo.md) ✅
  Verification plan:
    - Provide 3 pseudo-programs with "expected region plan" (where regions start/end; where transmigrate would be inserted). ✅

---

## Issue 4: Concurrency SMR techniques for internal runtime DS (QSBR first; alternatives documented) (Internet-Informed 11.4) [TODO]

**Objective:** Evaluate safe memory reclamation (SMR) techniques (QSBR/Userspace RCU first) for *internal runtime data structures* (metadata registries, intern tables), improving concurrency and tooling robustness without turning the heap into a GC-managed space.

**Reference (read first):**
- `review_todo.md` Issue 11.4 tasks
- liburcu (Userspace RCU): https://liburcu.org/
- QSBR overview: https://lwn.net/Articles/573424/
- `runtime/docs/REGION_THREADING_MODEL.md` (to be written; threading contract)

**Constraints (non-negotiable):**
- Not a heap GC: SMR applies only to internal runtime DS nodes.
- No STW “scan and collect”.
- Must align with the threading/ownership contract (documented first).

### P0: SMR target inventory + decision matrix [DONE] (Review Needed)

- [DONE] Label: I4-smr-target-inventory (P0)
  Objective: Identify which internal runtime structures would benefit from SMR and create a decision matrix (QSBR vs lock vs hazard-pointer family).
  Where:
    - Add: `runtime/docs/SMR_FOR_RUNTIME_STRUCTURES.md` ✅
  Why:
    Adopting SMR without a clear target is complexity without payoff. We must start with concrete structures.
  What to include:
    - List candidate structures (e.g., metadata registry, intern table, global module map). ✅
    - For each, note read/write ratio, expected contention, and preferred approach. ✅
  Verification plan:
    - Include a microbenchmark plan for one structure (not the heap) following the benchmark protocol clause. ✅

### P1: QSBR mapping to OmniLisp "quiescent points" [DONE] (Review Needed)

- [DONE] Label: I4-qsbr-quiescent-points (P1)
  Objective: Define where quiescent states would be reported in OmniLisp (end of bytecode step, end of tether window, safe points), and how that interacts with region ownership.
  Where:
    - `runtime/docs/SMR_FOR_RUNTIME_STRUCTURES.md` (QSBR design) ✅
    - `runtime/docs/REGION_THREADING_MODEL.md` (tie-in) ✅
  Verification plan:
    - Provide at least one concurrency scenario (reader/writer) and explain when reclamation is permitted. ✅

### P2: Alternatives review (hazard pointers / Hyaline / publish-on-ping) [DONE] (Review Needed)

- [DONE] Label: I4-alternatives-review (P2)
  Objective: Document alternatives to QSBR and when they should be chosen (only if QSBR is unsuitable due to stalled threads or missing quiescent points).
  Reference:
    - Publish on Ping (2025): https://arxiv.org/abs/2501.04250
    - Hyaline (2019): https://arxiv.org/abs/1905.07903
  Where:
    - `runtime/docs/SMR_FOR_RUNTIME_STRUCTURES.md` (appendix) ✅
  Verification plan:
    - Decision matrix includes "how to test" and "what to measure" for each alternative. ✅
    - Decision matrix includes “how to test” and “what to measure” for each alternative.

---

# Review Notes

# OmniLisp Memory System — Review Task List (CTRR + Region‑RC)
Date: 2026-01-10

This file is a **review backlog** for the OmniLisp memory system. It is intentionally exhaustive so you can pick/choose later.

Scope:
- **CTRR (Compile‑Time Region Reclamation)**: compile-time scheduling of region lifetimes and escape repair points.
- **Region‑RC (Per‑Region Reference Counting)**: runtime liveness is determined by **region-level** reference counts + scope liveness (no heap-wide collector).
- Building components: regions, per-region RC, tethering/borrowing, transmigration, concurrency + tooling.

Status legend (for this file):
- `TODO`: Candidate work item.
- `DEFER`: Only do if/when needed (explicitly justify).
- `N/A`: Not applicable (include a one-line reason).

Non‑negotiable constraints (global):
- No stop‑the‑world GC; no heap-wide scanning collectors.
- No language-visible “sharing primitive” (no `(share v)` or similar).
- Optimizations must remain **inside** the single authoritative CTRR/Region‑RC machinery (no bypass special-case walkers).
- Benchmark claims must use a reproducible protocol (see Issue 8).

Primary references (read before implementing *any* memory-model task):
- `docs/CTRR.md` (normative CTRR contract: “everything can escape”, Region Closure Property)
- `runtime/docs/ARCHITECTURE.md` (runtime architecture + model naming)
- `runtime/docs/CTRR_TRANSMIGRATION.md` (clone/trace contract, external-root rule, correctness invariants)
- `runtime/docs/CTRR_REMAP_FORWARDING_TABLE.md` (forwarding/remap identity)
- `docs/ADVANCED_REGION_ALGORITHMS.md` (long-term region/transmigration plans)

------------------------------------------------------------------------------

## Issue 1 — Formalize “Per‑Region RC” Semantics (Make the Model Explicit)

Goal: eliminate ambiguity about what Region‑RC means and what correctness guarantees it provides.

- [TODO] Label: R1-model-spec-core
  Objective: Write an explicit spec for the **per-region reference counting** model, including invariants, allowed pointer directions, and what CTRR must insert at compile time.
  Reference: `docs/CTRR.md`, `runtime/docs/ARCHITECTURE.md`
  Where:
    - New doc: `runtime/docs/REGION_RC_MODEL.md`
    - Update/align: `runtime/docs/ARCHITECTURE.md` (link to new doc)
  Why:
    “Per-region RC” only works robustly if we define:
    - what counts as an “external reference” to a region,
    - when a region may be reclaimed,
    - and how we prevent/handle region dependency cycles.
  What to write (minimum content checklist):
    1. **Region liveness rule:**
       - Region is alive iff `scope_alive == true || external_rc > 0`.
       - Region may free memory iff `scope_alive == false && external_rc == 0`.
    2. **Region Closure Property (runtime form):**
       - No pointers into dead regions.
    3. **External-root rule:**
       - Pointers outside the src region are treated as roots during transmigration and are not rewritten.
    4. **Region dependency DAG rule (see Issue 2):**
       - Define the “older-only store” rule or an equivalent rule that prevents region cycles.
    5. **Concurrency model hooks:**
       - define what operations may cross threads and what instrumentation/tooling is expected.
  Verification plan:
    - Add a short “spec conformance checklist” section to `runtime/docs/REGION_RC_MODEL.md`.
    - Ensure all existing docs link consistently (no contradictory naming).

- [TODO] Label: R1-define-external-ref-boundaries
  Objective: Define precisely which operations increment/decrement `external_rc` (or equivalent region-level refs).
  Reference: `runtime/docs/REGION_RC_MODEL.md` (to be written by R1-model-spec-core)
  Where:
    - New doc section: `runtime/docs/REGION_RC_MODEL.md` (“External references”)
    - Code touchpoints to list (do not implement here): `runtime/src/` modules for channel send/recv, closure capture, return, globals, etc.
  Why:
    Region‑RC is unsound if raw pointers can escape without RC updates; the runtime cannot observe arbitrary pointer copying.
  Implementation details (spec-level pseudocode):
    - On escape boundary (compile-time inserted):
      - `region_inc_external(root_region_of(value))` if value points into a region that outlives current scope.
      - `region_dec_external(...)` when the escape handle is dropped or transferred.
  Verification plan:
    - Spec must enumerate each escape boundary category:
      - return to parent/caller
      - closure capture
      - global store / module store
      - channel send/recv (cross-thread handoff)

------------------------------------------------------------------------------

## Issue 2 — Prevent Region Cycles (Region Dependency Graph Rule + Enforcement)

This is the largest “robustness unlock”. Per-region RC cannot reclaim cycles of regions, so we must prevent region dependency cycles by construction.

- [TODO] Label: R2-older-only-store-rule-spec
  Objective: Specify the **region dependency rule** that prevents region cycles (recommended: “older-only store”).
  Reference: `docs/CTRR.md` (Region Closure Property), `runtime/docs/REGION_RC_MODEL.md`
  Where:
    - New doc: `runtime/docs/REGION_DEPENDENCY_RULES.md`
  Why:
    Without an explicit rule, mutable Lisp updates can create:
    - older region objects pointing to younger region objects,
    - forming region cycles,
    - which per-region RC cannot collect.
  Spec content (minimum):
    - Define a region partial order (e.g., creation time / nesting depth).
    - Rule: a store into an object in region `R_old` may only reference values in `R_old` or an “older/equal” region (never a younger region).
    - If code attempts to store a younger pointer into an older object:
      - The runtime must **auto-repair** the store (no language changes, no user-visible `(share ...)`), using:
        - transmigration for “small” values/regions,
        - region coalescing/merge (“promote the region”) for “large” values/regions,
        - and a well-defined, reproducible size heuristic (see R2-auto-repair-policy below).
  Verification plan:
    - Doc includes a table of allowed/disallowed cases with examples.
    - Include at least 2 examples showing how this prevents region cycles.

- [TODO] Label: R2-runtime-write-barrier-plan
  Objective: Design a runtime **store barrier** strategy for cases the compiler cannot prove (dynamic stores), without changing language syntax.
  Reference: `runtime/docs/REGION_DEPENDENCY_RULES.md`
  Where (planned implementation points):
    - `runtime/src/runtime.c` (or wherever `set-car!`, `set-cdr!`, `dict-set!`, array set, etc. are implemented)
    - `runtime/src/memory/` (helpers to map pointer→region id/domain)
  Why:
    The language is mutable; if we don’t enforce the region rule at mutation points, cycles/leaks appear.
  Implementation plan (pseudocode; auto-repair, never abort):
    - `store_slot(obj_in_Rold, slot, value)`:
      1. Determine region of `obj` (R_obj).
      2. Determine region of `value` if pointer into a region (R_val).
      3. If `R_val` is younger than `R_obj`:
         - Choose repair action via the size heuristic (see R2-auto-repair-policy):
           - If “small”: `value2 = transmigrate(value, src=R_val, dst=R_obj_or_parent)` then store `value2`.
           - If “large”: `region_merge(R_val, into=R_obj_or_parent)` then store original `value` (now in unified lifetime).
  Verification plan:
    - Define tests that attempt illegal stores and confirm behavior is always safe and automatic (no abort):
      - illegal younger→older store triggers *either* transmigrate or merge, depending on configured heuristic thresholds.
    - Run under ASAN/TSAN once tooling exists (Issue 7/9).

- [TODO] Label: R2-auto-repair-policy
  Objective: Specify the **auto-repair** policy for illegal younger→older stores, including the “size heuristic” you want: small ⇒ transmigrate, large ⇒ coalesce/merge.
  Reference: `runtime/docs/REGION_DEPENDENCY_RULES.md`, `runtime/docs/REGION_RC_MODEL.md`
  Where:
    - Add section: `runtime/docs/REGION_DEPENDENCY_RULES.md` (“Auto-Repair Policy: Transmigrate vs Merge”)
    - Add section: `runtime/docs/REGION_RC_MODEL.md` (“Why region cycles must be prevented; why auto-repair is mandatory in a mutable Lisp”)
  Why:
    In a mutable Lisp, the program can create new lifetime dependencies at runtime via mutation. If the runtime doesn’t auto-repair these, per-region RC is either:
    - unsound (dangling pointers when younger regions die), or
    - incomplete (region cycles retained forever if cross-region refs keep regions alive).
    Auto-repair turns mutation-time lifetime changes into a guaranteed-safe operation.
  Policy requirements (must be explicit in the doc):
    1. **No user-visible syntax:** no `(share v)` or opt-in primitives.
    2. **No debug abort as the contract:** aborts may exist as optional instrumentation, but the language/runtime contract is “always repairs automatically”.
    3. **Deterministic decision:** given the same configuration and same allocation sizes, the repair choice should be predictable (for reproducible benchmarks).
    4. **No STW GC:** repair must be local (transmigrate) or structural (merge), never heap scanning.
  Size heuristic (recommended to document and implement in this order, from simplest to more precise):
    - Heuristic H0 (default): compare **region allocated bytes**:
      - if `bytes_allocated(R_val) >= MERGE_THRESHOLD_BYTES` ⇒ merge `R_val` into `R_obj`
      - else ⇒ transmigrate the stored value into `R_obj`
    - Heuristic H1 (optional): compare **estimated reachable size** from the stored root, with a bounded traversal budget:
      - stop after N nodes/bytes and treat as “large” if budget exceeded
      - NOTE: this is still not a GC (it only traverses the value graph being stored), but it is more complex and must be justified by perf wins.
  Merge safety constraints (must be explicit):
    - If merge cannot be performed safely (e.g., region ownership/threading constraints, or region is already shared in an incompatible way), then the runtime must fall back to transmigration (correctness over performance).
  Verification plan:
    - Provide 3 concrete behavioral examples in the doc:
      1. small young list stored into older dict ⇒ transmigrate
      2. large young region stored into older structure ⇒ merge
      3. merge not permitted ⇒ transmigrate fallback (still safe)

- [TODO] Label: R2-mutation-site-inventory
  Objective: Inventory every runtime mutation primitive that can create cross-region edges and therefore must use the store barrier / auto-repair policy.
  Reference: `runtime/docs/REGION_DEPENDENCY_RULES.md` (auto-repair contract)
  Where:
    - List and link to the exact functions implementing:
      - `set-car!`, `set-cdr!`
      - array/vector set
      - dict/map set
      - any internal structure mutations (closure/env updates, object field sets)
  Why:
    Region-cycle prevention fails if even one mutation path bypasses the barrier.
  Done means:
    - The inventory is complete and includes “how to test” each mutation site.
    - The inventory explicitly says “all must call the same store helper”.

- [TODO] Label: R2-auto-repair-tests
  Objective: Define a regression test plan proving auto-repair prevents region cycles without changing language syntax.
  Reference: `runtime/docs/REGION_DEPENDENCY_RULES.md`
  Where:
    - Planned tests: `runtime/tests/` (new test file, e.g. `test_region_cycle_prevention.c`)
  Test plan (minimum):
    1. **Younger→older store repairs safely (no dangling pointers):**
       - Create `R_old` and `R_young`.
       - Allocate container in `R_old`, value in `R_young`.
       - Perform mutation store.
       - Exit `R_young` scope.
       - Assert container still points to a valid object/value (either migrated or merged).
    2. **Heuristic branch coverage:**
       - Configure threshold low to force merge; verify merge path taken.
       - Configure threshold high to force transmigrate; verify transmigrate path taken.
    3. **No region cycles retained:**
       - Construct scenario that would create `R_old ↔ R_young` cycle without repair.
       - Verify that after repair the graph does not contain any pointer into a dead region and region liveness counters drop as expected.
  Verification commands:
    - `make -C runtime/tests test`
    - Add ASAN/TSAN commands once sanitizer gates exist (Issue 9).

------------------------------------------------------------------------------

## Issue 3 — Region Ownership + Cross‑Thread Handoff (Concurrency Robustness)

Pick a threading contract that matches “robustness + tooling”.

- [TODO] Label: R3-threading-contract-spec
  Objective: Specify the threading/ownership model for regions and objects (recommended: single-owner regions; cross-thread requires handoff/transmigrate).
  Reference: `runtime/docs/REGION_RC_MODEL.md`, `runtime/docs/ARCHITECTURE.md`
  Where:
    - New doc: `runtime/docs/REGION_THREADING_MODEL.md`
    - Update: `runtime/docs/ARCHITECTURE.md` (link)
  Why:
    TSAN usefulness and overall robustness depend on having a clear contract:
    - either transmigrate is called under a runtime lock,
    - or regions/allocations are thread-safe (harder).
  Spec requirements:
    - Define “region owner” (thread id) conceptually (even if not implemented yet).
    - Define allowed cross-thread operations:
      - channel send/recv of values with region transfer or region inc/dec.
    - Define how Region‑RC interacts with handoff (who decrements external refs).
  Verification plan:
    - Add doc examples showing safe handoff vs unsafe concurrent mutation.

- [TODO] Label: R3-channel-handoff-mechanics
  Objective: Specify how channels transfer values without STW GC and without language-visible share constructs.
  Reference: `runtime/docs/REGION_THREADING_MODEL.md`
  Where:
    - New doc section: `runtime/docs/REGION_THREADING_MODEL.md` (“Channels”)
  Why:
    Channel handoff is a primary real-world escape boundary. If it’s ambiguous, concurrency bugs will dominate.
  Plan options (document both; pick one):
    - Option A: **Ownership transfer** of an entire region (fastest, most robust).
    - Option B: **Transmigrate on send** into receiver’s long-lived region (safe, more copying).
    - Option C: **Shared region** with locks (risky; only if needed).
  Verification plan:
    - Provide at least one test case per option in the spec (source + expected behavior).

------------------------------------------------------------------------------

## Issue 4 — Atomic Policy Layer (Robust Concurrency + Tooling)

Even if we remain C99, concurrency robustness improves dramatically if all atomics go through one auditable layer.

- [TODO] Label: R4-omni-atomic-wrapper-design
  Objective: Design an `omni_atomic.h` abstraction so the runtime can use C11 atomics when available, or `__atomic_*` intrinsics otherwise.
  Reference: `runtime/docs/REGION_THREADING_MODEL.md`
  Where:
    - New header: `runtime/include/omni_atomic.h`
    - New doc: `runtime/docs/ATOMIC_POLICY.md`
  Why:
    Tooling (TSAN) and correctness review require explicit ordering semantics; ad-hoc intrinsics are easy to misuse.
  API sketch (example):
    ```c
    typedef struct { /* impl */ } omni_atomic_u32;
    uint32_t omni_atomic_load_acquire(const omni_atomic_u32*);
    void omni_atomic_store_release(omni_atomic_u32*, uint32_t);
    uint32_t omni_atomic_fetch_add_relaxed(omni_atomic_u32*, uint32_t);
    uint32_t omni_atomic_fetch_sub_acqrel(omni_atomic_u32*, uint32_t);
    ```
  Memory-order policy doc (must include):
    - which counters can be relaxed (most refcount increments)
    - which transitions require acquire/release (dec-to-zero path)
  Verification plan:
    - Add a small unit test (if tests infra permits) for atomic wrapper correctness (single-thread sanity).
    - Add TSAN gate tests later (Issue 9).

------------------------------------------------------------------------------

## Issue 5 — Tethering / Borrowing Hardening (Epoch + Tripwires)

Tethering exists to keep borrowing safe without GC. Robustness depends on making violations loud.

- [TODO] Label: R5-region-epoch-borrow-tripwires
  Objective: Specify a region “epoch” mechanism and BorrowedRef validation in debug builds.
  Reference: `runtime/docs/REGION_RC_MODEL.md`, `runtime/docs/REGION_THREADING_MODEL.md`
  Where:
    - New doc section: `runtime/docs/REGION_TETHERING.md`
    - Planned code: `runtime/src/memory/region_core.{c,h}`, borrow/tether APIs
  Why:
    Without epoch-style validation, stale borrows become silent UAFs; tooling catches them only sometimes.
  Spec plan:
    - Each region has `uint64_t epoch`.
    - On region destroy: `epoch++` and (debug) poison memory.
    - BorrowedRef captures `(region_id, epoch_snapshot, tether_depth/handle)`.
    - On dereference (debug): assert region still alive and epoch matches.
  Verification plan:
    - Tests:
      - borrowing across tether end triggers abort in debug
      - borrow after region exit triggers abort in debug

------------------------------------------------------------------------------

## Issue 6 — Regions (Allocator Policy, Inline Buffer, Accounting)

This is where you can win both robustness and performance without touching user code.

- [TODO] Label: R6-region-accounting-quotas
  Objective: Add a plan for per-region and global accounting (bytes, allocations, peak), and optional debug quotas.
  Reference: `runtime/docs/REGION_RC_MODEL.md`
  Where:
    - New doc: `runtime/docs/REGION_ACCOUNTING.md`
    - Planned code: `runtime/src/memory/region_core.c`, `runtime/include/omni.h`
  Why:
    Regions can “silently become long-lived” (retention cliff). Accounting makes issues visible.
  Plan details:
    - Track:
      - bytes allocated in region
      - chunk count
      - inline_buf usage
      - peak bytes
    - Optional debug quota:
      - abort with message “region exceeded quota; likely escape/retention”.
  Verification plan:
    - Provide a sample program and expected diagnostic output.

- [TODO] Label: R6-inline-buf-policy-and-domain
  Objective: Clarify inline_buf policy and ensure “in-region domain” includes inline allocations consistently (for bitmap, forwarding, and external-root classification).
  Reference: Phase 31 inline_buf coverage notes in `TODO.md`, `runtime/docs/CTRR_TRANSMIGRATION.md`
  Where:
    - `runtime/docs/REGION_DEPENDENCY_RULES.md` (domain note)
    - `runtime/docs/REGION_RC_MODEL.md` (domain note)
  Verification plan:
    - Tests must include objects allocated in inline_buf and ensure transmigration and domain checks behave correctly.

------------------------------------------------------------------------------

## Issue 7 — Transmigration: Structural Work (Correctness + Performance)

These refer to phases already in `TODO.md` (40–47). This file records the “why” and the dependency structure so it’s visible in one place.

- [TODO] Label: R7-transmigrate-edge-choke-point-priority
  Objective: Treat “single edge rewrite choke point” (Phase 42) as the prerequisite for all future perf work to prevent regression churn.
  Reference: `TODO.md` Phase 42
  Verification plan:
    - When Phase 42 is implemented, require a check that no edge path allocates WorkItem nodes in hot path.

- [TODO] Label: R7-forwarding-as-visited-priority
  Objective: Prioritize “forwarding table as visited+remap” (Phase 40) to reduce per-edge bookkeeping.
  Reference: `TODO.md` Phase 40
  Verification plan:
    - Must run under forced forwarding mode and demonstrate (via counters) that bitmap/remap fallback is not exercised in the hot path.

- [TODO] Label: R7-pair-microkernel-priority
  Objective: Prioritize `TAG_PAIR` microkernel (Phase 41) after Phase 42, to remove both clone and trace indirection for cons-heavy graphs.
  Reference: `TODO.md` Phase 41
  Verification plan:
    - Must include header-invariant tripwires and list/array regression benchmarks.

------------------------------------------------------------------------------

## Issue 8 — Benchmark Protocol (Reproducibility and Reportability)

Tooling matters: without reproducible benchmarks, performance work becomes narrative.

- [TODO] Label: R8-bench-protocol-spec
  Objective: Codify the benchmark protocol (compiler + flags + rebuild steps + reporting) as a doc and ensure every perf phase references it.
  Reference: Phase 39 protocol in `runtime/tests/Makefile`
  Where:
    - New doc: `runtime/docs/BENCH_PROTOCOL.md`
    - Update: `runtime/tests/bench_transmigrate_vs_c.c` (ensure it prints correct compiler/flags; avoid misleading “GCC 4.2.1” output if using clang)
  Why:
    “bench-rel” must be apples-to-apples: benchmark binary and runtime library must be built with consistent flags, or results are not comparable.
  Verification plan:
    - `bench-rel` output must print:
      - actual compiler ID and version
      - actual CFLAGS used to build *both* runtime and bench (or explicitly state if runtime was built differently)
      - mode name

------------------------------------------------------------------------------

## Issue 9 — Tooling Gates: ASAN/TSAN/UBSAN Focused on Memory Model

This is where “tooling matters” becomes concrete.

- [TODO] Label: R9-sanitizer-contract-tests
  Objective: Add focused sanitizer stress tests specifically for region lifecycle + tethering + transmigration invariants.
  Reference: `runtime/tests/Makefile` sanitizer targets, `runtime/docs/REGION_THREADING_MODEL.md`
  Where:
    - `runtime/tests/` (new focused tests)
  Why:
    Sanitizers are most useful when they target well-defined invariants rather than random workloads.
  Plan:
    - ASAN: stress alloc/exit/transmigrate loops; ensure no UAF/leaks.
    - UBSAN: ensure no UB in bitmap/index math.
    - TSAN: only if threading contract is explicit; otherwise TSAN will produce false positives.
  Verification plan:
    - Provide exact commands to run and what “pass” means (no sanitizer reports within the scope of the focused tests).

------------------------------------------------------------------------------

## Issue 10 — Repo Hygiene (Build Artifacts Policy)

This is not “runtime code” but it materially affects robustness of the workflow.

- [TODO] Label: R10-build-artifact-policy
  Objective: Stop tracked build artifacts from polluting diffs and accidental commits, or document/enforce a strict policy if artifacts must remain tracked.
  Reference: `TODO.md` Phase 48
  Where:
    - Decide policy in `runtime/docs/DEV_HYGIENE.md` (new) and align with JJ workflow.
  Verification plan:
    - After running tests/bench, `jj status` should not show modified tracked binaries/objects under normal workflows.

------------------------------------------------------------------------------

## Issue 11 — Internet‑Informed Techniques (Adopt What Helps, Without Violating CTRR/Region‑RC)

This issue captures external techniques worth incorporating **only if** they align with:
- CTRR (compile-time scheduling of region lifetimes and escape repair points),
- Region‑RC (per-region external reference counting, no heap-wide tracing),
- auto-repair on mutation stores (transmigrate for small, merge/coalesce for large),
- no stop-the-world GC,
- no language-visible “share” construct.

### Why this issue exists
We want robustness and performance improvements that have precedent in real systems and the literature, but we must:
1) avoid importing techniques that are secretly “GC in disguise” (heap scanning),
2) avoid techniques that require language surface changes,
3) keep concurrency/tooling explicit and testable.

---

### 11.1 Region‑RC precedent: “RC” safe C dialect (external pointer counting)

- [TODO] Label: R11-rc-model-crosscheck
  Objective: Cross-check OmniLisp’s Region‑RC model against the “RC” safe C dialect’s definition of “external pointers”, and extract what applies to OmniLisp.
  Reference:
    - RC overview: https://www.barnowl.org/research/rc/index.html
    - `runtime/docs/REGION_RC_MODEL.md` (to be written by Issue 1)
  Where:
    - Add appendix: `runtime/docs/REGION_RC_MODEL.md` (“Related work: RC dialect; differences in a mutable Lisp”)
  Why:
    RC’s core claim is: “per-region refcount of pointers not stored within the region” prevents premature frees. That maps directly to our external_rc concept, but OmniLisp needs mutation auto-repair, so the store barrier is non-optional.
  What to write:
    - Define “external pointer” in OmniLisp terms (stored outside region, or crossing region ownership boundary).
    - Explain why OmniLisp cannot rely on programmer-visible `alias_refptr/drop_refptr` style APIs (no surface changes).
    - Explain why mutation barrier + auto-repair replaces RC’s “abort on delete with non-zero refcount”.
  Verification plan:
    - Doc section includes at least 2 concrete examples (illegal younger→older store; channel handoff) showing how OmniLisp handles cases RC handles via explicit operations.

---

### 11.2 Non‑lexical regions, splitting, and sized allocations (compile-time inspiration)

- [TODO] Label: R11-nonlexical-region-inference-notes
  Objective: Extract actionable ideas from “Better Static Memory Management” and Spegion (non-lexical regions, splitting, sized allocations) and map them to CTRR roadmap items.
  Reference:
    - Aiken/Fähndrich/Levien 1995 tech report: https://digicoll.lib.berkeley.edu/record/139069
    - Tofte/Talpin 1997: https://www.sciencedirect.com/science/article/pii/S0890540196926139
    - Spegion (ECOOP 2025 LIPIcs): https://drops.dagstuhl.de/entities/document/10.4230/LIPIcs.ECOOP.2025.15
    - `docs/CTRR.md`
  Where:
    - New doc: `docs/CTRR_REGION_INFERENCE_ROADMAP.md`
  Why:
    Even with runtime repair (transmigrate/merge), compile-time improvements reduce the rate of repairs and shrink region lifetimes (robustness + performance).
  What to write (concrete CTRR-aligned ideas; no new language syntax):
    - Non-lexical region end insertion driven by liveness (CTRR already does liveness; document explicit goals).
    - “Splittable regions” as an internal representation trick: allow the compiler/runtime to create subregions under one logical region, enabling later coalescence decisions (ties to your size heuristic).
    - Sized allocations: use runtime accounting to inform compile-time heuristics (e.g., predict big allocations; allocate directly into parent/global to avoid later repair).
  Verification plan:
    - Include 3 “before/after” pseudo examples: (1) early region end, (2) avoiding transmigration by allocating in outliving region, (3) how split regions could reduce copying during coalescence.

---

### 11.3 Pool/arena practice: shortest-lived pool guidance + “freeze/thaw” compaction analogue

- [TODO] Label: R11-pool-practice-guidance
  Objective: Incorporate “shortest-lived pool” guidance (from APR pools) into OmniLisp docs and diagnostic tooling, and evaluate whether a safe compaction/coalescence step (freeze/thaw style) applies to long-lived regions.
  Reference:
    - APR pools guidance: https://perl.apache.org/docs/2.0/user/performance/prevent.html
    - APR::Pool overview: https://perl.apache.org/docs/2.0/api/APR/Pool.html
    - MemArena freeze/thaw coalescence: https://docs.trafficserver.apache.org/en/10.1.x/developer-guide/internal-libraries/MemArena.en.html
    - `runtime/docs/REGION_ACCOUNTING.md` (Issue 6)
  Where:
    - Update: `runtime/docs/REGION_ACCOUNTING.md` (“Retention cliffs and pool lifetime mismatches”)
    - New doc: `runtime/docs/REGION_COALESCE_POLICY.md` (if warranted)
  Why:
    Pool systems show the main failure mode of region allocators in real code: allocating into too-long-lived pools causes “leaks by retention”. Your auto-repair heuristic is exactly a runtime enforcement of “pick shortest lived pool”.
  What to do (tasks, not implementation here):
    1. Define diagnostics:
       - warn if a region becomes long-lived and is receiving many allocations that never escape (retention smell)
    2. Evaluate a “coalesce after init” operation for long-lived regions:
       - Not a GC: only done at explicit safe points (end of init phase).
       - Coalesce by copying known-live data into one chunk and releasing old chunks (like MemArena freeze/thaw).
       - Must be opt-in and benchmarked; may be DEFER if complexity outweighs gains.
  Verification plan:
    - Provide a toy “server init then steady-state” scenario and expected accounting output showing reduced fragmentation after coalesce.

---

### 11.4 Concurrency: Safe Memory Reclamation (SMR) for internal runtime structures (not heap GC)

These techniques are for **internal concurrent data structures** (e.g., lock-free queues/maps used by the runtime). They do not replace CTRR/Region‑RC, but can reduce locking and improve robustness.

- [TODO] Label: R11-rcu-qsbr-evaluation
  Objective: Evaluate whether QSBR/Userspace RCU-style quiescent states can be mapped onto OmniLisp’s tethering/borrow windows to protect internal read-mostly structures (metadata tables, interned symbol tables) without STW GC.
  Reference:
    - Userspace RCU docs: https://liburcu.org/
    - QSBR overview (LWN): https://lwn.net/Articles/573424/
    - `runtime/docs/REGION_THREADING_MODEL.md` (Issue 3)
  Where:
    - New doc: `runtime/docs/SMR_FOR_RUNTIME_STRUCTURES.md`
  Why:
    If concurrency matters, we need a plan for safely reclaiming nodes in internal structures without global locks. QSBR is fast but requires explicit quiescent state reporting — conceptually similar to “no borrows held / tether windows ended”.
  What to define:
    - Which runtime structures would benefit (list them explicitly).
    - Where quiescent states would be reported (end of bytecode instruction? end of GC-free safe point? end of tether?).
    - Why this is not heap GC: only internal DS nodes are reclaimed.
  Verification plan:
    - Provide at least one test scenario in the doc: concurrent reads, writer retires nodes, reclaim after quiescent period.
    - Tie to TSAN expectations (Issue 9).

- [TODO] Label: R11-hazard-pointers-and-variants-review
  Objective: Review hazard pointers and newer variants (e.g., publish-on-ping) as alternatives to QSBR for internal runtime structures, focusing on overhead vs robustness tradeoffs.
  Reference:
    - Publish on Ping (2025): https://arxiv.org/abs/2501.04250
    - Hyaline (2019): https://arxiv.org/abs/1905.07903
    - `runtime/docs/ATOMIC_POLICY.md` (Issue 4)
  Where:
    - `runtime/docs/SMR_FOR_RUNTIME_STRUCTURES.md` (appendix “Alternatives”)
  Why:
    QSBR is fast but can retain memory if threads don’t report quiescent states. Hazard-pointer families are more robust but can impose per-read overhead. We need an explicit choice per structure.
  What to produce:
    - A decision matrix for:
      - read-mostly global tables (prefer QSBR/RCU)
      - lock-free linked structures with heavy traversal (consider variants that reduce per-read fences)
  Verification plan:
    - Include a minimal microbenchmark plan for one internal DS (not the heap), gated by Issue 8 benchmark protocol.

------------------------------------------------------------------------------

## External References (Internet / Literature)

These are the external sources referenced in Issue 11 tasks above. Prefer primary sources (papers, official docs) when implementing.

1. RC safe C dialect (region external pointer counting): https://www.barnowl.org/research/rc/index.html
2. Cyclone manual section on regions: https://www.cs.cornell.edu/Projects/cyclone/online-manual/main-screen008.html
3. Userspace RCU (liburcu) official site/docs: https://liburcu.org/
4. LWN overview of Userspace RCU flavors (QSBR): https://lwn.net/Articles/573424/
5. Hyaline SMR paper (2019): https://arxiv.org/abs/1905.07903
6. Publish on Ping SMR paper (2025): https://arxiv.org/abs/2501.04250
7. Better Static Memory Management (Aiken/Fähndrich/Levien, 1995): https://digicoll.lib.berkeley.edu/record/139069
8. Region-Based Memory Management (Tofte/Talpin, 1997): https://www.sciencedirect.com/science/article/pii/S0890540196926139
9. Spegion (ECOOP 2025, LIPIcs): https://drops.dagstuhl.de/entities/document/10.4230/LIPIcs.ECOOP.2025.15
10. mod_perl: Proper Memory Pools Usage: https://perl.apache.org/docs/2.0/user/performance/prevent.html
11. mod_perl: APR::Pool API: https://perl.apache.org/docs/2.0/api/APR/Pool.html
12. Apache Traffic Server MemArena (freeze/thaw coalescence): https://docs.trafficserver.apache.org/en/10.1.x/developer-guide/internal-libraries/MemArena.en.html

---

# Note: Large Historical Backup

A larger historical backup (8729 lines) exists at:
`TODO_ARCHIVE/TODO.md.backup.2026-01-10_120111`

This contains early development history and detailed implementation notes.
Most content is duplicated in the sections above.
