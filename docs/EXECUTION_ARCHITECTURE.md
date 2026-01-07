# OmniLisp Execution Architecture

OmniLisp uses a high-performance, GC-less execution model based on **As Static As Possible (ASAP)** memory management and **Region-Based Reference Counting (RC-G)**.

---

## 1. Unified Pipeline (C99)

The OmniLisp toolchain is implemented in pure ANSI C99 + POSIX, ensuring portability and minimal overhead.

```text
Source.omni ──► Pika Parser ──► AST (Value*) ──► Region Inference ──► Codegen ──► Binary
```

### Key Stages:
1.  **Pika Parser**: A bottom-up dynamic programming parser that handles left recursion natively and provides optimal error recovery.
2.  **Region Inference**: A static analysis pass that identifies object lifetimes and groups them into Regions.
3.  **ASAP Analysis**: Determines the "Last Use Point" for every Region and individual object, injecting static `free` or `region_exit` calls.
4.  **Codegen**: Translates the annotated AST into optimized C code linked against the OmniLisp Runtime.

---

## 2. Runtime Model (RC-G)

The runtime provides the physical foundation for the ASAP model.

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

## 4. Portability & Compliance

- **Core**: ANSI C99.
- **Synchronization**: POSIX pthreads.
- **Memory**: strictly **manual/static** (no runtime GC loop).