# OmniLisp

A C-based compiler/runtime implementation of OmniLisp (stage-polymorphic, compile-time memory management).

**Implementation status:** the current C compiler/runtime implements a **subset** of the full language design. See `omnilisp/SUMMARY.md` for the implemented set and `docs/LANGUAGE_PARITY_PLAN.md` for planned features.

OmniLisp implements the **"Collapsing Towers of Interpreters"** paradigm from Amin & Rompf (POPL 2018), combined with **ASAP (As Static As Possible)** memory management that inserts deallocation calls at compile time.

## Features (Current C Compiler)

- **Core forms**: `define`, `lambda`/`fn`, `let`, `let*`, `if`, `do`/`begin`
- **Core data**: lists, integers, symbols, chars, floats
- **Primitives**: `+ - * / %`, `< > <= >= =`, `cons car cdr null?`, `display print newline`
- **ASAP Memory Management** - Compile-time memory analysis (no garbage collection)
- **C99 + POSIX output** - generated C compiles with gcc/clang

## Features (Planned Design)

- **Pattern Matching** (guards, constructor patterns)
- **Macro System** (hygienic, syntax-case)
- **Multiple Dispatch** (Julia-style)
- **Effect Handlers** (typed recovery protocols)
- **Expanded FFI** (auto-wrapped bindings)

## Quick Start (C Compiler)

```bash
# Build
go build -o omnilisp .

# Interactive REPL
./omnilisp

# Evaluate expression
./omnilisp -e '(+ 1 2)'

# Run file
./omnilisp program.omni

# Compile to C
./omnilisp -c program.omni -o output.c
```

## REPL Example

```
$ ./omnilisp
OmniLisp REPL - Tower of Interpreters with ASAP Memory Management

  JIT: enabled (gcc found)

Type 'help' for commands, 'quit' to exit

omnilisp> (+ 1 2)
=> 3

omnilisp> (map (lambda (x) (* x 2)) '(1 2 3 4 5))
=> (2 4 6 8 10)

omnilisp> (defmacro double (x) `(+ ,x ,x) (mcall double 21))
=> 42

omnilisp> (lift 42)
Code: mk_int(42)

omnilisp> help
[comprehensive help displayed]
```

## Language Overview

### Data Types
```scheme
42                    ; integers
3.14                  ; floats
'foo                  ; symbols
#\a                   ; characters
'(1 2 3)              ; lists
()                    ; empty list
nothing               ; nothing (falsy)
false                 ; false (falsy)
true                  ; true
```

### Functions
```scheme
; Lambda
(lambda (x) (+ x 1))

; Recursive lambda
(lambda self (n)
  (if (= n 0) 1 (* n (self (- n 1)))))

; Let bindings
(let ((x 10) (y 20)) (+ x y))
```

### Pattern Matching
```scheme
(match value
  ((0) 'zero)
  ((n :when (> n 0)) 'positive)
  (_ 'negative))
```

### Staging
```scheme
(lift 42)              ; quote as code
(run code)             ; execute code (JIT)
(EM expr)              ; meta-level evaluation
(meta-level)           ; get current level
```

### Macros
```scheme
(defmacro when (cond body)
  `(if ,cond ,body nothing)
  (mcall when (> 5 3) 'yes))
```

### Handler Customization
```scheme
; Customize literal handler to double values
(with-handlers
  ((lit (lambda (x) (* x 2))))
  (+ 3 4))  ; => 14
```

## Documentation

- [Language Reference](docs/LANGUAGE_REFERENCE.md) - Complete language documentation
- [Architecture](ARCHITECTURE.md) - Implementation details
- [Memory Optimizations](docs/MEMORY_OPTIMIZATIONS.md) - Reclamation vs safety, strategy routing
- [Generational Memory](docs/GENERATIONAL_MEMORY.md) - GenRef/IPGE soundness requirements
- [TODO](TODO.md) - Feature status and roadmap

## Project Structure

```
omnilisp/
├── main.go              # CLI and REPL
├── pkg/
│   ├── ast/             # AST types and values
│   ├── parser/          # S-expression parser
│   ├── eval/            # Evaluator with tower of interpreters
│   ├── codegen/         # C code generation
│   ├── analysis/        # ASAP liveness/escape analysis
│   ├── memory/          # Memory management strategies
│   └── jit/             # JIT compilation via GCC
└── docs/
    └── LANGUAGE_REFERENCE.md
```

## Key Concepts

### Tower of Interpreters

OmniLisp's evaluator consists of 9 customizable handlers:

| Handler | Purpose |
|---------|---------|
| `lit` | Literal evaluation |
| `var` | Variable lookup |
| `lam` | Lambda creation |
| `app` | Function application |
| `if` | Conditional evaluation |
| `lft` | Lift to code |
| `run` | Execute code |
| `em` | Meta-level escape |
| `clam` | Compiled lambda |

### ASAP Memory Management

Unlike garbage collection, ASAP analyzes programs at compile time to insert `free()` calls at optimal points:

1. **Liveness Analysis** - Track last use of each variable
2. **Escape Analysis** - Determine if values escape their scope
3. **Shape Analysis** - Classify data as Tree/DAG/Cyclic
4. **Automatic Weak Edges** - Break ownership cycles at compile time

## Testing

```bash
go test ./...
```

## References

- [Collapsing Towers of Interpreters](https://www.cs.purdue.edu/homes/rompf/papers/amin-popl18.pdf) - Amin & Rompf, POPL 2018
- [ASAP Memory Management](https://www.cl.cam.ac.uk/techreports/UCAM-CL-TR-908.pdf) - Proust, 2017
- [Perceus: Garbage Free Reference Counting](https://dl.acm.org/doi/10.1145/3453483.3454032) - PLDI 2021

## License

MIT
