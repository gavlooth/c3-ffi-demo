# OmniLisp TODO

## Conditions, Restarts, and Debugging Integration

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

- [TODO] Label: T-debug-logical-stack
  Objective: Add logical OmniLisp stack tracing for errors and condition reports.
  Where: `runtime/src/debug.c` (new), `runtime/src/debug.h` (new), `csrc/codegen/codegen.c`
  What to change:
    - Implement `dbg_push`, `dbg_pop`, `dbg_set_loc`, and `dbg_print_stack`.
    - Inject stack push/pop in generated functions and on error boundaries.
    - Print logical stack for uncaught conditions.
  How to verify: run a nested function call with a forced error and confirm the OmniLisp stack prints.
  Acceptance:
    - Logical stack shows function names + source locations.
    - Stack trace appears on uncaught conditions.

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

- [TODO] Label: T-introspect-types
  Objective: Add type/dispatch introspection helpers (`type-of`, `methods-of`, `subtype?`).
  Where: `runtime/src/runtime.c`, `docs/LANGUAGE_REFERENCE.md`
  What to change:
    - Implement primitives: `type-of`, `subtype?`, `methods-of`, `describe`.
    - Connect to the method/dispatch registry if present.
  How to verify: run REPL and check introspection results for core types.
  Acceptance:
    - Introspection helpers return stable, documented results.

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
  Objective: Log SCC/symmetric RC lifecycle events for cyclic structures in debug builds.
  Where: `runtime/src/memory/scc.c`, `runtime/src/memory/symmetric.c`
  What to change:
    - Assign cycle IDs and emit creation/orphan/free events.
    - Include object IDs and source spans when available.
  How to verify: create a cyclic structure and confirm cycle logs appear.
  Acceptance:
    - Cycle lifecycle events are logged in debug mode.

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

- [TODO] Label: T-diag-symbol-suggestions
  Objective: Provide “did you mean?” suggestions for unbound symbols.
  Where: `runtime/src/runtime.c`
  What to change:
    - Implement symbol similarity search (edit distance or prefix match).
    - Include top suggestions in unbound symbol errors.
  How to verify: reference a misspelled symbol and confirm suggested alternatives.
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

## References

- Original OmniLisp: `/home/heefoo/Documents/code/omnilisp`
- "Collapsing Towers of Interpreters" (Amin & Rompf, POPL 2018)
- HVM4: Higher Order Virtual Machine
