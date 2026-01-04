# Native Compiler Memory Management Integration Plan

## Goal
Wire shape analysis, Symmetric RC, weak edges, and ASAP strategies into `pkg/compiler/compiler.go` to match the design in CLAUDE.md.

## Current State (Updated 2026-01-01)
- ✅ Shape analysis wired into `pkg/compiler/compiler.go`
- ✅ Shape-aware free strategy selection (free_tree vs dec_ref)
- ✅ Symmetric RC scope management for cyclic data
- ✅ Weak edge detection in TypeRegistry for deftypes
- ✅ Stack allocation with save/restore in lambdas
- ✅ Arena allocation for non-escaping cyclic data
- ✅ Core pkg tests all pass

## Target State
| Technique | Target % | Data Structures |
|-----------|----------|-----------------|
| Pure ASAP | 60% | Trees, lists, records |
| Reference Counting | 20% | DAG-shaped shared data |
| Component Tethering | 5-10% | Cyclic structures |
| Stack Allocation | 5% | Non-escaping temps |
| Arena | 3% | Complex temp cycles |
| Weak Edges | 2% | Back-pointers |

---

## Phase 1: Infrastructure Setup ✅
- [x] P1.1 Add ShapeContext to Compiler struct
- [x] P1.2 Add TypeRegistry to Compiler struct
- [x] P1.3 Add ComponentPools for island tracking
- [x] P1.4 Create VarMemInfo struct (shape, strategy, escape class)
- [x] P1.5 Update VarInfo to include VarMemInfo

## Phase 2: Shape Analysis Integration ✅
- [x] P2.1 Call `shapeCtx.AnalyzeShapes()` for each expression
- [x] P2.2 Store shape results in CValue.Shape field
- [x] P2.3 Propagate shape through let bindings
- [x] P2.4 Propagate shape through compileIf/compileDo
- [x] P2.5 mergeShapes() for branch merging

## Phase 3: Free Strategy Selection ✅
- [x] P3.1 Implement `selectFreeStrategy(shape, escapeClass)` function
- [x] P3.2 Tree shape → `free_tree()` for let-bound vars (Pure ASAP)
- [x] P3.3 DAG shape → `dec_ref()` (Reference counting)
- [x] P3.4 Cyclic + non-escaping → `sym_release_handle()`
- [x] P3.5 Cyclic + escaping → Component Tethering
- [x] P3.6 Function args always use dec_ref (borrowing)

## Phase 4: Component Tethering Integration ✅
- [x] P4.1 Add `sym_acquire_handle()` calls when cyclic data detected
- [x] P4.2 Add `sym_component_new()` for island units
- [x] P4.3 Track active components for scope management
- [x] P4.4 Add `sym_release_handle()` at scope boundaries
- [x] P4.5 Inject `sym_tether_begin/end` for zero-cost blocks

## Phase 5: Weak Edge Integration ✅
- [x] P5.1 TypeRegistry.AnalyzeBackEdges() called from handleDeftype
- [x] P5.2 Mark back-edge fields as FieldWeak in TypeRegistry
- [x] P5.3 RuntimeGenerator skips weak fields for inc_ref
- [x] P5.4 Skip weak fields in release functions
- [x] P5.5 Weak ref invalidation in genref system

## Phase 6: Stack Allocation (ESCAPE_NONE) ✅
- [x] P6.1 Identify non-escaping allocations via escape analysis
- [x] P6.2 Stack pointer save/restore in genLambdaFunc
- [x] P6.3 STACK_PTR management per function scope
- [x] P6.4 Auto-restore stack at function exit
- Note: Runtime STACK_POOL now wired into lambdas

## Phase 7: Arena Allocation ✅
- [x] P7.1 Detect temp cyclic structures (don't escape function)
- [x] P7.2 Add `arena_create()` at let entry when needed
- [x] P7.3 Route allocations to `arena_mk_int()` / `arena_mk_pair()`
- [x] P7.4 Add `arena_destroy()` at let exit
- [x] P7.5 No individual frees needed for arena objects
- Note: Arena now wired into compileLet for non-escaping cyclic data

## Phase 8: Code Generation Updates ✅
- [x] P8.1 Update `compileLet` to emit shape-aware frees
- [x] P8.2 Update `compileApply` - uses dec_ref for borrowed args
- [x] P8.3 Symmetric scope handling in `compileLet`
- [x] P8.4 Update `compileIf` to merge shapes from branches
- [x] P8.5 Add debug comments showing chosen strategy

## Phase 9: Runtime Code Updates ✅
- [x] GenerateComponentRuntime() available
- [x] Component Tethering C functions exist
- [x] Arena runtime functions exist
- [x] Weak reference runtime support exists
- [x] free_tree, dec_ref, release_children implemented

## Phase 10: Testing & Validation
- [x] P10.1 Fix pkg/codegen build error (format string)
- [x] P10.2 Fix BackEdge integration tests
- [ ] P10.3 ASAN tests (staged compiler, not native)
- [ ] P10.4 Valgrind tests (staged compiler, not native)
- [ ] P10.5 Add native compiler unit tests
- [ ] P10.6 Add shape-specific test cases
- [ ] P10.7 Benchmark: compare RC-only vs component-aware

---

## Implementation Order

```
Week 1: Phase 1-2 (Infrastructure + Shape Analysis)
        ↓
Week 2: Phase 3-4 (Free Strategy + Component Tethering)
        ↓
Week 3: Phase 5-6 (Weak Edges + Stack Alloc)
        ↓
Week 4: Phase 7-8 (Arena + Codegen Updates)
        ↓
Week 5: Phase 9-10 (Runtime + Testing)
```

## Files to Modify

| File | Changes |
|------|---------|
| `pkg/compiler/compiler.go` | Add shape ctx, strategy selection, codegen |
| `pkg/codegen/runtime.go` | Ensure all strategies have C code |
| `pkg/analysis/shape.go` | May need minor API adjustments |
| `pkg/analysis/backedge.go` | May need compiler-friendly API |
| `test/native_memory_test.go` | New test file |

## Success Criteria

1. All existing tests pass
2. AddressSanitizer shows no memory errors
3. Valgrind shows no leaks
4. Shape analysis correctly identifies 80%+ of allocations
5. Cyclic structures handled by Component Tethering (no leaks)
6. Performance: no regression vs current RC-only approach

## Rollback Plan

If issues arise, the shape analysis can be disabled by:
```go
// In compiler.go
const USE_SHAPE_ANALYSIS = false // Toggle off
```

This falls back to simple RC for everything (current behavior).
