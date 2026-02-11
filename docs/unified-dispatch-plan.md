# Unified Multiple Dispatch + Val Types Refactor

## Overview

Remove `[method]` attribute, make all typed functions use multiple dispatch like Julia.
Add `Val` value types for dispatch on values.

## Example Target Syntax

```lisp
; Multiple definitions = multiple dispatch (no [method] needed)
(define (process (^(Val 1) v)) "one")
(define (process (^(Val 2) v)) "two")
(define (process (^Int n)) (+ n 1))
(define (process (^String s)) (concat "got: " s))

(process (Val 1))        ; => "one"
(process (Val 2))        ; => "two"
(process 42)             ; => 43
(process "hi")           ; => "got: hi"
```

---

## Agent 1: Type System (types.c3)

### Tasks:
1. Add `TypeParam` union that can hold TypeId OR Value*
2. Add `T_VALUE` TypeTag for Val types
3. Add `ValueType` struct to store wrapped value
4. Register `Val` as built-in type
5. Modify type comparison to handle value types

### New Structures:
```c3
// Type parameter that can be a type or a value
struct TypeParam {
    bool is_value;
    TypeId type_id;      // when is_value = false
    int int_val;         // when is_value = true (simplified - just int for now)
}

// Value type like Val{3}
struct ValueType {
    Value* wrapped_value;  // The actual value (3 in Val{3})
}
```

### Changes to existing:
- Extend `TypeTag` with `T_VALUE`
- Add `ValueType` to TypeData union
- Add `compare_value_types()` function

---

## Agent 2: Method Tables (value.c3)

### Tasks:
1. Add `MethodTable` struct for multiple dispatch
2. Add `MethodEntry` struct for individual methods
3. Add `METHOD_TABLE` ValueTag
4. Add MethodTableVal to Value union
5. Add method table storage to Interp

### New Structures:
```c3
const usz MAX_METHODS = 32;
const usz MAX_METHOD_PARAMS = 8;

struct MethodSignature {
    TypeId[MAX_METHOD_PARAMS] param_types;
    usz param_count;
}

struct MethodEntry {
    MethodSignature signature;
    Value* implementation;  // The closure
}

struct MethodTableVal {
    SymbolId name;
    MethodEntry[MAX_METHODS] methods;
    usz method_count;
    Value* fallback;  // Untyped catch-all method (if any)
}
```

### Changes:
- Add `METHOD_TABLE` to ValueTag enum
- Add `MethodTableVal method_table_val` to Value union
- Add helper functions: `add_method()`, `find_best_method()`

---

## Agent 3: Evaluator Dispatch (eval.c3)

### Tasks:
1. Modify `eval_define_function` to build method tables
2. Modify `apply` to dispatch on method tables
3. Add `eval_val_constructor` for (Val expr)
4. Remove/deprecate `eval_defmethod`
5. Update dispatch logic to check value types

### Key Functions:
```c3
// When defining typed function, add to method table
fn EvalResult eval_define_function(Expr* expr, Env* env, Interp* interp) {
    // If function has typed params, add to method table
    // If untyped, set as fallback or single method
}

// Dispatch: find best matching method
fn Value* find_matching_method(MethodTableVal* table, Value*[] args, Interp* interp) {
    // Score each method by specificity
    // Return best match or fallback
}

// Handle (Val expr)
fn EvalResult eval_val_constructor(Value* arg, Interp* interp) {
    // Create Val instance with type_args containing the value's type
}
```

---

## Agent 4: Parser Cleanup (parser.c3)

### Tasks:
1. Keep `[method]` parsing but make it optional/deprecated
2. Ensure `(Val expr)` parses as regular application
3. Ensure typed function params parse correctly for dispatch
4. Clean up any DEF_METHOD specific code paths

### Changes:
- `[method]` becomes alias for regular typed function
- Add deprecation warning if desired
- Verify (Val 3) parses as E_APPLICATION

---

## Integration Points

### Env.lookup changes:
When looking up a function name:
1. If value is METHOD_TABLE, return the table (apply will dispatch)
2. If value is CLOSURE, return as before

### Apply changes:
1. If func is METHOD_TABLE, find best method and apply
2. If func is CLOSURE, apply as before
3. If func is symbol `Val`, create value type instance

---

## Test Cases

```lisp
; Test 1: Basic value dispatch
(define (f (^(Val 1) x)) "one")
(define (f (^(Val 2) x)) "two")
(assert (= (f (Val 1)) "one"))
(assert (= (f (Val 2)) "two"))

; Test 2: Type dispatch
(define (g (^Int n)) (* n 2))
(define (g (^String s)) (length s))
(assert (= (g 21) 42))
(assert (= (g "hello") 5))

; Test 3: Mixed dispatch
(define (h (^(Val 0) x)) "zero val")
(define (h (^Int n)) "some int")
(assert (= (h (Val 0)) "zero val"))
(assert (= (h 42) "some int"))

; Test 4: Fallback
(define (k x) "fallback")
(define (k (^Int n)) "int")
(assert (= (k 42) "int"))
(assert (= (k "hi") "fallback"))
```

---

## Execution Order

1. **Agent 1 + Agent 2** in parallel (no dependencies)
2. **Agent 3** after 1 & 2 complete (needs types + method tables)
3. **Agent 4** can run in parallel with Agent 3
4. **Integration testing** after all complete
