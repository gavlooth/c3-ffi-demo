module lisp;

import std::collections::list;
import std::io;
import main;

// =============================================================================
// SECTION 1: VALUE TYPES
// =============================================================================

/**
 * ValueTag — Type tag for Lisp values.
 */
enum ValueTag : char {
    NIL,
    INT,
    STRING,         // String value
    SYMBOL,
    CONS,
    CLOSURE,
    CONTINUATION,
    PRIMITIVE,
    PARTIAL_PRIM,   // Partially applied primitive (curried)
    ERROR,          // Error value (carries error message in str_val)
    HASHMAP,        // Hash map (mutable dictionary)
    FFI_HANDLE,     // FFI library handle (dlopen result)
}

/**
 * Symbol — Interned string identifier.
 */
typedef SymbolId = uint;

// Maximum symbol name length
const usz MAX_SYMBOL_LEN = 64;
// Maximum string value length (for StringVal, file reads, string ops)
const usz MAX_STRING_LEN = 4096;
// Maximum number of symbols
const usz MAX_SYMBOLS = 512;
// Maximum number of macros in the macro table
const usz MAX_MACROS = 64;
// Maximum number of modules in the module table
const usz MAX_MODULES = 32;
// Hash table sizes for macro/module lookup (2x max entries for good load factor)
const usz MACRO_HASH_SIZE = 128;   // 2 * MAX_MACROS
const usz MODULE_HASH_SIZE = 64;   // 2 * MAX_MODULES
// Hash table size for symbol intern (must be power of 2, >= 2*MAX_SYMBOLS)
const usz HASH_TABLE_SIZE = 1024;
// Invalid symbol ID sentinel (returned on exhaustion; must not alias any valid index)
const SymbolId INVALID_SYMBOL_ID = (SymbolId)0xFFFFFFFF;

/**
 * SymbolEntry — Storage for a single interned symbol name.
 */
struct SymbolEntry {
    char[MAX_SYMBOL_LEN] name;
    usz len;
}

/**
 * SymbolTable — Table for interning symbol names.
 */
struct SymbolTable {
    SymbolEntry[MAX_SYMBOLS] entries;
    usz count;
    SymbolId[HASH_TABLE_SIZE] hash_index;  // Maps hash slot → symbol index (INVALID_SYMBOL_ID = empty)
}

fn void SymbolTable.init(SymbolTable* self) {
    self.count = 0;
    // Initialize hash table slots to empty
    for (usz i = 0; i < HASH_TABLE_SIZE; i++) {
        self.hash_index[i] = INVALID_SYMBOL_ID;
    }
}

/**
 * hash_symbol — FNV-1a hash for symbol names.
 */
fn usz hash_symbol(char[] name) {
    usz h = 2166136261;  // FNV offset basis (32-bit)
    for (usz i = 0; i < name.len; i++) {
        h ^= (usz)name[i];
        h *= 16777619;   // FNV prime (32-bit)
    }
    return h;
}

fn SymbolId SymbolTable.intern(SymbolTable* self, char[] name) {
    usz h = hash_symbol(name);
    usz mask = HASH_TABLE_SIZE - 1;

    // Hash table lookup with linear probing
    for (usz probe = 0; probe < HASH_TABLE_SIZE; probe++) {
        usz slot = (h + probe) & mask;
        SymbolId sid = self.hash_index[slot];
        if (sid == INVALID_SYMBOL_ID) {
            // Empty slot: symbol not in hash table, stop probing
            break;
        }
        // Check if this slot's entry matches
        usz idx = (usz)sid;
        if (self.entries[idx].len == name.len) {
            bool match = true;
            for (usz j = 0; j < name.len; j++) {
                if (self.entries[idx].name[j] != name[j]) {
                    match = false;
                    break;
                }
            }
            if (match) {
                return sid;
            }
        }
    }

    // Not found — insert new symbol
    if (self.count >= MAX_SYMBOLS) {
        io::eprintfn("error: symbol table exhausted (max %d symbols)", MAX_SYMBOLS);
        return INVALID_SYMBOL_ID;
    }
    SymbolId id = (SymbolId)self.count;
    usz len = name.len;
    if (len > MAX_SYMBOL_LEN - 1) {
        io::eprintfn("WARNING: symbol name truncated to %d bytes", MAX_SYMBOL_LEN - 1);
        len = MAX_SYMBOL_LEN - 1;
    }
    for (usz i = 0; i < len; i++) {
        self.entries[self.count].name[i] = name[i];
    }
    self.entries[self.count].name[len] = 0;
    self.entries[self.count].len = len;
    self.count++;

    // Insert into hash table
    for (usz probe = 0; probe < HASH_TABLE_SIZE; probe++) {
        usz slot = (h + probe) & mask;
        if (self.hash_index[slot] == INVALID_SYMBOL_ID) {
            self.hash_index[slot] = id;
            break;
        }
    }

    return id;
}

fn char[] SymbolTable.get_name(SymbolTable* self, SymbolId id) {
    usz idx = (usz)id;
    assert(idx < self.count, "invalid symbol id");
    return self.entries[idx].name[:self.entries[idx].len];
}

/**
 * Cons — Pair/list cell.
 */
struct Cons {
    Value* car;
    Value* cdr;
}

/**
 * Closure — Lambda with captured environment.
 * Supports variadic lambdas: (lambda (x y .. rest) body)
 */
struct Closure {
    SymbolId  param;          // Parameter name (first/only param, or unused if param_count==0)
    SymbolId[64] params;      // All fixed params (including first)
    usz       param_count;    // Number of fixed params
    bool      has_rest;       // True if variadic (.. rest)
    SymbolId  rest_param;     // Rest parameter name (if has_rest)
    Expr*     body;           // Body expression
    Env*      env;            // Captured environment
    bool      has_param;      // false for zero-arg lambdas: (lambda () body)
}

/**
 * Continuation — Stub for delimited continuation.
 * Full implementation will be in continuation.c3
 */
struct Continuation {
    void* data;           // Opaque pointer to continuation data
}

/**
 * Primitive — Built-in function.
 */
alias PrimitiveFn = fn Value*(Value*[] args, Env* env, Interp* interp);

struct Primitive {
    char[32] name;
    PrimitiveFn func;
    int arity;           // -1 for variadic
}

/**
 * PartialPrim — Partially applied primitive (curried).
 */
struct PartialPrim {
    PrimitiveFn func;
    Value*      first_arg;   // First argument stored
    int         remaining;   // Arguments still needed (usually 1 for binary ops)
}

/**
 * StringVal — String value storage.
 */
struct StringVal {
    char[MAX_STRING_LEN] chars;
    usz len;
}

/**
 * HashEntry — Single key-value pair in a hash map.
 */
struct HashEntry {
    Value* key;     // null = empty slot
    Value* value;
}

/**
 * HashMap — Open-addressing hash map with linear probing.
 */
struct HashMap {
    HashEntry* entries;  // contiguous array (region-allocated)
    uint capacity;       // power of 2
    uint count;
    uint mask;           // capacity - 1
}

/**
 * MacroClause — Single clause in a pattern-based macro definition.
 * Pattern matches against args, template is a Value* datum for substitution.
 */
struct MacroClause {
    Pattern* pattern;    // Pattern to match macro args against
    Value*   tmpl;       // Template datum (Value*) for expansion
}

/**
 * CapturedBinding — A definition-time binding captured for macro hygiene.
 * Stores a snapshot of a symbol's value at macro definition time.
 */
struct CapturedBinding {
    SymbolId sym;
    Value*   value;
}

/**
 * MacroDef — Stored macro definition (pattern-based).
 * (define [macro] name (pattern1 template1) (pattern2 template2) ...)
 * captured_bindings stores a SNAPSHOT of template literal values at definition time,
 * preventing expansion-site shadowing from capturing them.
 */
struct MacroDef {
    SymbolId name;
    MacroClause[8] clauses;
    usz clause_count;
    CapturedBinding[32] captured_bindings;  // Snapshot of def-time bindings for hygiene
    usz captured_count;
}

/**
 * FfiHandle — Foreign library handle from dlopen().
 */
struct FfiHandle {
    void*    lib_handle;     // dlopen() result
    char[256] lib_name;      // for display/debugging
    usz      name_len;
}

/**
 * Value — Tagged union for all Lisp values.
 */
struct Value {
    ValueTag tag;

    union {
        long          int_val;
        StringVal     str_val;        // String value
        SymbolId      sym_val;
        Cons          cons_val;
        Closure       closure_val;
        Continuation* cont_val;      // From our continuation system
        Primitive     prim_val;
        PartialPrim   partial_val;   // Curried primitive
        HashMap*      hashmap_val;   // Hash map
        FfiHandle     ffi_val;       // FFI library handle
    }
}

// =============================================================================
// SECTION 2: VALUE CONSTRUCTORS
// =============================================================================

fn Value* make_nil(Interp* interp) @inline {
    Value* v = interp.alloc_value();
    v.tag = NIL;
    return v;
}

fn Value* make_int(Interp* interp, long n) @inline {
    Value* v = interp.alloc_value();
    v.tag = INT;
    v.int_val = n;
    return v;
}

fn Value* make_string(Interp* interp, char[] str) {
    Value* v = interp.alloc_value();
    v.tag = STRING;
    usz len = str.len;
    if (len > MAX_STRING_LEN - 1) {
        io::eprintfn("WARNING: string truncated from %d to %d bytes", str.len, MAX_STRING_LEN - 1);
        len = MAX_STRING_LEN - 1;
    }
    for (usz i = 0; i < len; i++) {
        v.str_val.chars[i] = str[i];
    }
    v.str_val.chars[len] = 0;
    v.str_val.len = len;
    return v;
}

fn Value* make_symbol(Interp* interp, SymbolId sym) @inline {
    Value* v = interp.alloc_value();
    v.tag = SYMBOL;
    v.sym_val = sym;
    return v;
}

fn Value* make_cons(Interp* interp, Value* car, Value* cdr) @inline {
    Value* v = interp.alloc_value();
    v.tag = CONS;
    v.cons_val.car = car;
    v.cons_val.cdr = cdr;
    return v;
}

fn Value* make_closure(Interp* interp, SymbolId param, Expr* body, Env* env) @inline {
    Value* v = interp.alloc_value();
    v.tag = CLOSURE;
    v.closure_val.param = param;
    v.closure_val.params[0] = param;
    v.closure_val.param_count = 1;
    v.closure_val.has_rest = false;
    v.closure_val.rest_param = 0;
    v.closure_val.body = body;
    v.closure_val.env = env;
    v.closure_val.has_param = true;
    return v;
}

fn Value* make_closure_no_param(Interp* interp, Expr* body, Env* env) @inline {
    Value* v = interp.alloc_value();
    v.tag = CLOSURE;
    v.closure_val.param = 0;
    v.closure_val.param_count = 0;
    v.closure_val.has_rest = false;
    v.closure_val.rest_param = 0;
    v.closure_val.body = body;
    v.closure_val.env = env;
    v.closure_val.has_param = false;
    return v;
}

fn Value* make_continuation(Interp* interp, Continuation* k) @inline {
    Value* v = interp.alloc_value();
    v.tag = CONTINUATION;
    v.cont_val = k;
    return v;
}

fn Value* make_primitive(Interp* interp, char[] name, PrimitiveFn func, int arity) {
    Value* v = interp.alloc_value();
    v.tag = PRIMITIVE;
    // Copy name into fixed buffer
    usz len = name.len;
    if (len > 31) {
        io::eprintfn("WARNING: primitive name truncated to 31 bytes");
        len = 31;
    }
    for (usz i = 0; i < len; i++) {
        v.prim_val.name[i] = name[i];
    }
    v.prim_val.name[len] = 0;
    v.prim_val.func = func;
    v.prim_val.arity = arity;
    return v;
}

fn Value* make_ffi_handle(Interp* interp, void* handle, char[] name) {
    // Allocate in root_region so handle survives REPL line reclamation
    main::RegionHandle saved = interp.current_frame;
    interp.current_frame = interp.root_region;
    Value* v = interp.alloc_value();
    v.tag = FFI_HANDLE;
    v.ffi_val.lib_handle = handle;
    usz len = name.len;
    if (len > 255) {
        io::eprintfn("WARNING: FFI handle name truncated from %d to 255 bytes", name.len);
        len = 255;
    }
    for (usz i = 0; i < len; i++) {
        v.ffi_val.lib_name[i] = name[i];
    }
    v.ffi_val.lib_name[len] = 0;
    v.ffi_val.name_len = len;
    interp.current_frame = saved;
    return v;
}

fn Value* make_error(Interp* interp, char[] msg) {
    Value* v = interp.alloc_value();
    v.tag = ERROR;
    usz len = msg.len;
    if (len > MAX_STRING_LEN - 1) len = MAX_STRING_LEN - 1;
    for (usz i = 0; i < len; i++) {
        v.str_val.chars[i] = msg[i];
    }
    v.str_val.chars[len] = 0;
    v.str_val.len = len;
    return v;
}

// =============================================================================
// SECTION 3: VALUE PREDICATES
// =============================================================================

fn bool is_nil(Value* v) @inline {
    return v == null || v.tag == NIL;
}

fn bool is_int(Value* v) @inline {
    return v != null && v.tag == INT;
}

fn bool is_string(Value* v) @inline {
    return v != null && v.tag == STRING;
}

fn bool is_symbol(Value* v) @inline {
    return v != null && v.tag == SYMBOL;
}

fn bool is_cons(Value* v) @inline {
    return v != null && v.tag == CONS;
}

fn bool is_closure(Value* v) @inline {
    return v != null && v.tag == CLOSURE;
}

fn bool is_continuation(Value* v) @inline {
    return v != null && v.tag == CONTINUATION;
}

fn bool is_primitive(Value* v) @inline {
    return v != null && v.tag == PRIMITIVE;
}

fn bool is_error(Value* v) @inline {
    return v != null && v.tag == ERROR;
}

fn bool is_ffi_handle(Value* v) @inline {
    return v != null && v.tag == FFI_HANDLE;
}

fn bool is_list(Value* v) {
    while (v != null && v.tag == CONS) {
        v = v.cons_val.cdr;
    }
    return is_nil(v);
}

// =============================================================================
// SECTION 4: VALUE ACCESSORS
// =============================================================================

fn long get_int(Value* v) @inline {
    assert(is_int(v), "expected int");
    return v.int_val;
}

fn SymbolId get_symbol(Value* v) @inline {
    assert(is_symbol(v), "expected symbol");
    return v.sym_val;
}

fn Value* car(Value* v) @inline {
    assert(is_cons(v), "expected cons");
    return v.cons_val.car;
}

fn Value* cdr(Value* v) @inline {
    assert(is_cons(v), "expected cons");
    return v.cons_val.cdr;
}

// List helpers
fn Value* cadr(Value* v) @inline { return car(cdr(v)); }
fn Value* caddr(Value* v) @inline { return car(cdr(cdr(v))); }
fn Value* cadddr(Value* v) @inline { return car(cdr(cdr(cdr(v)))); }

fn usz list_length(Value* v) {
    usz len = 0;
    while (is_cons(v)) {
        len++;
        v = cdr(v);
    }
    return len;
}

// =============================================================================
// SECTION 5: ENVIRONMENT
// =============================================================================

// Maximum bindings per environment frame
const usz MAX_BINDINGS = 256;

/**
 * Binding — Single name-value binding.
 */
struct Binding {
    SymbolId name;
    Value*   value;
}

/**
 * Env — Linked list of environment frames.
 */
struct Env {
    Binding[MAX_BINDINGS] bindings;
    usz                   binding_count;
    Env*                  parent;
}

fn Env* make_env(Interp* interp, Env* parent) {
    Env* env = interp.alloc_env();
    env.binding_count = 0;
    env.parent = parent;
    return env;
}

fn void Env.define(Env* self, SymbolId name, Value* value) {
    // Check if binding already exists and update it
    for (usz i = 0; i < self.binding_count; i++) {
        if (self.bindings[i].name == name) {
            self.bindings[i].value = value;
            return;
        }
    }
    // Add new binding
    if (self.binding_count >= MAX_BINDINGS) {
        io::eprintfn("ERROR: environment binding limit exceeded (%d)", MAX_BINDINGS);
        return;
    }
    self.bindings[self.binding_count].name = name;
    self.bindings[self.binding_count].value = value;
    self.binding_count++;
}

fn Value* Env.lookup(Env* self, SymbolId name) {
    // Search current frame
    for (usz i = 0; i < self.binding_count; i++) {
        if (self.bindings[i].name == name) {
            return self.bindings[i].value;
        }
    }
    // Search parent
    if (self.parent != null) {
        return self.parent.lookup(name);
    }
    return null;  // Not found
}

fn bool Env.set(Env* self, SymbolId name, Value* value) {
    for (usz i = 0; i < self.binding_count; i++) {
        if (self.bindings[i].name == name) {
            self.bindings[i].value = value;
            return true;
        }
    }
    if (self.parent != null) return self.parent.set(name, value);
    return false;
}

fn Env* Env.extend(Env* self, Interp* interp, SymbolId name, Value* value) {
    Env* new_env = make_env(interp, self);
    new_env.define(name, value);
    return new_env;
}

// =============================================================================
// SECTION 6: EXPRESSION AST
// =============================================================================

/**
 * ExprTag — Type of expression.
 */
// Maximum path segments (e.g., person.address.city has 3)
const usz MAX_PATH_SEGMENTS = 8;

enum ExprTag : char {
    E_LIT,        // Literal value
    E_VAR,        // Variable reference
    E_LAMBDA,     // (lambda (x) body)
    E_APP,        // (f arg)
    E_IF,         // (if test then else)
    E_LET,        // (let ((x val)) body)
    E_DEFINE,     // (define name val)
    E_QUOTE,      // (quote datum)
    E_RESET,      // (reset body)
    E_SHIFT,      // (shift k body)
    E_PERFORM,    // (perform tag arg)
    E_HANDLE,     // (handle body ((tag k x) handler) ...)
    E_INDEX,      // arr.[0] - path index access
    E_PATH,       // point.x, person.address.city - field access
    E_MATCH,      // (match expr (pattern result) ...)
    E_AND,        // (and left right) - short-circuit boolean and
    E_OR,         // (or left right) - short-circuit boolean or
    E_CALL,       // (f a b c) - multi-arg call (not curried)
    E_BEGIN,      // (begin e1 e2 ... en) - sequence, returns last
    E_SET,        // (set! name value) - variable mutation
    E_QUASIQUOTE, // `template - quasiquote
    E_UNQUOTE,    // ,expr - unquote (inside quasiquote)
    E_UNQUOTE_SPLICING, // ,@expr - unquote-splicing (inside quasiquote)
    E_DEFMACRO,   // (defmacro name transformer) - macro definition
    E_MODULE,     // (module name (export ...) body...) - module definition
    E_IMPORT,     // (import name) - module import
}

struct ExprLit {
    Value* value;
}

struct ExprVar {
    SymbolId name;
}

struct ExprLambda {
    SymbolId param;           // First/only param (or unused if param_count==0)
    SymbolId[64] params;      // All fixed params
    usz      param_count;     // Number of fixed params
    bool     has_rest;        // True if variadic (.. rest)
    SymbolId rest_param;      // Rest parameter name
    Expr*    body;
}

struct ExprApp {
    Expr* func;
    Expr* arg;
}

struct ExprIf {
    Expr* test;
    Expr* then_branch;
    Expr* else_branch;
}

struct ExprLet {
    SymbolId name;
    Expr*    init;
    Expr*    body;
    bool     is_recursive;  // true for (let ^rec ...) recursive bindings
}

struct ExprDefine {
    SymbolId name;
    Expr*    value;
}

struct ExprQuote {
    Value* datum;
}

struct ExprReset {
    Expr* body;
}

struct ExprShift {
    SymbolId k_name;   // Name to bind continuation
    Expr*    body;
}

struct ExprPerform {
    SymbolId tag;
    Expr*    arg;
}

// Maximum effect clauses per handle expression
const usz MAX_EFFECT_CLAUSES = 8;

struct EffectClause {
    SymbolId effect_tag;    // Effect name
    SymbolId k_name;        // Continuation param
    SymbolId arg_name;      // Argument param
    Expr*    handler_body;
}

struct ExprHandle {
    Expr*                          body;
    EffectClause[MAX_EFFECT_CLAUSES] clauses;
    usz                            clause_count;
}

/**
 * ExprIndex — Index access expression: arr.[0], dict.['key]
 * OmniLisp uses dot-bracket notation for indexing.
 */
struct ExprIndex {
    Expr* collection;  // The thing being indexed
    Expr* index;       // The index expression
}

/**
 * ExprPath — Field access path: point.x, person.address.city
 */
struct ExprPath {
    SymbolId[MAX_PATH_SEGMENTS] segments;
    usz segment_count;
}

// =============================================================================
// SECTION 6.5: PATTERN MATCHING
// =============================================================================

// Maximum elements in a sequence pattern
const usz MAX_PATTERN_ELEMS = 16;
// Maximum match clauses
const usz MAX_MATCH_CLAUSES = 16;

/**
 * PatternTag — Type of pattern.
 */
enum PatternTag : char {
    PAT_WILDCARD,   // _ - matches anything, binds nothing
    PAT_VAR,        // x - matches anything, binds to x
    PAT_LIT,        // 42, "hello", 'sym - matches literal value
    PAT_CONS,       // (head . tail) - matches cons cell
    PAT_SEQ,        // [a b c], [head .. tail], [x y ..] - sequence pattern
    PAT_QUOTE,      // 'symbol or 'literal - matches quoted datum
}

/**
 * RestPosition — Where the rest pattern (..) appears in a sequence.
 */
enum RestPosition : char {
    REST_NONE,      // No rest: [a b c] - exact match
    REST_START,     // [.. last] - skip all but last
    REST_MIDDLE,    // [head .. tail] - first + rest
    REST_END,       // [x y ..] - first N, ignore rest
}

/**
 * Pattern — Pattern AST node.
 * Note: PatternSeq and PatternCons are embedded inside to avoid forward declaration issues.
 */
struct Pattern {
    PatternTag tag;

    union {
        SymbolId var_name;        // PAT_VAR
        Value* lit_value;         // PAT_LIT
        Value* quote_datum;       // PAT_QUOTE

        // PAT_CONS: cons pattern (head . tail)
        struct {
            Pattern* car_pat;
            Pattern* cdr_pat;
        }

        // PAT_SEQ: sequence pattern [a b c], [head .. tail], etc.
        struct {
            Pattern*[MAX_PATTERN_ELEMS] elements;
            usz elem_count;
            RestPosition rest_pos;
            SymbolId rest_binding;  // Name to bind rest to (if REST_MIDDLE)
        }
    }
}

/**
 * MatchClause — Single clause in a match expression.
 */
struct MatchClause {
    Pattern* pattern;
    Expr* result;
}

/**
 * ExprMatch — Pattern matching expression.
 * (match expr (pattern1 result1) (pattern2 result2) ...)
 */
struct ExprMatch {
    Expr* scrutinee;
    MatchClause[MAX_MATCH_CLAUSES] clauses;
    usz clause_count;
}

/**
 * ExprAnd — Short-circuit boolean and.
 * (and left right) - returns left if falsy, otherwise right
 */
struct ExprAnd {
    Expr* left;
    Expr* right;
}

/**
 * ExprOr — Short-circuit boolean or.
 * (or left right) - returns left if truthy, otherwise right
 */
struct ExprOr {
    Expr* left;
    Expr* right;
}

/**
 * ExprCall — Multi-argument function call (not curried).
 * (f a b c) - calls f with all arguments at once.
 */
struct ExprCall {
    Expr* func;
    Expr*[64] args;
    usz arg_count;
}

/**
 * ExprBegin — Sequence expression (begin e1 e2 ... en).
 * Evaluates all expressions, returns the last.
 */
struct ExprBegin {
    Expr*[64] exprs;
    usz expr_count;
}

/**
 * ExprSet — Variable mutation: (set! name value)
 */
struct ExprSet {
    SymbolId name;
    Expr*    value;
}

/**
 * ExprQuasiquote — Quasiquote template: `expr
 */
struct ExprQuasiquote {
    Expr* body;
}

/**
 * ExprUnquote — Unquote inside quasiquote: ,expr
 */
struct ExprUnquote {
    Expr* body;
}

/**
 * ExprUnquoteSplicing — Splice inside quasiquote: ,@expr
 */
struct ExprUnquoteSplicing {
    Expr* body;
}

/**
 * ExprDefineMacro — Pattern-based macro definition:
 * (define [macro] name (pattern template) ...)
 */
struct ExprDefineMacro {
    SymbolId name;
    MacroClause[8] clauses;
    usz clause_count;
}

/**
 * ExprModule — Module definition: (module name (export sym...) body...)
 */
struct ExprModule {
    SymbolId     name;
    SymbolId[64] exports;
    usz          export_count;
    Expr*[64]    body;
    usz          body_count;
}

/**
 * ExprImport — Module import: (import name) or (import "path")
 */
struct ExprImport {
    SymbolId name;        // module name (symbol)
    bool     has_path;    // true if importing by file path
    char[256] path;       // file path (if has_path)
    usz      path_len;
}

/**
 * Module — Loaded module record.
 */
struct Module {
    SymbolId     name;
    Env*         env;              // Module's internal environment
    SymbolId[128] exports;         // Exported symbol names
    usz          export_count;
    bool         loaded;           // false during loading (circular import detection)
    char[256]    path;             // Source file path
    usz          path_len;
}

/**
 * Expr — Expression AST node.
 */
struct Expr {
    ExprTag tag;
    usz     loc_line;     // Source line (1-indexed, 0 if unknown)
    usz     loc_column;   // Source column (1-indexed, 0 if unknown)

    union {
        ExprLit     lit;
        ExprVar     var_expr;
        ExprLambda  lambda;
        ExprApp     app;
        ExprIf      if_expr;
        ExprLet     let_expr;
        ExprDefine  define;
        ExprQuote   quote;
        ExprReset   reset;
        ExprShift   shift;
        ExprPerform perform;
        ExprHandle  handle;
        ExprIndex   index;
        ExprPath    path;
        ExprMatch   match;
        ExprAnd     and_expr;
        ExprOr      or_expr;
        ExprCall    call;
        ExprBegin   begin;
        ExprSet     set_expr;
        ExprQuasiquote   quasiquote;
        ExprUnquote      unquote;
        ExprUnquoteSplicing unquote_splicing;
        ExprDefineMacro  define_macro;
        ExprModule       module_expr;
        ExprImport       import_expr;
    }
}

// =============================================================================
// SECTION 7: EFFECT HANDLERS
// =============================================================================

/**
 * EffectHandler - Active effect handler in the handler stack.
 */
struct EffectHandler {
    SymbolId[MAX_EFFECT_CLAUSES] tags;
    EffectClause[MAX_EFFECT_CLAUSES] clauses;
    usz clause_count;
    Env* handler_env;
    Expr* body_expr;    // The handle body for continuation capture
    Env*  body_env;     // Environment at handle time
}

/**
 * CapturedCont - Captured continuation state for replay-based continuations.
 * Stores the reset/handle body and environment so that calling (k value)
 * re-evaluates the body with the shift/perform replaced by value.
 */
struct CapturedCont {
    Expr*  reset_body;    // The reset or handle body to re-evaluate
    Env*   reset_env;     // Environment at reset/handle time
    bool   is_effect;     // true for effect handler continuations
    SymbolId effect_tag;  // For effect continuations: which effect tag
    // Copy of handler to reinstall during effect replay
    EffectHandler handler_copy;
    bool has_handler;     // whether handler_copy is valid
    // Multi-perform tracking
    usz perform_index;
    Value*[16] prior_results;
    usz prior_count;
    // Multi-shift tracking
    usz shift_index;
    Value*[16] shift_prior_results;
    usz shift_prior_count;
}

// =============================================================================
// SECTION 8: INTERPRETER STATE
// =============================================================================

/**
 * Interp — Interpreter state.
 */
struct Interp {
    SymbolTable  symbols;
    Env*         global_env;

    // Region-based allocation
    main::RegionHandle root_region;      // For Expr, Pattern, Continuation, CapturedCont, closure envs
    main::RegionHandle current_frame;    // For Values, Envs during eval

    // Pre-interned common symbols
    SymbolId sym_lambda;
    SymbolId sym_if;
    SymbolId sym_let;
    SymbolId sym_define;
    SymbolId sym_quote;
    SymbolId sym_reset;
    SymbolId sym_shift;
    SymbolId sym_perform;
    SymbolId sym_handle;
    SymbolId sym_match;
    SymbolId sym_and;
    SymbolId sym_or;
    SymbolId sym_true;
    SymbolId sym_false;
    SymbolId sym_begin;
    SymbolId sym_set;
    SymbolId sym_quasiquote;
    SymbolId sym_unquote;
    SymbolId sym_unquote_splicing;
    SymbolId sym_defmacro;
    SymbolId sym_dotdot;    // ".." symbol for template splice detection
    SymbolId sym_macro;     // "macro" symbol for [macro] annotation

    // Stack overflow protection
    usz eval_depth;
    usz max_eval_depth;

    // Macro table
    MacroDef[MAX_MACROS] macro_table;
    usz macro_count;
    usz gensym_counter;
    usz[MACRO_HASH_SIZE] macro_hash_index;    // Hash slot -> macro_table index (usz.max = empty)

    // Module system
    Module[MAX_MODULES] modules;
    usz module_count;
    usz[MODULE_HASH_SIZE] module_hash_index;  // Hash slot -> modules index (usz.max = empty)
    SymbolId sym_module;
    SymbolId sym_import;
    SymbolId sym_export;

    // Continuation/shift state
    usz      reset_depth;
    bool     shift_occurred;
    usz      shift_target_depth;
    Value*   shift_result;

    // Effect handler stack
    EffectHandler[16] handler_stack;
    usz               handler_count;

    // Effect handler result propagation (like shift_occurred for effects)
    bool     effect_occurred;       // Handler body was evaluated, result should bypass to handle
    Value*   effect_result;         // Result of the handler body

    // Continuation replay state
    Value*   cont_substitution;     // Value to substitute during replay
    bool     cont_substituting;     // Are we in replay mode?
    bool     cont_is_effect;        // Is this an effect continuation replay?
    SymbolId cont_effect_tag;       // Effect tag for effect replay
    // Multi-perform replay tracking
    usz    effect_perform_counter;
    Value*[16] effect_prior_results;
    usz    cont_target_perform_index;
    // Multi-shift replay tracking
    usz    shift_counter;
    Value*[16] shift_prior_results;
    usz    cont_target_shift_index;

    // Reset body/env stack (for continuation capture)
    Expr*[16] reset_body_stack;
    Env*[16]  reset_env_stack;

}

fn void Interp.init(Interp* self) {
    self.symbols.init();
    self.global_env = null;

    // Region setup (thread_registry_init must be called before this)
    self.root_region = main::thread_root_region();
    self.current_frame = self.root_region;

    // Continuation/shift state
    self.reset_depth = 0;
    self.shift_occurred = false;
    self.shift_target_depth = 0;
    self.shift_result = null;
    self.handler_count = 0;
    self.effect_occurred = false;
    self.effect_result = null;

    // Continuation replay state
    self.cont_substitution = null;
    self.cont_substituting = false;
    self.cont_is_effect = false;
    // Multi-perform replay tracking
    self.effect_perform_counter = 0;
    self.cont_target_perform_index = 0;
    // Multi-shift replay tracking
    self.shift_counter = 0;
    self.cont_target_shift_index = 0;

    // Pre-intern keywords
    self.sym_lambda  = self.symbols.intern("lambda");
    self.sym_if      = self.symbols.intern("if");
    self.sym_let     = self.symbols.intern("let");
    self.sym_define  = self.symbols.intern("define");
    self.sym_quote   = self.symbols.intern("quote");
    self.sym_reset   = self.symbols.intern("reset");
    self.sym_shift   = self.symbols.intern("shift");
    self.sym_perform = self.symbols.intern("perform");
    self.sym_handle  = self.symbols.intern("handle");
    self.sym_match   = self.symbols.intern("match");
    self.sym_and     = self.symbols.intern("and");
    self.sym_or      = self.symbols.intern("or");
    self.sym_true    = self.symbols.intern("true");
    self.sym_false   = self.symbols.intern("false");
    self.sym_begin   = self.symbols.intern("begin");
    self.sym_set     = self.symbols.intern("set!");
    self.sym_quasiquote = self.symbols.intern("quasiquote");
    self.sym_unquote = self.symbols.intern("unquote");
    self.sym_unquote_splicing = self.symbols.intern("unquote-splicing");
    self.sym_defmacro = self.symbols.intern("defmacro");
    self.sym_dotdot = self.symbols.intern("..");
    self.sym_macro = self.symbols.intern("macro");

    // Module system
    self.sym_module = self.symbols.intern("module");
    self.sym_import = self.symbols.intern("import");
    self.sym_export = self.symbols.intern("export");
    self.module_count = 0;

    // Stack overflow protection
    self.eval_depth = 0;
    self.max_eval_depth = 200;

    // Macro table
    self.macro_count = 0;
    self.gensym_counter = 0;
    for (usz i = 0; i < MACRO_HASH_SIZE; i++) self.macro_hash_index[i] = usz.max;
    for (usz i = 0; i < MODULE_HASH_SIZE; i++) self.module_hash_index[i] = usz.max;

    // Create global environment
    self.global_env = make_env(self, null);
}

fn Continuation* Interp.alloc_lisp_continuation(Interp* self) {
    Continuation k;
    k.data = null;
    main::ObjectHandle h = main::allocate_in(self.root_region, Continuation, k);
    return main::dereference_as(Continuation, h);
}

fn Value* Interp.alloc_value(Interp* self) {
    Value v;
    main::ObjectHandle h = main::allocate_in(self.current_frame, Value, v);
    return main::dereference_as(Value, h);
}

fn Env* Interp.alloc_env(Interp* self) {
    Env e;
    e.binding_count = 0;
    e.parent = null;
    main::ObjectHandle h = main::allocate_in(self.current_frame, Env, e);
    return main::dereference_as(Env, h);
}

fn Expr* Interp.alloc_expr(Interp* self) {
    Expr e;
    main::ObjectHandle h = main::allocate_in(self.root_region, Expr, e);
    return main::dereference_as(Expr, h);
}

fn Pattern* Interp.alloc_pattern(Interp* self) {
    Pattern p;
    main::ObjectHandle h = main::allocate_in(self.root_region, Pattern, p);
    return main::dereference_as(Pattern, h);
}

// =============================================================================
// SECTION 8: VALUE PRINTING
// =============================================================================

fn void print_value(Value* v, SymbolTable* syms) {
    if (v == null || v.tag == NIL) {
        io::print("nil");
        return;
    }

    switch (v.tag) {
        case INT:
            io::printf("%d", v.int_val);
        case STRING:
            io::printf("\"%s\"", (ZString)&v.str_val.chars);
        case SYMBOL:
            io::printf("%s", syms.get_name(v.sym_val));
        case CONS:
            io::print("(");
            print_value(v.cons_val.car, syms);
            Value* rest = v.cons_val.cdr;
            while (is_cons(rest)) {
                io::print(" ");
                print_value(rest.cons_val.car, syms);
                rest = rest.cons_val.cdr;
            }
            if (!is_nil(rest)) {
                io::print(" . ");
                print_value(rest, syms);
            }
            io::print(")");
        case CLOSURE:
            io::print("#<closure>");
        case CONTINUATION:
            io::print("#<continuation>");
        case PRIMITIVE:
            io::printf("#<primitive %s>", (ZString)&v.prim_val.name);
        case PARTIAL_PRIM:
            io::print("#<partial>");
        case ERROR:
            io::printf("#<error: %s>", (ZString)&v.str_val.chars);
        case HASHMAP:
            io::printf("#<hashmap:%d>", v.hashmap_val.count);
        case FFI_HANDLE:
            io::printf("#<ffi-handle:%s>", (ZString)&v.ffi_val.lib_name);
        default:
            io::print("#<unknown>");
    }
}

fn void println_value(Value* v, SymbolTable* syms) {
    print_value(v, syms);
    io::printn("");
}
