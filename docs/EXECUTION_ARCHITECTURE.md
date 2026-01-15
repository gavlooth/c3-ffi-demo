# OmniLisp Execution Architecture

OmniLisp uses a high-performance, GC-less execution model based on **CTRR (Compile-Time Region Reclamation)** and region control blocks (regions reclaimed in bulk).

---

## 1. Unified Pipeline (C99)

The OmniLisp toolchain targets **C99 + POSIX + extensions**, ensuring portability while allowing practical low-level optimizations (e.g., TLS).

```text
Source.omni ──► Pika Parser ──► AST (Value*) ──► Region Inference ──► Codegen ──► Binary
```

### Key Stages:
1.  **Pika Parser**: A bottom-up dynamic programming parser that handles left recursion natively and provides optimal error recovery.
2.  **Region Inference**: A static analysis pass that identifies object lifetimes and groups them into Regions.
3.  **CTRR Scheduling**: Determines region lifetimes and escape points, injecting `region_exit` and escape repair (e.g., `transmigrate`) at the right boundaries.
4.  **Codegen**: Translates the annotated AST into optimized C code linked against the OmniLisp Runtime.

---

## 2. Runtime Model (CTRR)

The runtime provides the physical foundation for CTRR.

- **Regions**: Logical owners of memory blocks.
- **Arenas**: physical storage backend (bump-pointer allocation).
- **Tethers**: Thread-local mechanisms for safe concurrent access without global locks.
- **Transmigration**: Efficient movement of data between regions using iterative deep copy or block splicing.

---

## 3. Concurrency Model

OmniLisp supports two levels of concurrency:
1.  **Fibers**: Lightweight, cooperative coroutines managed by the runtime (stack-switching via `ucontext`).
2.  **Threads**: OS-level parallelism using POSIX pthreads, synchronized via Region Tethers.

---

## 4. Control Flow Architecture

OmniLisp has two **intentionally separate** control flow mechanisms:

### 4.1 Trampolines (Tail Call Optimization)

**Location:** `runtime/src/trampoline.c`

Trampolines provide stack-safe mutual recursion via explicit thunks ("bounce" objects).
They are a simple, self-contained mechanism that does NOT use continuations.

```text
func() → bounce(next_func, args) → trampoline loop → func() → ... → result
```

- **Purpose:** Prevent stack overflow in tail-recursive code
- **Implementation:** Mark field hack to store function pointers in bounce objects
- **Scope:** Local optimization, no control flow capture

### 4.2 Continuations & CEK Machine (Advanced Control Flow)

**Location:** `runtime/src/memory/continuation.c`

The CEK machine provides delimited continuations for advanced control flow abstractions:

```text
prompt → computation → effect/yield → capture continuation → handler → resume
```

- **Algebraic Effects:** `handle`/`perform`/`resume` for composable error handling
- **Generators:** `yield` suspends computation, returns continuation
- **Fibers:** Cooperative multitasking via continuation switching

**Key Design Decision:** Trampolines and continuations are **not unified** because:
1. Trampolines solve a simpler problem (tail calls) with minimal overhead
2. Continuations require heap-allocated frames and region integration
3. Mixing them would add complexity without benefit
4. They serve different abstraction levels

### 4.3 Region-Continuation Integration

Continuation prompts are integrated with CTRR regions:
- Each `prompt` creates a child region
- `capture` transmigrates frame contents to parent region
- `prompt_exit` reclaims the prompt's region

This ensures memory safety for captured continuations without GC.

---

## 5. Portability & Compliance

- **Core**: ANSI C99.
- **Synchronization**: POSIX pthreads.
- **Memory**: strictly **manual/static** (no runtime GC loop).
