# OmniLisp TODO

## Conditions, Restarts, and Debugging Integration (Deprecated - superseded by Effects/Handlers)

- [DONE] Label: T-cond-core-types
  Objective: Define structured condition objects and a base condition hierarchy.
  Where: `runtime/src/condition.c` (new), `runtime/src/condition.h` (new), `docs/LANGUAGE_REFERENCE.md`
  What to change:
    - Add C structs for `Condition`, `ConditionType`, and a registry for condition types.
    - Define base types (e.g., `:error`, `:type-error`, `:arithmetic-error`, `:ffi-error`, `:memory-error`).
    - Expose constructors for conditions in the runtime API.
  How to verify: run a small C test that constructs a condition, sets fields, and renders it.
  Acceptance:
    - Condition objects can be created and queried from runtime code.
    - Base condition types are registered at init.

- [DONE] Label: T-cond-runtime-throw
  Objective: Route all runtime errors through condition objects instead of raw strings.
  Where: `runtime/src/runtime.c`, `runtime/src/condition.c`
  What to change:
    - Update `error`, `assert`, and FFI error hooks to produce `Condition` values.
    - Preserve existing `try` semantics while passing structured conditions.
    - Ensure uncaught conditions print full condition data (type + message).
  How to verify: run `./csrc/omnilisp -e '(error \"boom\")'` and confirm structured condition output.
  Acceptance:
    - All runtime errors emit `Condition` objects.
    - Uncaught errors include condition type and message.

- [DONE] Label: T-restart-core
  Objective: Implement restart stacks and named restart invocation.
  Where: `runtime/src/restart.c` (new), `runtime/src/restart.h` (new), `runtime/src/runtime.c`
  What to change:
    - Add a thread-local restart stack with named restart entries.
    - Expose runtime APIs: `restart_push`, `restart_pop`, `restart_invoke`.
    - Add minimal restart selection by name.
  How to verify: call a C test that registers two restarts and invokes one by name.
  Acceptance:
    - Restart stack supports push/pop/invoke.
    - Invoking a restart resumes control flow deterministically.

- [DONE] Label: T-cond-restart-syntax
  Objective: Provide OmniLisp syntax for conditions and restarts.
  Where: `omnilisp/src/runtime/eval/omni_eval.c`, `docs/LANGUAGE_REFERENCE.md`
  What to change:
    - Add forms: `handler-case`, `handler-bind`, `restart-case`, `signal`, `invoke-restart`, `find-restart`.
    - Implement condition handler stack with setjmp/longjmp for non-local control.
    - Implement restart frame stack with named restart invocation.
    - Document syntax and examples.
  How to verify: run OmniLisp programs that trigger errors and select restarts.
  Acceptance:
    - `handler-case` captures conditions and executes its handler.
    - `handler-bind` allows handlers to invoke restarts without unwinding.
    - `restart-case` makes restarts visible and invocable via `invoke-restart`.

- [DONE] Label: T-debug-logical-stack
  Objective: Add logical OmniLisp stack tracing for errors and condition reports.
  Where: `omnilisp/src/runtime/eval/omni_eval.c`
  What to change:
    - Added CallFrame struct with name, source, line fields.
    - Implemented call_stack array with push/pop/print helpers.
    - Instrumented omni_apply to track primitive and lambda calls.
    - Added (call-stack) special form with :print, :depth, :clear commands.
    - Added (stack-trace) special form to print formatted stack traces.
  How to verify: run `(define (foo x) (bar x)) (define (bar x) (call-stack :print)) (foo 42)` and confirm stack prints.
  Acceptance:
    - Logical stack shows function names at each call level.
    - Stack trace prints formatted output with depth.

- [TODO] Label: T-debug-macro-trace
  Objective: Emit macro expansion traces in diagnostics.
  Where: macro expansion path (preprocessor), `runtime/src/debug.c`, `docs/LANGUAGE_REFERENCE.md`
  What to change:
    - Track expansion parents and store expansion spans.
    - Print expansion chain in error reports.
  How to verify: trigger an error inside a macro expansion and confirm the expansion trace prints.
  Acceptance:
    - Error output includes macro expansion chain.

- [TODO] Label: T-debug-asap-why-freed
  Objective: Surface compile-time ASAP free reasons in runtime errors.
  Where: `csrc/analysis/analysis.c`, `csrc/codegen/codegen.c`, `runtime/src/runtime.c`
  What to change:
    - Emit a debug table mapping free sites to reason metadata (last use, escape class, capture).
    - On UAF/double-free errors, print the free reason.
  How to verify: trigger a controlled UAF in debug mode and confirm the “why freed” message.
  Acceptance:
    - Runtime errors explain the compile-time free decision.

- [TODO] Label: T-debug-rc-tracing
  Objective: Add debug-only RC tracing with source spans for inc/dec/free.
  Where: `runtime/src/runtime.c`, `csrc/codegen/codegen.c`
  What to change:
    - Add debug hooks in `inc_ref`/`dec_ref`/`free_*` that emit events.
    - Record source span IDs from codegen for each RC operation.
  How to verify: run with debug enabled and confirm RC events are logged.
  Acceptance:
    - RC logs show source spans and operation type.

- [TODO] Label: T-debug-continuations
  Objective: Make continuation/prompt frames visible in logical stack traces.
  Where: `runtime/src/memory/continuation.c`, `runtime/src/debug.c`
  What to change:
    - Add debug metadata to continuation frames (span + function).
    - Merge continuation frames into stack output on error.
  How to verify: trigger an error inside a continuation and confirm the stack includes continuation frames.
  Acceptance:
    - Continuation frames appear in debug stack traces.

---

## Type System Features

- [DONE] Label: T-enum-simple
  Objective: Implement simple enum types (variants without data).
  Where: `omnilisp/src/runtime/eval/omni_eval.c`
  What to change:
    - Parse `(define {enum Name} Var1 Var2 ...)` syntax.
    - Create dict values for each variant with __enum__, __variant__, __index__ fields.
    - Generate type predicate `Name?` and bind EnumName to variant list.
  How to verify: `./omni '(do (define {enum Color} Red Green Blue) (Color? Red))'` => true
  Acceptance:
    - Simple enums work with predicates.
    - Variants can be compared with `=`.

- [DONE] Label: T-enum-data
  Objective: Implement enums with data variants (algebraic data types).
  Where: `omnilisp/src/runtime/eval/omni_eval.c`, `match_pattern` function
  What to change:
    - Parse data variants: `(define {enum Option} (Some [value]) None)`.
    - Data variants become constructor functions that create dicts with data fields.
    - Simple variants remain as dict values.
    - Generate per-variant predicates: `Some?`, `None?`, etc.
    - Update pattern matching to destructure enum dicts by __variant__ field.
  How to verify: `./omni /tmp/test_enum_data.omni` with Option/Result tests passes.
  Acceptance:
    - `(Some 42)` creates enum value with data.
    - `(match (Some 42) [(Some v) v])` extracts value.
    - Both type (`Option?`) and variant (`Some?`) predicates work.

- [DONE] Label: T-default-params
  Objective: Add default parameter support for functions.
  Where: `omnilisp/src/runtime/eval/omni_eval.c`, `omnilisp/src/runtime/types.{c,h}`
  What to change:
    - Extended lambda struct with `defaults` field.
    - Parse `[param default]` in parameter lists.
    - Added `fill_defaults` helper to substitute defaults at call time.
    - Route `define` function definitions through `eval_lambda`.
  How to verify: `./omni '(do (define (greet [name "Guest"]) name) (greet))'` => "Guest"
  Acceptance:
    - Functions with defaults work when called without args.
    - Mixed required and optional parameters work.

---

## Effects/Handlers and Recovery Protocols (Supersedes restarts)

- [DONE] Label: T-eff-core-types
  Objective: Define effect and handler core types and runtime registry.
  Where: `runtime/src/effect.c`, `runtime/src/effect.h`, `runtime/tests/test_effect.c`
  What to change:
    - Added C structs: Effect, EffectType, Handler, HandlerClause, Resumption, RecoveryProtocol.
    - Implemented effect type registry with thread-safe registration and lookup.
    - Implemented handler stack with push/pop/find operations.
    - Added built-in effects: Fail, Ask, Emit, State, Yield, Async, Choice.
    - Added recovery modes: ONE_SHOT, MULTI_SHOT, TAIL, ABORT.
  How to verify: run `./tests/test_effect` - 16 tests pass.
  Acceptance:
    - Effects can be created and registered.
    - Handler stack resolves the nearest matching handler.
    - Built-in effect types available at startup.

- [DONE] Label: T-eff-perform
  Objective: Implement `perform` with continuation capture and resumption.
  Where: `runtime/src/effect.c`, `runtime/src/runtime.c`
  What to change:
    - Implemented `effect_perform` to capture delimited continuation at perform point.
    - Created resumption objects with proper refcounting and mode tracking.
    - Added TAG_RESUMPTION and TAG_EFFECT to runtime ObjTag enum.
    - Implemented `mk_resumption_obj`, `mk_effect_obj` and related functions.
    - Added `prim_resume`, `prim_resumption_valid`, `prim_perform` primitives.
    - Invoke handler functions via `call_closure` with (payload, resumption) args.
  How to verify: run `./tests/test_effect` - 22 tests pass.
  Acceptance:
    - `perform` captures continuation and transfers control to handler.
    - Resumptions can be invoked to continue computation.
    - One-shot/multi-shot/abort modes enforced correctly.

- [DONE] Label: T-eff-handler-syntax
  Objective: Provide OmniLisp syntax for `effect`, `perform`, and `handle`.
  Where: `omnilisp/src/runtime/eval/omni_eval.c`
  What to change:
    - Added special forms: handle, perform, resume, defeffect.
    - Implemented effect handler stack with setjmp/longjmp for control flow.
    - Handler syntax: `(handle body (effect-name (payload _) handler-body)...)`.
    - Perform syntax: `(perform effect-name payload)`.
    - Resume syntax: `(resume value)` to continue computation.
    - defeffect syntax: `(defeffect name :mode)` for recovery mode declaration.
  How to verify: run `./omni '(handle (+ 1 (perform ask nothing)) (ask (p _) (resume 42)))'` => 43.
  Acceptance:
    - `handle` intercepts effects and can resume computation.
    - Nested handlers work correctly.

- [DONE] Label: T-eff-recovery-protocols
  Objective: Add typed recovery protocols (better than untyped restarts).
  Where: `omnilisp/src/runtime/eval/omni_eval.c`
  What to change:
    - Implemented effect type registry with recovery modes in OmniLisp evaluator.
    - Added `(defeffect name :mode)` syntax to declare recovery modes.
    - Supported modes: :one-shot, :multi-shot, :abort, :tail.
    - Resume validation: abort effects can't be resumed, one-shot can only resume once.
  How to verify: run `./omni '(do (defeffect fail :abort) (handle ... (fail (p _) (resume 1))))'` => error.
  Acceptance:
    - Recovery protocols are enforced at runtime.
    - Abort mode prevents resume, one-shot prevents multiple resumes.

- [DONE] Label: T-eff-restart-compat
  Objective: Provide a compatibility layer that exposes CL-style restarts via effects.
  Where: `omnilisp/src/runtime/eval/omni_eval.c`
  What to change:
    - Added `(with-restarts ((name (params) body)...) expr)` syntax.
    - Added `(call-restart name value)` to invoke restarts.
    - Restarts implemented as effect handlers that automatically resume.
    - Restart handlers bind payload to parameters and evaluate body.
  How to verify: run `./omni '(with-restarts ((use-value (v) v)) (+ 1 (call-restart use-value 42)))'` => 43.
  Acceptance:
    - Restart compatibility works via the effect system.
    - with-restarts and call-restart behave like CL restarts.

- [DONE] Label: T-eff-debug-trace
  Objective: Add effect traces to the logical stack and diagnostics.
  Where: `omnilisp/src/runtime/eval/omni_eval.c`
  What to change:
    - Added effect trace buffer with 256-entry circular storage.
    - Record effect name and handler depth at each perform/call-restart.
    - Added `(effect-trace :on/:off/:clear/:print)` for trace control.
    - Added `(effect-stack)` to inspect current handler stack.
  How to verify: run with tracing enabled and use effect-trace :print to see trace.
  Acceptance:
    - Effect traces can be enabled/disabled/printed.
    - Effect stack inspection available.

## Diagnostics, Introspection, and Developer UX (Missing Pieces)

- [TODO] Label: T-diag-source-spans
  Objective: Ensure all AST nodes carry source spans to enable snippet+caret diagnostics everywhere.
  Where: `csrc/ast/ast.h`, `csrc/parser/parser.c`, `csrc/util/span.c` (new)
  What to change:
    - Add span fields to `OmniValue` and propagate in parser constructors.
    - Build a file table with line offsets for fast line/col mapping.
    - Expose a span resolver in the compiler/runtime for diagnostics.
  How to verify: force a parse error and confirm it prints correct line/col and snippet/caret.
  Acceptance:
    - All AST nodes have spans.
    - Diagnostics include accurate snippet+caret.

- [TODO] Label: T-diag-structured-output
  Objective: Produce structured diagnostics (text + JSON) for editor integration.
  Where: `csrc/util/diagnostic.c` (new), `csrc/compiler/compiler.c`, `csrc/cli/main.c`
  What to change:
    - Implement a diagnostic object model (severity, spans, notes, help).
    - Add `--diag=json` CLI flag to output machine-readable diagnostics.
    - Keep human-readable output as default.
  How to verify: run `./csrc/omnilisp --diag=json -e '(+ 1 )'` and confirm valid JSON diagnostics.
  Acceptance:
    - JSON diagnostics emitted on error.
    - Text diagnostics unchanged by default.

- [TODO] Label: T-diag-line-mapping
  Objective: Emit `#line` directives in generated C for native debugger source mapping.
  Where: `csrc/codegen/codegen.c`
  What to change:
    - Emit `#line <n> "<file>"` before function bodies and key expressions.
    - Ensure paths are stable and relative to project root.
  How to verify: compile and inspect generated C for `#line` directives and confirm gdb/llb backtrace points to source.
  Acceptance:
    - Generated C includes `#line` mapping.
    - Native debugger shows OmniLisp source locations.

## Truthiness & Nil Removal Cleanup

- [DONE] Label: T-truthy-tower
  Objective: Make Tower interpreter truthiness match language spec (only `false`/`nothing` falsy; `0` and `()` truthy).
  Where: `omnilisp/src/runtime/tower/tower.c`, `omnilisp/tests/test_pika_tower.c`
  What to change:
    - tower_truthy function already correctly implemented.
    - Added tests for `false` and `#f` being falsy to complete coverage.
    - Tests: `(if 0 1 2) => 1`, `(if () 1 2) => 1`, `(if nothing 1 2) => 2`, `(if false 1 2) => 2`.
  How to verify: `cd omnilisp/tests && ./test_pika_tower` - 64/64 tests pass.
  Acceptance:
    - Truthiness tests cover `0`, empty list, `false`, `#f`, `nothing`.
    - All 64 tests pass.

- [TODO] Label: T-truthy-codegen-standalone
  Objective: Align standalone AOT/JIT header truthiness with spec without conflating `0` with `false`.
  Where: `omnilisp/src/runtime/compiler/omni_compile.c`
  What to change:
    - [ ] Represent booleans distinctly (e.g., dedicated tag or interned symbols) instead of `mk_int(0/1)` in standalone header.
    - [ ] Update `obj_to_bool` to return false only for `false` literal and `NOTHING_VAL`.
    - [ ] Add a tiny AOT test program compiled via `aot_compile_to_file` that evaluates `(if 0 1 2)` → `1` and `(if false 1 2)` → `2`.
  How to verify: build the generated C and run it (`gcc -std=c99 -pthread prog.c && ./a.out`), assert outputs.
  Acceptance:
    - Generated standalone code treats numeric `0` as truthy, `false`/`nothing` as falsy.
    - Sample AOT/JIT smoke test passes.

- [DONE] Label: T-ctr-tag-empty
  Objective: Remove `nil` from type introspection results; emit meaningful tags for empty list and `nothing`.
  Where: `runtime/src/runtime.c`, `runtime/tests/test_primitives.c`
  What to change:
    - [x] Decided tag strings: `"list"` for empty list (NULL) and non-empty pairs, `"nothing"` for unit.
    - [x] Updated `ctr_tag` to return "list" for NULL and TAG_PAIR, "nothing" for TAG_NOTHING.
    - [x] Updated tests to expect "list" instead of "nil"/"cell".
  How to verify: `make -C runtime/tests test`; 454/454 tests pass.
  Acceptance:
    - No runtime output mentions `nil`.
    - Tests assert new tag strings.

- [DONE] Label: T-doc-nil-sweep
  Objective: Purge lingering `nil`/`t` references from docs and align truthiness text.
  Where: `docs/`, `ROADMAP.md`, `FUTURE_WORK.md`, `IMPLEMENTATION_PLAN.md`
  What to change:
    - [x] Replaced `nil` with `nothing` or `()` in all OmniLisp code examples.
    - [x] FUTURE_WORK.md: Fixed mk-Node examples and return values.
    - [x] IMPLEMENTATION_PLAN.md: Fixed literal lists and code examples.
    - [x] ROADMAP.md: Fixed all benchmark and test OmniLisp code snippets.
    - [x] docs/LANGUAGE_PARITY_PLAN.md: Fixed pattern matching example.
  How to verify: `rg -n "\bnil\b" *.md docs omnilisp` shows only Go code and design notes.
  Acceptance:
    - All OmniLisp code examples use `()` for empty list and `nothing` for unit.
    - All Go code removed from documentation.
    - Design docs correctly state "no nil" philosophy.

## Scientific Computing (BLAS & Torch)

- [TODO] Label: T-blas-core-bindings
  Objective: Ship a first-pass `SciComp.BLAS` module exposing CBLAS Level 1–3 ops with ownership-safe FFI signatures.
  Where: `omnilisp/modules/SciComp/BLAS.omni` (new), `SCIENTIFIC_COMPUTING_PLAN.md`
  What to change:
    - [ ] Define externs for core CBLAS calls (dot, axpy, gemv, gemm) with layout/transpose enums.
    - [ ] Add usage examples and minimal property tests (small matrices) comparing against reference C computations.
  How to verify: run BLAS smoke test script (to be added) that checks gemm on 2x2 matrices.
  Acceptance:
    - Module loads and calls into OpenBLAS successfully on Linux (mark `N/A` for platforms without BLAS).
    - Tests validate numeric results within tolerance.

- [TODO] Label: T-libtorch-c-wrap
  Objective: Scaffold C wrapper around LibTorch and publish Omnilisp externs mirroring the wrapper surface.
  Where: `third_party/libtorch_c_api.{h,cpp}` (new), `omnilisp/modules/SciComp/Torch.omni` (new), `SCIENTIFIC_COMPUTING_PLAN.md`
  What to change:
    - [ ] Generate/handwrite minimal C API for tensor creation, basic ops, and autograd entry points.
    - [ ] Add build script to produce `libomnitorch.so`/`.dylib`.
    - [ ] Provide Omnilisp extern bindings and a smoke test that creates two tensors and runs `torch_add`.
  How to verify: build wrapper (`./build_libtorch_wrapper.sh`) and run a sample Omnilisp program invoking `torch_add`.
  Acceptance:
    - Wrapper compiles and links against LibTorch on a supported platform.
    - Omnilisp call returns correct tensor data or graceful error if LibTorch unavailable.

- [TODO] Label: T-debug-breakpoints
  Objective: Add `break` / `debug` forms to pause evaluation and inspect locals in REPL.
  Where: `csrc/parser/parser.c`, `csrc/codegen/codegen.c`, `runtime/src/runtime.c`
  What to change:
    - Parse new forms `(break)` and `(debug expr)`.
    - When triggered, print locals and allow interactive continuation.
    - Provide REPL commands: `step`, `next`, `continue`.
  How to verify: run a program with `(break)` and confirm REPL pause + continue behavior.
  Acceptance:
    - Breakpoint pauses evaluation.
    - User can continue execution.

- [TODO] Label: T-debug-trace-enhance
  Objective: Improve `trace`/`untrace` to show call depth, args, and return values with filtering.
  Where: `runtime/src/runtime.c`, `csrc/codegen/codegen.c`, `docs/LANGUAGE_REFERENCE.md`
  What to change:
    - Implement per-function tracing with depth and arg/return logging.
    - Add optional filters by symbol name or namespace.
  How to verify: enable trace on a function and confirm call/return logs include args and values.
  Acceptance:
    - Trace output includes call depth and return values.

- [DONE] Label: T-introspect-types
  Objective: Add type/dispatch introspection helpers (`type-of`, `methods-of`, `type?`).
  Where: `omnilisp/src/runtime/eval/omni_eval.c`
  What to change:
    - Implemented `type-of` primitive returning type symbol (integer, symbol, list, function, etc.).
    - Implemented `describe` primitive returning human-readable type description.
    - Implemented `methods-of` primitive returning list of applicable methods per type.
    - Implemented `type?` primitive for type checking.
  How to verify: run `(type-of 42)` => integer, `(describe '(1 2))` => list info.
  Acceptance:
    - Type introspection helpers return stable, documented results.

- [TODO] Label: T-debug-weak-edge-explain
  Objective: Explain why a field was auto-marked weak (pattern + cycle reasoning).
  Where: `csrc/analysis/analysis.c`, `csrc/codegen/codegen.c`, `runtime/src/runtime.c`
  What to change:
    - Emit weak-edge metadata (pattern match vs cycle proof).
    - Provide a runtime query helper or diagnostic note.
  How to verify: define a doubly-linked type and confirm diagnostics explain the weak edge.
  Acceptance:
    - Weak-edge reasoning is visible in debug output.

- [TODO] Label: T-debug-region-explain
  Objective: Provide region mismatch explanations with source spans.
  Where: `runtime/src/memory/region.c`, `runtime/src/runtime.c`
  What to change:
    - Track source spans for region creation and cross-region references.
    - On mismatch, print region chain and source locations.
  How to verify: create a cross-scope ref and confirm the diagnostic explains the mismatch.
  Acceptance:
    - Region errors show source spans and suggested fixes.

- [TODO] Label: T-debug-borrow-explain
  Objective: Report borrow/tether misuse with origin and invalid access spans.
  Where: `runtime/src/runtime.c`, `csrc/codegen/codegen.c`
  What to change:
    - Record borrow origin spans in debug metadata.
    - Emit a diagnostic when invalid access occurs.
  How to verify: trigger a borrow misuse and confirm diagnostic includes origin + access spans.
  Acceptance:
    - Borrow misuse diagnostics are precise and actionable.

- [TODO] Label: T-debug-cycle-logs
  Objective: Log SCC/Component/Tether lifecycle events for cyclic structures in debug builds.
  Where: `runtime/src/memory/scc.c`, `runtime/src/memory/component.c`
  What to change:
    - Assign cycle IDs and emit creation/orphan/tether/free events.
    - Include object IDs and source spans when available.
  How to verify: create a cyclic structure and confirm cycle logs appear.
  Acceptance:
    - Cycle and Component lifecycle events are logged in debug mode.

- [DONE] Label: T-perf-tether-bench
  Objective: Benchmark zero-cost tethered access vs standard RC access.
  Where: `runtime/bench/bench_component.c`
  What to change:
    - Create benchmark cases for large cyclic graphs.
    - Compare performance with and without scope tethers.
  How to verify: `make -C runtime/tests bench` (or manual run).
  Acceptance:
    - Benchmarks show quantified speedup for tethered scopes (2.3x faster in initial tests).

- [TODO] Label: T-perf-regression-ci
  Objective: Integrate benchmarks into CI to prevent performance regression.
  Where: `.github/workflows/` or `Makefile`
  What to change:
    - Add a `make bench` target that runs benchmarks and checks against a baseline.
  How to verify: run `make bench` and see pass/fail based on timing.
  Acceptance:
    - CI fails if performance drops by >10%.

- [TODO] Label: T-debug-event-ring
  Objective: Add a bounded event ring buffer for alloc/free/rc/borrow events.
  Where: `runtime/src/debug.c`, `runtime/src/debug.h`, `runtime/src/runtime.c`
  What to change:
    - Implement a fixed-size ring buffer with event types and span IDs.
    - Add REPL command `events N` to display recent events.
  How to verify: trigger allocations and confirm `events` prints recent history.
  Acceptance:
    - Event ring captures and prints recent actions.

- [TODO] Label: T-replay-deterministic
  Objective: Add deterministic replay for debugging (scheduler + RNG).
  Where: `runtime/src/runtime.c`, `runtime/src/memory/continuation.c`
  What to change:
    - Record scheduling decisions and random seeds.
    - Provide a replay mode flag and ensure runs are deterministic.
  How to verify: run the same program twice with replay mode and confirm identical output and event order.
  Acceptance:
    - Deterministic replay reproduces behavior reliably.

- [TODO] Label: T-diag-error-ids
  Objective: Assign stable error IDs for documentation and test matching.
  Where: `runtime/src/condition.c`, `csrc/util/diagnostic.c`, `docs/LANGUAGE_REFERENCE.md`
  What to change:
    - Add numeric or symbolic error IDs to each condition type.
    - Include IDs in diagnostic output.
  How to verify: trigger an error and confirm the ID appears in output.
  Acceptance:
    - All conditions emit stable IDs.

- [DONE] Label: T-diag-symbol-suggestions
  Objective: Provide "did you mean?" suggestions for unbound symbols.
  Where: `omnilisp/src/runtime/eval/omni_eval.c`
  What to change:
    - Implemented Levenshtein edit distance function.
    - Added find_similar_symbols function that searches primitives and environment.
    - Returns top 3 suggestions with distance <= 2.
    - Deduplicates suggestions before display.
  How to verify: run `(printn 42)` => suggests 'print', 'println'.
  Acceptance:
    - Unbound symbol errors include relevant suggestions.

- [TODO] Label: T-debug-tests-golden
  Objective: Add golden tests for diagnostics, conditions, and stack traces.
  Where: `csrc/tests/test_diagnostics.c` (new), `runtime/tests/`
  What to change:
    - Add tests for error formatting, macro traces, and stack traces.
    - Store expected output in golden files.
  How to verify: run test suite and confirm golden comparisons pass.
  Acceptance:
    - Golden diagnostics tests pass.

- [TODO] Label: T-debug-tests-memory
  Objective: Add tests for UAF, double-free, borrow misuse, and region errors.
  Where: `runtime/tests/`
  What to change:
    - Create targeted tests that trigger each memory error.
    - Assert the diagnostic output contains required context.
  How to verify: run runtime tests and confirm all memory diagnostics tests pass.
  Acceptance:
    - Memory error diagnostics are exercised and validated.

---

## Macro System

| Feature | Status | Description |
|---------|--------|-------------|
| Quasiquote `` ` `` | ✅ | Quote with evaluation |
| Unquote `,` | ✅ | Evaluate in quasiquote |
| Unquote-splicing `,@` | ✅ | Splice list |
| `defmacro` | ✅ | Define macro with transformer |
| `mcall` | ✅ | Call macro by name |
| `macroexpand` | ✅ | Expand without eval |

---

## Implementation Details

### Variable Representation
| Feature | Status | Notes |
|---------|--------|-------|
| Named variables | ✅ | Current approach |
| De Bruijn indices | ❌ | Original uses indices |

### Memory Management
| Feature | Status | Notes |
|---------|--------|-------|
| ASAP free insertion | ✅ | Compile-time |
| Shape analysis | ✅ | TREE/DAG/CYCLIC |
| Arena allocation | ✅ | For cyclic data |
| Weak edges | ✅ | Break ownership cycles |
| HVM4 interaction nets | ❌ | Original uses HVM4 |

---

## Priority Order for Implementation

### High Priority (Core Language) - ✅ COMPLETE
1. ✅ **Pattern matching** - fundamental for idiomatic code
2. ✅ **Recursive lambda** - `(lambda self ...)` for cleaner recursion
3. ✅ **Error handling** - `error`, `try`, `assert`
4. ✅ **List operations** - `map`, `filter`, `fold`, etc.

### Medium Priority (Staging) - ✅ COMPLETE
5. ✅ **`run` form** - execute code at base level
6. ✅ **Meta-level operations** - EM, shift, clambda, meta-level
7. ✅ **Handler customization** - 9-handler table with get/set-meta!, with-handlers

### Lower Priority (Convenience) - ✅ COMPLETE
8. ✅ **Quasiquote** - template syntax
9. ✅ **Macro system** - syntactic abstraction (defmacro, mcall, macroexpand)
10. ✅ **FFI/I/O** - practical programs
11. ✅ **Characters/strings** - text handling
12. ✅ **Introspection** - gensym, eval, sym-eq?, trace
13. ✅ **JIT execution** - Runtime C code execution via GCC

---

## Fiber System Refactor

Redesign concurrency with clear separation: **fibers** (lightweight cooperative) vs **threads** (OS-level parallelism).

See `FIBER_SYSTEM_PLAN.md` for full design.

### Phase 1: Remove pthread Channels

- [DONE] Label: T-fiber-rm-pthread-chan
  Objective: Remove pthread-based channel implementation from runtime.
  Where: `runtime/src/runtime.c` (lines ~2615-2810)
  What to change:
    - [x] Remove `Channel` struct with pthread mutex/cond
    - [x] Remove `make_channel()`, `channel_send()`, `channel_recv()`, `channel_close()`
    - [x] Remove `free_channel()`
    - [x] Keep `TAG_CHANNEL` for continuation-based channels
  How to verify: `make -C runtime && make -C runtime/tests test` passes without pthread channel code.
  Acceptance:
    - No pthread-based channel code in runtime.c
    - TAG_CHANNEL repurposed for fiber channels

### Phase 2: Rename Internal APIs

- [DONE] Label: T-fiber-rename-internals
  Objective: Rename fiber infrastructure to fiber terminology.
  Where: `runtime/src/memory/continuation.c`, `runtime/src/memory/continuation.h`
  What to change:
    - [x] Rename `Task` struct → `Fiber`
    - [x] Rename `spawn_green()` → `fiber_spawn()`
    - [x] Rename `yield_green()` → `fiber_yield()`
    - [x] Rename `task_park()` → `fiber_park()`
    - [x] Rename `task_unpark()` → `fiber_unpark()`
    - [x] Rename `task_current()` → `fiber_current()`
    - [x] Rename `GreenChannel` → `FiberChannel`
    - [x] Update all call sites
  How to verify: grep for old names returns nothing; tests pass.
  Acceptance:
    - All internal APIs use fiber terminology
    - All 454 runtime tests pass

### Phase 3: Add Fiber Syntax Forms

- [N/A] Label: T-fiber-syntax
  Objective: Add fiber-related syntax forms to reader/parser.
  Where: `omnilisp/src/runtime/reader/omni_reader.c`, `omnilisp/SYNTAX.md`
  Reason: Not needed - fiber primitives work as regular function calls.
  The reader already handles (fiber ...), (resume ...), (spawn ...) etc.
  as standard S-expressions. No special syntax required.

### Phase 4: Implement Fiber Primitives

- [DONE] Label: T-fiber-primitives
  Objective: Wire up fiber primitives in evaluator.
  Where: `omnilisp/src/runtime/eval/omni_eval.c`
  What to change:
    - [x] Add `prim_fiber` - create paused fiber from thunk
    - [x] Add `prim_resume` - step into fiber (integrated with effect system resume)
    - [x] Add `prim_yield` - yield from current fiber
    - [x] Add `prim_spawn` - add fiber to scheduler run queue
    - [x] Add `prim_join` - wait for fiber completion
    - [x] Add `prim_run_fibers` - run all spawned fibers
    - [x] Add `prim_fiber_q` - fiber predicate
    - [x] Add `prim_fiber_done_q` - check if fiber done
    - [x] Register all in `omni_env_init()`
  How to verify: `(resume (fiber (lambda () (+ 1 2))))` → `3`
  Acceptance:
    - All fiber primitives callable from OmniLisp
    - Basic fiber creation and resumption works
    - Note: yield requires continuation capture for true suspension

### Phase 5: Implement Channel Primitives

- [DONE] Label: T-fiber-channels
  Objective: Wire up channel primitives using continuation-based channels.
  Where: `omnilisp/src/runtime/eval/omni_eval.c`
  What to change:
    - [x] Add `prim_chan` - create channel (optional capacity)
    - [x] Add `prim_send` - send to channel (parks if full/unbuffered)
    - [x] Add `prim_recv` - receive from channel (parks if empty)
    - [x] Add `prim_chan_close` - close channel
    - [x] Add `prim_chan_q` - channel predicate
    - [x] Register all in `omni_env_init()`
  How to verify: producer-consumer test with buffered channels.
  Acceptance:
    - Channels work for fiber communication
    - Buffered channels work correctly

### Phase 6: Implement with-fibers Scoped Context

- [DONE] Label: T-fiber-scoped
  Objective: Add scoped fiber cleanup form.
  Where: `omnilisp/src/runtime/eval/omni_eval.c`
  What to change:
    - [x] Add `eval_with_fibers` special form handler
    - [x] Track spawned fibers in scope
    - [x] On scope exit: run scheduler until all fibers complete
    - [x] Return body result after cleanup
  How to verify: `(with-fibers (spawn f1) (spawn f2) body)` runs all fibers.
  Acceptance:
    - Spawned fibers cleaned up on scope exit
    - Nested with-fibers works correctly

### Phase 7: Rename OS Thread Primitives

- [TODO] Label: T-fiber-os-thread-rename
  Objective: Clear naming distinction between fibers and OS threads.
  Where: `runtime/src/runtime.c`, `omnilisp/src/runtime/eval/omni_eval.c`
  What to change:
    - [ ] Rename `spawn_thread()` → `os_thread_create()`
    - [ ] Rename `thread_join()` → `os_thread_join()`
    - [ ] Remove `spawn_goroutine()` (use fibers instead)
    - [ ] Add OmniLisp primitives: `thread`, `thread-join`, `thread-id`
    - [ ] Keep `atomic`, `mutex` for OS-level sync
  How to verify: OS thread primitives work independently of fibers.
  Acceptance:
    - Clear separation: `fiber` = lightweight, `thread` = OS-level
    - Both systems work independently

### Phase 8: Add Thread Pool for Fiber Scheduler

- [DONE] Label: T-fiber-pool
  Objective: Allow fibers to run across multiple OS threads with work-stealing.
  Where: `runtime/src/memory/continuation.c`, `runtime/src/memory/continuation.h`
  What to change:
    - [x] Add `ThreadPool` struct with N worker threads
    - [x] Add `WorkStealDeque` - Chase-Lev lock-free deque with CAS
    - [x] Add per-worker local queues + global overflow queue
    - [x] Add `fiber_pool_init(n)` and `fiber_pool_shutdown()`
    - [x] Add `fiber_pool_spawn()`, `fiber_pool_current_worker()`
    - [x] Implement work-stealing: local → global → steal from others
  How to verify: `runtime/tests/test_threadpool` - all tests pass with steals.
  Acceptance:
    - Fibers can run on any worker thread
    - Work-stealing for load balancing (1000 steals in test)
    - Auto-detect CPU count when n=0

### Phase 9: Nested Fiber Resume

- [DONE] Label: T-fiber-nested
  Objective: Fix nested fiber resume (fiber A resuming fiber B).
  Where: `omnilisp/src/runtime/eval/omni_eval.c`
  What to change:
    - [x] Replace single `fiber_resumer_ctx` with stack `fiber_resumer_stack[MAX_FIBER_DEPTH]`
    - [x] Add `fiber_resumer_sp` stack pointer
    - [x] Update `fiber_entry`, `fiber_resume_internal`, `prim_yield` to use stack
    - [x] Update `prim_send`, `prim_recv` for proper context switching
  How to verify: `omnilisp/test_nested_fiber.lisp` - deep nesting works.
  Acceptance:
    - Nested resume works without stack corruption
    - Deep nesting (A→B→C→D) returns correct values

---

## Core Language Features (from UNIMPLEMENTED_FEATURES.md)

### Match as Core Primitive

- [TODO] Label: T-match-core
  Objective: Implement full `match` with pattern destructuring as THE core control primitive in C.
  Where: `omnilisp/src/runtime/eval/omni_eval.c`
  What to change:
    - [ ] Implement pattern destructuring: literals, symbols, cons, wildcards
    - [ ] Add guard clauses: `[pattern :when guard body]`
    - [ ] Add nested patterns: `[(cons (cons a b) c) ...]`
    - [ ] Optimize: compile patterns to decision trees
  How to verify: `(match '(1 2) [(cons a (cons b ())) (+ a b)])` => 3
  Acceptance:
    - Match handles all pattern types
    - Destructuring binds variables correctly
    - Guards evaluated lazily

- [TODO] Label: T-if-cond-macro
  Objective: Convert `if` and `cond` to macros over `match`.
  Where: `omnilisp/src/runtime/eval/omni_eval.c`, `omnilisp/prelude.omni`
  What to change:
    - [ ] Remove `eval_if` and `eval_cond` special form handlers
    - [ ] Define `if` as macro: `(match test [#t then] [#f else])`
    - [ ] Define `cond` as macro over nested match
    - [ ] Load macros in prelude before user code
  How to verify: `(if #t 1 2)` => 1 via macro expansion
  Acceptance:
    - if/cond work as macros
    - No special form handlers needed

### Type System

- [TODO] Label: T-string-type
  Objective: Add T_STRING and T_CHAR types with full string operations.
  Where: `omnilisp/src/runtime/types.h`, `omnilisp/src/runtime/types.c`, `omnilisp/src/runtime/eval/omni_eval.c`
  What to change:
    - [ ] Add T_STRING, T_CHAR to Tag enum
    - [ ] Add mk_string(), mk_char() constructors
    - [ ] Add string primitives: string-length, string-append, substring, string-ref
    - [ ] Add char primitives: char->int, int->char, char=?
    - [ ] Parse string literals "hello" and char literals #\a
  How to verify: `(string-append "hello" " " "world")` => "hello world"
  Acceptance:
    - String literals work
    - All string operations implemented

- [TODO] Label: T-float-type
  Objective: Add T_FLOAT type with transcendental math functions.
  Where: `omnilisp/src/runtime/types.h`, `omnilisp/src/runtime/eval/omni_eval.c`
  What to change:
    - [ ] Add T_FLOAT to Tag enum with double storage
    - [ ] Add mk_float() constructor
    - [ ] Parse float literals: 3.14, 1.0e-5
    - [ ] Add math primitives: sin, cos, tan, exp, log, sqrt, pow
    - [ ] Update arithmetic ops to handle mixed int/float
  How to verify: `(sin 0.0)` => 0.0, `(sqrt 2.0)` => 1.414...
  Acceptance:
    - Float literals parse correctly
    - Transcendental functions available

### Data Structure Access

- [TODO] Label: T-array-access
  Objective: Implement array indexing `arr.(i)` and slicing `arr.[start:end]`.
  Where: `omnilisp/src/runtime/eval/omni_eval.c`, `omnilisp/src/runtime/reader/omni_reader.c`
  What to change:
    - [ ] Parse `arr.(i)` as (array-ref arr i)
    - [ ] Parse `arr.[start:end]` as (array-slice arr start end)
    - [ ] Parse `arr.[::step]` for stride slicing
    - [ ] Implement array-ref, array-set!, array-slice primitives
    - [ ] Support negative indices (Python-style)
  How to verify: `(let [a (array 1 2 3)] a.(1))` => 2
  Acceptance:
    - Dot-paren indexing works
    - Bracket slicing works
    - Mutation via set! works

- [TODO] Label: T-dict-access
  Objective: Implement dict key access `dict.:key` and key/value operations.
  Where: `omnilisp/src/runtime/eval/omni_eval.c`, `omnilisp/src/runtime/reader/omni_reader.c`
  What to change:
    - [ ] Parse `dict.:key` as (dict-ref dict :key)
    - [ ] Parse `(set! dict.:key val)` as (dict-set! dict :key val)
    - [ ] Implement keys, values, entries primitives
    - [ ] Implement dict-has?, dict-remove
  How to verify: `(let [d (dict :a 1 :b 2)] d.:a)` => 1
  Acceptance:
    - Dot-colon key access works
    - keys/values return lists

- [TODO] Label: T-struct-enum
  Objective: Add struct and enum type definitions.
  Where: `omnilisp/src/runtime/eval/omni_eval.c`, `omnilisp/src/runtime/types.h`
  What to change:
    - [ ] Add T_STRUCT, T_ENUM tags
    - [ ] Parse `(define {struct Name} [field type]...)`
    - [ ] Parse `(define {enum Name} Variant1 Variant2...)`
    - [ ] Generate constructor, predicate, field accessors
    - [ ] Integrate with match for pattern matching
  How to verify: `(define {struct Point} [x int] [y int])` creates Point, Point?, Point-x
  Acceptance:
    - Struct creation and access works
    - Enums work with match

### Bindings

- [TODO] Label: T-destructuring
  Objective: Implement destructuring in let/define bindings.
  Where: `omnilisp/src/runtime/eval/omni_eval.c`
  What to change:
    - [ ] Handle list patterns: `(let [[a b] '(1 2)] ...)`
    - [ ] Handle cons patterns: `(let [(cons h t) list] ...)`
    - [ ] Handle nested patterns
    - [ ] Integrate with match pattern compiler
  How to verify: `(let [[a b c] '(1 2 3)] (+ a b c))` => 6
  Acceptance:
    - List destructuring works
    - Nested patterns work

### External Integration

- [TODO] Label: T-ffi
  Objective: Implement FFI for C library integration.
  Where: `omnilisp/src/runtime/eval/omni_eval.c`, new `omnilisp/src/runtime/ffi.c`
  What to change:
    - [ ] Add `(extern name)` to declare external C functions
    - [ ] Add `(opaque Handle :destructor free)` for opaque types
    - [ ] Add `(@ffi "lib.so" "func" ret-type arg-types...)` for dynamic loading
    - [ ] Handle type conversions between OmniLisp and C
    - [ ] Use libffi or dlopen/dlsym
  How to verify: `(extern sin) (sin 0.0)` => 0.0
  Acceptance:
    - Can call C library functions
    - Opaque handles managed correctly

- [TODO] Label: T-file-io
  Objective: Add file I/O operations.
  Where: `omnilisp/src/runtime/eval/omni_eval.c`
  What to change:
    - [ ] Add T_PORT type for file handles
    - [ ] Implement open, close, read-line, read-char, write, flush
    - [ ] Add `(with-open-file [f "path" :read] ...)` for RAII
    - [ ] Handle stdin/stdout/stderr as default ports
  How to verify: `(with-open-file [f "test.txt" :write] (write f "hello"))`
  Acceptance:
    - File read/write works
    - Ports cleaned up on scope exit

- [TODO] Label: T-modules
  Objective: Complete module loading and namespace isolation.
  Where: `omnilisp/src/runtime/eval/omni_eval.c`, new `omnilisp/src/runtime/module.c`
  What to change:
    - [ ] Implement module registry with path resolution
    - [ ] Add `(module name (export ...) body...)` with real isolation
    - [ ] Add `(import module)` that actually loads file
    - [ ] Enforce visibility (exported vs private)
    - [ ] Handle circular imports
  How to verify: Create two modules, import one from other
  Acceptance:
    - Modules load from filesystem
    - Exports are enforced

### Concurrency Extensions

- [TODO] Label: T-system-threads
  Objective: Add OS-level thread primitives for true parallelism.
  Where: `omnilisp/src/runtime/eval/omni_eval.c`
  What to change:
    - [ ] Add `(thread (lambda () ...))` to spawn pthread
    - [ ] Add `(thread-join t)` to wait for completion
    - [ ] Add `(thread-id)` to get current thread ID
    - [ ] Add thread-safe channel variant for cross-thread communication
    - [ ] Add `(atomic-cas! ref old new)` for lock-free ops
  How to verify: Two threads incrementing shared counter with atomic ops
  Acceptance:
    - Threads run in parallel (not just concurrent)
    - Thread-safe primitives work

---

## References

- "Collapsing Towers of Interpreters" (Amin & Rompf, POPL 2018)
