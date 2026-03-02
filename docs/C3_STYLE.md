# C3 Best Practices — Omni Lisp

Derived from actual bugs found and fixed in Session 67.
Each pattern maps to a real issue that cost debugging time.

---

## 1. defer — Prevent Resource Leaks

**Bug it prevents:** JIT pool exhaustion (jit_gc never called in run() path).

Rule: **acquire → defer release → use**. Never separate.

```c3
// LMDB transactions — MUST defer abort (commit replaces it)
MdbTxn* txn = null;
int rc = mdb_txn_begin(env, null, 0, &txn);
if (rc != MDB_SUCCESS) return raise_error(interp, "txn failed");
defer mdb_txn_abort(txn);
// ... work ... then:
mdb_txn_commit(txn);  // abort in defer is harmless after commit

// Scope save/restore — MUST defer restore
main::ScopeRegion* saved = interp.current_scope;
interp.current_scope = interp.root_scope;
defer interp.current_scope = saved;

// Parser depth — MUST defer decrement
self.depth++;
defer self.depth--;

// Scheduler running flag
g_scheduler.running = true;
defer g_scheduler.running = false;

// defer catch — cleanup only on error path
char[] data = mem::new_array(char, 12)!;
defer (catch err) { (void)mem::free(data); }
```

**Where defer is MISSING in the codebase (tech debt):**
- `prim_tcp_connect`: `c_freeaddrinfo(result)` not deferred — leaks on socket error
- `prim_deduce_open`: `mdb_env_close` not deferred — leaks on open error
- `jit_compile`: `_jit_destroy_state(s)` not deferred — leaks on compile error
- Multiple `interp.current_scope` save/restore pairs without defer

---

## 2. distinct — Prevent Type Confusion

**Bug it prevents:** FFI_HANDLE reused for TcpHandle*, Relation*, AtomicRef*, TlsHandle*, DeduceDb* — all cast to `FfiHandle*`. A `mem::free` on any of them frees the wrong struct size. No compile-time checking.

```c3
// BAD: all FFI handles are the same type
v.ffi_val = (FfiHandle*)tcp_handle;   // TcpHandle is 8 bytes
v.ffi_val = (FfiHandle*)relation;     // Relation is ~300 bytes
v.ffi_val = (FfiHandle*)atomic_ref;   // AtomicRef is 8 bytes
// All freed with mem::free(v.ffi_val) — wrong size? heap corruption?

// GOOD: distinct handle types
distinct TcpHandlePtr = void*;
distinct RelationPtr = void*;
distinct AtomicRefPtr = void*;
distinct TlsHandlePtr = void*;
// Cannot accidentally pass TcpHandlePtr where RelationPtr expected
```

**Where distinct types would help:**
- `FfiHandle*` reuse → separate `ValueTag` per handle type, or distinct wrapper
- `SymbolId` is already `uint` — should be `distinct SymbolId = uint`
- `TypeId` is already `uint` — should be `distinct TypeId = uint`
- Scope generation stamps — `distinct ScopeGen = uint`

---

## 3. Contracts — Enforce Invariants

**Bug it prevents:** copy_to_parent called with stale releasing_scope → use-after-free.

```c3
// Scope nesting — the critical invariant
<* @require interp.current_scope != null : "current_scope is null" *>
<* @require interp.current_scope != interp.releasing_scope : "copying into releasing scope" *>
fn Value* copy_to_parent(Value* v, Interp* interp) { ... }

// Value tag validity
<* @require v != null : "null value" *>
<* @require v.tag != ERROR : "unchecked error" *>
fn void use_value(Value* v) { ... }

// JIT pool bounds
<* @require g_jit_state_count < JIT_STATE_POOL_SIZE : "JIT pool full" *>
fn JitFn jit_compile(Expr* expr, Interp* interp) { ... }

// Scope release safety
<* @require scope.refcount > 0 : "releasing already-freed scope" *>
fn void scope_release(ScopeRegion* scope) { ... }

// Relation column count
<* @require rel.col_count > 0 : "empty relation" *>
<* @require val_count == rel.col_count : "wrong number of values" *>
fn void assert_fact(Relation* rel, Value*[] vals) { ... }
```

**Key invariants to enforce:**
- `current_scope != null` at all allocation points
- `releasing_scope` is null or valid during copy_to_parent
- Value tag matches the union field being accessed
- Scope refcount > 0 before release
- Handler stack depth ≤ MAX_HANDLERS

---

## 4. Optionals — Replace Null Checks

**Bug it prevents:** Silent null propagation through long call chains → crash far from cause.

```c3
// BAD: null checks everywhere, easy to miss one
Value* result = hashmap_get(dict.hashmap_val, key);
if (result == null) return raise_error(interp, "key not found");

// GOOD: optional makes the caller handle the absence
fn Value*! hashmap_get(HashMap* map, Value* key) {
    // ... search ...
    if (!found) return LookupError.KEY_NOT_FOUND?;
    return entry.value;
}
// Caller must handle:
Value* val = hashmap_get(map, key)!;  // rethrow
Value* val = hashmap_get(map, key) ?? make_nil(interp);  // default
```

**Where optionals would improve the codebase:**
- `hashmap_get` → `Value*!` instead of returning null
- `Env.lookup` → `Value*!` instead of returning null
- `jit_compile` → `JitFn!` instead of returning null
- `parse_expr` → `Expr*!` instead of returning null + setting error flag
- All `prim_*` functions → `Value*!` instead of returning ERROR-tagged values

---

## 5. fault — Structured Error Categories

**Bug it prevents:** Error strings compared by text content instead of typed categories.

```c3
fault ParseError {
    UNTERMINATED_STRING,
    UNEXPECTED_TOKEN,
    NESTING_TOO_DEEP,
    UNKNOWN_ATTRIBUTE,
}

fault EvalError {
    UNBOUND_VARIABLE,
    TYPE_MISMATCH,
    ARITY_MISMATCH,
    DIVISION_BY_ZERO,
    STACK_OVERFLOW,
}

fault IoError {
    CONNECTION_REFUSED,
    DNS_FAILED,
    TLS_HANDSHAKE_FAILED,
    FILE_NOT_FOUND,
}

// Instead of: return raise_error(interp, "tcp-connect: connection failed")
// Use: return IoError.CONNECTION_REFUSED?
```

---

## 6. Generic Modules — Reduce Duplication

**Where generics would help:**
- `FastPathEntry[32]` dispatch table → generic `LookupTable{Key, Value}`
- `HashMap` (open-addressing) → generic `HashMap{Key, Value}`
- `StackPool` / JIT state pool → generic `Pool{Type}`
- `Bindings` (unification) → generic `SmallMap{Key, Value, N}`

```c3
module lookup_table {Key, Value, usz SIZE};
struct Table {
    Key[SIZE] keys;
    Value[SIZE] values;
    usz count;
}
fn Value* Table.get(Table* self, Key key) { ... }
fn void Table.set(Table* self, Key key, Value val) { ... }

// Instantiate
alias FastPathTable = lookup_table::Table{SymbolId, Value*, 32};
alias JitCache = lookup_table::Table{Expr*, JitFn, 1024};
```

---

## 7. Compile-Time — Catch Errors Early

```c3
// PrimReg array size must match entry count
$assert(regular_prims.len == 137, "PrimReg array size mismatch");

// Value union size check
$assert(Value.sizeof <= 24, "Value struct too large");

// Scope chunk alignment
$assert(ScopeChunk.sizeof % 16 == 0, "ScopeChunk not aligned");

// Conditional debug code
$if env::DEBUG:
    io::eprintfn("[debug] scope_release: %p refcount=%d", scope, scope.refcount);
$endif

// Embed stdlib at compile time (already used)
char[] stdlib_src = $embed("../../stdlib/stdlib.lisp");
```

---

## 8. foreach — Replace Index Loops

```c3
// BAD: manual index
for (usz i = 0; i < args.len; i++) {
    Value* arg = args[i];
    // ...
}

// GOOD: foreach
foreach (arg : args) {
    // ...
}

// With index when needed
foreach (i, arg : args) {
    // ...
}

// Mutate via pointer
foreach (&entry : hash_entries) {
    if (entry.key == target) entry.value = new_val;
}
```

---

## 9. bitstruct — Pack Flags

```c3
// Current: multiple bool fields in Interp
//   bool strict_mode;
//   bool jit_debug_trace;
//   bool escape_env_mode;
// Each bool is 1 byte = wasted space

// Better: bitstruct
bitstruct InterpFlags : char {
    bool strict_mode : 0;
    bool escape_env_mode : 1;
    bool raise_pending : 2;
    bool in_macro_expand : 3;
}
```

---

## 10. Switch — Exhaustive Tag Dispatch

C3 switches are exhaustive by default — use this for Value tag dispatch.

```c3
// GOOD: compiler warns on missing cases
switch (v.tag) {
    case NIL: ...;
    case INT: ...;
    case STRING: ...;
    case SYMBOL: ...;
    case CONS: ...;
    case CLOSURE: ...;
    // ... all tags ...
    // No default → compiler error if a tag is added but not handled
}
```

---

## Bug Prevention Checklist

| Session 67 Bug | C3 Feature That Prevents It |
|---|---|
| copy_to_parent use-after-free | `@require releasing_scope valid` + distinct scope types |
| JIT pool exhaustion | `defer jit_gc()` at every allocation site |
| FFI_HANDLE type confusion | `distinct` handle types per subsystem |
| Pika global mutable state | Pass state struct (already fixed), `@pure` on query fns |
| malloc hangs (heap corruption) | `@ensure` postconditions on scope_release, ASAN in CI |
| PrimReg array size mismatch | `$assert` array size matches entry count |

---

## Omni-Specific Rules

**malloc vs scope-region**: Prefer scope-region for Values. `mem::malloc` for C structs, FFI, Expr nodes. Every malloc → `defer mem::free` or registered scope dtor. **Never mix**: don't `mem::free` a scope-region pointer, don't scope-release a malloc'd pointer.

**Register pattern**: `regular_prims[]` for primitives, `dispatched_prims[]` for dispatch tables, `register_fast_path()` for effects. Update array size. Use `$assert` to verify.

**Testing**: `test_eq`/`test_str`/`test_tag` test both interp + JIT paths. `setup()` for side effects. `run()` for single expressions. Run with `--sanitize=address` to catch use-after-free.

**Context boundaries**: Save/restore ALL interp state (eval_depth, jit_env, match_env, flags, TCO, scope, escape_scope) at every StackCtx switch. Use defer for restore.

Sources:
- [C3 Language](https://c3-lang.org/)
- [C3 Features](https://c3-lang.org/getting-started/faq/allfeatures)
