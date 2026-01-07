# Memory Model Technical Reference (RC-G)

## 1. Object Layout
OmniLisp objects are header-light. Reference counts and cycle metadata are moved to the **Region Control Block**.

```c
typedef struct Value {
    int mark;               // Region status
    Tag tag;                // Type tag
    const TypeInfo* type;   // Metadata & Trace function
    union {
        long i;
        char* s;
        struct { struct Value* car; struct Value* cdr; } cell;
        // ... rest of fields ...
    };
} Value;
```

## 2. Pointer Semantics
...
## 4. Concurrency (Tethers)
Threads do not lock individual objects. They **Tether** the entire Region.
*   **`region_tether_start(Region*)`**: Pins the region layout.
*   **Thread-Local Cache**: Tethers are cached per-thread (`MAX_THREAD_LOCAL_TETHERS = 16`) to avoid atomic operations for redundant borrows.
*   **Multiple Readers**: Many threads can tether the same region for reading.