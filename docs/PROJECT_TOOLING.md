# Omni Lisp — Project Tooling

**Last updated:** 2026-02-25

Omni provides CLI commands for creating projects and auto-generating FFI bindings from C headers.

---

## CLI Commands

### `--init` — Scaffold a New Project

```bash
omni --init myproject
```

Creates a complete project directory:

```
myproject/
  omni.toml             # Project config (the only file you edit)
  src/
    main.omni           # Entry point
  lib/
    ffi/                # Auto-generated FFI bindings go here
  include/              # Drop C headers here for local use
  build/                # Build artifacts
    project.json        # C3 build config (generated — do not edit)
```

The generated `src/main.omni` contains a hello-world program:

```lisp
(println "Hello from myproject!")
```

### `--bind` — Generate FFI Bindings

```bash
omni --bind myproject/
omni --bind                 # uses current directory
```

Reads `omni.toml`, parses C headers using libclang, and writes Omni FFI modules to `lib/ffi/`.

**Requires:** libclang installed on the system (see [Dependencies](#dependencies) below).

---

## Developer Test Profiles

### Boundary-Hardening Profile

```bash
scripts/run_boundary_hardening.sh
```

Runs a full hardening matrix:

- normal build + test run,
- ASAN build + test run,
- `OMNI_FIBER_TEMP=1` enabled,
- `OMNI_STACK_AFFINITY_HARNESS=1` enabled (subprocess misuse probe),
- summary output via `OMNI_TEST_SUMMARY=1`.
- summary assertions (required suites must report `fail=0`).

Toggles:

- `OMNI_BOUNDARY_ENABLE_FIBER_TEMP=0` to disable Fiber TEMP exercise.
- `OMNI_BOUNDARY_ENABLE_AFFINITY_HARNESS=0` to disable misuse probe.
- `OMNI_BOUNDARY_QUIET=0` for verbose suite output.
- `OMNI_BOUNDARY_SUMMARY=0` to disable summary lines.
- `OMNI_BOUNDARY_ASSERT_SUMMARY=0` to skip summary verification.

Artifacts:

- `build/boundary_hardening_normal.log`
- `build/boundary_hardening_asan.log`

---

## omni.toml Format

The project configuration uses a minimal TOML subset.

### Project Metadata

```toml
[project]
name = "myproject"
version = "0.1.0"
```

### Build Configuration

The `[build]` section controls compilation settings. These map to C3 compiler flags in the generated `build/project.json`.

```toml
[build]
output-dir = "build"        # Where build artifacts go
safety = "safe"             # "safe", "fast", or "none"
opt = "O0"                  # "O0", "O1", "O2", "O3", "Os", "Oz"
debug-info = "full"         # "full", "line-tables", "none"
# sanitize = "none"         # "none", "address", "memory", "thread"
# single-module = false
```

| Field | Default | Values | Description |
|-------|---------|--------|-------------|
| `output-dir` | `"build"` | Any path | Build output directory |
| `safety` | `"safe"` | `"safe"`, `"fast"`, `"none"` | Runtime bounds/null checks, contracts |
| `opt` | `"O0"` | `"O0"`–`"O3"`, `"Os"`, `"Oz"` | Optimization level |
| `debug-info` | `"full"` | `"full"`, `"line-tables"`, `"none"` | Debug symbol level |
| `sanitize` | `"none"` | `"none"`, `"address"`, `"memory"`, `"thread"` | Sanitizer |
| `single-module` | `false` | `true`, `false` | Compile all modules together (more inlining) |

**Safety levels explained:**

- **`"safe"`** (default) — Bounds checking, null pointer checks, contract enforcement. Use during development.
- **`"fast"`** — Disables safety checks for maximum performance. Use for release/production.
- **`"none"`** — No safety, no contracts. Only for benchmarking or when you know exactly what you're doing.

### FFI Dependencies

Each C library dependency gets a `[dependencies.ffi.NAME]` section:

```toml
[dependencies.ffi.math]
library = "m"                           # Shared lib name → dlopen("libm.so")
headers = ["/usr/include/math.h"]       # C headers to parse
functions = ["sin", "cos", "sqrt"]      # Optional: only bind these functions
```

| Field | Required | Description |
|-------|----------|-------------|
| `library` | Yes | Shared library name (without `lib` prefix and `.so` suffix) |
| `headers` | Yes | Array of C header file paths to parse |
| `functions` | No | Array of function names to bind. If omitted, all exported functions are bound. |

### Multiple Dependencies

```toml
[dependencies.ffi.math]
library = "m"
headers = ["/usr/include/math.h"]
functions = ["sin", "cos", "sqrt", "pow", "floor", "ceil"]

[dependencies.ffi.curl]
library = "curl"
headers = ["/usr/include/curl/curl.h"]
# No functions list = bind ALL exported function declarations
```

---

## Generated Bindings

Running `--bind` produces one `.omni` file per dependency in `lib/ffi/`.

### Example: `lib/ffi/math.omni`

```lisp
;; Auto-generated FFI bindings for libm
;; Regenerate with: omni --bind

(module ffi-math (export cos sin sqrt)

  (define _lib (ffi-open "libm.so"))

  (define (cos (^Double arg0))
    (ffi-call _lib "cos" 'double arg0 'double))

  (define (sin (^Double arg0))
    (ffi-call _lib "sin" 'double arg0 'double))

  (define (sqrt (^Double arg0))
    (ffi-call _lib "sqrt" 'double arg0 'double))

)
```

### What Gets Generated

- A `module` with an `export` list of all bound functions
- A `_lib` handle opened via `ffi-open`
- Each function gets typed parameters (`^Int`, `^Double`, `^String`) and a body calling `ffi-call` with the appropriate type annotations
- C `snake_case` names are converted to Omni `kebab-case` (e.g., `string_length` becomes `string-length`)
- Variadic C functions are skipped with a comment (Omni's FFI doesn't support variadic calls)

### Using Generated Bindings

```lisp
;; Import the generated module
(import "lib/ffi/math.omni")

;; Use via qualified access
(ffi-math.sin 1.0)    ;; => 0.8414709848...
(ffi-math.sqrt 2.0)   ;; => 1.4142135623...

;; Or import specific functions
(import ffi-math (sin cos sqrt))
(sin 3.14159)          ;; => ~0.0
```

---

## C-to-Omni Type Mapping

The binding generator uses libclang to resolve types (including typedefs) and maps them:

| C Type | Omni FFI Symbol | Omni Type Annotation | Notes |
|--------|----------------|---------------------|-------|
| `int`, `long`, `unsigned int`, `unsigned long` | `'int` | `^Int` | All integer-width types |
| `size_t`, `ssize_t` | `'int` | `^Int` | Resolved via typedef |
| `enum` types | `'int` | `^Int` | Enums are integers |
| `float`, `double` | `'double` | `^Double` | All floating-point types |
| `char *`, `const char *` | `'string` | `^String` | Detected by type spelling |
| `void *`, other pointers | `'ptr` | `^Int` | Opaque handle as integer |
| `void` (return only) | `'void` | — | No annotation for void returns |

### Limitations

- **Struct parameters/returns**: C functions that pass or return structs by value cannot be bound (Omni's `ffi-call` only handles scalars and pointers)
- **Variadic functions**: Skipped automatically (e.g., `printf`)
- **Function pointers as parameters**: Mapped as `'ptr`/`^Int` (callback registration requires manual wrappers)
- **Macros**: `#define` constants and macro-functions are not parsed (libclang only sees declarations)

---

## Dependencies

### libclang (optional — only needed for `--bind`)

libclang is loaded at runtime via `dlopen` only when `--bind` is invoked. It is **not** required for building, running, or compiling Omni programs.

**Install:**

| Distribution | Command |
|-------------|---------|
| Arch Linux | `pacman -S clang` |
| Debian/Ubuntu | `apt install libclang-dev` |
| Fedora | `dnf install clang-devel` |

The tool searches for libclang in this order:
1. `libclang.so`
2. `libclang.so.1`
3. `/usr/lib/libclang.so`
4. `/usr/lib/llvm/lib/libclang.so`

If libclang is not found, `--bind` prints install instructions and exits with an error.

---

## Complete Workflow Example

```bash
# 1. Create a new project
omni --init calculator

# 2. Edit omni.toml to add math library
cat > calculator/omni.toml << 'EOF'
[project]
name = "calculator"
version = "0.1.0"

[build]
output-dir = "build"
safety = "safe"
opt = "O0"
debug-info = "full"

[dependencies.ffi.math]
library = "m"
headers = ["/usr/include/math.h"]
functions = ["sin", "cos", "tan", "sqrt", "pow", "log", "exp", "floor", "ceil"]
EOF

# 3. Generate FFI bindings
omni --bind calculator/

# 4. Write your program
cat > calculator/src/main.omni << 'EOF'
(import "lib/ffi/math.omni")

(println "sin(pi/2) =" (ffi-math.sin 1.5707963))
(println "sqrt(2)   =" (ffi-math.sqrt 2.0))
(println "2^10      =" (ffi-math.pow 2.0 10.0))
EOF

# 5. Run it
cd calculator
LD_LIBRARY_PATH=/usr/local/lib ../build/main src/main.omni
```

---

## Implementation Details

### Architecture

```
omni.toml ──► TOML Parser ──► TomlConfig
                                    │
C Headers ──► libclang (dlopen) ──► ParsedFunc[]
                                    │
                                    ▼
                            Binding Generator ──► lib/ffi/*.omni
```

### Source Files

| File | Description |
|------|-------------|
| `src/lisp/toml.c3` | Minimal TOML parser (~260 lines) |
| `src/lisp/libclang_bind.c3` | libclang dlopen wrapper + C header visitor (~300 lines) |
| `src/lisp/bindgen.c3` | Omni FFI module code generator (~150 lines) |
| `src/entry.c3` | `run_init()` and `run_bind()` CLI handlers |

### Design Decisions

- **`omni.toml` + `build/project.json`**: Omni's config lives at project root; C3's `project.json` is generated inside `build/` to keep the root clean and make the ownership clear. C3 is invoked with `--path build/` to find it.
- **libclang via C3-level dlopen** (not Omni FFI): libclang returns structs by value (`CXString`, `CXCursor`, `CXType`) which Omni's `ffi-call` can't handle. C3 function pointer aliases with proper struct types handle the x86-64 hidden-pointer ABI correctly.
- **Optional runtime dependency**: libclang is only loaded when `--bind` is invoked. Projects that don't use `--bind` have zero additional dependencies.
- **Canonical type resolution**: The type mapper calls `clang_getCanonicalType` to resolve typedefs before mapping, so `size_t` correctly maps to `'int` regardless of the typedef chain.
