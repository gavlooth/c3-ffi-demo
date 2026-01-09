# Omnilisp FFI Proposal

A comprehensive Foreign Function Interface design for Omnilisp, leveraging the C runtime's handle-based memory infrastructure for safety and determinism.

---

## Design Principles

1. **No Raw Pointers** - All foreign pointers wrapped in handles with generation counters
2. **Explicit Ownership** - Ownership transfer is always explicit in the type signature
3. **Region-Aware** - FFI buffers can use the IRegion infrastructure
4. **Safe Callbacks** - Callbacks use WeakHandles that invalidate when closures are freed
5. **Deterministic Mode** - Optional sequential handle allocation for replay/debugging
6. **Fits Omnilisp Syntax** - Uses `{}` for types, `[]` for specs, metadata for annotations

---

## 1. Library Loading

### 1.1 Import Syntax

```lisp
;; Load shared library
(import {ffi "libfoo.so"})

;; With namespace alias
(import {ffi "libSDL2.so" ^:as sdl})

;; System library with version
(import {ffi "libc.so.6" ^:as libc})

;; Platform-specific loading
(import {ffi ^:darwin "libfoo.dylib"
             ^:linux  "libfoo.so"
             ^:windows "foo.dll"
        ^:as foo})
```

### 1.2 Inline Loading

```lisp
;; Load in expression context
(let [lib (ffi/load "libcrypto.so")]
  (ffi/call lib "SHA256" data len out))
```

---

## 2. C Type System

### 2.1 Primitive Types

| Omnilisp Type | C Type | Size | Notes |
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
| `{CSSize}` | `ssize_t` | platform | |
| `{CPtr}` | `void*` | platform | Opaque pointer |
| `{CString}` | `char*` | platform | Null-terminated |

### 2.2 Derived Types

```lisp
;; Typed pointer
{CPtr CInt}              ; int*
{CPtr {CPtr CChar}}      ; char**

;; Fixed array
{CArray CInt 10}         ; int[10]
{CArray CChar 256}       ; char[256]

;; Function pointer
{CFn [CInt CInt] CInt}   ; int (*)(int, int)

;; Const qualifier
{CConst CString}         ; const char*
{CConst {CPtr CInt}}     ; const int*
```

### 2.3 Handle Types (Safe Wrappers)

```lisp
;; External handle (64-bit: index + generation)
{Handle T}               ; Safe wrapper for T*
{Handle Window}          ; Safe wrapper for Window*

;; Weak handle for callbacks
{WeakHandle T}           ; Invalidates when target freed

;; Nullable handle (wraps C NULL as None)
{Option {Handle T}}      ; May be None (from C NULL)
```

---

## 3. Function Declarations

### 3.1 Basic Declaration

```lisp
;; Declare foreign function
(define {extern puts ^:from libc}
  [s {CString}]
  {CInt})

;; Multiple parameters
(define {extern fwrite ^:from libc}
  [ptr {CPtr}]
  [size {CSize}]
  [nmemb {CSize}]
  [stream {Handle FILE}]
  {CSize})

;; Variadic function
(define {extern printf ^:from libc ^:variadic}
  [format {CString}]
  [.. args]
  {CInt})
```

### 3.2 Ownership Annotations

Ownership is specified via metadata on parameters and return type:

```lisp
;; Caller owns returned pointer (must free)
(define {extern malloc ^:from libc}
  [size {CSize}]
  {^:owned CPtr})

;; Function consumes (takes ownership of) parameter
(define {extern free ^:from libc}
  [^:consumed ptr {CPtr}]
  {Nothing})

;; Function borrows parameter (doesn't affect ownership)
(define {extern strlen ^:from libc}
  [^:borrowed s {CString}]
  {CSize})

;; Function may store reference (escapes)
(define {extern set_callback}
  [^:escapes cb {Handle Callback}]
  -> {Nothing})
```

**Ownership Classes:**

| Annotation | Meaning | Caller Responsibility |
|------------|---------|----------------------|
| `^:owned` | Caller receives ownership | Must free/release |
| `^:borrowed` | Callee borrows temporarily | None (default for params) |
| `^:consumed` | Callee takes ownership | Don't use after call |
| `^:escapes` | Callee may store reference | Keep alive until released |

### 3.3 Error Handling

```lisp
;; Function that may fail (returns -1 on error)
(define {extern open ^:from libc ^:may-fail}
  [path {CString}]
  [flags {CInt}]
  {(Result CInt CInt)})  ; Ok(fd) or Err(errno)

;; C NULL-returning function (returns Nothing on NULL)
(define {extern fopen ^:from libc ^:nothing-on-error}
  [path {CString}]
  [mode {CString}]
  {Option {Handle FILE}})

;; Custom error check
(define {extern custom_api ^:from libfoo}
  [x {CInt}]
  {CInt}
  ^:error-when (fn [r] (< r 0))
  ^:error-code errno)
```

---

## 4. Struct Definitions

### 4.1 C-Compatible Structs

```lisp
;; Basic C struct
(define {struct ^:ffi Point}
  [x {CFloat}]
  [y {CFloat}])

;; Packed struct (no padding)
(define {struct ^:ffi ^:packed PacketHeader}
  [magic {CUInt32}]
  [length {CUInt16}]
  [flags {CUInt8}]
  [checksum {CUInt8}])

;; With explicit alignment
(define {struct ^:ffi ^:align-16 AlignedData}
  [data {CArray CFloat 4}])

;; Nested struct
(define {struct :ffi Rect}
  [origin {Point}]
  [size {Point}])
```

### 4.2 Opaque Types

```lisp
;; Opaque handle (size unknown, always pointer)
(define {opaque FILE})
(define {opaque SDL_Window})
(define {opaque sqlite3})

;; With destructor (for automatic cleanup)
(define {opaque SDL_Surface
  ^:destructor SDL_FreeSurface})

;; With custom release
(define {opaque sqlite3_stmt
  ^:destructor (fn [stmt] (sqlite3_finalize stmt))})
```

### 4.3 Unions

```lisp
(define {union ^:ffi Value}
  [i {CInt64}]
  [f {CDouble}]
  [p {CPtr}])
```

---

## 5. Calling Foreign Functions

### 5.1 Direct Calls

```lisp
;; After declaring extern, call directly
(puts "Hello, World!")

;; With explicit library prefix
(libc/printf "Value: %d\n" 42)

;; Call returns Omnilisp-wrapped value
(let [len (strlen "hello")]
  (print "Length: $len"))
```

### 5.2 Dynamic Calls

```lisp
;; Call by name at runtime
(ffi/call libc "printf" "Value: %d\n" 42)

;; Get function pointer
(let [fn (ffi/symbol libc "printf")]
  (ffi/invoke fn {CFn [CString ..] CInt} "test\n"))
```

### 5.3 Error Handling

```lisp
;; Functions marked :may-fail return Result
(match (open "/etc/passwd" O_RDONLY)
  [(Ok fd)
   (let [buf (make-buffer 1024)]
     (read fd buf 1024)
     (close fd))]
  [(Err errno)
   (error "Failed to open: $(strerror errno)")])

;; Or use try! for propagation
(define (read-file path)
  (let [fd (try! (open path O_RDONLY))]
    (finally-do
      (close fd)
      (read-all fd))))
```

---

## 6. Memory Management

### 6.1 Handle-Based API

All foreign pointers are wrapped in handles for safety:

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
```

### 6.2 Region-Based Allocation

Using the IRegion infrastructure:

```lisp
;; Linear region for fixed buffer
(with-ffi-region [r (ffi/linear-region 4096)]
  (let [buf (ffi/region-alloc r 1024)]
    (read fd buf 1024)
    (process-buffer buf)))
;; Region freed, all allocations invalid

;; Arena for temporary work
(with-ffi-arena [a 8192]
  (let [tmp1 (ffi/arena-alloc a {CArray CChar 256})
        tmp2 (ffi/arena-alloc a {CArray CChar 256})]
    (sprintf tmp1 "Hello")
    (sprintf tmp2 "World")
    (strcat tmp1 tmp2)
    (puts tmp1)))
;; Arena destroyed, tmp1/tmp2 invalid

;; Offset region for serialization
(with-ffi-region [r (ffi/offset-region 4096)]
  (let [data (serialize-to-region r my-struct)]
    ;; Write region as contiguous buffer
    (write fd (ffi/region-base r) (ffi/region-used r))))
```

### 6.3 Buffer Operations

```lisp
;; Create buffer from Omnilisp data
(let [buf (ffi/to-buffer my-string)]  ; Copies to C buffer
  (ffi/call lib "process" buf (ffi/buffer-size buf))
  (ffi/free-buffer buf))

;; Zero-copy view (careful with lifetime!)
(ffi/with-buffer-view my-array
  (-> ptr len
    (ffi/call lib "read_only_process" ptr len)))

;; Copy from C buffer to Omnilisp
(let [result (ffi/from-buffer ptr len {String})]
  result)
```

---

## 7. Callbacks

### 7.1 Callback Type Declaration

```lisp
;; Define callback type
(define {callback Comparator}
  [a {CPtr}]
  [b {CPtr}]
  -> {CInt})

(define {callback EventHandler}
  [event {Handle Event}]
  [user_data {Handle}]
  -> {Nothing})
```

### 7.2 Passing Callbacks

```lisp
;; Declare function taking callback
(define {extern qsort :from libc}
  [base {CPtr}]
  [nmemb {CSize}]
  [size {CSize}]
  [compar {Callback Comparator}]
  -> {Nothing})

;; Pass Omnilisp function as callback
(qsort arr n (sizeof CInt)
  (-> (a b)
    (- (deref {CPtr CInt} a)
       (deref {CPtr CInt} b))))
```

### 7.3 Long-Lived Callbacks

```lisp
;; Register callback that outlives the call
(define {extern set_event_handler}
  [^:escapes handler {Callback EventHandler}]
  [^:escapes user_data {Handle}]
  -> {Nothing})

;; Must use explicit handle management
(let [my-data (to-handle {:count 0})]
  ;; Register - callback must be kept alive
  (let [cb-handle (ffi/register-callback
                    (-> (event user)
                      (let [data (from-handle user)]
                        (set! data.count (+ data.count 1)))))]
    (set_event_handler cb-handle my-data)

    ;; ... later, when done:
    (unset_event_handler)
    (ffi/unregister-callback cb-handle)
    (release-handle my-data)))
```

### 7.4 Weak Callbacks (Safe)

```lisp
;; Callback becomes invalid when Omnilisp object freed
(let [obj (create-handler)]
  (let [weak-cb (ffi/weak-callback obj process-method)]
    (register_callback weak-cb)
    ;; If obj is freed, callback safely returns default/error
    ))
```

---

## 8. Handle System Details

### 8.1 Creating Handles

```lisp
;; Wrap Omnilisp object for FFI
(define obj (MyStruct 1 2 3))
(let [h (to-handle obj)]
  (ffi/store-handle h)    ; C code stores handle
  ...)

;; Explicit type annotation
(let [h (to-handle {Handle MyStruct} obj)]
  ...)
```

### 8.2 Using Handles

```lisp
;; Retrieve object from handle
(define (ffi-callback [h {Handle MyStruct}])
  (match (from-handle h)
    [(Some obj) (process obj)]
    [None (error "Handle invalid")]))

;; Check validity
(when (handle-valid? h)
  (process (from-handle! h)))  ; ! = assert valid

;; Dereference foreign handle
(let [win (SDL_CreateWindow "Test" 0 0 800 600 0)]
  (SDL_SetWindowTitle win "New Title")  ; Implicit deref
  (SDL_DestroyWindow win))
```

### 8.3 Handle Lifecycle

```lisp
;; Manual lifecycle
(let [h (to-handle obj)]
  (use h)
  (release-handle h))  ; Explicit release

;; Automatic with scope
(with-handle [h obj]
  (use h))
;; h released here

;; Transfer ownership to C
(let [h (to-handle obj :transfer)]
  (ffi/give-ownership lib h))
;; obj ownership transferred, don't use h
```

### 8.4 Deterministic Mode

For replay and debugging:

```lisp
;; Enable deterministic handle allocation
(ffi/set-deterministic true)

;; All handles now have sequential IDs
(let [h1 (to-handle a)    ; ID = 1
      h2 (to-handle b)    ; ID = 2
      h3 (to-handle c)]   ; ID = 3
  ;; Reproducible across runs
  (assert (= (handle-id h1) 1))
  (assert (= (handle-id h2) 2))
  (assert (= (handle-id h3) 3)))
```

---

## 9. Type Conversions

### 9.1 Automatic Conversions

| Omnilisp | C | Conversion |
|----------|---|------------|
| `Int` | `int`, `int64_t` | Direct |
| `Float` | `double` | Direct |
| `Bool` | `_Bool`, `int` | `true`→1, `false`→0 |
| `String` | `char*` | Copy to null-terminated |
| `{Array T}` | `T*` + length | Pointer to data |
| `{Handle T}` | `uint64_t` | Handle value |
| `Nothing` | `void` | Ignored |

### 9.2 Explicit Conversions

```lisp
;; Cast between pointer types
(let [int-ptr (ffi/cast {CPtr CInt} void-ptr)]
  (deref int-ptr))

;; Convert handle to raw ID (for C storage)
(let [id (handle-to-id h)]
  (ffi/store-id id))

;; Reconstruct handle from ID
(let [h (id-to-handle id {MyType})]
  (from-handle h))
```

---

## 10. Complete Examples

### 10.1 SDL2 Bindings

```lisp
(module SDL2
  (import {ffi "libSDL2.so" :as sdl})

  ;; Constants
  (define SDL_INIT_VIDEO 0x00000020)
  (define SDL_WINDOW_SHOWN 0x00000004)

  ;; Opaque types
  (define {opaque Window :destructor sdl/SDL_DestroyWindow})
  (define {opaque Renderer :destructor sdl/SDL_DestroyRenderer})
  (define {opaque Surface :destructor sdl/SDL_FreeSurface})

  ;; Structs
  (define {struct :ffi Rect}
    [x {CInt}] [y {CInt}]
    [w {CInt}] [h {CInt}])

  (define {struct :ffi Color}
    [r {CUInt8}] [g {CUInt8}]
    [b {CUInt8}] [a {CUInt8}])

  ;; Functions
  (define {extern sdl/SDL_Init}
    [flags {CUInt32}]
    -> {CInt})

  (define {extern sdl/SDL_Quit}
    -> {Nothing})

  (define {extern sdl/SDL_CreateWindow}
    [title {CString}]
    [x {CInt}] [y {CInt}]
    [w {CInt}] [h {CInt}]
    [flags {CUInt32}]
    -> {^:owned Handle Window})

  (define {extern sdl/SDL_CreateRenderer}
    [window {Handle Window}]
    [index {CInt}]
    [flags {CUInt32}]
    -> {^:owned Handle Renderer})

  (define {extern sdl/SDL_SetRenderDrawColor}
    [renderer {Handle Renderer}]
    [r {CUInt8}] [g {CUInt8}]
    [b {CUInt8}] [a {CUInt8}]
    -> {CInt})

  (define {extern sdl/SDL_RenderClear}
    [renderer {Handle Renderer}]
    -> {CInt})

  (define {extern sdl/SDL_RenderPresent}
    [renderer {Handle Renderer}]
    -> {Nothing})

  ;; High-level API
  (define (with-sdl body)
    (SDL_Init SDL_INIT_VIDEO)
    (try
      (body)
      (finally (SDL_Quit))))

  (define (with-window title w h body)
    (let [win (SDL_CreateWindow title 100 100 w h SDL_WINDOW_SHOWN)]
      (try
        (body win)
        (finally (SDL_DestroyWindow win)))))

  (define (with-renderer win body)
    (let [ren (SDL_CreateRenderer win -1 0)]
      (try
        (body ren)
        (finally (SDL_DestroyRenderer ren)))))

  (export with-sdl with-window with-renderer
          Rect Color
          SDL_SetRenderDrawColor SDL_RenderClear SDL_RenderPresent))
```

**Usage:**

```lisp
(import SDL2)

(with-sdl
  (with-window "Hello SDL" 800 600
    (-> win
      (with-renderer win
        (-> ren
          (SDL_SetRenderDrawColor ren 64 128 255 255)
          (SDL_RenderClear ren)
          (SDL_RenderPresent ren)
          (sleep 2000))))))
```

### 10.2 SQLite Bindings

```lisp
(module SQLite
  (import {ffi "libsqlite3.so" :as sql})

  (define SQLITE_OK 0)
  (define SQLITE_ROW 100)
  (define SQLITE_DONE 101)

  (define {opaque Database})
  (define {opaque Statement})

  (define {extern sql/sqlite3_open :may-fail}
    [filename {CString}]
    [^:owned db-out {CPtr {Handle Database}}]
    -> {Result {Handle Database} CInt})

  (define {extern sql/sqlite3_close}
    [^:consumed db {Handle Database}]
    -> {CInt})

  (define {extern sql/sqlite3_prepare_v2}
    [db {Handle Database}]
    [sql {CString}]
    [nbyte {CInt}]
    [^:owned stmt-out {CPtr {Handle Statement}}]
    [tail {CPtr CString}]
    -> {CInt})

  (define {extern sql/sqlite3_step}
    [stmt {Handle Statement}]
    -> {CInt})

  (define {extern sql/sqlite3_column_text}
    [stmt {Handle Statement}]
    [col {CInt}]
    -> {CString})

  (define {extern sql/sqlite3_finalize}
    [^:consumed stmt {Handle Statement}]
    -> {CInt})

  ;; High-level API
  (define (with-db path body)
    (match (sqlite3_open path)
      [(Ok db)
       (try (body db)
         (finally (sqlite3_close db)))]
      [(Err code)
       (error "Failed to open database: $code")]))

  (define (query db sql-str)
    (with-ffi [stmt-ptr (ffi/alloc {CPtr})]
      (let [rc (sqlite3_prepare_v2 db sql-str -1 stmt-ptr c-null)]
        (when (!= rc SQLITE_OK)
          (error "Prepare failed: $rc"))
        (let [stmt (deref stmt-ptr)
              results []]
          (let loop []
            (match (sqlite3_step stmt)
              [SQLITE_ROW
               (push! results (get-row stmt))
               (loop)]
              [SQLITE_DONE
               (sqlite3_finalize stmt)
               results]
              [code
               (sqlite3_finalize stmt)
               (error "Step failed: $code")]))))))

  (export with-db query))
```

### 10.3 Callback Example

```lisp
(module SignalHandler
  (import {ffi "libc.so.6" :as libc})

  (define {callback SigHandler}
    [signum {CInt}]
    -> {Nothing})

  (define {extern libc/signal}
    [signum {CInt}]
    [^:escapes handler {Callback SigHandler}]
    -> {Callback SigHandler})

  (define SIGINT 2)
  (define SIGTERM 15)

  ;; Mutable state for signal handling
  (define *caught-signals* (atom []))

  (define (setup-signal-handler sig)
    (let [cb (ffi/register-callback
               (-> (signum)
                 (swap! *caught-signals* (-> s (push s signum)))))]
      (signal sig cb)
      cb))

  (define (teardown-signal-handler sig cb)
    (signal sig SIG_DFL)
    (ffi/unregister-callback cb))

  (export setup-signal-handler teardown-signal-handler
          SIGINT SIGTERM *caught-signals*))
```

---

## 11. Safety Guarantees

### 11.1 What the FFI Prevents

1. **Use-After-Free** - Handles have generation counters; stale handles return `None`
2. **Double-Free** - Handles track release state
3. **Memory Leaks** - `with-*` forms ensure cleanup
4. **Dangling Callbacks** - WeakHandles invalidate safely
5. **Type Confusion** - Handles are typed; casts are explicit
6. **Buffer Overflows** - Region-based allocation tracks bounds

### 11.2 What the FFI Cannot Prevent

1. **Incorrect C Code** - Foreign code can corrupt memory
2. **Thread Safety** - C libraries may not be thread-safe
3. **Resource Leaks** - Non-memory resources (files, sockets) need explicit cleanup
4. **Incorrect Signatures** - Mismatched function declarations cause undefined behavior

### 11.3 Debug Mode

```lisp
;; Enable FFI debugging
(ffi/set-debug true)

;; All FFI calls are logged
;; Handle operations are traced
;; Invalid handle access is caught and reported
```

---

## 12. Implementation Notes

### 12.1 Runtime Infrastructure Used

| Feature | Implementation |
|---------|---------------|
| Handles | `ExternalHandleTable` with 32-bit index + 32-bit generation |
| Callbacks | `WeakControlBlock` for safe invalidation |
| Regions | `IRegion` vtable (Arena, Linear, Offset, Pool) |
| Deterministic | `external_table_set_deterministic()` |
| ABA Protection | Generation counters on all handles |

### 12.2 Code Generation

The Omnilisp compiler generates:

1. **Thunks** - Wrapper functions that convert Omnilisp values to C values
2. **Handle Tables** - Per-library handle tables for foreign objects
3. **Callback Trampolines** - C functions that call back into Omnilisp
4. **Cleanup Code** - Automatic release of handles in finally blocks

### 12.3 Performance Considerations

- Handle lookup is O(1) via direct indexing
- Callback invocation has ~10ns overhead for handle validation
- Region allocation is bump-pointer fast
- Deterministic mode adds no overhead beyond sequential allocation

---

## 13. Future Extensions

### 13.1 Async FFI

```lisp
;; Non-blocking FFI call
(let [future (ffi/async (slow_operation arg))]
  ;; Do other work
  (await future))
```

### 13.2 Struct Mapping

```lisp
;; Automatic struct conversion
(define {struct :ffi :mirror Point}
  [x {Float -> CFloat}]
  [y {Float -> CFloat}])

;; Omnilisp Point <-> C Point automatically
```

### 13.3 FFI Macros

```lisp
;; Generate bindings from C header
(ffi/from-header "mylib.h"
  :include [func1 func2 MyStruct]
  :exclude [internal_*])
```

---

## Summary

The Omnilisp FFI provides:

- **Safety** through handle-based memory management with ABA protection
- **Clarity** through explicit ownership annotations (`^:owned`, `^:borrowed`, `^:consumed`)
- **Flexibility** through region-based allocation for different use cases
- **Callbacks** with safe invalidation via weak handles
- **Determinism** for replay and debugging
- **Integration** with Omnilisp's type system and syntax conventions

All foreign memory operations go through the handle table, ensuring that invalid or stale pointers are caught rather than causing crashes.
