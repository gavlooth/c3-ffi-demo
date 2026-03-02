# C3 Feature Migration — Complete Gap Analysis

## What We Use vs What C3 Provides

### USED (partially or fully)
- defer (3 of 184 malloc sites)
- modules/imports
- slices
- foreach (partially — 415 manual loops remain)
- extern fn (FFI)
- $embed (stdlib)
- struct, enum, switch, union
- basic types (usz, int, char, bool)
- std::collections::list (parser only)
- std::atomic (atomics only)

### NOT USED — Should Be

#### Tier 1: Replace Custom Implementations (eliminate ~500 lines)

| C3 Feature | What It Replaces | Lines Saved | Bug Prevention |
|---|---|---|---|
| `std::collections::map{K,V}` | Custom HashMap (open-addressing, HashEntry) | ~200 | Battle-tested hash impl |
| `std::collections::list{T}` | Custom Array (items + length + capacity) | ~50 | Bounds checking, iterator |
| `DString` | Custom StringVal (chars + len + capacity) | ~100 | No double-malloc per string |
| Allocator interface | Raw mem::malloc everywhere | ~50 | Custom scope-region allocator |
| `@pool()` / temp allocator | Fixed char[256] buffers (95 instances) | ~100 | No truncation risk |

#### Tier 2: Type Safety (prevent bugs found this session)

| C3 Feature | What It Fixes | Instances |
|---|---|---|
| `distinct` types | SymbolId/TypeId are plain uint, FfiHandle* reused for 5 struct types | ~20 |
| `fault` types | 409 string-based raise_error calls | 409 |
| `Type!` optionals | null returns + raise_error pattern | ~200 |
| `@require`/`@ensure` | Missing preconditions on scope ops, alloc, copy_to_parent | ~30 critical |
| `$assert` | PrimReg array size, Value.sizeof, alignment | ~10 |

#### Tier 3: Idiomatic C3 (code quality)

| C3 Feature | What It Improves | Instances |
|---|---|---|
| Value methods `fn Type.method(self)` | Free functions like `hashmap_get(map, key)` → `map.get(key)` | ~50 |
| Operator overloading `@operator` | `values_equal(a, b)` → `a == b` for Value | ~20 |
| Interfaces `interface` + `@dynamic` | MethodTable hand-rolled dispatch → C3 dynamic dispatch | 1 subsystem |
| Associated enum data | ValueTag could carry metadata (size, has_heap, needs_dtor) | 1 enum |
| `foreach` | 415 manual `for (usz i = 0; ...)` loops | 415 |
| `bitstruct` | InterpFlags (5+ bool fields → 1 byte) | 1 struct |
| `@scope` builtin | Manual interp state save/restore at 8 context boundaries | 8 sites |
| `@likely`/`@unlikely` | Hot path hints in jit_signal_impl fast-path loop | ~5 |

#### Tier 4: Advanced (future)

| C3 Feature | Use Case |
|---|---|
| `any` type | Simplify FFI handle dispatch (typeid-based instead of tag) |
| `typeid` | Runtime type checks alongside ValueTag |
| SIMD vectors `[<N>]` | Tuple encoding/comparison in Deduce |
| Saturating arithmetic | Bounds-safe counter operations |
| Built-in test framework | Replace hand-rolled test infrastructure |
| `@pool_init()` | Thread-local temp allocator for Phase G threads |
| `tlocal` | Thread-local state (already used in aot.c3) |

## Migration Roadmap

### Phase M1: Collections (highest impact, ~400 lines)
1. Create `distinct SymbolId = uint` and `distinct TypeId = uint`
2. Add `.hash()` method to Value* wrapper for map compatibility
3. Replace `HashMap`/`HashEntry` with `std::collections::map{Value*, Value*}`
4. Replace `Array` with `std::collections::list{Value*}`
5. Inline string descriptor into Value union (eliminate StringVal malloc)
6. Update `scope_dtor_value` for new collection types
7. Update all `hashmap_get`/`hashmap_set`/`array_set!` call sites

### Phase M2: Error Handling (~300 lines)
1. Define `fault ParseError`, `fault EvalError`, `fault IoError`, `fault TypeError`
2. Convert `raise_error` hot paths to return `Value*!` with fault
3. Convert `hashmap_get`, `Env.lookup`, `jit_compile` to optionals
4. Add `@require`/`@ensure` to scope operations and copy_to_parent
5. Add `$assert` for array sizes and struct invariants

### Phase M3: Idioms (~200 lines)
1. Replace char[256] buffers with DString + temp allocator
2. Convert value functions to methods: `hashmap_get(m, k)` → `m.get(k)`
3. Convert 415 manual loops to foreach where applicable
4. Add `@scope` for interp state save/restore at context boundaries
5. Pack InterpFlags into bitstruct
6. Add `@likely` to fast-path dispatch loop

### Phase M4: Architecture (large, deferred)
1. Custom allocator interface backed by scope-region
2. Interfaces for MethodTable dispatch
3. Associated enum data on ValueTag
4. Built-in test framework migration
5. `@pool_init()` for thread-local temp allocators

## Key Insight
We're writing **C-in-C3** instead of **idiomatic C3**. The language provides:
- Generic collections (HashMap, List, DString) — we reimplemented all three
- Error handling (optionals, faults) — we use null + string errors
- Type safety (distinct, contracts) — we cast everything to void*
- Allocator interface — we use raw malloc everywhere
- Value methods + operators — we use free functions

The migration is significant (~1000 lines touched) but each phase is independent.
