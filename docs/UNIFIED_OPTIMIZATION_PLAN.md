# Unified Optimization Plan: Vale + Lobster + Perceus

## Design Principle: No Language Restrictions

All optimizations are **inferred automatically** or enabled via optional hints. Existing Purple code works unchanged - the compiler just generates faster code when it can prove safety.

## Optimization Layers (Unified)

```
┌─────────────────────────────────────────────────────────────────────────┐
│                         COMPILE-TIME ANALYSIS                            │
├─────────────────────────────────────────────────────────────────────────┤
│  1. Purity Analysis     → Which expressions are read-only?              │
│  2. Ownership Analysis  → Borrowed / Consumed / Owned (Lobster)         │
│  3. Uniqueness Analysis → Is this reference provably unique?            │
│  4. Escape Analysis     → Local / Arg / Global                          │
│  5. Shape Analysis      → Tree / DAG / Cyclic                           │
│  6. Reuse Analysis      → Can we reuse this allocation? (Perceus)       │
└─────────────────────────────────────────────────────────────────────────┘
                                    │
                                    ▼
┌─────────────────────────────────────────────────────────────────────────┐
│                         OPTIMIZATION SELECTION                           │
├─────────────────────────────────────────────────────────────────────────┤
│  Pure + Borrowed    → Zero-cost access (no checks, no RC)               │
│  Pure + Owned       → Tethered access (skip repeated gen checks)        │
│  Unique + Local     → Direct free (no RC check)                         │
│  Consumed           → Ownership transfer (no inc_ref)                   │
│  Reuse Match        → In-place reuse (no alloc/free)                    │
│  Pool-eligible      → Bump allocation (O(1) alloc)                      │
│  Default            → Full safety (IPGE + RC)                           │
└─────────────────────────────────────────────────────────────────────────┘
```

## Phase 1: Purity Analysis (Vale-inspired)

### Concept
Automatically detect expressions that don't mutate captured state. These get zero-cost access.

### Detection Rules
```
PURE if:
  - Literal values (int, char, string, etc.)
  - Variable references (read-only)
  - Pure function calls (known pure primitives)
  - let/if/lambda bodies where all sub-expressions are pure
  - No set!, box-set!, or other mutation

IMPURE if:
  - Contains set!, box-set!, channel operations
  - Calls unknown/impure functions
  - Contains side effects (I/O, etc.)
```

### Implementation
```go
// pkg/analysis/purity.go
type PurityAnalyzer struct {
    KnownPure map[string]bool  // Built-in pure functions
}

func (a *PurityAnalyzer) IsPure(expr *ast.Value, env *PurityEnv) bool {
    if expr == nil || ast.IsNil(expr) {
        return true
    }

    switch expr.Tag {
    case ast.TInt, ast.TFloat, ast.TChar, ast.TSym:
        return true  // Literals are pure

    case ast.TCell:
        if ast.IsSym(expr.Car) {
            op := expr.Car.Str

            // Mutation operations are impure
            if op == "set!" || op == "box-set!" ||
               op == "chan-send!" || op == "atom-reset!" {
                return false
            }

            // Check if known pure function
            if a.KnownPure[op] {
                return a.AllArgsPure(expr.Cdr, env)
            }

            // let/if/lambda - check body
            if op == "let" || op == "if" || op == "lambda" {
                return a.AnalyzeSpecialForm(op, expr.Cdr, env)
            }
        }
        // Unknown call - conservatively impure
        return false
    }
    return false
}
```

### Codegen Impact
```go
func (g *CodeGenerator) GenerateAccess(varName string) string {
    if g.purityCtx.IsPureContext() && g.purityCtx.IsReadOnly(varName) {
        // Zero-cost: no IPGE check, no RC
        return varName
    }
    // Normal access with safety
    return g.GenerateSafeAccess(varName)
}
```

## Phase 2: Scope Tethering (Vale-inspired)

### Concept
When borrowing a reference in a scope, mark it "tethered" to skip repeated generation checks.

### Runtime Addition
```c
// Add to Obj struct (uses padding, no size increase)
typedef struct Obj {
    uint64_t generation;
    int mark;
    int tag;
    int is_pair;
    int scc_id;
    unsigned int scan_tag : 31;
    unsigned int tethered : 1;  // NEW: single bit flag
    union { ... };
} Obj;

// Tether operations
static inline void tether(Obj* obj) {
    if (obj && !IS_IMMEDIATE(obj)) obj->tethered = 1;
}

static inline void untether(Obj* obj) {
    if (obj && !IS_IMMEDIATE(obj)) obj->tethered = 0;
}

// Fast deref - skips gen check if tethered
static inline Obj* deref_tethered(Obj* obj, uint64_t expected_gen) {
    if (!obj) return NULL;
    if (IS_IMMEDIATE(obj)) return obj;
    if (obj->tethered) return obj;  // FAST PATH
    return (obj->generation == expected_gen) ? obj : NULL;
}
```

### Automatic Application
Compiler tethers at scope entry for borrowed references:
```c
// Generated for: (let ((x (get-item list))) (use x) (use x) (use x))
{
    Obj* x = get_item(list);
    tether(x);  // Inserted by compiler

    use(x);  // No gen check - tethered
    use(x);  // No gen check - tethered
    use(x);  // No gen check - tethered

    untether(x);  // Inserted by compiler
}
```

## Phase 3: Lobster Ownership Modes (Enhanced)

### Ownership Classification
```go
type OwnershipMode int
const (
    OwnershipOwned    OwnershipMode = iota  // Caller owns, must free
    OwnershipBorrowed                        // Temporary access, no RC
    OwnershipConsumed                        // Ownership transfers to callee
)
```

### Inference Rules
```
BORROWED if:
  - Parameter used read-only in function body
  - Let binding in pure context
  - Loop variable

CONSUMED if:
  - Parameter stored in data structure
  - Parameter returned from function
  - Parameter passed to consuming function

OWNED if:
  - Fresh allocation
  - Return value from function
  - Result of copy operation
```

### Codegen Impact
```go
func (g *CodeGenerator) GenerateCall(fn string, args []*ast.Value) string {
    var argStrs []string
    for i, arg := range args {
        mode := g.ownershipCtx.GetParamMode(fn, i)
        argStr := g.Generate(arg)

        switch mode {
        case OwnershipBorrowed:
            // No RC operations
            argStrs = append(argStrs, argStr)
        case OwnershipConsumed:
            // No inc_ref (ownership transfers)
            argStrs = append(argStrs, argStr)
            g.ownershipCtx.MarkTransferred(arg)
        case OwnershipOwned:
            // Normal RC
            argStrs = append(argStrs, argStr)
        }
    }
    return fmt.Sprintf("%s(%s)", fn, strings.Join(argStrs, ", "))
}
```

## Phase 4: Perceus Reuse Analysis

### Concept
When freeing an object, check if a same-sized allocation follows. If so, reuse the memory.

### Pattern Detection
```
REUSE CANDIDATE if:
  - dec_ref(x) followed by mk_*(...)
  - sizeof(x) == sizeof(new allocation)
  - x is provably dead after this point

Example:
  (let ((old (cons 1 2)))
    (let ((new (cons 3 4)))  ;; Can reuse old's memory
      new))
```

### Implementation
```go
// pkg/analysis/reuse.go
type ReuseAnalyzer struct {
    PendingFrees map[string]*FreeInfo  // Variables about to be freed
}

type FreeInfo struct {
    VarName  string
    TypeSize int
    Line     int
}

func (a *ReuseAnalyzer) FindReuseCandidate(allocType string, size int) *FreeInfo {
    for _, info := range a.PendingFrees {
        if info.TypeSize == size {
            delete(a.PendingFrees, info.VarName)
            return info
        }
    }
    return nil
}
```

### Codegen
```c
// Without reuse:
dec_ref(old);
Obj* new = mk_pair(mk_int(3), mk_int(4));

// With reuse:
Obj* new = reuse_as_pair(old, mk_int(3), mk_int(4));
// old's memory reused, no free/malloc
```

## Phase 5: Pool Allocation (Automatic)

### Eligibility Detection
```
POOL-ELIGIBLE if:
  - Allocated in let binding
  - Does not escape function (ESCAPE_NONE)
  - In pure or known-bounded context
  - Not captured by closure
```

### Implementation
```c
// Thread-local pool for temporary allocations
static __thread PurePool* _temp_pool = NULL;

static Obj* pool_mk_pair(Obj* a, Obj* b) {
    if (_temp_pool) {
        Obj* x = pure_pool_alloc(_temp_pool, sizeof(Obj));
        if (x) {
            x->generation = _next_generation();
            x->mark = -3;  // Special mark: pool-allocated
            x->tag = TAG_PAIR;
            x->is_pair = 1;
            x->a = a;
            x->b = b;
            return x;
        }
    }
    return mk_pair(a, b);  // Fallback to heap
}
```

### Scope Management
```c
// Generated for functions with pool-eligible allocations
Obj* process_list(Obj* items) {
    PurePool* _saved_pool = _temp_pool;
    _temp_pool = pure_pool_create(4096);

    // ... function body using pool_mk_* ...
    Obj* result = ...;

    // Move result to heap if escaping
    if (result && result->mark == -3) {
        result = deep_copy_to_heap(result);
    }

    pure_pool_destroy(_temp_pool);
    _temp_pool = _saved_pool;
    return result;
}
```

## Phase 6: NaN-Boxing for Floats

### Concept
Use NaN payload bits to store pointers, enabling unboxed floats.

### IEEE 754 Double Layout
```
Quiet NaN:  0 11111111111 1xxx...xxx (51 payload bits)
Signaling:  0 11111111111 0xxx...xxx

We use: 0x7FF8_0000_0000_0000 as NaN prefix
Payload: 48 bits for pointer (enough for current x86-64)
```

### Implementation
```c
#define NAN_BOXING_ENABLED 1

#if NAN_BOXING_ENABLED

#define NANBOX_PREFIX    0x7FF8000000000000ULL
#define NANBOX_MASK      0xFFFF000000000000ULL
#define NANBOX_PTR_MASK  0x0000FFFFFFFFFFFFULL

// Check if value is a boxed pointer
#define IS_NANBOXED_PTR(v)  (((v) & NANBOX_MASK) == NANBOX_PREFIX)

// Extract pointer from NaN-boxed value
#define NANBOX_TO_PTR(v)    ((Obj*)((v) & NANBOX_PTR_MASK))

// Box a pointer as NaN
#define PTR_TO_NANBOX(p)    (NANBOX_PREFIX | ((uint64_t)(p) & NANBOX_PTR_MASK))

// Universal value type
typedef union {
    double f;
    uint64_t bits;
} Value;

static inline int is_float(Value v) {
    // A real float won't have our NaN prefix (statistically impossible)
    return !IS_NANBOXED_PTR(v.bits) && !IS_IMMEDIATE(v.bits);
}

static inline double value_to_float(Value v) {
    return v.f;
}

static inline Obj* value_to_obj(Value v) {
    if (IS_NANBOXED_PTR(v.bits)) return NANBOX_TO_PTR(v.bits);
    if (IS_IMMEDIATE(v.bits)) return (Obj*)v.bits;
    return NULL;  // It's a float, not an object
}

#endif
```

## Combined Optimization Table

| Analysis Result | Optimization Applied | RC Cost | Check Cost | Alloc Cost |
|-----------------|---------------------|---------|------------|------------|
| Pure + Borrowed | Zero-cost access | 0 | 0 | N/A |
| Pure + Owned | Tethered access | 0 | 0 (after 1st) | N/A |
| Borrowed param | Skip RC | 0 | 1 | N/A |
| Consumed param | Transfer ownership | 0 | 1 | N/A |
| Unique + Local | Direct free | 0 | 0 | N/A |
| Reuse match | In-place reuse | 0 | 0 | 0 |
| Pool-eligible | Bump allocation | 0 | 0 | O(1) |
| Default | Full safety | 2 | 1 | O(1) |

## Implementation Order

| Phase | Feature | Complexity | Impact | Dependencies |
|-------|---------|------------|--------|--------------|
| 1a | Purity Analysis | Medium | Very High | None |
| 1b | Scope Tethering | Low | High | Runtime change |
| 2a | Ownership Modes | Medium | High | Purity |
| 2b | Perceus Reuse | Medium | Medium | Ownership |
| 3a | Pool Allocation | Medium | Medium | Escape analysis |
| 3b | NaN-Boxing | Low | Medium | None |

## Expected Results

### RC Operation Reduction
- Current: ~75% eliminated (Lobster-style)
- After Phase 1-2: ~90% eliminated
- After Phase 3: ~95% eliminated

### Check Reduction
- Current: 1 IPGE check per deref
- After Tethering: ~0.1 checks per deref (amortized)
- After Purity: 0 checks in pure contexts

### Allocation Speedup
- Current: malloc/free per object (~50ns)
- After Pool: bump allocation (~5ns)
- After Reuse: 0 allocation for reused objects

### Overall Hot-Path Improvement
Conservative estimate: **2-5× speedup** for typical functional code
Optimistic (pure-heavy): **5-10× speedup**

## Backward Compatibility

All optimizations are transparent:
- No new syntax required
- Existing code works unchanged
- Compiler infers everything
- Falls back to safe defaults when analysis fails

Optional hints (future):
```scheme
;; Hint that function is pure (helps analysis)
(define (sum xs) ^pure
  (fold + 0 xs))

;; Hint that parameter is consumed
(define (push-all target items) ^(consumed target)
  ...)
```

These hints are **optional** - just help the compiler when inference is insufficient.
