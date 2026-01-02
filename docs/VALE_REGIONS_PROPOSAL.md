# Vale Region Adaptations for Purple Go

## Executive Summary

Vale's region system provides **zero-cost references** through compile-time region tracking and implicit read-only locking. Key techniques we can adapt:

| Technique | Vale Overhead | Benefit | Implementation Complexity |
|-----------|---------------|---------|---------------------------|
| Pure/Read-Only Regions | Zero | Skip all safety checks | Medium |
| Scope Tethering | 1 bit/object | Skip repeated gen checks | Low |
| Pool Allocation | None | O(1) alloc, cache-friendly | Low |
| Region Heights | 4 bytes/ref | O(1) scope validation | Low |
| RC Elimination (95%) | Compile-time | Near-zero RC overhead | Medium |

## Phase 1: Pure Regions (Highest Value)

### Concept

Mark functions/blocks as "pure" to indicate they only READ existing data. The compiler then:
- Skips all generation checks for captured variables
- Skips all RC operations (no inc_ref/dec_ref)
- Can use raw pointers internally

### Syntax Proposal

```scheme
;; Pure function - read-only access to all captures
(define-pure (analyze-world world)
  ;; world is read-only, zero-cost access
  (fold + 0 (map get-health (world-units world))))

;; Pure block - temporary read-only region
(let ((world (get-world)))
  (pure-block
    ;; world is frozen here - zero overhead
    (expensive-analysis world)))
```

### Implementation

```go
// In analysis/pure.go
type PureContext struct {
    PureDepth int              // Current pure block nesting
    FrozenVars map[string]bool // Variables frozen in current pure scope
}

func (ctx *PureContext) IsPure() bool {
    return ctx.PureDepth > 0
}

func (ctx *PureContext) IsFrozen(varName string) bool {
    return ctx.FrozenVars[varName]
}
```

```c
// In runtime - pure functions skip all checks
Obj* analyze_world_pure(Obj* world) {
    // No IPGE check - world is frozen
    // No inc_ref - pure functions don't modify RC
    Obj* units = world->a;  // Direct field access
    ...
}
```

### Codegen Changes

```go
// In codegen.go
func (g *CodeGenerator) GeneratePureBlock(body *ast.Value, frozenVars []string) string {
    // Mark all frozenVars as read-only
    g.pureCtx.PureDepth++
    for _, v := range frozenVars {
        g.pureCtx.FrozenVars[v] = true
    }

    // Generate body - all accesses to frozen vars are zero-cost
    code := g.Generate(body)

    // Restore context
    g.pureCtx.PureDepth--
    for _, v := range frozenVars {
        delete(g.pureCtx.FrozenVars, v)
    }

    return code
}

// In access generation
func (g *CodeGenerator) GenerateVarAccess(varName string) string {
    if g.pureCtx.IsFrozen(varName) {
        // Zero-cost access - no IPGE check, no RC
        return varName
    }
    // Normal access with safety checks
    return fmt.Sprintf("safe_deref(%s_ref)", varName)
}
```

## Phase 2: Scope Tethering (Low-Hanging Fruit)

### Concept

When borrowing a reference, set a "tethered" bit on the target object. While tethered, skip all generation checks within that scope. Clear on scope exit.

### Implementation

```c
// Add to Obj struct
typedef struct Obj {
    uint64_t generation;
    int mark;
    int tag;
    int is_pair;
    int scc_id;
    unsigned int scan_tag;
    uint8_t tethered;      // NEW: scope tethering flag
    uint8_t _pad[3];       // Alignment padding
    union { ... };
} Obj;

// Tether a reference (on borrow)
static inline void tether_obj(Obj* obj) {
    if (obj && !IS_IMMEDIATE(obj)) {
        obj->tethered = 1;
    }
}

// Untether (on scope exit)
static inline void untether_obj(Obj* obj) {
    if (obj && !IS_IMMEDIATE(obj)) {
        obj->tethered = 0;
    }
}

// Fast deref with tether check
static inline Obj* tethered_deref(Obj* obj, uint64_t expected_gen) {
    if (!obj || IS_IMMEDIATE(obj)) return obj;
    if (obj->tethered) return obj;  // Skip gen check!
    return (obj->generation == expected_gen) ? obj : NULL;
}
```

### Codegen Integration

```go
func (g *CodeGenerator) GenerateBorrowedAccess(varName string, expr string) string {
    return fmt.Sprintf(`({
        tether_obj(%s);
        Obj* _result = %s;
        untether_obj(%s);
        _result;
    })`, varName, expr, varName)
}
```

### Performance Impact

For a function that accesses a borrowed reference 10 times:
- Without tethering: 10 generation checks
- With tethering: 1 tether + 10 fast accesses + 1 untether = ~3 ops

## Phase 3: Pool Allocation for Pure Regions

### Concept

Pure regions allocate from a bump-pointer pool instead of malloc. Benefits:
- O(1) allocation (just increment pointer)
- Cache-friendly contiguous memory
- O(1) bulk deallocation at region exit
- No per-object generation tracking needed

### Implementation

```c
// Pool allocator
typedef struct PurePool {
    char* base;
    char* cursor;
    char* end;
    size_t size;
} PurePool;

static PurePool* pure_pool_create(size_t size) {
    PurePool* pool = malloc(sizeof(PurePool));
    pool->base = malloc(size);
    pool->cursor = pool->base;
    pool->end = pool->base + size;
    pool->size = size;
    return pool;
}

static void pure_pool_destroy(PurePool* pool) {
    free(pool->base);
    free(pool);
}

static void* pure_pool_alloc(PurePool* pool, size_t size) {
    size = (size + 7) & ~7;  // 8-byte alignment
    if (pool->cursor + size > pool->end) return NULL;
    void* ptr = pool->cursor;
    pool->cursor += size;
    return ptr;
}

// Reset for reuse (faster than recreate)
static void pure_pool_reset(PurePool* pool) {
    pool->cursor = pool->base;
}
```

### Integration with Pure Blocks

```scheme
(pure-block
  ;; All allocations in this block use pool
  (let ((temp-list (map process items)))
    (reduce combine temp-list)))
;; Pool freed here - all temp-list objects gone
```

Generated code:
```c
{
    PurePool* _pool = pure_pool_create(65536);  // 64KB default

    // All mk_* calls use pool allocation
    #define mk_pair(a, b) pure_mk_pair(_pool, a, b)

    Obj* temp_list = map(process, items);
    Obj* result = reduce(combine, temp_list);

    #undef mk_pair
    pure_pool_destroy(_pool);  // O(1) - free everything at once
}
```

## Phase 4: Region Height Tracking

### Concept

Assign integer "heights" to regions:
- Negative: Region parameters from caller
- Zero: Current/default region
- Positive: Pure blocks (nested depth)

References can only point to same-height or outer (lower height) regions.

### Implementation

```c
typedef struct RegionRef {
    Obj* ptr;
    int region_height;
} RegionRef;

// O(1) validation
static inline int can_reference(int src_height, int tgt_height) {
    return tgt_height <= src_height;  // Can only point outward
}

// Create reference with height check
static RegionRef region_ref_create(Obj* obj, int current_height) {
    RegionRef ref;
    ref.ptr = obj;
    ref.region_height = current_height;
    return ref;
}
```

### Codegen Context

```go
type RegionContext struct {
    CurrentHeight int
}

func (ctx *RegionContext) EnterPure() int {
    ctx.CurrentHeight++
    return ctx.CurrentHeight
}

func (ctx *RegionContext) ExitPure() {
    ctx.CurrentHeight--
}
```

## Phase 5: Enhanced RC Elimination (95% Target)

### Current Status
Purple's Lobster-style RC optimization achieves ~75% elimination.

### Additional Techniques from Vale

**1. Definitely-Unique Tracking**
```go
// In analysis/rcopt.go
type Uniqueness int
const (
    UniquenessUnknown Uniqueness = iota
    UniquenessDefinitelyUnique    // Skip ALL RC
    UniquenessDefinitelyShared    // Must do RC
)

func (ctx *RCOptContext) GetUniqueness(varName string) Uniqueness {
    // Track through dataflow analysis
    ...
}
```

**2. Pure Parameter Optimization**
Parameters to pure functions never need RC operations:
```go
func (g *CodeGenerator) GeneratePureFunctionCall(fn, args) string {
    // No inc_ref on arguments - they're borrowed read-only
    // No dec_ref after call - ownership unchanged
    return fmt.Sprintf("%s(%s)", fn, strings.Join(args, ", "))
}
```

**3. Known-Lifetime Elision**
If we can prove a reference doesn't outlive its target:
```go
func (ctx *RCOptContext) LifetimeContained(ref, target string) bool {
    // ref's scope ends before target's scope
    return ctx.scopeEnd[ref] <= ctx.scopeEnd[target]
}
```

## Implementation Priority

| Phase | Effort | Impact | Priority |
|-------|--------|--------|----------|
| 1. Pure Regions | Medium | Very High | **P0** |
| 2. Scope Tethering | Low | High | **P0** |
| 3. Pool Allocation | Low | Medium | P1 |
| 4. Region Heights | Low | Medium | P1 |
| 5. RC Elimination 95% | Medium | High | P1 |

## Expected Performance Improvements

Based on Vale's benchmarks and analysis:

| Optimization | Current | After | Improvement |
|--------------|---------|-------|-------------|
| Gen checks/deref | 1 | 0 (pure) | 100% elimination |
| RC ops | 25% remaining | 5% remaining | 80% reduction |
| Allocation (pure) | ~50ns | ~5ns | 10× faster |
| Cache misses | High (scattered) | Low (pool) | ~50% reduction |

**Combined hot-path improvement: 3-10× for pure workloads**

## Comparison: Vale vs Purple Approach

| Aspect | Vale | Purple (Proposed) |
|--------|------|-------------------|
| Generation storage | Header before alloc | Inline in Obj |
| Generation algorithm | Random/increment | IPGE (LCG) |
| Zero collision | Statistical (~73K years) | Guaranteed |
| Read-only regions | `pure` + `<a' ro>` | `pure-block` |
| Scope tethering | Implicit | Explicit bit |
| Pool allocation | Per pure region | Per pure block |
| Region heights | Complex polymorphism | Simple integers |
| Transmigration | Complex secession | Not needed |

## Key Insight

Vale's most impactful feature is **implicit read-only locking**. By proving code doesn't mutate data, all safety checks become unnecessary. This is orthogonal to IPGE/GenRef - it's a higher-level optimization that makes those checks unnecessary in the first place.

Purple should prioritize:
1. **Pure analysis** - determine which code is read-only
2. **Scope tethering** - reduce repeated checks
3. **Pool allocation** - fast allocation in pure regions

These provide most of Vale's benefits without the complexity of full region polymorphism.

## References

- Vale Guide: Regions - https://vale.dev/guide/regions
- Zero-Cost References - https://verdagon.dev/blog/zero-cost-refs-regions
- Generational References - https://verdagon.dev/blog/generational-references
- Hybrid-Generational Memory - https://verdagon.dev/blog/hybrid-generational-memory
- First Regions Prototype - https://verdagon.dev/blog/first-regions-prototype
