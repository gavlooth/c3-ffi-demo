# Language Feature Parity Plan

## Goal
Make the native compiler (pkg/compiler) support all **language constructs**
that the Go interpreter (pkg/eval) supports.

**Note**: This is LANGUAGE parity (compiler features), not RUNTIME parity.
The C runtime (runtime/src/runtime.c) already has all necessary primitives.

This aligns with the architectural principle:
> "Go is compiler-only - Go must NEVER be in runtime/interpretation hot paths."

## Priority Tiers

### Tier 1: Essential (Required for Production)
These features are commonly used and blocking production adoption.

| Feature | Current Status | Implementation Strategy | Effort |
|---------|---------------|------------------------|--------|
| `match` (pattern matching) | Interpreter only | Compile to nested if/switch in C | Medium |
| `cond` | Missing everywhere | Compile to chained if-else | Easy |
| `try`/`catch`/`error` | Partial | Use setjmp/longjmp in C runtime | Medium |
| Full `deftype` | Type registry only | Generate mk-*, accessors, predicates | Medium |

### Tier 2: Concurrency (Required for Concurrent Programs)
The compiler has OS threads; need full channel/select support.

| Feature | Current Status | Implementation Strategy | Effort |
|---------|---------------|------------------------|--------|
| `chan` (buffered) | Compiled to pthread mutex | Already works | Done |
| `>!` / `<!` (send/recv) | Compiled | Already works | Done |
| `select` | Interpreter only | Compile to poll-based select | Hard |
| `atom`/`deref`/`reset!`/`swap!` | Compiler only | Expose via primitives | Easy |
| Green thread scheduler | Interpreter (goroutines) | Not needed - use OS threads | Skip |

### Tier 3: Metaprogramming (Nice to Have)
These enable advanced patterns but aren't essential.

| Feature | Current Status | Implementation Strategy | Effort |
|---------|---------------|------------------------|--------|
| `defmacro` | Interpreter only | Pre-compile macro expansion phase | Hard |
| `quasiquote`/`unquote` | Interpreter only | AST rewriting before compilation | Medium |
| `gensym` | Interpreter only | Compile-time symbol generation | Easy |
| `eval` (runtime) | Interpreter only | Embed mini-interpreter or skip | Very Hard |

### Tier 4: Continuations (Advanced)
These require significant architectural changes.

| Feature | Current Status | Implementation Strategy | Effort |
|---------|---------------|------------------------|--------|
| `call/cc` | Interpreter only | CPS transform entire program | Very Hard |
| `prompt`/`control` | Interpreter only | Delimited continuation monad | Very Hard |
| `shift`/`reset` | Interpreter only | Same as above | Very Hard |

**Recommendation**: Skip Tier 4 for now. Most programs don't need first-class continuations.

---

## Implementation Plan

### Phase 1: Pattern Matching & Conditionals
**Goal**: Make `match` and `cond` work in compiled code.

#### 1.1 Add `cond` to compiler (Easy - 1 day)
```scheme
;; Source
(cond
  ((< x 0) "negative")
  ((= x 0) "zero")
  (t "positive"))

;; Compiles to:
if (x < 0) { result = "negative"; }
else if (x == 0) { result = "zero"; }
else { result = "positive"; }
```

**Files to modify:**
- `pkg/compiler/compiler.go`: Add `compileCond()` function
- Pattern: chain of if-else statements

#### 1.2 Add `match` to compiler (Medium - 3-5 days)
```scheme
;; Source
(match expr
  ((Nil) "empty")
  ((Cons h t) (process h t))
  (_ "other"))

;; Compiles to:
if (expr == NULL) { result = "empty"; }
else if (expr->tag == TAG_PAIR) {
    Obj* h = expr->a;
    Obj* t = expr->b;
    result = process(h, t);
}
else { result = "other"; }
```

**Files to modify:**
- `pkg/compiler/compiler.go`: Add `compileMatch()` function
- `pkg/compiler/pattern.go`: New file for pattern compilation
- Handle: literals, wildcards, constructors, nested patterns

### Phase 2: Error Handling
**Goal**: Full try/catch/error support in compiled code.

#### 2.1 Add setjmp/longjmp error handling (Medium - 2-3 days)
```c
// Runtime addition
typedef struct ErrorContext {
    jmp_buf buf;
    Obj* error;
    struct ErrorContext* parent;
} ErrorContext;

static __thread ErrorContext* current_error_ctx = NULL;

Obj* try_catch(Obj* (*thunk)(void), Obj* (*handler)(Obj*)) {
    ErrorContext ctx;
    ctx.parent = current_error_ctx;
    current_error_ctx = &ctx;

    if (setjmp(ctx.buf) == 0) {
        Obj* result = thunk();
        current_error_ctx = ctx.parent;
        return result;
    } else {
        current_error_ctx = ctx.parent;
        return handler(ctx.error);
    }
}

void raise_error(Obj* err) {
    if (current_error_ctx) {
        current_error_ctx->error = err;
        longjmp(current_error_ctx->buf, 1);
    }
    // No handler - abort
    fprintf(stderr, "Unhandled error\n");
    exit(1);
}
```

**Files to modify:**
- `runtime/src/runtime.c`: Add ErrorContext, try_catch, raise_error
- `pkg/compiler/compiler.go`: Compile `try`/`catch`/`error` forms

### Phase 3: User-Defined Types
**Goal**: Full `deftype` support with generated constructors.

#### 3.1 Generate type constructors (Medium - 2-3 days)
```scheme
;; Source
(deftype Point (x y))

;; Generates:
;; mk-Point : (x, y) -> Point
;; Point? : obj -> bool
;; Point-x : Point -> x
;; Point-y : Point -> y
```

**Files to modify:**
- `pkg/compiler/compiler.go`: Add `compileDeftype()`
- `pkg/compiler/types.go`: Track user types and generate accessors
- `runtime/src/runtime.c`: Add generic `mk_usertype()` function

### Phase 4: Concurrency Completion
**Goal**: Full channel/atom support.

#### 4.1 Add `select` to compiler (Hard - 3-5 days)
```scheme
;; Source
(select
  ((recv ch1) => (lambda (v) (handle-ch1 v)))
  ((recv ch2) => (lambda (v) (handle-ch2 v)))
  (default => (lambda () (handle-timeout))))

;; Compiles to poll-based selection
```

**Implementation options:**
1. **Poll-based**: Loop checking all channels with short sleep
2. **Condition variable**: Use pthread_cond_wait with multiple producers
3. **Pipe/eventfd**: Use OS primitives for wakeup

**Files to modify:**
- `runtime/src/runtime.c`: Add `channel_select()` function
- `pkg/compiler/compiler.go`: Compile `select` form

#### 4.2 Expose atoms as primitives (Easy - 1 day)
Already implemented in runtime, just need compiler support.

### Phase 5: Macro System (Optional)
**Goal**: Compile-time macro expansion.

#### 5.1 Pre-expansion phase (Hard - 5-7 days)
```
Source → Parse → Macro Expand → Compile → C Code
                     ↑
              Uses Go interpreter
              for macro bodies
```

**Strategy**:
- Macros run at compile time using Go interpreter
- Expanded AST is then compiled to C
- This keeps Go out of runtime path

**Files to modify:**
- `pkg/compiler/macro.go`: New file for macro expansion
- `pkg/compiler/compiler.go`: Add expansion phase before compilation

---

## Timeline Estimate

| Phase | Features | Effort | Priority |
|-------|----------|--------|----------|
| Phase 1 | cond, match | 4-6 days | P0 |
| Phase 2 | try/catch/error | 2-3 days | P0 |
| Phase 3 | deftype full | 2-3 days | P1 |
| Phase 4.1 | select | 3-5 days | P1 |
| Phase 4.2 | atoms | 1 day | P2 |
| Phase 5 | macros | 5-7 days | P2 |

**Total for P0+P1**: ~2-3 weeks
**Total for all**: ~4-5 weeks

---

## What to Skip (Intentionally)

### Continuations (call/cc, prompt/control)
- Require CPS transformation of entire program
- Massive complexity for rare use case
- Alternative: Use explicit state machines

### Runtime eval
- Would require embedding interpreter in C runtime
- Violates "no GC, no Go in runtime" principle
- Alternative: Pre-compile all code

### Green thread scheduler
- OS threads (pthreads) are sufficient
- Green threads add complexity without clear benefit
- Already have spawn_thread/thread_join

---

## Testing Strategy

For each phase:
1. Add tests to `pkg/compiler/compiler_test.go`
2. Add validation tests to `test/validation/`
3. Verify output matches Go interpreter (reference)

```go
// Example test pattern
func TestMatch(t *testing.T) {
    code := "(match (cons 1 2) ((Cons h t) h) (_ 0))"

    // Compile and run
    result := compileAndRun(code)

    // Compare with interpreter
    expected := interpret(code)

    if result != expected {
        t.Errorf("match: got %v, want %v", result, expected)
    }
}
```

---

## Next Steps

1. Start with Phase 1.1 (`cond`) - easiest win
2. Then Phase 2 (error handling) - commonly needed
3. Then Phase 1.2 (`match`) - enables many patterns
4. Evaluate need for remaining phases based on usage
