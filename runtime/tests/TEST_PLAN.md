# OmniLisp Runtime Test Plan - 100% Coverage

## Test Categories

### 1. Object Constructors (11 functions)
| Function | Test Cases | Edge Cases |
|----------|-----------|------------|
| `mk_int` | positive, negative, zero, LONG_MAX, LONG_MIN | malloc failure |
| `mk_float` | positive, negative, zero, infinity, NaN | malloc failure |
| `mk_char` | ASCII, extended chars, 0, 255 | malloc failure |
| `mk_pair` | normal, NULL car, NULL cdr, both NULL | malloc failure |
| `mk_sym` | normal string, empty string, NULL | malloc failure, string copy failure |
| `mk_box` | normal value, NULL value | malloc failure |
| `mk_error` | normal message, empty, NULL | malloc failure |
| `mk_int_stack` | normal, pool exhaustion fallback | pool overflow |
| `mk_closure` | normal, no captures, no args | malloc failure |
| `arena_mk_int` | normal | NULL arena |
| `arena_mk_pair` | normal | NULL arena |

### 2. Memory Management (15 functions)
| Function | Test Cases | Edge Cases |
|----------|-----------|------------|
| `inc_ref` | normal, NULL, stack obj, negative mark | multiple increments |
| `dec_ref` | normal, to zero, NULL, stack obj | double decrement |
| `free_obj` | normal, NULL, stack obj, already freed | re-free |
| `free_tree` | single, nested, deep tree | cyclic (should handle) |
| `free_unique` | normal, NULL, stack obj | |
| `release_children` | pair, box, closure, sym, error, channel, user | NULL children |
| `flush_freelist` | empty, single, multiple | |
| `deferred_release` | normal | |
| `defer_decrement` | normal, coalesce same obj | malloc failure |
| `process_deferred` | partial batch, full batch | |
| `flush_deferred` | empty, with items | |
| `safe_point` | below threshold, above threshold | |
| `is_stack_obj` | stack obj, heap obj, NULL | boundary cases |
| `is_nil` | NULL, non-NULL | |

### 3. Box Operations (2 functions)
| Function | Test Cases | Edge Cases |
|----------|-----------|------------|
| `box_get` | normal, empty box | NULL, wrong tag |
| `box_set` | normal, set NULL, overwrite | NULL box, wrong tag |

### 4. Pair/List Operations (10 functions)
| Function | Test Cases | Edge Cases |
|----------|-----------|------------|
| `obj_car` | normal pair | NULL, non-pair |
| `obj_cdr` | normal pair | NULL, non-pair |
| `list_length` | empty, single, multiple | non-list |
| `list_append` | both non-empty, first empty, second empty | both empty |
| `list_reverse` | empty, single, multiple | |
| `list_map` | empty, single, multiple | NULL fn |
| `list_filter` | keep all, keep none, keep some | NULL fn |
| `list_fold` | empty, single, multiple | NULL fn |
| `list_foldr` | empty, single, multiple | NULL fn |

### 5. Arithmetic Primitives (6 functions)
| Function | Test Cases | Edge Cases |
|----------|-----------|------------|
| `prim_add` | int+int, float+float, int+float | overflow, NULL |
| `prim_sub` | normal | underflow, NULL |
| `prim_mul` | normal | overflow, NULL |
| `prim_div` | normal, float division | divide by zero, NULL |
| `prim_mod` | normal, negative | mod by zero, NULL |
| `prim_abs` | positive, negative, zero | LONG_MIN |

### 6. Comparison Primitives (6 functions)
| Function | Test Cases | Edge Cases |
|----------|-----------|------------|
| `prim_lt` | true, false, equal | NULL, type mismatch |
| `prim_gt` | true, false, equal | NULL, type mismatch |
| `prim_le` | true, false, equal | NULL, type mismatch |
| `prim_ge` | true, false, equal | NULL, type mismatch |
| `prim_eq` | same, different, same ref | NULL, type mismatch |
| `prim_not` | truthy, falsy | NULL |

### 7. Type Predicates (6 functions)
| Function | Test Cases | Edge Cases |
|----------|-----------|------------|
| `prim_null` | NULL, non-NULL | |
| `prim_pair` | pair, non-pair | NULL |
| `prim_int` | int, non-int | NULL |
| `prim_float` | float, non-float | NULL |
| `prim_char` | char, non-char | NULL |
| `prim_sym` | sym, non-sym | NULL |

### 8. Type Introspection (2 functions)
| Function | Test Cases | Edge Cases |
|----------|-----------|------------|
| `ctr_tag` | each tag type | NULL, user types |
| `ctr_arg` | pair car, pair cdr, box | NULL, invalid idx |

### 9. I/O Primitives (3 functions)
| Function | Test Cases | Edge Cases |
|----------|-----------|------------|
| `prim_display` | int, float, char, string, pair | NULL, nested |
| `prim_print` | int, float, char, string, pair | NULL, nested |
| `prim_newline` | normal | |

### 10. Character/String Primitives (2 functions)
| Function | Test Cases | Edge Cases |
|----------|-----------|------------|
| `char_to_int` | normal char | NULL, non-char |
| `int_to_char` | valid range, 0, 255 | NULL, non-int, out of range |

### 11. Float Primitives (4 functions)
| Function | Test Cases | Edge Cases |
|----------|-----------|------------|
| `int_to_float` | normal | NULL, non-int |
| `float_to_int` | normal, truncation | NULL, non-float, overflow |
| `prim_floor` | positive, negative, whole | NULL, non-float |
| `prim_ceil` | positive, negative, whole | NULL, non-float |

### 12. Closure Operations (3 functions)
| Function | Test Cases | Edge Cases |
|----------|-----------|------------|
| `mk_closure` | no captures, with captures | malloc failure |
| `call_closure` | 0 args, 1 arg, multiple args | NULL closure, wrong arity |
| `closure_release` | normal | NULL |

### 13. Arena Allocator (5 functions)
| Function | Test Cases | Edge Cases |
|----------|-----------|------------|
| `arena_create` | normal | malloc failure |
| `arena_destroy` | empty, with objects, with externals | NULL |
| `arena_reset` | empty, with objects | NULL |
| `arena_alloc` | small, large, multiple blocks | NULL arena, 0 size |
| `arena_register_external` | normal | NULL arena, NULL ptr |

### 14. SCC Detection (8 functions)
| Function | Test Cases | Edge Cases |
|----------|-----------|------------|
| `create_scc` | normal | malloc failure |
| `scc_add_member` | normal, capacity expansion | NULL scc, NULL obj |
| `freeze_scc` | normal | NULL |
| `find_scc` | found, not found | empty registry |
| `release_scc` | refcount > 0, refcount = 0 | NULL |
| `release_with_scc` | in scc, not in scc | NULL |
| `detect_and_freeze_sccs` | simple cycle, complex | NULL root |
| `tarjan_strongconnect` | single node, tree, dag, cycle | deep recursion |

### 15. Generational References (5 functions)
| Function | Test Cases | Edge Cases |
|----------|-----------|------------|
| `genref_from_obj` | normal | NULL obj |
| `genref_release` | normal | NULL, double release |
| `genref_get` | valid, invalidated | NULL |
| `genref_invalidate_obj` | normal | NULL |

### 16. Weak References (3 functions)
| Function | Test Cases | Edge Cases |
|----------|-----------|------------|
| `_mk_weak_ref` | normal | malloc failure |
| `_deref_weak` | alive, dead | NULL |
| `invalidate_weak_refs_for` | found, not found | NULL target |

### 17. Component Tethering (10 functions)
- [x] `sym_component_new`: verify header allocation
- [x] `sym_component_add_member`: verify island grouping
- [x] `sym_acquire_handle`: verify boundary count
- [x] `sym_release_handle`: verify immediate dismantle
- [x] `sym_tether_begin`: verify zero-cost lock
- [x] `sym_tether_end`: verify deferred dismantle
- [x] `sym_dismantle_component`: verify edge cancellation
- [x] `sym_cleanup`: verify thread-local cleanup
- [x] `sym_alloc`: verify slab allocation
- [x] `sym_get_data`: verify payload extraction


### 18. Channels (5 functions)
| Function | Test Cases | Edge Cases |
|----------|-----------|------------|
| `make_channel` | buffered, unbuffered | malloc failure, 0 capacity |
| `channel_send` | normal, buffer full, closed | NULL channel, NULL value |
| `channel_recv` | normal, empty wait, closed | NULL channel |
| `channel_close` | normal, already closed | NULL |
| `free_channel_obj` | empty, with values | NULL |

### 19. Atoms (5 functions)
| Function | Test Cases | Edge Cases |
|----------|-----------|------------|
| `make_atom` | with initial, NULL initial | malloc failure |
| `atom_deref` | normal | NULL atom |
| `atom_reset` | normal | NULL atom, NULL value |
| `atom_swap` | normal | NULL atom, NULL fn |
| `atom_cas` | success, failure | NULL |

### 20. Threads (3 functions)
| Function | Test Cases | Edge Cases |
|----------|-----------|------------|
| `spawn_thread` | normal | NULL closure |
| `thread_join` | immediate, delayed | NULL handle |
| `spawn_goroutine` | normal, with captures | NULL closure |

### 21. Higher-Order Functions (2 functions)
| Function | Test Cases | Edge Cases |
|----------|-----------|------------|
| `prim_apply` | normal | NULL fn, NULL args |
| `prim_compose` | normal | NULL f, NULL g |

### 22. Truthiness (1 function)
| Function | Test Cases | Edge Cases |
|----------|-----------|------------|
| `is_truthy` | NULL, 0, non-zero, pair | |

### 23. IRegion Vtable (17 functions) ✅
| Function | Test Cases | Edge Cases |
|----------|-----------|------------|
| `iregion_new_arena` | normal, small block | NULL |
| `iregion_new_linear` | normal | NULL |
| `iregion_new_offset` | normal | NULL |
| `iregion_new_pool` | normal | zero size/count |
| `iregion_alloc` | normal, alignment | frozen, exhausted |
| `iregion_free_one` | normal (pool), noop (arena) | NULL |
| `iregion_free_all` | normal | NULL |
| `iregion_freeze` | normal | already frozen |
| `iregion_is_frozen` | frozen, not frozen | NULL |
| `iregion_kind` | each kind | NULL |
| `iregion_clone` | normal | NULL |
| `iregion_serialize` | linear/offset | NULL, empty |
| `iregion_remaining` | normal | NULL |
| `iregion_stats` | normal | NULL |
| `offset_region_serialize` | normal | empty |
| `offset_region_deserialize` | normal | corrupted data |
| `offset_to_ptr` | valid, OFFSET_NULL | out of bounds |

### 24. Weak Reference Control Blocks (21 functions) ✅
| Function | Test Cases | Edge Cases |
|----------|-----------|------------|
| `weak_cb_new` | normal, with destructor | malloc failure |
| `weak_cb_invalidate` | normal, with weak refs | NULL |
| `weak_cb_inc_ref` | normal | NULL |
| `weak_cb_dec_ref` | normal, to zero | NULL |
| `weak_cb_is_valid` | valid, invalidated | NULL |
| `weak_cb_get_target` | valid, invalidated | NULL |
| `weak_handle_new` | normal | NULL cb |
| `weak_handle_clone` | normal | NULL |
| `weak_handle_free` | normal | NULL |
| `weak_handle_is_valid` | valid, invalidated | NULL |
| `weak_handle_lock` | valid, invalidated | NULL |
| `weak_object_new` | normal, with destructor | NULL |
| `weak_object_free` | normal | NULL |
| `weak_table_new` | normal | zero capacity |
| `weak_table_free` | normal, with active entries | NULL |
| `weak_table_register` | normal | full table |
| `weak_table_get_handle` | valid | invalid index |
| `weak_table_invalidate` | normal | invalid index |
| `weak_table_lock` | correct gen, wrong gen | invalid index |
| `weak_table_global` | singleton | |

### 25. Transmigration/Isolation (17 functions) ✅
| Function | Test Cases | Edge Cases |
|----------|-----------|------------|
| `transmigration_new` | normal | NULL dest |
| `transmigration_free` | normal | NULL |
| `transmigration_record` | normal, many entries | NULL |
| `transmigration_lookup` | found, not found | NULL |
| `transmigrate` | normal, NULL obj | frozen dest |
| `transmigration_register_visitor` | valid types | invalid type tags |
| `check_isolation` | isolated, escaped | NULL root/region |
| `isolation_result_free` | normal | NULL |
| `region_bound_ref_new` | normal | NULL region |
| `region_bound_ref_free` | normal | NULL |
| `region_bound_ref_deref` | valid, frozen region | NULL |
| `region_bound_ref_is_valid` | valid, frozen | NULL |

### 26. External Handle Indexing (27 functions) ✅
| Function | Test Cases | Edge Cases |
|----------|-----------|------------|
| `external_table_new` | normal | zero capacity |
| `external_table_free` | normal, with active handles | NULL |
| `external_table_set_deterministic` | enable, disable | NULL |
| `external_handle_create` | normal | full table, NULL |
| `external_handle_release` | normal, with destructor | invalid, NULL |
| `external_handle_get` | valid | invalid, NULL |
| `external_handle_is_valid` | valid, released | NULL |
| `external_table_clear` | normal, with destructors | NULL |
| `external_table_count` | empty, non-empty | NULL |
| `external_table_iterate` | normal, empty | NULL |
| `external_table_global` | singleton | |
| `ffi_obj_to_handle` | normal | NULL |
| `ffi_handle_to_obj` | valid | invalid |
| `ffi_release_handle` | normal | invalid |

## Stress Tests

1. **Deep recursion** - Stack overflow handling
2. **Large allocations** - Memory pressure
3. **Rapid alloc/free cycles** - Fragmentation
4. **Concurrent channel ops** - Race conditions
5. **SCC with many nodes** - Algorithm correctness
6. **Long-running with many safe_points** - Deferred processing

### Advanced Memory Stress Tests (test_stress_memory.c)

| Category | Tests | Description |
|----------|-------|-------------|
| Memory Pressure | 5 | OOM patterns, large lists, small allocations, nested structures, interleaved types |
| Reference Counting | 3 | Extreme refcount values, shared refs, rapid cycles |
| Free List | 2 | Capacity limits, interleaved operations |
| Deferred RC | 3 | Large batches, coalescing, safe points |
| Arena | 3 | Large blocks, reset cycles, multiple arenas |
| Slot Pool | 2 | Alloc/free cycles, pool growth |
| Concurrent | 1 | Multi-threaded alloc/free |
| Memory Patterns | 2 | Wave and random allocation patterns |
| Complex Structures | 2 | Shared graphs, deep chains |
| Boxes | 2 | Many boxes, update stress |
| Symbols | 2 | Many symbols, interning stress |
| Lists | 2 | Append and reverse on large lists |
| Integration | 1 | Mixed operations |
| **Total** | **33** | Comprehensive stress testing |

### Memory Edge Cases (test_edge_cases_memory.c)

| Category | Tests | Description |
|----------|-------|-------------|
| NULL Pointers | 11 | All NULL handling edge cases |
| Immediate Values | 7 | Immediate value operations |
| Integer Edge Cases | 5 | Zero, negatives, large values |
| Float Edge Cases | 5 | Zero, infinity, NaN, limits |
| Character Edge Cases | 4 | Null, max, extended, immediates |
| Pair Edge Cases | 5 | NULL components, self-referential |
| Symbol Edge Cases | 4 | Empty, NULL, very long, special chars |
| Box Edge Cases | 5 | NULL values, immediates, wrong types |
| Reference Counting | 4 | Underflow, overflow, zero, stack objects |
| Free List | 3 | Empty flush, single item, multiple flushes |
| Deferred RC | 4 | NULL, immediate, empty flush, coalescing |
| Arena | 5 | Zero size, small blocks, empty reset, NULL ops |
| List Operations | 7 | Empty lists, circular, append edge cases, reverse |
| Arithmetic | 7 | Overflow, underflow, divide by zero, mod edge cases |
| Comparisons | 4 | NULL handling, different types, not with zero |
| Type Predicates | 2 | NULL and immediates |
| IPGE/BorrowedRef | 4 | NULL, immediate, invalid borrow, evolution |
| Tethering | 4 | NULL, immediate, edge cases |
| Stack Pool | 1 | Exhaustion |
| Concurrency | 6 | Channel edge cases, atom CAS failures |
| Safe Points | 2 | No deferred, many safe points |
| Booleans | 3 | True/false, mk_bool |
| Truthiness | 6 | NULL, zero, bools, empty list, pairs |
| **Total** | **106** | Comprehensive edge case coverage |

### Performance Benchmarks (test_performance.c)

| Category | Tests | Description |
|----------|-------|-------------|
| Allocation | 5 | Int/float/pair/list/mixed allocation throughput |
| Reference Counting | 2 | Inc/dec ref cycles, cascade frees |
| Immediate Values | 2 | Immediate int ops, boxed vs immediate comparison |
| List Operations | 5 | Traversal, length, map, reverse, append |
| Symbol Operations | 2 | Creation, interning |
| Box Operations | 1 | Get/set performance |
| Free List | 1 | Batch alloc/free reuse |
| Deferred RC | 2 | Defer decrement, safe points |
| Arena | 3 | Alloc, vs heap comparison, reset |
| Arithmetic | 3 | Int, float, mixed operations |
| Comparison | 2 | Integer compare, immediate eq |
| Deep Structures | 2 | Deep lists, tree building |
| Concurrency | 1 | Multi-threaded allocation |
| Memory Pressure | 1 | Stress test with alloc/free cycles |
| **Total** | **35** | Comprehensive performance benchmarks |

## Memory Safety Tests (with sanitizers)

1. **AddressSanitizer** - Buffer overflows, use-after-free
2. **ThreadSanitizer** - Data races
3. **MemorySanitizer** - Uninitialized reads
4. **Valgrind** - Memory leaks

## Test Infrastructure

```
runtime/tests/
├── test_main.c                  # Test runner
├── test_constructors.c          # Object constructor tests
├── test_memory.c                # Memory management tests
├── test_primitives.c            # Primitive operation tests
├── test_lists.c                 # List operation tests
├── test_closures.c              # Closure tests
├── test_tagged_pointers.c       # Tagged pointer tests
├── test_concurrency.c           # Channel, atom, thread tests
├── test_stress.c                # Stress tests
├── test_weak_refs.c             # Weak reference tests
├── test_borrowref.c             # BorrowRef tests
├── test_deferred.c              # Deferred RC tests
├── test_channel_semantics.c     # Channel semantics tests
├── test_sym_concurrency.c       # Symbol concurrency tests
├── test_component.c             # Component tethering tests
├── test_stress_memory.c         # Advanced memory stress tests (33 tests)
├── test_edge_cases_memory.c     # Memory edge cases (106 tests)
├── test_performance.c           # Performance benchmarks (35 tests)
├── test_iregion.c               # IRegion vtable tests (17 tests) ✅
├── test_weak_control_blocks.c   # Weak ref control block tests (21 tests) ✅
├── test_transmigration.c        # Transmigration/isolation tests (17 tests) ✅
├── test_external_handles.c      # External handle indexing tests (27 tests) ✅
└── Makefile                     # Build tests
```

**Note**: `test_arena.c` and `test_scc.c` are commented out as they test internal APIs that have been removed or significantly changed.

### New Region Infrastructure Tests (99 tests total)

| Test File | Tests | Categories |
|-----------|-------|------------|
| `test_iregion.c` | 17 | Arena, Linear, Offset, Pool region backends |
| `test_weak_control_blocks.c` | 21 | Control blocks, handles, weak tables, stress |
| `test_transmigration.c` | 17 | Context, isolation, region-bound refs, stress |
| `test_external_handles.c` | 27 | Create, release, generation, deterministic, FFI |

## Coverage Target

- **Line coverage**: 100%
- **Branch coverage**: 100%
- **Function coverage**: 100%

Total functions to test: ~180 (increased with region infrastructure)
Total test cases: **719+** (446 main + 99 region tests + 33 stress memory + 106 edge cases + 35 performance)
