# Omni Lisp to C3 Compiler

This document describes the Omni Lisp compiler that translates Lisp source code to C3 source code for native execution.

## Overview

The compiler uses a hybrid approach:
- Straightforward expressions compile to direct C3 code
- Dynamic features (closures, continuations) use a minimal runtime library
- Memory is managed using hierarchical region-based allocation with automatic promotion

## Usage

```bash
# Compile Lisp to C3
./build/main --compile input.lisp output.c3

# Build the generated C3 code with the runtime
c3c compile output.c3 src/lisp/runtime.c3 src/main.c3 -o program

# Run the native executable
./program
```

## Architecture

```
┌─────────────┐    ┌─────────────┐    ┌─────────────┐    ┌──────────┐
│ Lisp Source │───▶│   Parser    │───▶│  Compiler   │───▶│ C3 Source│
└─────────────┘    └─────────────┘    └─────────────┘    └──────────┘
                                            │
                                            ▼
                                    ┌─────────────┐
                                    │  Runtime    │ (linked library)
                                    │  + Regions  │
                                    └─────────────┘
```

## Files

| File | Description |
|------|-------------|
| `src/lisp/compiler.c3` | Main compiler module |
| `src/lisp/runtime.c3` | Runtime library with region-based memory |
| `src/main.c3` | Entry point with region system and `--compile` flag |

## Memory Management: Region-Based Allocation

The runtime uses a hierarchical region-based memory system from `src/main.c3`:

### Key Concepts

1. **Regions** are memory containers that hold allocated objects
2. **Root Region** holds global definitions (lives for the entire program)
3. **Frame Regions** are created for functions that create closures
4. **Write Barriers** mark objects as "escaped" when they cross region boundaries
5. **Automatic Promotion** moves escaped objects to parent regions when a child dies

### Region Lifecycle

```
┌─────────────────────────────────────────────────────────────────┐
│ Root Region (lifetime: entire program)                          │
│  ├── Global definitions (define x ...)                         │
│  └── Frame Region for function A (lifetime: call duration)      │
│       ├── Local allocations                                     │
│       ├── Closure C (captures value V) [marked escaped]        │
│       └── When frame dies:                                      │
│           - Escaped objects (C) promoted to root                │
│           - Local objects destroyed                             │
└─────────────────────────────────────────────────────────────────┘
```

### How it Works

1. **Function Entry** (for closure-creating functions):
   ```c3
   RegionHandle _frame = runtime::rt_push_frame();
   ```

2. **Closure Creation** (with captures):
   ```c3
   Lambda_1 _closure_data;
   _closure_data.home_region = runtime::rt_current_region();
   _closure_data.captured_n = n;
   runtime::rt_capture_value(_closure_data.home_region, n);  // Write barrier
   ObjectHandle _h = allocate_in(rt_current_region(), Lambda_1, _closure_data);
   ```

3. **Function Exit**:
   ```c3
   runtime::rt_pop_frame(_frame);  // Promotes escaped objects, destroys locals
   ```

## Compiler Module (`src/lisp/compiler.c3`)

### Key Structures

```c3
struct Compiler {
    Interp*     interp;          // For symbol table access
    List{char}  output;          // Generated C3 code buffer
    usz         indent;          // Current indentation level
    usz         temp_counter;    // For generating unique temp names
    usz         lambda_counter;  // For generating lambda function names
    List{SymbolId} current_captures;
    List{SymbolId} defined_globals;
    List{LambdaDef} lambda_defs;
    bool[256] lambda_creates_closure;  // Region optimization tracking
}

struct LambdaDef {
    usz         id;              // Lambda ID
    SymbolId    param;           // Parameter name
    Expr*       body;            // Body expression
    SymbolId[16] captures;       // Captured variables
    usz         capture_count;
    bool        creates_closure; // Does this lambda create nested closures?
}
```

### Compilation Pipeline

1. **Parse** - Convert source to AST using existing parser
2. **Collect globals** - Find all top-level `define` forms
3. **Scan lambdas** - Register all lambdas, analyze free variables, detect closure creation
4. **Emit prelude** - Import statements (including `main` for region system)
5. **Emit lambda definitions** - Closure structs (with home_region) and invoke functions
6. **Emit global declarations** - Module-level variables for defines
7. **Emit main** - Initialize/shutdown runtime with region API

### Expression Compilation

| Lisp | C3 |
|------|-----|
| `42` | `runtime::make_int(42)` |
| `"hello"` | `runtime::make_string("hello")` |
| `nil` | `runtime::make_nil()` |
| `true` | `runtime::make_true()` |
| `(+ 1 2)` | `runtime::rt_invoke(runtime::rt_invoke(runtime::make_prim(&runtime::rt_add), runtime::make_int(1)), runtime::make_int(2))` |
| `(if test then else)` | `(runtime::rt_is_truthy(test) ? then : else)` |
| `(let ((x 10)) body)` | `{ runtime::Value x = 10; body }` |
| `(lambda (x) body)` | `runtime::make_closure(...)` with region allocation |
| `(define name value)` | `name = value;` (with global declaration) |

### Closure Compilation with Regions

Lambdas that capture free variables generate a struct with region tracking:

```c3
// Lisp: (lambda (x) (+ x n))  where n is captured
struct Lambda_1 {
    main::RegionHandle home_region;  // Region where closure was created
    runtime::Value captured_n;
}

fn runtime::Value invoke_lambda_1(void* _self, runtime::Value x) {
    // If this lambda creates closures, push frame region
    main::RegionHandle _frame = runtime::rt_push_frame();

    Lambda_1* self = (Lambda_1*)_self;
    runtime::Value n = self.captured_n;

    runtime::Value _result = runtime::rt_invoke(runtime::rt_invoke(
        runtime::make_prim(&runtime::rt_add), x), n);

    // Pop frame before returning
    runtime::rt_pop_frame(_frame);
    return _result;
}
```

Closure allocation at creation site:
```c3
// In expression context:
Lambda_1 _closure_data;
_closure_data.home_region = runtime::rt_current_region();
_closure_data.captured_n = n;
runtime::rt_capture_value(_closure_data.home_region, n);  // Write barrier
main::ObjectHandle _h = main::allocate_in(runtime::rt_current_region(), Lambda_1, _closure_data);
runtime::make_closure(main::dereference(_h), &invoke_lambda_1);
```

## Runtime Module (`src/lisp/runtime.c3`)

### Value Representation (Region-Based)

```c3
enum ValueTag : char {
    V_NIL,
    V_INT,
    V_STRING,
    V_SYMBOL,
    V_CONS,
    V_CLOSURE,
    V_PRIM,        // Binary primitive (curried)
    V_PRIM_UNARY,  // Unary primitive (called immediately)
    V_PARTIAL,     // Partially applied primitive
    V_TRUE,
}

struct Value {
    ValueTag tag;
    union {
        long               int_val;      // V_INT - immediate
        main::ObjectHandle str_handle;   // V_STRING - region-allocated
        main::ObjectHandle sym_handle;   // V_SYMBOL - region-allocated
        ConsCell           cons_val;     // V_CONS - inline with handles
        ClosureData        closure_val;  // V_CLOSURE - with home region
        PrimFn             prim_val;     // V_PRIM - function pointer
        PartialData        partial_val;  // V_PARTIAL - with handle
    }
}

struct ConsCell {
    main::ObjectHandle car;  // Handle to car Value
    main::ObjectHandle cdr;  // Handle to cdr Value
}

struct ClosureData {
    void*              data;         // Closure struct pointer
    ClosureFn          invoke;       // Function pointer
    main::RegionHandle home_region;  // Where closure was created
}
```

### Value Constructors

| Function | Description |
|----------|-------------|
| `make_nil()` | Create nil value |
| `make_true()` | Create true value |
| `make_int(n)` | Create integer value (immediate) |
| `make_string(s)` | Create string (allocated in current region) |
| `make_symbol(s)` | Create symbol (allocated in current region) |
| `make_closure(data, fn)` | Create closure with current region |
| `make_prim(fn)` | Create binary primitive (curried) |
| `make_prim_unary(fn)` | Create unary primitive |
| `make_partial(fn, first)` | Create partial application (arg in region) |

### Region Management Functions

| Function | Description |
|----------|-------------|
| `rt_init()` | Initialize runtime and region registry |
| `rt_shutdown()` | Shutdown and clean up all regions |
| `rt_push_frame()` | Create child region for function call |
| `rt_pop_frame(frame)` | Release frame, promote escaped objects |
| `rt_current_region()` | Get current allocation region |
| `rt_root_region()` | Get root region for globals |
| `rt_capture_value(region, v)` | Apply write barrier for captured value |

### Core Functions

| Function | Description |
|----------|-------------|
| `rt_invoke(func, arg)` | Apply function to argument |
| `rt_is_truthy(v)` | Check if value is truthy (not nil/0) |
| `rt_values_equal(a, b)` | Deep equality comparison |
| `rt_cons(car, cdr)` | Create cons cell in current region |

### Primitives

**Arithmetic** (binary, curried):
- `rt_add`, `rt_sub`, `rt_mul`, `rt_div`, `rt_mod`

**Comparison** (binary, curried):
- `rt_eq`, `rt_lt`, `rt_gt`, `rt_le`, `rt_ge`

**List operations**:
- `rt_cons_prim` (binary) - Create cons cell
- `rt_car`, `rt_cdr` (unary) - Access car/cdr (dereferences handles)
- `rt_null_p`, `rt_pair_p` (unary) - Type predicates
- `rt_list`, `rt_length` (unary) - List utilities

**Boolean**:
- `rt_not` (unary) - Logical negation

**I/O**:
- `rt_print`, `rt_println` (unary) - Output values

### Currying Model

Binary primitives use currying:
1. First `rt_invoke` creates a partial application
2. Second `rt_invoke` completes the call

```
(+ 1 2) compiles to:
  rt_invoke(rt_invoke(make_prim(&rt_add), 1), 2)

  Step 1: rt_invoke(prim, 1) → partial{rt_add, 1}
  Step 2: rt_invoke(partial, 2) → rt_add(1, 2) → 3
```

Unary primitives (V_PRIM_UNARY) call immediately:
```
(println x) compiles to:
  rt_invoke(make_prim_unary(&rt_println), x)

  → rt_println(x, nil) → prints x
```

## Examples

### Simple Arithmetic
```lisp
(+ 1 2)
```
Output: `3`

### Function Definition
```lisp
(define square (lambda (x) (* x x)))
(println (square 5))
```
Output: `25`

### Closure with Capture
```lisp
(define make-adder (lambda (n) (lambda (x) (+ x n))))
(define add5 (make-adder 5))
(println (add5 10))
```
Output: `15`

### Recursion
```lisp
(define fact (lambda (n) (if (= n 0) 1 (* n (fact (- n 1))))))
(println (fact 5))
```
Output: `120`

### Nested Closures (Region Promotion Test)
```lisp
(define outer (lambda (a)
  (lambda (b)
    (lambda (c) (+ a (+ b c))))))
(println (((outer 1) 2) 3))
```
Output: `6`

This tests proper promotion: when `outer` returns, its closure escapes and is promoted.
When that closure is called, its inner closure escapes and is promoted. All captured
values remain accessible through the forwarding chain.

## Region System Benefits

1. **No GC Pauses** - Deterministic cleanup when regions die
2. **Locality** - Related allocations stay together in memory
3. **Automatic Cleanup** - No manual memory management required
4. **Efficient Closure Support** - Escaped closures automatically promoted

## Limitations

- Maximum 16 captured variables per closure
- Strings use 4096-byte buffer in runtime
- TCO via V_THUNK trampoline (resolved — TCO now works)
- Reset/shift continuations delegate to interpreter via `rt_eval_source()`
- Handle/perform effects delegate to interpreter via `rt_eval_source()`
- Type system forms (define [type], [union], etc.) delegate to interpreter
- Macros expanded at compile time, complex macros delegate to interpreter

## Generated Code Structure

```c3
// Generated by Omni Lisp Compiler
// Do not edit manually

import std::io;
import main;
import lisp::runtime;

// Closure structs (if any captures)
struct Lambda_1 {
    main::RegionHandle home_region;
    runtime::Value captured_x;
}

// Lambda functions
fn runtime::Value invoke_lambda_0(void* _self, runtime::Value arg) {
    // Push frame if creates closures
    main::RegionHandle _frame = runtime::rt_push_frame();

    // ... compiled body ...

    // Pop frame before return
    runtime::rt_pop_frame(_frame);
    return _result;
}

// Global variables (for defines)
runtime::Value my_function;

fn int main() {
    runtime::rt_init();

    // Compiled top-level expressions
    my_function = runtime::make_closure(null, &invoke_lambda_0);
    // ...

    runtime::rt_shutdown();
    return 0;
}
```
