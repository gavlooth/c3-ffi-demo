# Core Libraries Inspection — Making Omni Usable

## Current State (Session 67)

Omni has a solid runtime (JIT, fibers, effects, scope-region memory, FFI) plus a full
I/O and data processing stack. The effect fast-path dispatch table allows adding new
I/O operations without touching the JIT.

### Integrated (Session 67, all statically linked)

| Library       | Role                         | License  | Files                          |
|--------------|------------------------------|----------|--------------------------------|
| GNU Lightning | JIT code generation          | LGPL-3.0 | (dynamic)                      |
| libffi        | FFI calling convention       | MIT      | (dynamic)                      |
| replxx        | REPL (highlighting, history) | BSD-2    | (dynamic)                      |
| libclang      | Bindgen (`--bind`)           | Apache-2.0 | (dynamic)                    |
| Pika engine   | Regex + PEG grammar (built-in C3) | —   | `src/pika/` (11 files)         |
| utf8proc      | Unicode string ops           | MIT      | `src/lisp/unicode.c3`          |
| yyjson        | JSON parse/emit              | MIT      | `src/lisp/json.c3` + `csrc/json_helpers.c` |
| libdeflate    | gzip/deflate compression     | MIT      | `src/lisp/compress.c3`         |
| libuv         | TCP/DNS/timers (linked, POSIX fast-path) | MIT | `src/lisp/async.c3`       |
| BearSSL       | TLS client connections       | MIT      | `src/lisp/tls.c3` + `csrc/tls_helpers.c` (33 lines) |

Static archives built from source via `deps/build_static.sh` → `deps/lib/*.a`.

### Effect Fast-Path Dispatch Table

All I/O goes through effects (`signal`/`handle`/`resolve`). When no handler is installed,
a dispatch table (`FastPathEntry[32]` on Interp) routes to blocking `__raw-*` primitives.
New effects: just call `register_fast_path(interp, "io/foo", "__raw-foo")` — zero JIT changes.

---

## Tier 1: DONE — Foundation ✓

### 1. libuv — Async I/O ✓
TCP connect/read/write/close, DNS resolve, async-sleep. All through effects.
POSIX blocking fast-path for v1. libuv linked for future async scheduler.

### 2. utf8proc — Unicode ✓
Unicode-aware string-upcase/downcase, string-normalize (NFC/NFD/NFKC/NFKD),
string-graphemes, string-codepoints, char-category.

### 3. Pika regex/grammar — INTEGRATED ✓
re-match, re-find-all, re-split, re-replace, re-fullmatch, re-match-pos, re-find-all-pos.
pika/grammar, pika/parse, pika/fold. `lisp_semantics.c3` disabled (AST mismatch).

### 4. yyjson — JSON ✓
json-parse (JSON → dict/array/string/int/double/nil), json-emit, json-emit-pretty.

### 5. BearSSL — TLS ✓
tls-connect (wraps TCP handle), tls-read, tls-write, tls-close.
Direct extern fn to BearSSL, 33-line C callback helper. v1 skips cert verification.

### 6. libdeflate — Compression ✓
gzip, gunzip, deflate, inflate.

---

## Tier 2: Data Integrity — Contracts + Datalog

### 7. Data-Driven Contracts (Malli-inspired)

**What:** Schema validation system where schemas are plain Omni data (lists + symbols).
Validates dicts, arrays, and values against structural schemas.

**Design lineage:** Malli (Clojure) → adapted for Omni's quoted symbols + multiple dispatch.

**What's kept from Malli:**
- Schemas are data, not macros — composable, introspectable, serializable
- Combinators: `and`, `or`, `not`, `maybe` (optional)
- Collection schemas: `map` (required/optional keys), `vector`, `tuple`
- Coercion and explain (error reporting)

**What's changed for Omni:**
- Clojure keywords (`:string`) → quoted symbols (`'string`) — Omni has no keywords
- Protocol dispatch → multiple dispatch on schema tag symbol
- Runtime schema registry → schemas are just values, dispatch handles extension
- Zero-allocation validation path — dispatch directly, no intermediate schema objects

**Syntax:**

```lisp
;; Schema as quoted data
(define [schema] person
  (map
    (name string)
    (age (and int (> 0)))
    (email (maybe (re "^[^@]+@[^@]+$")))))

;; Validate
(validate 'person (dict 'name "Alice" 'age 30))
;; => true

;; Explain failures
(explain 'person (dict 'name "Alice" 'age -1))
;; => ((age "must satisfy (> 0)"))

;; Compose schemas
(define [schema] team
  (map
    (name string)
    (members (vector-of person))))
```

**Schema types (dispatch targets):**

| Tag | Validates | Example |
|-----|-----------|---------|
| `string` | String value | `'string` |
| `int` | Integer value | `'int` |
| `double` | Float value | `'double` |
| `bool` | Boolean | `'bool` |
| `any` | Always passes | `'any` |
| `(and s1 s2)` | Both pass | `'(and int (> 0))` |
| `(or s1 s2)` | Either passes | `'(or string int)` |
| `(not s)` | Fails | `'(not nil)` |
| `(maybe s)` | Nil or passes | `'(maybe string)` |
| `(= v)` | Exact value | `'(= 42)` |
| `(> n)` `(< n)` | Comparison | `'(> 0)` |
| `(re pat)` | Regex match (Pika) | `'(re "[a-z]+")` |
| `(map ...)` | Dict with keys | `'(map (name string))` |
| `(vector-of s)` | Array of schema | `'(vector-of int)` |
| `(tuple s1 s2)` | Fixed-size array | `'(tuple string int)` |
| `(enum v1 v2)` | One of values | `'(enum "a" "b" "c")` |

**Implementation approach:**
- Parser: `define [schema]` stores schema value in global env
- Validation: recursive walk of schema data, multiple dispatch on `car` of each node
- `validate` returns bool, `explain` returns list of `(path message)` pairs
- Zero allocation on success (dispatch + comparisons only)
- Pika regex integrated via `re` schema tag

**Estimated complexity:** ~300 lines C3 + ~50 lines stdlib

---

### 8. Relational Datalog on LMDB

**Deduce** — embedded relational Datalog for Omni. Named relations (not EAV triples),
bottom-up semi-naive evaluation, recursive rules, LMDB-backed persistence.
Query results are **lists of dicts** (keyed by variable name, `?` stripped).

**Design lineage:**

```
Malli ──→ data-driven schemas (kept)
Datalevin/Datomic ──→ LMDB backend (kept), EAV triples (DROPPED)
Soufflé ──→ relational model, bottom-up evaluation (adopted)

Result: Soufflé's query model + Datalevin's storage + Omni's define [attr] syntax
Name: Deduce (the engine deduces new facts from rules)
```

**Why not Datomic/DataScript style (EAV triples):**

| Datomic Pain Point | Problem | Relational Fix |
|--------------------|---------|---------------|
| EAV triples | Reading 1 entity = N index lookups | Relations return complete tuples |
| Entity reassembly | Need Pull API to reconstruct objects | Tuples are already complete |
| Implicit schema | Entity "types" = bags of attributes | `define [relation]` with named columns |
| Immutable history forever | Disk grows monotonically | Mutable default, opt-in history |
| Entity ID confusion | Tempids, lookup refs, idents | Declared primary keys |
| Weak aggregation | Datalog bad at GROUP BY / ORDER BY | Aggregate functions in query syntax |

**Why not other approaches:**

| Alternative | Rejected Because |
|-------------|-----------------|
| DataScript (in-memory) | Persistent data structures (HAMT) need GC. Worst fit for region memory. |
| Curry (functional-logic) | Non-deterministic narrowing generates allocation pressure from search trees. |
| PostgreSQL | External process, network overhead. Violates self-contained. |
| SQLite | Good for SQL, but Datalog rules/recursion are awkward via CTEs. |

**Why LMDB:**
- **mmap-based** — OS manages page caching, no interaction with Omni's region allocator
- **Embedded** — single C library, ~50KB compiled
- **Single-writer** — matches Omni's single-threaded model
- **Copy-on-write B+ tree** — ACID transactions, crash-safe
- **Proven** — Datalevin (Clojure) and Mentat (Rust/Mozilla) both do Datalog-on-LMDB

#### Deduce Grammar

**Database — LMDB-backed, always in-memory (mmap), optionally persistent:**
```lisp
;; Persistent — file on disk, survives restart
(define db (deduce-open "app.db"))

;; Ephemeral — anonymous LMDB, same API, no durable file
(define tmp (deduce-open 'memory))
```

LMDB is mmap-based: reads hit OS page cache (memory speed), writes flush to disk lazily.
No separate "in-memory mode" — LMDB already IS in-memory with persistence as side effect.

**Relations — columns with `^` metadata (types, roles, flags):**

`^` is Omni's metadata mechanism — types, flags, and dict metadata all in one prefix.
For Deduce columns, the dict key IS the column role, the value IS the type:

```lisp
;; Minimal — bare column names, no types or roles
(define [relation db] edge (from to))

;; Typed columns — ^Type for plain columns
(define [relation db] road
  (city1 ^String)
  (city2 ^String)
  (km ^Int))

;; Roles via metadata dict — ^{'role Type}
(define [relation db] person
  (name ^{'key String})         ;; primary key, type String
  (age ^{'index Int})           ;; indexed, type Int
  (email ^String))              ;; plain, type String

;; Relation-level metadata in bracket attr or ^{...}
(define [relation db history] person
  (name ^{'key String})
  (age ^{'index Int})
  (email ^String))

;; Rich metadata when needed
(define [relation db] ^{'schema person-schema} employee
  (id ^{'key Int})
  (name ^String)
  (dept ^{'index String})
  (salary ^Int))
```

**Column metadata — two equivalent forms:**

Short form (bare `^` modifiers, like `^rec`/`^strict`):
```lisp
(name ^String ^key)        ;; type + role as separate hints
(age ^Int ^index)
```

Dict form (`^{'role Type}`, role carries the type):
```lisp
(name ^{'key String})      ;; same thing, one annotation
(age ^{'index Int})
```

| Metadata | Short form | Dict form | Meaning |
|----------|-----------|-----------|---------|
| Plain typed | `^String` | — | Column with type, no special role |
| Primary key | `^String ^key` | `^{'key String}` | Upsert on duplicate |
| Indexed | `^Int ^index` | `^{'index Int}` | Secondary B+ tree for range scans |
| Key + indexed | `^String ^key ^index` | `^{'key String 'index true}` | Both roles |

**Relation-level metadata:**

| Metadata | Syntax | Purpose |
|----------|--------|---------|
| `history` | `[relation db history]` | Temporal: keep old versions. Enables `:at`/`:time` queries. |
| `schema` | `^{'schema name}` | Contract validation on every `assert!`. |
| `memory` | `^{'memory "200mb"}` | Override LMDB map size. Default ~1GB. |

**Facts — assert tuples into relations:**
```lisp
;; person is a first-class Relation value (from define [relation])
(assert! person "Alice" 30 "alice@b.com")
(assert! person "Bob" 25 "bob@b.com")
(assert! edge "A" "B")
(assert! edge "B" "C")
```

**Rules — derived relations (name, output vars, body clauses):**
```lisp
(define [rule] adult (?name ?age)
  (person ?name ?age _)
  (>= ?age 18))

;; Recursive — multiple defines = implicit OR (standard Datalog)
(define [rule] reachable (?a ?b)
  (edge ?a ?b))

(define [rule] reachable (?a ?b)
  (edge ?a ?mid)
  (reachable ?mid ?b))
```

**Query — special form, no quoting. Returns list of dicts (? stripped from keys):**
```lisp
;; All columns
(query (person ?name ?age _) (> ?age 28))
;; => ((dict 'name "Alice" 'age 30 'email "alice@b.com"))

;; Projection — [...] selects which variables to return
(query [?name ?age]
  (person ?name ?age _)
  (> ?age 28))
;; => ((dict 'name "Alice" 'age 30))

;; Synergy with 'symbol-as-function:
(|> (query [?name ?age] (person ?name ?age _) (> ?age 28))
    (map 'name))
;; => ("Alice")
```

**Negation — (not ...) as a clause (stratified):**
```lisp
(query [?name]
  (person ?name _ _)
  (not (edge ?name "BadNode")))
```

**Aggregation — aggregates in select, bare vars become group-by:**
```lisp
(query [(count ?name)]
  (adult ?name _))
;; => ((dict 'count 5))

(query [?dept (avg ?salary) (max ?salary)]
  (employee ?name ?dept ?salary))
;; => ((dict 'dept "engineering" 'avg 95000 'max 140000)
;;     (dict 'dept "sales" 'avg 72000 'max 90000))
```

**Ordering — (order-by ?var 'asc/'desc) as final clause:**
```lisp
(query [?name ?age]
  (person ?name ?age _)
  (order-by ?age 'desc))
;; => ((dict 'name "Alice" 'age 30) (dict 'name "Bob" 'age 25))
```

**Temporal queries — when history is enabled on a relation:**
```lisp
;; Query state at a specific time
(query [?name ?age]
  (person ?name ?age _ :at "2025-06-01"))

;; Query all versions of a record
(query [?name ?age ?t]
  (person ?name ?age _ :time ?t)
  (= ?name "Alice"))
;; => ((dict 'name "Alice" 'age 29 't "2025-01-01")
;;     (dict 'name "Alice" 'age 30 't "2026-01-15"))
```

**Retract — with wildcard support:**
```lisp
(retract! person "Alice" 30 "alice@b.com")  ;; exact match
(retract! person "Alice" _ _)               ;; wildcard: all Alice records
```

#### Graph Encoding — Datalog's Sweet Spot

```lisp
(define [relation] edge (from to))
(define [relation] road (city1 city2 km))

;; Reachability — 2 rules
(define [rule] reachable (?a ?b) (edge ?a ?b))
(define [rule] reachable (?a ?b) (edge ?a ?mid) (reachable ?mid ?b))

;; Cycle detection
(define [rule] has-cycle (?node) (reachable ?node ?node))

;; Shortest path (weighted + aggregation)
(define [rule] path (?a ?b ?cost) (road ?a ?b ?cost))
(define [rule] path (?a ?b ?cost)
  (road ?a ?mid ?c1)
  (path ?mid ?b ?c2)
  (= ?cost (+ ?c1 ?c2)))

(query [?dest (min ?km)]
  (path "Athens" ?dest ?km))

;; Social graph
(define [rule] fof (?a ?c)
  (edge ?a ?b)
  (edge ?b ?c)
  (not (= ?a ?c)))

(define [rule] mutual (?a ?b)
  (edge ?a ?b)
  (edge ?b ?a))

(query [?who] (reachable "A" ?who))
;; => ((dict 'who "B") (dict 'who "C"))
```

#### Contracts + Deduce Integration

Schemas validate data before insertion:
```lisp
(define [schema] person-valid
  (map
    (name string)
    (age (and int (> 0)))
    (email (re "^[^@]+@.+"))))

;; assert! can check schema before writing to LMDB
;; (configurable per-relation)
```

Query results are dicts — validatable with the same contract system:
```lisp
(map (lambda (row) (validate 'person-valid row))
     (query [?name ?age ?email] (person ?name ?age ?email)))
```

**LMDB storage model:**

Each relation gets B+ tree indices in LMDB:
- Primary index: all columns concatenated as sorted key bytes
- Secondary indices: by each column for efficient lookups

```
relation "person":
  index 0 (full):  "Alice\x0030\x00alice@b.com" → ∅
  index 1 (name):  "Alice" → [ptr to full tuple]
  index 2 (age):   30 → [ptr to full tuple]
```

Tuples stored as concatenated bytes with type tags. LMDB's sorted B+ tree gives
range queries for free (e.g., `(> ?age 28)` becomes a range scan on index 2).

**Query evaluation:**

Bottom-up semi-naive evaluation (like Soufflé):
1. Evaluate base relations (LMDB lookups)
2. Apply rules to derive new facts
3. Repeat until no new facts (fixpoint)
4. Filter by query pattern

Semi-naive optimization: only join with *newly derived* facts each round,
avoiding redundant re-derivation.

**Deduce implementation:**

| Component | Lines (est.) | Notes |
|-----------|-------------|-------|
| LMDB extern fn (C3 native, no wrapper) | ~30 | `mdb_env_*`, `mdb_txn_*`, `mdb_cursor_*` |
| Relation storage + attributes | ~250 | Tuple encoding, LMDB indices, key/index/schema/history handling |
| Unification engine | ~150 | `?var` pattern matching with logic variables |
| Semi-naive evaluator | ~200 | Bottom-up fixpoint with stratified negation |
| Query compiler | ~150 | `query` special form → evaluation plan |
| Dict result builder | ~50 | Query results as dicts (strip `?` from var names) |
| Aggregation engine | ~100 | `count`, `sum`, `avg`, `min`, `max` + group-by |
| Temporal (history/`:at`/`:time`) | ~80 | Version tracking with timestamps, temporal query clauses |
| Parser (`[relation]`, `[rule]`, `query`, `?var`) | ~120 | Bracket attrs + relation attribute clauses + `?` reader char |
| Primitives (`deduce-open`, `assert!`, `retract!`) | ~100 | Database lifecycle, relation as first-class value |
| **Total** | **~1230** | |

**Storage model:** LMDB is always the backend (mmap = in-memory speed, disk persistence
as side effect). `(deduce-open 'memory)` for ephemeral use (anonymous mmap, no file).
No separate in-memory engine — LMDB already IS in-memory.

**Parser changes needed:**
- `?` as reader prefix for logic variables → `T_LOGIC_VAR` token type
- `define [relation]` → `E_DEFRELATION` expr tag
- `define [rule]` → `E_DEFRULE` expr tag (name, output vars list, body clauses)
- `query` → special form, treats body as pattern data (like `match`), `[...]` = projection

**Dependencies:** LMDB (BSD, ~50KB compiled). Build from source like the other 5 libraries.

---

## Three-Tier Pattern Matching (PCRE2 eliminated)

Backreferences (`\1`, `\2`) are **context-sensitive** — they make regex matching NP-complete.
Pika is a **context-free** bottom-up DP engine with linear/polynomial guarantees.
These are fundamentally incompatible. Adding backreferences would break Pika's architecture.

**Dialectic reasoning (4 rounds):**

1. "Add semantic predicates" → fails: Pika parses R→L, capture group doesn't exist yet when
   backreference is encountered (it's to the left in the string, parsed later)
2. "Parse `\1` as `.*`, filter post-hoc" → fails: combinatorial explosion of spurious parse
   trees, destroys O(n³) guarantee, exponential memory
3. "Redirect to grammar DSL" → viable but ergonomically painful for simple cases
4. "Accept the theoretical boundary" → correct: backreferences are NP-complete, don't pretend
   a polynomial engine can handle them

**Resolution: three tiers, no PCRE2.**

```
Tier 1: re-match (fast, linear)     — 95% of use cases
         [0-9]+, \w+, (foo|bar), (?=...), [a-z]{3,5}
         Zero external deps, Pika-native

Tier 2: pika/grammar (powerful)     — 4.9% of use cases
         Recursive grammars, left recursion, structural parsing
         Handles what backreferences typically solve:

         ;; Match balanced quotes (would need \1 in PCRE)
         ;; Instead: match with Omni code
         (define (match-quoted s)
           (let (q (char-at s 0))
             (and (or (= q "'") (= q "\""))
                  (= q (char-at s (- (string-length s) 1))))))

Tier 3: Omni code (unlimited)       — 0.1% of use cases
         When you need context-sensitive matching, write a function.
         Multiple dispatch + Pika regex + string ops = anything.
```

**Why this beats PCRE2:**

| | PCRE2 via FFI | Three-tier (no PCRE2) |
|---|---|---|
| Dependencies | +1 C library (~500KB) | Zero new deps |
| Two regex engines | Confusing: which to use? | One engine, clear escalation |
| Catastrophic backtracking | PCRE2 can ReDoS | Pika always terminates |
| Learning curve | Two regex flavors | One system to learn |
| Maintenance | Two engines to maintain | One engine + grammar DSL |

---

### 9. Reader Dispatch and Symbol-as-Function

**Two syntax enhancements** that unlock ergonomics across contracts, Datalog, and pipelines.

#### `'symbol` as lookup function

When a quoted symbol is applied as a function, it performs a key lookup:

```lisp
('name (dict 'name "Alice" 'age 30))   ;; => "Alice"

;; Powerful with HOFs and pipelines
(map 'name people)                       ;; => ("Alice" "Bob")
(filter 'active? users)
(sort-by 'age people)

;; Works with Datalog results
(map 'age (query '((person ?name ?age _))))

;; Pipe-friendly
(|> people (filter 'active?) (sort-by 'age) (map 'name))
```

Implementation: ~10 lines in `jit_apply_value` — when applying a SYMBOL, treat as `(ref arg symbol)`.

**Why not `.name` accessor lambdas:** `.name` conflicts visually with `person.name` field access.
`person.name.city` vs `.name.city` — same characters, different meanings. Confusing.
`'name` has zero ambiguity: it's always a quoted symbol, never a field access.

#### `#` reader dispatch extensions

| Syntax | Meaning | Example |
|--------|---------|---------|
| `;` | Line comment | `; this is a comment` |
| `#\| ... \|#` | Block comment (nestable) | `#\| multi-line \|#` |
| `#_` | Skip next form | `#_ (ignored) (this runs)` |
| `#N_` | Skip next N forms | `#3_ (a) (b) (c) (this runs)` |
| `#{}` | Set literal | `#{"red" "green" "blue"}` |
| `#r"pat"` | Compiled Pika regex | `#r"[0-9]+"` |

**Set literals** complete the collection trinity:
```lisp
'(1 2 3)          ;; list
[1 2 3]           ;; array
{'a 1 'b 2}       ;; dict
#{"a" "b" "c"}    ;; set (desugars to (set "a" "b" "c"))
```

**Regex literals** make Pika regex first-class:
```lisp
;; #r"" compiles regex at read time, no string escaping needed
(re-match #r"[0-9]+" "abc123")      ;; vs (re-match "[0-9]+" "abc123")
(re-split #r"\s+" "hello world")    ;; vs (re-split "\\s+" "hello world")

;; Particularly valuable for complex patterns
(re-match #r"(\w+)\s*=\s*(\w+)" "key = value")
```

**Multi-form comment** `#N_` — the number comes BEFORE `_` to avoid ambiguity with `#_ 3`:
```lisp
;; Disable a function and its test
#2_ (define (broken x) ...)
     (assert (= (broken 1) 2))

;; Disable one branch of a cond
(cond
  (< x 0) "negative"
  #1_ (= x 0) "zero"       ;; disabled: skip 1 form (the "zero" string)
  true "non-negative")
```

**Block comments** nest — safe to comment out code containing block comments:
```lisp
#|
  This whole section is disabled.
  #| Even nested comments |# are fine.
  (define (unused) ...)
|#
```

**Implementation:** ~80 lines in parser (reader dispatch table for `#`).

---

## Tier 3: Nice to Have

### 10. stb_image — Image loading

Single-header image decoder. Relevant given omni-torch exists. Public domain, ~100KB.

### 11. HTTP/1.1 client

Build on libuv TCP + BearSSL TLS. Parse with Pika grammar.
Alternative: libcurl via FFI if building from scratch is too slow.

### 12. Concurrency Model — Fibers, Scheduler, Threads

Omni's concurrency builds entirely on existing primitives (coroutines + effects).
No new concepts — just composition.

**Three tiers, increasing isolation:**

```
Coroutines    — cooperative, same thread, share everything (zero copy)
Fibers        — cooperative, same thread, scheduler manages (zero copy)
Threads       — parallel, isolated Interp, share NOTHING (copy flat data only)
```

#### Tier 1: Coroutines (LANDED)

Manual cooperative multitasking. `coroutine`/`resume`/`yield`. StackCtx-based.
User explicitly controls scheduling. Good for generators, lazy sequences, streams.

#### Tier 2: Fibers = Coroutines + Scheduler Effect Handler

A fiber is a coroutine whose `resume` is called by the scheduler, not the user.
The scheduler is an effect handler wrapping user code:

```lisp
;; spawn creates a coroutine, registers it with the scheduler
(spawn (lambda () (tcp-read (tcp-connect "a.com" 80))))

;; Under the hood:
;; 1. spawn = create coroutine + add to scheduler's ready queue
;; 2. tcp-connect signals io/tcp-connect effect
;; 3. Scheduler handler catches it, starts libuv async op, suspends fiber
;; 4. libuv callback fires → scheduler marks fiber as ready
;; 5. Scheduler resumes fiber with the result via resolve
```

Scheduler loop (~150 lines C3):
```
while fibers exist:
    while ready_queue not empty:
        resume next fiber
        if it signaled I/O effect → start libuv op, keep suspended
    uv_run(loop, UV_RUN_ONCE)   ;; poll libuv for completions
    ;; callbacks push fibers back to ready_queue
```

No new types. A fiber IS a coroutine. The scheduler IS an effect handler.

#### Tier 3: Threads = Fibers on OS Threads (share-nothing)

Each thread gets its own `Interp` + scope-region (complete isolation).
Communication via effects — the handler manages a lock-free inter-thread queue.
`uv_thread_create` + `uv_async_t` (cross-thread wakeup) from libuv.

**Critical design: send DATA, not closures.**

Copying closures across threads is heavy (env chain + all captured bindings = O(n)).
Instead, send flat data (int, string, symbol, list of scalars). Each thread loads
the same source code, so all functions are already defined in every Interp:

```lisp
;; LIGHT — send a symbol + an int (O(1) copy)
(signal io/thread-send (cons target '(compute-fib 40)))
;; Receiving thread already has compute-fib, just calls it

;; NOT THIS — copying a closure means copying the entire env chain
;; (signal io/thread-send (cons target (lambda () (compute big-captured-data))))
```

**No built-in channels.** Inter-thread communication goes through effects.
The "channel" is a queue inside the effect handler — not a language primitive.
Users who want channel abstractions write a handler:

```lisp
;; Channel = effect handler managing a queue
(define (make-channel)
  (let (queue (array))
    (dict 'send (lambda (msg) (signal io/chan-send (cons queue msg)))
          'recv (lambda () (signal io/chan-recv queue)))))
```

**Why no channels as primitives:**
- Effects already provide the suspend/resume mechanism
- A channel is just a queue + two effects (send/recv)
- Baking it in adds a concept where composition suffices
- Different channel types (buffered, unbuffered, broadcast) = different handlers

**What this model provides (all via composition):**

| Pattern | How |
|---------|-----|
| Message passing | Effect handler + queue |
| Future/Promise | Spawn fiber, await via effect |
| Fork/Join | Spawn N fibers, collect via effects |
| Map-reduce | Scatter data to threads, gather via effects |
| Pipeline | Chain of effect handlers |
| Supervision | Parent handler monitors child fibers |

**What's NOT needed (share-nothing eliminates):**
- Locks/mutexes — no shared mutable state
- STM — no shared state to transact on

#### Fine-Grained Shared State: Agent Pattern + Atomic Refs

Share-nothing is correct for 95% of concurrency. For the remaining 5% (hot counters,
parallel accumulators), two escape hatches — neither requires locks.

**Tier A: Agent pattern (Omni-native, no new types)**

An agent is a fiber that owns mutable state and processes update messages serially.
Other threads send requests, the agent applies them one at a time. No races by construction.
This is how Erlang's `gen_server` works.

```lisp
;; Agent = fiber that owns state, processes messages serially
(define (make-counter initial)
  (spawn (lambda ()
    (let loop (n initial)
      (let (msg (yield n))
        (match msg
          ('inc   (loop (+ n 1)))
          ('dec   (loop (- n 1)))
          ('get   (loop n))))))))

;; Any thread sends update requests
(signal io/thread-send (cons counter-agent 'inc))
```

**Tier B: Atomic refs (hardware atomics, ~30 lines C3)**

When message roundtrip overhead matters (millions of increments per second),
atomic integers provide direct hardware CAS. Uses C3's native `std::atomic` module —
no C builtins, no FFI, pure C3:

```lisp
;; Omni API
(define counter (atomic 0))      ;; shared atomic integer
(atomic-add! counter 1)          ;; hardware fetch-and-add, returns old value
(atomic-read counter)            ;; atomic load
(atomic-cas! counter 0 1)        ;; compare-and-swap, returns true/false
```

C3 implementation (uses `std::atomic::Atomic{long}`):

```c3
import std::atomic;

// atomic ref = malloc'd Atomic{long}, shared across threads (not in any scope-region)
fn Value* prim_atomic(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1 || !is_int(args[0])) return raise_error(interp, "atomic: expected integer");
    atomic::Atomic{long}* atom = (atomic::Atomic{long}*)mem::malloc(atomic::Atomic{long}.sizeof);
    atom.store(args[0].int_val);
    // Wrap as FFI_HANDLE in root_scope — lives for program lifetime
    return make_ffi_handle(interp, (void*)atom, "atomic");
}

fn Value* prim_atomic_add(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 2) return raise_error(interp, "atomic-add!: expected (atomic-add! ref n)");
    atomic::Atomic{long}* atom = (atomic::Atomic{long}*)args[0].ffi_val;
    long old = atom.add(args[1].int_val);
    return make_int(interp, old);
}

fn Value* prim_atomic_read(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1) return raise_error(interp, "atomic-read: expected (atomic-read ref)");
    atomic::Atomic{long}* atom = (atomic::Atomic{long}*)args[0].ffi_val;
    return make_int(interp, atom.load());
}

fn Value* prim_atomic_cas(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 3) return raise_error(interp, "atomic-cas!: expected (atomic-cas! ref old new)");
    atomic::Atomic{long}* atom = (atomic::Atomic{long}*)args[0].ffi_val;
    long expected = args[1].int_val;
    long desired = args[2].int_val;
    long* exp_ptr = &expected;
    bool ok = mem::compare_exchange((long*)atom, exp_ptr, desired);
    return ok ? interp.global_env.lookup(interp.symbols.intern("true")) : make_nil(interp);
}
```

**Only integers.** No atomic closures, no atomic dicts. Atomic refs live outside
scope-regions (plain `malloc`) — shared across threads with zero coordination.

**When to use which:**

| Need | Solution | Overhead |
|------|----------|----------|
| Shared mutable state (general) | Agent pattern | Message roundtrip (~µs) |
| Hot counter / parallel accumulator | `atomic` + `atomic-add!` | Hardware CAS (~ns) |
| Shared large data (rare) | FFI to C (e.g., omni-torch tensors) | Zero (C manages it) |

**Not a GIL.** Python's GIL = one interpreter, N threads fighting for it.
Omni = N interpreters, zero contention, true parallelism. Same model as Erlang
(powers WhatsApp 2B users, Discord, RabbitMQ).

**Implementation:**

| Component | Lines (est.) | Phase |
|-----------|-------------|-------|
| Fiber scheduler (effect handler + libuv poll loop) | ~150 C3 | E |
| `spawn` / `await` primitives | ~50 C3 | E |
| Thread spawn (`uv_thread_create` + per-thread Interp init) | ~100 C3 | G |
| Inter-thread message queue (`uv_async_t` wakeup) | ~80 C3 | G |
| Cross-thread value copy (flat data only, no closures) | ~50 C3 | G |
| Atomic refs (`atomic`, `atomic-add!`, `atomic-read`, `atomic-cas!`) | ~30 C3 | G |

---

## Recommended Adoption Order

```
Phase A (DONE):    libuv + utf8proc + Pika + yyjson + libdeflate + BearSSL
                   Effect fast-path dispatch table
                   ↓
Phase B (next):    Reader dispatch (#r"", #{}, #N_, #| |#) + symbol-as-function
                   ~90 lines parser, pure syntax — no new deps
                   ↓
Phase C:           Contracts (define [schema]) — ~300 lines, pure Omni + dispatch
                   ↓
Phase D:           LMDB integration + Datalog engine (define [relation], define [rule])
                   ↓
Phase E:           Fiber scheduler (coroutines + effect handler + libuv event loop)
                   ↓
Phase F:           HTTP/1.1 client (libuv + BearSSL + Pika grammar)
                   ↓
Phase G:           Threads (uv_thread_create + per-thread Interp + effect-mediated messaging)
                   ↓
Phase H (extras):  stb_image
```

Phase B is pure parser work. Phase C is pure Omni + dispatch. Phase D adds LMDB (~50KB).
Phase E is ~200 lines C3 (scheduler + spawn/await). Phase F composes existing libraries.
Phase G is ~230 lines C3 (threads + messaging). No new external dependencies after Phase D.
PCRE2 eliminated — three-tier pattern matching covers all use cases.

---

## Dependency Summary

```
INTEGRATED:
  libuv (MIT)          ── TCP/DNS/timers, future async scheduler
  utf8proc (MIT)       ── Unicode normalization, case, graphemes
  Pika engine (built-in) ── regex, PEG grammar
  yyjson (MIT)         ── JSON parse/emit
  BearSSL (MIT)        ── TLS client (no-malloc design)
  libdeflate (MIT)     ── gzip/deflate compression

PLANNED:
  LMDB (OpenLDAP PL)  ── embedded B+ tree DB for Datalog storage

EXISTING:
  GNU Lightning (LGPL) ── JIT code generation
  libffi (MIT)         ── FFI calling convention
  replxx (BSD-2)       ── REPL
  libclang (Apache-2)  ── bindgen
```

All MIT/BSD/PD/OpenLDAP — no license conflicts.

---

## Integration Architecture

```
User-facing Omni code
        |
    signal effect (io/read-file, io/tcp-connect, io/tls-read, ...)
        |
    ┌── Handler installed? ──→ handler clause runs, may resolve
    │
    └── No handler ──→ fast-path dispatch table
                       │
                       lookup FastPathEntry[tag] → raw primitive
                       │
                       arity 0: call(nil)
                       arity 1: call(arg)
                       arity 2+: curry via cons pair
                       │
                       C3 primitive → extern fn to C library → result as Omni Value
```

For contracts: `validate`/`explain` are pure Omni dispatch, no effects involved.
For Datalog: `query`/`assert!` go through LMDB extern fns, results as Omni lists.
