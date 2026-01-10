# FFI Design Proposals for OmniLisp

Based on deep investigation of state-of-the-art FFI designs across Rust, Zig, LuaJIT, Julia, Nim, OCaml, Common Lisp (CFFI), Janet, and Swift, here are 5 proposals ranked by developer ergonomics.

**Syntax Note:** All examples follow the OmniLisp Character Calculus:
- `{}` = Kind domain (types, blueprints)
- `[]` = Slot domain (parameters, bindings, fields)
- `()` = Flow domain (execution, constructors)
- `^` = Metadata prefix (followed by a symbol: `^:foo` and `^'foo` are equivalent)

## Current State

The existing `FFI_PROPOSAL.md` is architecturally solid with:
- Handle-based safety (generation counters, ABA protection)
- Ownership annotations (`^:owned`, `^:borrowed`, `^:consumed`)
- Region integration
- Weak callbacks

However, it requires **explicit declaration of every function and type**, which creates friction.

---

## Proposal 1: Julia-Style "Zero Boilerplate" Inline FFI

**Inspiration**: [Julia's ccall/@ccall](https://docs.julialang.org/en/v1/manual/calling-c-and-fortran-code/) - "no glue code, no code generation, even from the interactive prompt"

### Design

```lisp
;; One-liner call - no prior declaration needed
;; Syntax: (ccall lib fn [args...] {ReturnType})
(ccall libc strlen ["hello" {CString}] {CSize})

;; With explicit types on each argument
(ccall libm pow [2.0 {CDouble}] [10.0 {CDouble}] {CDouble})

;; Variadic function
(ccall libc printf ["Value: %d\n" {CString}] [42 {CInt}] {CInt})

;; Return handle (auto-wraps pointer)
(let [win (ccall sdl SDL_CreateWindow
            ["Test" {CString}]
            [100 {CInt}] [100 {CInt}]
            [800 {CInt}] [600 {CInt}]
            [0 {CUInt32}]
            {^:owned {Handle SDL_Window}})]
  (use-window win))
```

### Alternative: Macro Form

```lisp
;; @ccall macro with more concise syntax
;; Types inferred from Slot position
(@ccall libc/strlen "hello")              ; Returns CSize
(@ccall libm/pow 2.0 10.0)                ; Returns CDouble
(@ccall libc/printf "Value: %d\n" 42)     ; Returns CInt

;; Explicit return type when needed
(@ccall sdl/SDL_CreateWindow "Test" 100 100 800 600 0
  {^:owned (Handle SDL_Window)})
```

### Ergonomics Advantages

| Feature | Benefit |
|---------|---------|
| Inline types in Slots | Follows Character Calculus consistently |
| Library prefix | Clear provenance, lazy loading |
| REPL-friendly | Immediate experimentation |
| No setup required | Call any C function immediately |

### Implementation

1. `ccall` / `@ccall` macro expands to thunk generation at compile time
2. Library loaded lazily on first call (cached)
3. Type conversion generated based on `{Type}` annotations in Slots
4. Handles wrapped automatically when return type includes `Handle`

### Tradeoffs

| Pro | Con |
|-----|-----|
| Lowest friction for quick calls | Verbose for repeated calls |
| No setup required | Types repeated each call |
| REPL experimentation | No compile-time signature validation |

---

## Proposal 2: LuaJIT-Style "Declare Block + Instant Calls"

**Inspiration**: [LuaJIT FFI](https://luajit.org/ext_ffi.html) - "20x faster than pure Lua, calls can be inlined by JIT"

### Design

```lisp
;; C-like declaration block (parsed once, cached)
(ffi/cdef "
  typedef struct { float x, y; } Point;
  int printf(const char *fmt, ...);
  void *malloc(size_t size);
  void free(void *ptr);

  // SDL2
  typedef struct SDL_Window SDL_Window;
  SDL_Window* SDL_CreateWindow(const char*, int, int, int, int, uint32_t);
  void SDL_DestroyWindow(SDL_Window*);
")

;; Calls look like native functions - zero ceremony
(printf "Hello %s!\n" "world")

(let [win (SDL_CreateWindow "Demo" 100 100 800 600 0)]
  (SDL_DestroyWindow win))

;; Load library explicitly when needed
(ffi/load "libSDL2.so")
```

### Ctype Caching (Critical for Performance)

```lisp
;; BAD: Creates new ctype each call - don't do in loops!
(ffi/new "struct { int x; int y; }")

;; GOOD: Cache the ctype using define
(define Point (ffi/typeof "struct { int x; int y; }"))
(ffi/new Point)  ; Reuse cached type - JIT-friendly
```

### Ergonomics Advantages

| Feature | Benefit |
|---------|---------|
| C syntax in cdef | Copy-paste from headers |
| Functions callable directly | No wrapper overhead |
| Bulk declarations | Declare entire API at once |
| Familiar to C devs | Literal C type syntax |

### Implementation

1. `ffi/cdef` parses C declarations (using libclang or custom parser)
2. Generates OmniLisp thunks for each function
3. Caches ctypes for struct/union definitions
4. `ffi/typeof` returns cached ctype for repeated instantiation

### Tradeoffs

| Pro | Con |
|-----|-----|
| Bulk declaration | Requires C syntax parser |
| Copy-paste friendly | Two languages in one file |
| High performance | Parser maintenance burden |

---

## Proposal 3: Zig-Style "Direct Header Import"

**Inspiration**: [Zig's @cImport](https://ziglang.org/learn/overview/) - "seamless interop, designed from the ground up"

### Design

```lisp
;; Import C header directly - translated to OmniLisp types
(define sdl (c/import "<SDL2/SDL.h>"))

;; Use translated types and functions
(sdl.SDL_Init sdl.SDL_INIT_VIDEO)

(let [win (sdl.SDL_CreateWindow "Test" 100 100 800 600
                                sdl.SDL_WINDOW_SHOWN)]
  (sdl.SDL_DestroyWindow win))

;; Can also translate specific headers
(define stdio (c/import "<stdio.h>" ^:only [printf puts fopen fclose]))

;; With custom defines
(define mylib (c/import "mylib.h"
                ^:defines [DEBUG 1] [VERSION "2.0"]
                ^:include-paths ["/usr/local/include"]))
```

### translate-c Command

```bash
# Generate OmniLisp binding file from C header
omnilisp translate-c SDL2/SDL.h -o sdl2.omni
```

Produces:

```lisp
;; AUTO-GENERATED from SDL2/SDL.h
(module SDL2/FFI

  (define SDL_INIT_VIDEO 0x00000020)
  (define SDL_WINDOW_SHOWN 0x00000004)

  (define {extern SDL_Init ^'from "libSDL2.so"}
    [flags {CUInt32}] {CInt})

  (define {extern SDL_CreateWindow ^:from "libSDL2.so"}
    [title {CString}]
    [x {CInt}] [y {CInt}]
    [w {CInt}] [h {CInt}]
    [flags {CUInt32}]
    {^:owned {CPtr SDL_Window}})

  ...)
```

### Ergonomics Advantages

| Feature | Benefit |
|---------|---------|
| Zero manual typing | Headers are the source of truth |
| Auto-sync | Re-import when library updates |
| Platform-correct | Uses actual C compiler for types |
| Macro expansion | Translates simple C macros |

### Implementation

1. Use libclang to parse C headers
2. Generate AST of declarations
3. Convert to OmniLisp FFI declarations
4. Cache translation (rebuild on header change)

### Tradeoffs

| Pro | Con |
|-----|-----|
| No manual declarations | Requires libclang dependency |
| Always correct types | Complex macros may fail |
| Handles #ifdefs | Large headers = slow import |

---

## Proposal 4: OCaml ctypes-Style "Pure Lisp Declarative Bindings"

**Inspiration**: [OCaml ctypes](https://github.com/yallop/ocaml-ctypes) - "binding to C using pure OCaml, no stub functions to write"

### Design

```lisp
;; Define C types using combinators
(define int* (ffi/ptr ffi/int))
(define char** (ffi/ptr (ffi/ptr ffi/char)))

;; Structs via combinators
(define Point
  (ffi/struct 'Point
    [x ffi/float]
    [y ffi/float]))

;; Function signatures as data
(define strlen-sig (ffi/fn [ffi/string] ffi/size_t))
(define printf-sig (ffi/fn [ffi/string] ffi/int ^'variadic))

;; Bind to actual functions
(define strlen (ffi/foreign "strlen" strlen-sig ^:from "libc.so.6"))
(define printf (ffi/foreign "printf" printf-sig ^:from "libc.so.6"))

;; Now use them
(strlen "hello")  ; => 5
(printf "Value: %d\n" 42)
```

### Composability

```lisp
;; Higher-order type combinators
(define array-of [typ n]
  (ffi/array typ n))

(define nullable [typ]
  (ffi/view typ
    [read  (fn [ptr] (if (ffi/null? ptr) nil (ffi/read typ ptr)))]
    [write (fn [val] (if (nil? val) ffi/null (ffi/write typ val)))]))

;; Build complex types compositionally
(define OptionalStringArray
  (nullable (array-of ffi/string 10)))
```

### Stub Generation (Optional Compile-Time Safety)

```lisp
;; In build.omni
(ffi/generate-stubs
  [module 'mylib-bindings]
  [functions [strlen printf my-custom-fn]]
  [output "mylib_stubs.c"])
```

Generates C code that validates types at compile time:

```c
// mylib_stubs.c - AUTO-GENERATED
#include <string.h>
#include <stdio.h>

// Type-check: strlen signature matches
_Static_assert(sizeof(size_t) == sizeof(strlen("")), "strlen return mismatch");
```

### Ergonomics Advantages

| Feature | Benefit |
|---------|---------|
| Pure Lisp | No external tools or parsers |
| Composable | Build types programmatically |
| Type-safe | Signatures are first-class values |
| Stub generation | Optional compile-time validation |

### Implementation

1. Type combinators build description of C layout
2. `ffi/foreign` uses libffi for dynamic binding
3. Optional: generate C stubs for compile-time checking
4. Struct field offsets computed at bind time

### Tradeoffs

| Pro | Con |
|-----|-----|
| No external dependencies | More verbose than C syntax |
| Fully composable | Must manually declare everything |
| Pure Lisp | Learning curve for combinators |

---

## Proposal 5: Hybrid Multi-Modal FFI (Recommended)

**Inspiration**: Best practices from all languages - "right tool for the job"

### Design: Three Tiers

```
┌─────────────────────────────────────────────────────────────────┐
│  Tier 1: Quick Inline (ccall / @ccall)                          │
│  - One-off calls, REPL exploration, prototyping                 │
├─────────────────────────────────────────────────────────────────┤
│  Tier 2: Bulk Import (c/import)                                 │
│  - Large libraries, auto-generated bindings                     │
├─────────────────────────────────────────────────────────────────┤
│  Tier 3: Explicit Declaration (define {extern ...})             │
│  - Custom ownership, refined APIs, safety-critical code         │
└─────────────────────────────────────────────────────────────────┘
```

### Tier 1: Quick Inline (for exploration)

```lisp
;; Immediate call - no setup
(ccall libc puts ["Hello!" {CString}] {CInt})

;; REPL-friendly experimentation
> (ccall libm sin [3.14159 {CDouble}] {CDouble})
0.0000026535...

;; Shorthand macro form (types inferred where possible)
> (@ccall libc/puts "Hello!")
7
```

### Tier 2: Bulk Import (for large libraries)

```lisp
;; Import entire header
(define sdl (c/import "<SDL2/SDL.h>"))

;; Or use pre-translated module
(import SDL2)  ; From sdl2.omni generated by translate-c

;; All functions available with path notation
(sdl.SDL_Init sdl.SDL_INIT_VIDEO)
```

### Tier 3: Explicit Declaration (for production)

```lisp
;; Full control over ownership, handles, errors
(define {extern SDL_CreateWindow ^'from "libSDL2.so"}
  [title {CString}]
  [x {CInt}] [y {CInt}] [w {CInt}] [h {CInt}]
  [flags {CUInt32}]
  {^:owned (Handle SDL_Window)}
  ^'destructor SDL_DestroyWindow
  ^:nothing-on-null)

;; Now with full safety
(with-handle [win (SDL_CreateWindow "Safe" 0 0 800 600 0)]
  (render win))
;; Automatic cleanup via destructor
```

### Tier Interaction

```lisp
;; Start with Tier 1 for exploration
> (ccall sdl SDL_CreateWindow
    ["Test" {CString}]
    [0 {CInt}] [0 {CInt}] [800 {CInt}] [600 {CInt}] [0 {CUInt32}]
    {CPtr})
#<CPtr 0x7f8a...>

;; Graduate to Tier 2 for development
(define sdl (c/import "<SDL2/SDL.h>"))
(let [win (sdl.SDL_CreateWindow "Dev" 0 0 800 600 0)]
  ...)

;; Finalize with Tier 3 for production
(import SDL2/Safe)  ; Hand-crafted bindings with ownership
(with-window "Prod" 800 600
  (fn [win] ...))
```

### Command-Line Tools

```bash
# Generate Tier 2 bindings from header
omnilisp ffi translate SDL2/SDL.h -o lib/sdl2.omni

# Generate Tier 3 skeleton from Tier 2
omnilisp ffi harden lib/sdl2.omni -o lib/sdl2-safe.omni

# Validate FFI declarations against headers
omnilisp ffi check lib/sdl2-safe.omni
```

### Ergonomics Summary

| Use Case | Tier | Syntax |
|----------|------|--------|
| Quick test | 1 | `(ccall lib fn [args...] {RetType})` |
| Wrap library | 2 | `(c/import "header.h")` |
| Production API | 3 | `(define {extern fn} ...)` |

---

## Complete Syntax Reference

### C Type Mappings

| OmniLisp Kind | C Type | Size | Notes |
|---------------|--------|------|-------|
| `{CInt}` | `int` | platform | Signed integer |
| `{CInt8}` | `int8_t` | 1 | |
| `{CInt16}` | `int16_t` | 2 | |
| `{CInt32}` | `int32_t` | 4 | |
| `{CInt64}` | `int64_t` | 8 | |
| `{CUInt}` | `unsigned int` | platform | |
| `{CUInt8}` | `uint8_t` | 1 | |
| `{CUInt16}` | `uint16_t` | 2 | |
| `{CUInt32}` | `uint32_t` | 4 | |
| `{CUInt64}` | `uint64_t` | 8 | |
| `{CFloat}` | `float` | 4 | |
| `{CDouble}` | `double` | 8 | |
| `{CBool}` | `_Bool` | 1 | |
| `{CChar}` | `char` | 1 | |
| `{CSize}` | `size_t` | platform | |
| `{CPtr}` | `void*` | platform | Opaque pointer |
| `{CString}` | `char*` | platform | Null-terminated |

### Derived Types

```lisp
;; Typed pointer
{CPtr CInt}              ; int*
{CPtr {CPtr CChar}}      ; char**

;; Fixed array
{CArray CInt 10}         ; int[10]
{CArray CChar 256}       ; char[256]

;; Function pointer (using fn Kind constructor)
{(fn [{CInt} {CInt}] {CInt})}  ; int (*)(int, int)

;; Const qualifier
{CConst CString}         ; const char*
{CConst {CPtr CInt}}     ; const int*
```

### Handle Types (Safe Wrappers)

```lisp
;; External handle (64-bit: index + generation)
{Handle T}               ; Safe wrapper for T*
{Handle SDL_Window}      ; Safe wrapper for SDL_Window*

;; Weak handle for callbacks
{WeakHandle T}           ; Invalidates when target freed

;; Nullable handle (wraps C NULL as None)
{Option {Handle T}}      ; May be None (from C NULL)
```

### Ownership Metadata

```lisp
;; Return type annotations
{^:owned {CPtr}}         ; Caller receives ownership, must free
{^:borrowed {CPtr}}      ; Callee borrows temporarily (default for params)

;; Parameter annotations
[^:consumed ptr {CPtr}]  ; Callee takes ownership, don't use after call
[^:escapes cb {Handle}]  ; Callee may store reference, keep alive
```

### Function Declarations (Tier 3)

```lisp
;; Basic extern declaration
(define ^:from libc {extern puts}
  [s {CString}]
  {CInt})

;; With ownership annotations
(define ^:from libc {extern malloc}
  [size {CSize}]
  {^:owned {CPtr}})

(define ^:from libc {extern free}
  [^:consumed ptr {CPtr}]
  {Nothing})

;; Variadic function
(define ^:from libc ^:variadic {extern printf}
  [format {CString}]
  {CInt})

;; With error handling
(define ^:from libc ^:may-fail {extern open}
  [path {CString}]
  [flags {CInt}]
  {(Result {CInt} {CInt})})  ; Ok(fd) or Err(errno)

;; NULL-returning function
(define ^:from libc ^:nothing-on-null {extern fopen}
  [path {CString}]
  [mode {CString}]
  {Option {Handle FILE}})
```

### Struct Definitions

```lisp
;; Basic C struct
(define {struct ^:ffi Point}
  [x {CFloat}]
  [y {CFloat}])

;; Packed struct (no padding)
(define ^:packed {struct ^:ffi PacketHeader}
  [magic {CUInt32}]
  [length {CUInt16}]
  [flags {CUInt8}]
  [checksum {CUInt8}])

;; With explicit alignment
(define ^:align-16 {struct ^:ffi AlignedData}
  [data {CArray CFloat 4}])

;; Opaque type (size unknown, always pointer)
(define {opaque FILE})
(define {opaque SDL_Window ^:destructor SDL_DestroyWindow})
```

### Callback Definitions

```lisp
;; Define callback type using CFn (C Function pointer)
(define {CFn Comparator}
  [a {CPtr}]
  [b {CPtr}]
  {CInt})

;; Function taking callback
(define ^:from libc {extern qsort}
  [base {CPtr}]
  [nmemb {CSize}]
  [size {CSize}]
  [compar {CFn Comparator}]
  {Nothing})

;; Pass OmniLisp function as callback
(qsort arr n (sizeof CInt)
  (fn [a {CPtr}] [b {CPtr}] {CInt}
    (- (deref {CPtr CInt} a)
       (deref {CPtr CInt} b))))
```

### Memory Management

```lisp
;; Allocate with handle
(let [h (ffi/alloc {Point})]        ; Returns Handle<Point>
  (set! (deref h).x 10.0)
  (set! (deref h).y 20.0)
  (use-point h)
  (ffi/free h))                      ; Explicit free

;; Automatic cleanup with with-ffi
(with-ffi [h (ffi/alloc {Point})]
  (set! (deref h).x 10.0)
  (process h))
;; h automatically freed here

;; Region-based allocation
(with-ffi-region [r (ffi/linear-region 4096)]
  (let [buf (ffi/region-alloc r 1024)]
    (read fd buf 1024)
    (process-buffer buf)))
;; Region freed, all allocations invalid
```

---

## Complete Example: SDL2 Bindings

### Tier 3: Production-Ready

```lisp
(module SDL2

  ;; Constants
  (define SDL_INIT_VIDEO 0x00000020)
  (define SDL_WINDOW_SHOWN 0x00000004)

  ;; Opaque types with destructors
  (define {opaque SDL_Window ^'destructor SDL_DestroyWindow})
  (define {opaque SDL_Renderer ^:destructor SDL_DestroyRenderer})
  (define {opaque SDL_Surface ^'destructor SDL_FreeSurface})

  ;; Structs
  (define {struct ^'ffi SDL_Rect}
    [x {CInt}] [y {CInt}]
    [w {CInt}] [h {CInt}])

  (define {struct ^:ffi SDL_Color}
    [r {CUInt8}] [g {CUInt8}]
    [b {CUInt8}] [a {CUInt8}])

  ;; Functions
  (define ^:from "libSDL2.so" {extern SDL_Init}
    [flags {CUInt32}]
    {CInt})

  (define ^:from "libSDL2.so" {extern SDL_Quit}
    {Nothing})

  (define ^:from "libSDL2.so" {extern SDL_CreateWindow}
    [title {CString}]
    [x {CInt}] [y {CInt}]
    [w {CInt}] [h {CInt}]
    [flags {CUInt32}]
    {^:owned {Handle SDL_Window}})

  (define ^:from "libSDL2.so" {extern SDL_CreateRenderer}
    [window {Handle SDL_Window}]
    [index {CInt}]
    [flags {CUInt32}]
    {^:owned {Handle SDL_Renderer}})

  (define ^:from "libSDL2.so" {extern SDL_SetRenderDrawColor}
    [renderer {Handle SDL_Renderer}]
    [r {CUInt8}] [g {CUInt8}]
    [b {CUInt8}] [a {CUInt8}]
    {CInt})

  (define ^:from "libSDL2.so" {extern SDL_RenderClear}
    [renderer {Handle SDL_Renderer}]
    {CInt})

  (define ^:from "libSDL2.so" {extern SDL_RenderPresent}
    [renderer {Handle SDL_Renderer}]
    {Nothing})

  ;; High-level API with automatic cleanup
  (define with-sdl [body {(fn [] {Any})}]
    (SDL_Init SDL_INIT_VIDEO)
    (try
      (body)
      (finally (SDL_Quit))))

  (define with-window [title {String}] [w {Int}] [h {Int}] [body {(fn [{Handle SDL_Window}] {Any})}]
    (let [win (SDL_CreateWindow title 100 100 w h SDL_WINDOW_SHOWN)]
      (try
        (body win)
        (finally nil))))  ; Handle auto-freed via destructor

  (define with-renderer [win {Handle SDL_Window}] [body {(fn [{Handle SDL_Renderer}] {Any})}]
    (let [ren (SDL_CreateRenderer win -1 0)]
      (try
        (body ren)
        (finally nil))))  ; Handle auto-freed via destructor

  (export with-sdl with-window with-renderer
          SDL_Rect SDL_Color
          SDL_SetRenderDrawColor SDL_RenderClear SDL_RenderPresent))
```

### Usage

```lisp
(import SDL2)

(with-sdl
  (fn []
    (with-window "Hello SDL" 800 600
      (fn [win]
        (with-renderer win
          (fn [ren]
            (SDL_SetRenderDrawColor ren 64 128 255 255)
            (SDL_RenderClear ren)
            (SDL_RenderPresent ren)
            (sleep 2000)))))))
```

---

## Comparison Matrix

| Aspect | P1: Julia | P2: LuaJIT | P3: Zig | P4: ctypes | P5: Hybrid |
|--------|-----------|------------|---------|------------|------------|
| **Setup friction** | ★★★★★ | ★★★★☆ | ★★★★★ | ★★★☆☆ | ★★★★★ |
| **Large library support** | ★★☆☆☆ | ★★★★☆ | ★★★★★ | ★★★☆☆ | ★★★★★ |
| **Type safety** | ★★☆☆☆ | ★★★☆☆ | ★★★★☆ | ★★★★★ | ★★★★★ |
| **Ownership control** | ★★★☆☆ | ★★☆☆☆ | ★★☆☆☆ | ★★★★☆ | ★★★★★ |
| **REPL ergonomics** | ★★★★★ | ★★★☆☆ | ★★☆☆☆ | ★★★☆☆ | ★★★★★ |
| **C programmer familiarity** | ★★★☆☆ | ★★★★★ | ★★★★★ | ★★☆☆☆ | ★★★★☆ |
| **Implementation complexity** | ★★★☆☆ | ★★★★☆ | ★★★★★ | ★★☆☆☆ | ★★★★★ |
| **Fits CTRR model** | ★★★☆☆ | ★★☆☆☆ | ★★★☆☆ | ★★★★☆ | ★★★★★ |

---

## Recommendation

**Proposal 5 (Hybrid)** provides the best developer experience because:

1. **Progressive disclosure**: Start simple, add complexity only when needed
2. **Right tool for job**: Quick hacks vs. production code have different needs
3. **Preserves existing design**: Tier 3 is essentially your current FFI_PROPOSAL.md
4. **REPL-first development**: Tier 1 enables rapid experimentation
5. **Scalability**: Tier 2 handles large C libraries without manual work
6. **Character Calculus consistent**: All tiers use `{}`, `[]`, `()` correctly

### Implementation Priority

1. **Tier 3** (already designed) - Implement first for foundation
2. **Tier 1** (`ccall` / `@ccall`) - Low effort, high ergonomic value
3. **Tier 2** (`c/import`) - Requires libclang, implement last

---

## Appendix A: Language-Specific Research Notes

### Julia ccall/@ccall

- Built-in, no boilerplate philosophy
- JIT generates same machine code as native C call
- `@ccall` macro provides cleaner syntax than `ccall` function
- Supports `gcsafe=true` for concurrent GC during blocking calls
- Type annotations inline with arguments

**Reference**: [Julia C Interface Docs](https://docs.julialang.org/en/v1/manual/calling-c-and-fortran-code/)

### LuaJIT FFI

- 20x faster than pure Lua for FFI-heavy code
- Calls can be inlined by JIT compiler
- `ffi.cdef` parses C declarations directly
- Critical: use `ffi.typeof()` to cache ctypes for performance
- Callbacks from C to Lua are slower than Lua to C

**Reference**: [LuaJIT FFI Library](https://luajit.org/ext_ffi.html)

### Zig @cImport / translate-c

- Designed from ground up for C interop
- `@cImport` translates C to Zig under the hood
- `translate-c` command for offline translation
- Handles platform-specific types via `c_` prefixed types
- Some C features (bitfields) may become opaque types

**Reference**: [Zig C Interop Guide](https://ziglang.org/learn/overview/)

### Rust bindgen/cbindgen

- bindgen: C/C++ → Rust (uses libclang)
- cbindgen: Rust → C/C++ headers
- cxx crate: generates both sides from common schema
- Minimum supported Rust version: 1.70.0
- C++ support limited (no automatic RAII, must call destructors manually)

**Reference**: [bindgen User Guide](https://rust-lang.github.io/rust-bindgen/)

### OCaml ctypes

- Pure OCaml type combinators for C types
- No C stub code required
- Two modes: libffi dynamic binding, or stub generation
- Stub generation provides compile-time type checking
- Academic paper: "A Modular Foreign Function Interface"

**Reference**: [OCaml ctypes GitHub](https://github.com/yallop/ocaml-ctypes)

### Nim importc / Futhark

- `{.importc.}` pragma imports C symbols
- `{.header: "x.h".}` specifies include
- `{.dynlib: "lib.so".}` for runtime linking
- Futhark: automatic wrapping using libclang
- Nimterop: alternative auto-wrapper

**Reference**: [Nim FFI Manual](https://nim-docs.readthedocs.io/en/latest/manual/pragmas/ffi/pragma_importc/)

### Common Lisp CFFI

- Portable across CL implementations
- CFFI-SYS: low-level backend per implementation
- CFFI: high-level declarative frontend
- c2ffi: auto-generate bindings from headers via Clang JSON
- UFFI deprecated, CFFI is replacement

**Reference**: [CFFI Documentation](https://cffi.common-lisp.dev)

### Janet FFI

- Available since version 1.23.0 (x86-64, non-Windows)
- Hybrid approach: FFI for convenience, C API for performance
- Abstract types for custom C objects
- Macro-based abstraction over low-level primitives

**Reference**: [Janet FFI Docs](https://janet-lang.org/docs/ffi.html)

### Swift ClangImporter

- Uses Clang to parse C/C++ headers
- `<swift/bridging>` header for custom annotations
- Automatic header generation for Swift → C++
- Bridging headers for app targets
- PCH support for faster builds

**Reference**: [Swift C++ Interop](https://www.swift.org/documentation/cxx-interop/)

---

## Appendix B: Key Design Decisions

### Why Handle-Based (Not Raw Pointers)

1. **Generation counters** catch use-after-free at runtime
2. **Type safety** - handles are typed, casts explicit
3. **Deterministic mode** enables replay debugging
4. **Fits CTRR** - compile-time region scheduling still works

### Why Ownership Annotations

1. **Explicit is better** - no guessing who frees what
2. **Enables static analysis** - compiler can warn on leaks
3. **Documentation** - signature tells the story
4. **Fits CTRR** - informs region/escape scheduling decisions

### Why Multiple Tiers

1. **Different needs** - REPL vs production have different priorities
2. **Progressive refinement** - explore → develop → harden
3. **No one-size-fits-all** - each tier optimizes for its use case
4. **Interoperability** - tiers can call each other

---

## Sources

- [Julia C Interface](https://docs.julialang.org/en/v1/manual/calling-c-and-fortran-code/)
- [LuaJIT FFI](https://luajit.org/ext_ffi.html) / [FFI Tutorial](https://luajit.org/ext_ffi_tutorial.html)
- [Zig C Interop](https://ziglang.org/learn/overview/) / [translate-c](https://zig.guide/working-with-c/translate-c/)
- [Rust bindgen](https://github.com/rust-lang/rust-bindgen) / [User Guide](https://rust-lang.github.io/rust-bindgen/)
- [OCaml ctypes](https://github.com/yallop/ocaml-ctypes) / [Real World OCaml FFI](https://dev.realworldocaml.org/foreign-function-interface.html)
- [Nim importc](https://nim-docs.readthedocs.io/en/latest/manual/pragmas/ffi/pragma_importc/) / [Futhark](https://github.com/PMunch/futhark)
- [Common Lisp CFFI](https://cffi.common-lisp.dev) / [CL Cookbook FFI](https://lispcookbook.github.io/cl-cookbook/ffi.html)
- [Janet FFI](https://janet-lang.org/docs/ffi.html)
- [Swift C++ Interop](https://www.swift.org/documentation/cxx-interop/)
- [Inko FFI Challenges](https://inko-lang.org/news/the-challenge-of-building-a-foreign-function-interface/)
- [FFI Practical Guide](https://swenotes.com/2025/09/25/foreign-function-interfaces-ffi-a-practical-guide-for-software-teams/)
