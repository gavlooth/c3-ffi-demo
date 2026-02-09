# Architecture: Region Memory System with Delimited Continuations

## Overview

This system combines **hierarchical region-based memory management** with **delimited continuations**, providing:

- Automatic memory reclamation without garbage collection
- First-class continuations for advanced control flow
- Thread isolation with zero synchronization overhead
- Foundation for algebraic effects and fibers

```
┌─────────────────────────────────────────────────────────────────┐
│                         Thread N                                 │
│  ┌─────────────────────────────────────────────────────────────┐│
│  │                   RegionRegistry                            ││
│  │  ┌─────────┐    ┌─────────┐    ┌─────────┐                 ││
│  │  │ Region  │───▶│ Region  │───▶│ Region  │  (tree)         ││
│  │  │ (root)  │    │ (child) │    │ (leaf)  │                 ││
│  │  └────┬────┘    └────┬────┘    └────┬────┘                 ││
│  │       │              │              │                       ││
│  │       ▼              ▼              ▼                       ││
│  │    [Pool]         [Pool]         [Pool]    ← objects       ││
│  │    [SlotTable]    [SlotTable]    [SlotTable]               ││
│  │    [GhostTables]  [GhostTables]                            ││
│  └─────────────────────────────────────────────────────────────┘│
│  ┌─────────────────────────────────────────────────────────────┐│
│  │                    PromptStack                              ││
│  │  ┌──────────┐  ┌──────────┐  ┌──────────┐                  ││
│  │  │ Prompt 0 │◀─│ Prompt 1 │◀─│ Prompt 2 │  (stack)         ││
│  │  └──────────┘  └──────────┘  └──────────┘                  ││
│  │       │                                                     ││
│  │       ▼                                                     ││
│  │  [Continuations] ← captured execution states               ││
│  └─────────────────────────────────────────────────────────────┘│
└─────────────────────────────────────────────────────────────────┘
```

---

## Part 1: Region-Based Memory Management

### 1.1 Core Concepts

**Regions** are hierarchical memory containers forming a tree:

```
root
├── request_scope
│   ├── parse_scope
│   └── validate_scope
└── cache_scope
```

**Key properties:**
- Objects allocated in a region live until the region dies
- When a region dies, its objects are **promoted** to the parent
- Handles remain valid after promotion (via forwarding)
- No manual free() — regions clean up automatically

### 1.2 Data Structures

```
RegionRegistry (per-thread singleton)
 ├── region_storage: List{Region}      // All regions
 ├── live_region_tracker: SparseSet    // O(1) liveness check
 ├── ghost_index: GhostIndex           // O(1) ghost lookup
 └── root_id: RegionId                 // Root never dies

Region
 ├── id, generation                    // Identity + validation
 ├── refcount                          // Prevent premature death
 ├── parent: RegionId                  // Tree structure
 ├── pool: Pool                        // Object storage
 ├── slot_table: SlotTable             // Handle indirection
 └── inherited_ghost_tables: List      // From dead children
```

### 1.3 Object Lifecycle

```
1. ALLOCATE
   ObjectHandle h = allocate_in(region, int, 42);

   region.pool        → stores the bytes
   region.slot_table  → creates ObjectRecord(LIVE, pool_id)
   handle             → (region_id, slot_id, generation)

2. ACCESS
   int* p = dereference_as(int, h);

   slot_table[slot_id].kind == LIVE?
     → return pool.get_data(pool_id)
   slot_table[slot_id].kind == FORWARDED?
     → follow forward_target recursively

3. REGION DIES
   release_region(region);  // refcount → 0

   For each LIVE object:
     → Copy to parent.pool
     → Convert slot to FORWARDED(new_handle)
   Create ghost_table from slot_table
   Transfer to parent.inherited_ghost_tables
   Register in ghost_index for O(1) lookup

4. ACCESS AFTER DEATH
   int* p = dereference_as(int, h);  // h points to dead region

   ghost_index.lookup(region_id, generation)
     → (host_id, ghost_idx)
   host.inherited_ghost_tables[ghost_idx]
     → forwarding_records[slot_id]
     → follow forward_target
```

### 1.4 Ghost Table Promotion

When region B dies after inheriting ghost tables from child C:

```
BEFORE:                          AFTER:
A (grandparent)                  A (grandparent)
└── B (parent)                   └── [dead]
    ├── ghost_table(C)
    └── C (child) [dead]         A now has:
                                 ├── ghost_table(B)
                                 └── ghost_table(C)  ← promoted!
```

Ghost index updated: `(C, gen) → (A, new_idx)`

---

## Part 2: Thread-Local Isolation

### 2.1 Design

Each thread has completely isolated state:

```c3
tlocal RegionRegistry* g_thread_registry;
tlocal PromptStack*    g_prompt_stack;
```

**Benefits:**
- Zero synchronization for intra-thread operations
- No lock contention
- Clean shutdown when thread exits
- Natural fit for request-per-thread servers

### 2.2 API

```c3
// Per-thread lifecycle
thread_registry_init();      // Call once at thread start
thread_registry_shutdown();  // Call before thread exit

// Convenience functions (use thread-local state)
RegionHandle r = create_region();
ObjectHandle h = allocate_in(r, MyType, value);
void* ptr = dereference(h);
release_region(r);
```

---

## Part 3: Delimited Continuations

### 3.1 Conceptual Model

```
reset {                     // Establish delimiter
    ...
    shift k {               // Capture continuation k
        // k represents "rest of reset body"
        resume(k, value);   // Continue with value
    }
    ...
}
```

**Continuations capture:**
- CPU register state (callee-saved registers)
- Stack segment (from shift point to reset point)
- Control flow position (instruction pointer)

### 3.2 Implementation Layers

```
┌─────────────────────────────────────────────────┐
│              delimited.c3                        │
│  reset_begin/end, shift, resume, perform_effect │
├─────────────────────────────────────────────────┤
│              continuation.c3                     │
│  Continuation, PromptFrame, PromptStack         │
├─────────────────────────────────────────────────┤
│              context.c3                          │
│  context_capture, context_restore (x86_64 asm)  │
└─────────────────────────────────────────────────┘
```

### 3.3 Data Structures

```
PromptStack (per-thread)
 ├── frames: List{PromptFrame}        // Active delimiters
 ├── continuations: List{Continuation} // All captured k's
 └── next_tag: PromptTag              // Unique ID generator

PromptFrame
 ├── tag: PromptTag                   // Unique identifier
 ├── region_id, region_gen            // Associated region
 ├── stack_ptr, frame_ptr             // Capture boundary
 ├── entry_context: SavedContext      // For unwinding
 └── handler_fn, handler_data         // Effect handler

Continuation
 ├── id: ContinuationId               // Unique identifier
 ├── prompt_tag: PromptTag            // Which reset
 ├── home_region, home_generation     // Ownership
 ├── status: ContinuationStatus       // SUSPENDED/RUNNING/etc
 ├── context: SavedContext            // Register state
 ├── stack: StackSegment              // Captured stack
 ├── resume_value, return_value       // Communication
 └── is_one_shot, has_been_resumed    // Optimization flags
```

### 3.4 Capture/Restore Mechanism

```
SHIFT (capture):
1. Find prompt frame by tag
2. Calculate stack segment size (prompt_sp - current_sp)
3. Allocate storage in prompt's region
4. Copy stack segment
5. Save registers via context_capture()
6. Return continuation to handler

RESUME:
1. Validate continuation is SUSPENDED
2. Restore stack segment (copy back to original location)
3. Restore registers via context_restore()
4. Execution continues from shift point with resume_value
```

### 3.5 Region Integration

Continuations are **owned by regions**:

```c3
// In destroy_region(), Phase 0:
if (g_prompt_stack != null) {
    g_prompt_stack.invalidate_region_continuations(id, generation);
}
```

When a region dies:
- All continuations with `home_region == dying_region` are invalidated
- Attempts to resume return error / panic
- Stack segment memory is freed with the region

---

## Part 4: Effect Handlers (Future)

### 4.1 Design

Effects are syntactic sugar over shift/reset:

```
handle {
    let x = perform Read();      // shift + tag
    let y = perform Read();
    perform Write(x + y);
} with {
    | Read()    -> resume(42)    // pattern match on tag
    | Write(v)  -> { print(v); resume(()) }
}
```

### 4.2 Implementation

```c3
// Install handler for a prompt
install_effect_handler(tag, handler_fn, handler_data);

// Perform effect (shift with tag dispatch)
void* result = perform_effect(tag, effect_id, arg);
```

The handler receives:
- `effect_id`: Which effect was performed
- `arg`: Argument passed to perform
- `k`: Captured continuation
- `handler_data`: User context

---

## Part 5: File Structure

```
src/
├── main.c3           # Core types, Region, Pool, SlotTable, Registry
├── ghost_index.c3    # O(1) ghost table lookup (hash map)
├── context.c3        # Platform-specific context capture (x86_64)
├── continuation.c3   # Continuation and PromptStack structures
└── delimited.c3      # High-level shift/reset/resume API
```

### Dependencies

```
main.c3
    ↑
ghost_index.c3
    ↑
continuation.c3 ←── context.c3
    ↑
delimited.c3
```

---

## Part 6: Key Invariants

### Memory Safety
1. Handles are validated via generation counters
2. Dead objects return DEAD status, not garbage
3. Forwarding chains always terminate at LIVE or error
4. Region death invalidates all owned continuations

### Thread Safety
1. All state is thread-local (no sharing)
2. Cross-thread communication requires explicit transfer
3. No locks needed for intra-thread operations

### Continuation Safety
1. One-shot continuations can only be resumed once
2. Multi-shot requires explicit cloning
3. Resuming invalidated continuation panics
4. Stack restoration happens atomically with register restore

---

## Part 7: Performance Characteristics

| Operation | Complexity | Notes |
|-----------|------------|-------|
| Object allocation | O(1) amortized | Pool + SlotTable |
| Object deref (live) | O(1) | Direct pool access |
| Object deref (forwarded) | O(chain length) | Usually 1-2 hops |
| Ghost table lookup | O(1) | Hash map |
| Region creation | O(1) | ID recycling |
| Region destruction | O(objects) | Promotion loop |
| Continuation capture | O(stack size) | Stack copy |
| Continuation resume | O(stack size) | Stack restore |

---

## Part 8: Future Directions

### Planned
- [ ] Minimal Lisp interpreter using regions + continuations
- [ ] Algebraic effects with pattern matching
- [ ] Fiber scheduler built on continuations
- [ ] AArch64 context capture support

### Possible
- [ ] Cross-thread region transfer
- [ ] Incremental stack copying for large continuations
- [ ] Continuation serialization for persistence
- [ ] JIT compilation of hot paths
