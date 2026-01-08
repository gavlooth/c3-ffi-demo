# OmniLisp Architecture

OmniLisp uses a high-performance, GC-less execution model based on **As Static As Possible (ASAP)** memory management and **Region-Based Reference Counting (RC-G)**.

---

## 1. Unified Pipeline

The OmniLisp system is composed of two primary components:

1.  **Compiler (`csrc/`)**: A standalone compiler written in C99.
    *   **Parser**: Pika Parser (Packrat PEG).
    *   **Analysis**: Escape analysis, Region Inference, Scope Tree generation.
    *   **Codegen**: Generates C99 code annotated with Region lifecycle calls.
2.  **Runtime (`runtime/`)**: A static library (`libomni.a`) providing the execution environment.
    *   **Memory**: Regions, Arenas, Tethers, Transmigration.
    *   **Primitives**: Core language functions (arithmetic, I/O, list ops).
    *   **Concurrency**: Thread spawning, channels, atomic operations.

```text
Source.omni ──► [Compiler] ──► Intermediate.c ──► [GCC + libomni.a] ──► Binary
```

### Key Stages:
1.  **Pika Parser**: A bottom-up dynamic programming parser that handles left recursion natively.
2.  **Region Inference**: A static analysis pass that identifies object lifetimes and groups them into Regions.
3.  **ASAP Analysis**: Determines the "Last Use Point" for every Region and individual object, injecting static `region_create` / `region_exit` calls.
4.  **Codegen**: Translates the annotated AST into optimized C code linked against `libomni.a`.

---

## 2. Runtime Model (RC-G)

The runtime provides the physical foundation for the ASAP model.

### 2.1 Memory Model Visualization

```text
┌────────────────────────────────────────────────────────────────────────┐
│                          MEMORY MODEL (RC-G)                           │
└────────────────────────────────────────────────────────────────────────┘

      THREAD STACK                     LOGICAL REGIONS (RC-G)
    ┌──────────────┐                ┌───────────────────────────┐
    │   Frame 1    │                │         REGION A          │
    ├──────────────┤                │  (External RC: 2)         │
    │   Tether 1 ──┼───────┐        │  ┌───────┐     ┌───────┐  │
    ├──────────────┤       └────────┼─►│ Obj 1 │────►│ Obj 2 │  │
    │   Frame 2    │                │  └───────┘     └───────┘  │
    ├──────────────┤                └──────┬─────────────▲──────┘
    │   Tether 2 ──┼─────────────┐         │             │
    └──────────────┘             │         │ Cross-      │
                                 │         │ Region      │
                                 │         │ Ref         │
                                 ▼         ▼             │
    ┌────────────────────────────┴─────────┴─────────────┴──────┐
    │                         REGION B                          │
    │  (External RC: 1)                                         │
    │  ┌───────┐      ┌───────┐      ┌───────┐                  │
    │  │ Obj 3 │─────►│ Obj 4 │◄─────│ Obj 5 │                  │
    │  └───────┘      └───────┘      └───────┘                  │
    └─────────────────────┬─────────────────────────────────────┘
                          │
                          ▼
    ┌───────────────────────────────────────────────────────────┐
    │                    PHYSICAL ARENA (Bump)                  │
    ├───────────────────────────────────────────────────────────┤
    │ [ Block 1 ] [ Block 2 ] [ Block 3 ] [ Block 4 ] [  FREE  ]│
    └───────────────────────────────────────────────────────────┘
```

- **Regions**: Logical owners of memory blocks (`Region` struct).
- **Arenas**: Physical storage backend (bump-pointer allocation).
- **Tethers**: Thread-local mechanisms for safe concurrent access without global locks.
- **Transmigration**: Efficient movement of data between regions using iterative deep copy or block splicing.

---

## 3. Concurrency Model

OmniLisp supports:
1.  **Threads**: OS-level parallelism using POSIX pthreads, synchronized via Region Tethers and Channels.
2.  **Channels**: Typed conduits for moving objects (and their ownership) between threads/regions.

---

## 4. Portability & Compliance

- **Core**: ANSI C99.
- **Synchronization**: POSIX pthreads / C11 Atomics.
- **Memory**: strictly **manual/static** (no runtime GC loop).