module lisp;

import std::collections::list;
import std::io;
import main;

// ============================================================
// Fault types — structured error categories for C3 optionals
// Accessed as lisp::FAULT_NAME (e.g., lisp::UNBOUND_VARIABLE)
// ============================================================

faultdef
    // Parse errors
    UNTERMINATED_STRING,
    UNEXPECTED_TOKEN,
    NESTING_TOO_DEEP,
    UNKNOWN_ATTRIBUTE,
    INVALID_SYNTAX,
    // Eval errors
    UNBOUND_VARIABLE,
    TYPE_MISMATCH,
    ARITY_MISMATCH,
    DIVISION_BY_ZERO,
    STACK_OVERFLOW,
    NOT_A_FUNCTION,
    INDEX_OUT_OF_BOUNDS,
    UNHANDLED_EFFECT,
    // I/O errors
    CONNECTION_REFUSED,
    DNS_FAILED,
    TLS_HANDSHAKE_FAILED,
    READ_FAILED,
    WRITE_FAILED,
    // Type errors
    EXPECTED_STRING,
    EXPECTED_INT,
    EXPECTED_LIST,
    EXPECTED_ARRAY,
    EXPECTED_DICT,
    EXPECTED_CLOSURE,
    EXPECTED_SYMBOL;

// Compile-time debug flag: set to false for release builds (zero-cost elimination).
const bool DEBUG_BUILD = true;

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
    DOUBLE,         // Floating-point number
    HASHMAP,        // Hash map (mutable dictionary)
    FFI_HANDLE,     // FFI library handle (dlopen result)
    ARRAY,          // Mutable array (dynamic array)
    TYPE_INFO,      // Type descriptor value
    INSTANCE,       // User-defined type instance
    METHOD_TABLE,   // Dispatch table for multiple dispatch
    MODULE,         // First-class module reference
    ITERATOR,       // Lazy iterator (backed by closure thunk)
    COROUTINE,          // User-facing coroutine (wraps StackCtx*)
}

/**
 * Symbol — Interned string identifier.
 */
typedef SymbolId = uint;  // Already distinct (non-inline typedef = no implicit conversion)

// Initial capacities for dynamic tables
const usz MACRO_INITIAL_CAPACITY = 32;
const usz MODULE_INITIAL_CAPACITY = 16;
const usz HANDLER_INITIAL_CAPACITY = 16;
// Invalid symbol ID sentinel (returned on exhaustion; must not alias any valid index)
const SymbolId INVALID_SYMBOL_ID = (SymbolId)0xFFFFFFFF;

// Initial capacities for dynamic symbol table
const usz SYMBOL_INITIAL_CAPACITY = 256;
const usz SYMBOL_HASH_INITIAL_CAPACITY = 512;

// Type system constants
typedef TypeId = uint;
const TypeId INVALID_TYPE_ID = (TypeId)0xFFFFFFFF;
const usz TYPE_INITIAL_CAPACITY = 64;
const usz MAX_TYPE_PARAMS = 32;
const usz MAX_META_ENTRIES = 16;
const usz MAX_TYPE_FIELDS = 64;
/**
 * SymbolEntry — Storage for a single interned symbol name.
 */
struct SymbolEntry {
    char* name;   // heap-allocated, null-terminated
    usz len;
}

/**
 * SymbolTable — Table for interning symbol names (dynamic, growable).
 */
struct SymbolTable {
    SymbolEntry* entries;
    usz count;
    usz capacity;           // entries array capacity
    SymbolId* hash_index;   // Maps hash slot → symbol index (INVALID_SYMBOL_ID = empty)
    usz hash_capacity;      // hash_index array capacity (power of 2)
}

fn void SymbolTable.init(SymbolTable* self) {
    self.count = 0;
    self.capacity = SYMBOL_INITIAL_CAPACITY;
    self.hash_capacity = SYMBOL_HASH_INITIAL_CAPACITY;
    self.entries = (SymbolEntry*)mem::malloc(SymbolEntry.sizeof * self.capacity);
    self.hash_index = (SymbolId*)mem::malloc(SymbolId.sizeof * self.hash_capacity);
    for (usz i = 0; i < self.hash_capacity; i++) {
        self.hash_index[i] = INVALID_SYMBOL_ID;
    }
}

fn void SymbolTable.destroy(SymbolTable* self) {
    for (usz i = 0; i < self.count; i++) {
        if (self.entries[i].name != null) {
            mem::free(self.entries[i].name);
        }
    }
    if (self.entries != null) {
        mem::free(self.entries);
        self.entries = null;
    }
    if (self.hash_index != null) {
        mem::free(self.hash_index);
        self.hash_index = null;
    }
}

fn void SymbolTable.grow(SymbolTable* self) {
    // Double entries capacity
    usz new_cap = self.capacity * 2;
    SymbolEntry* new_entries = (SymbolEntry*)mem::malloc(SymbolEntry.sizeof * new_cap);
    for (usz i = 0; i < self.count; i++) {
        new_entries[i] = self.entries[i];
    }
    mem::free(self.entries);
    self.entries = new_entries;
    self.capacity = new_cap;

    // Double hash capacity and rebuild
    usz new_hash_cap = self.hash_capacity * 2;
    mem::free(self.hash_index);
    self.hash_index = (SymbolId*)mem::malloc(SymbolId.sizeof * new_hash_cap);
    self.hash_capacity = new_hash_cap;
    for (usz i = 0; i < new_hash_cap; i++) {
        self.hash_index[i] = INVALID_SYMBOL_ID;
    }
    // Re-insert all entries into new hash table
    usz mask = new_hash_cap - 1;
    for (usz i = 0; i < self.count; i++) {
        usz h = hash_symbol(self.entries[i].name[:self.entries[i].len]);
        for (usz probe = 0; probe < new_hash_cap; probe++) {
            usz slot = (h + probe) & mask;
            if (self.hash_index[slot] == INVALID_SYMBOL_ID) {
                self.hash_index[slot] = (SymbolId)i;
                break;
            }
        }
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
    usz mask = self.hash_capacity - 1;

    // Hash table lookup with linear probing
    for (usz probe = 0; probe < self.hash_capacity; probe++) {
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

    // Grow at 70% load factor
    if (self.count * 10 >= self.capacity * 7) {
        self.grow();
        mask = self.hash_capacity - 1;
        h = hash_symbol(name);  // recompute with new mask
    }

    // Insert new symbol
    SymbolId id = (SymbolId)self.count;
    usz len = name.len;
    // Heap-allocate symbol name
    char* heap_name = (char*)mem::malloc(len + 1);
    for (usz i = 0; i < len; i++) {
        heap_name[i] = name[i];
    }
    heap_name[len] = 0;
    self.entries[self.count].name = heap_name;
    self.entries[self.count].len = len;
    self.count++;

    // Insert into hash table
    for (usz probe = 0; probe < self.hash_capacity; probe++) {
        usz slot = (h + probe) & mask;
        if (self.hash_index[slot] == INVALID_SYMBOL_ID) {
            self.hash_index[slot] = id;
            break;
        }
    }

    return id;
}

<* @require (usz)id < self.count : "invalid symbol id" *>
fn char[] SymbolTable.get_name(SymbolTable* self, SymbolId id) {
    usz idx = (usz)id;
    return self.entries[idx].name[:(usz)self.entries[idx].len];
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
    uint      scope_gen;      // Scope generation stamp for O(1) membership check
    main::ScopeRegion* env_scope; // Standalone scope holding env copy (null if root-scope closure)
    SymbolId  param;          // Parameter name (first/only param, or unused if param_count==0)
    SymbolId* params;         // All fixed params (including first), bump-allocated
    usz       param_count;    // Number of fixed params
    bool      has_rest;       // True if variadic (.. rest)
    SymbolId  rest_param;     // Rest parameter name (if has_rest)
    Expr*     body;           // Body expression
    Env*      env;            // Captured environment
    bool      has_param;      // false for zero-arg lambdas: (lambda () body)
    bool      has_typed_params;  // true if any param has type annotations
    MethodSignature* type_sig;   // bump-allocated type signature (null if no types)
    SymbolId  name;           // Bound name (set by define), 0 if anonymous
}

/**
 * Continuation — Delimited continuation backed by a coroutine (StackCtx).
 *
 * The ctx field points to a suspended StackCtx. Multi-shot continuations
 * clone the context on each invocation.
 */
struct Continuation {
    StackCtx* ctx;            // Suspended stack context backing this continuation
    bool  is_coroutine_based; // Always true (kept for resolve dispatch)
    void* handle_state;   // HandleEffectState* (for resolve to reinstall handler)
}

/**
 * Primitive — Built-in function.
 */
alias PrimitiveFn = fn Value*(Value*[] args, Env* env, Interp* interp);

struct Primitive {
    char[64] name;
    PrimitiveFn func;
    int arity;           // -1 for variadic
    int tag;             // Extra data (e.g., TypeId for constructors)
    void* user_data;     // Optional data pointer for closure-like primitives
}

/**
 * PartialPrim — Partially applied primitive (curried).
 */
struct PartialPrim {
    PrimitiveFn func;
    Value*      first_arg;   // First argument stored
    Value*      second_arg;  // Second argument (for 3+ arity primitives)
    int         remaining;   // Arguments still needed
}

/**
 * StringVal — String value storage (heap-allocated, dynamic).
 */
struct StringVal {
    char* chars;    // heap-allocated, null-terminated
    usz len;
    usz capacity;   // allocated size
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
    MacroClause[32] clauses;
    usz clause_count;
    CapturedBinding[64] captured_bindings;  // Snapshot of def-time bindings for hygiene
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
 * FastPathEntry — Maps an effect tag to its default (unhandled) primitive.
 * Used by jit_signal_impl to dispatch effects when no handler is installed.
 */
struct FastPathEntry {
    SymbolId tag;        // effect tag (e.g., sym for "io/print")
    Value*   prim;       // raw primitive (e.g., __raw-print)
}

/**
 * Array — Mutable dynamic array. Heap-allocated like HashMap.
 */
struct Array {
    Value** items;     // malloc'd array of Value pointers
    usz length;
    usz capacity;
}

// =============================================================================
// SECTION 1.5: TYPE SYSTEM TYPES
// =============================================================================

/**
 * MetaEntry — Single key-value pair in a metadata dictionary annotation.
 * Keys are quoted symbols, values are either symbol (type bound) or integer.
 */
struct MetaEntry {
    SymbolId key;          // quoted symbol key ('T, 'with, etc.)
    SymbolId sym_value;    // symbol value (Number, Comparable)
    long int_value;        // integer value (arity: 1, 2)
    bool is_int;           // true if value is integer, false if symbol
}

/**
 * TypeAnnotation — Parsed ^Type annotation on a parameter.
 * Three forms: ^Int (simple), ^(List Int) (compound), ^{'T Number} (dict)
 */
struct TypeAnnotation {
    bool has_annotation;
    bool is_compound;          // ^(List Int) form
    bool is_dict;              // ^{...} form
    SymbolId base_type;        // Int, List, Lambda, Val, etc.
    SymbolId[MAX_TYPE_PARAMS] params;  // compound type params
    usz param_count;
    MetaEntry[MAX_META_ENTRIES] meta;  // dict entries
    usz meta_count;
    long val_literal;          // For ^(Val 42) — the literal value
    bool has_val_literal;      // true if this is a Val type with literal
}

/**
 * TypeKind — Kind of user-defined type.
 */
enum TypeKind : char { TK_CONCRETE, TK_ABSTRACT, TK_UNION, TK_ALIAS, TK_BUILTIN, TK_EFFECT }

/**
 * TypeFieldInfo — Field descriptor in a concrete type.
 */
struct TypeFieldInfo { SymbolId name; TypeId field_type; SymbolId annotation_sym; }

/**
 * UnionVariant — Single variant in a union type.
 */
struct UnionVariant {
    SymbolId name;
    SymbolId[MAX_TYPE_PARAMS] fields;
    usz field_count;
}

/**
 * TypeInfo — Complete type descriptor stored in the registry.
 */
struct TypeInfo {
    SymbolId name;
    TypeKind kind;
    TypeId parent;           // INVALID_TYPE_ID if no parent
    TypeFieldInfo[MAX_TYPE_FIELDS] fields;
    usz field_count;
    SymbolId[MAX_TYPE_PARAMS] type_params;
    usz type_param_count;
    MethodConstraint[MAX_TYPE_PARAMS] constraints;  // Parametric type constraints (e.g., T <: Number)
    usz constraint_count;
    UnionVariant[64] variants;  // for TK_UNION
    usz variant_count;
    TypeId alias_target;     // for TK_ALIAS
}

/**
 * TypeRegistry — Registry of all types (builtin + user-defined).
 */
struct TypeRegistry {
    TypeInfo* types;
    usz type_count;
    usz capacity;
    TypeId* type_hash_index;
    usz hash_capacity;
}

fn void TypeRegistry.init(TypeRegistry* self) {
    self.capacity = TYPE_INITIAL_CAPACITY;
    self.type_count = 0;
    self.types = (TypeInfo*)mem::malloc(TypeInfo.sizeof * self.capacity);
    self.hash_capacity = self.capacity * 2;
    self.type_hash_index = (TypeId*)mem::malloc(TypeId.sizeof * self.hash_capacity);
    for (usz i = 0; i < self.hash_capacity; i++) {
        self.type_hash_index[i] = INVALID_TYPE_ID;
    }
}

fn void TypeRegistry.grow(TypeRegistry* self, SymbolTable* syms) {
    usz new_cap = self.capacity * 2;
    TypeInfo* new_types = (TypeInfo*)mem::malloc(TypeInfo.sizeof * new_cap);
    for (usz i = 0; i < self.type_count; i++) new_types[i] = self.types[i];
    mem::free(self.types);
    self.types = new_types;
    self.capacity = new_cap;

    // Rebuild hash table at 2x new capacity
    usz new_hash_cap = new_cap * 2;
    TypeId* new_hash = (TypeId*)mem::malloc(TypeId.sizeof * new_hash_cap);
    for (usz i = 0; i < new_hash_cap; i++) new_hash[i] = INVALID_TYPE_ID;
    mem::free(self.type_hash_index);
    self.type_hash_index = new_hash;
    self.hash_capacity = new_hash_cap;

    usz mask = new_hash_cap - 1;
    for (usz i = 0; i < self.type_count; i++) {
        char[] name_str = syms.get_name(self.types[i].name);
        usz h = hash_symbol(name_str);
        for (usz probe = 0; probe < new_hash_cap; probe++) {
            usz slot = (h + probe) & mask;
            if (new_hash[slot] == INVALID_TYPE_ID) {
                new_hash[slot] = (TypeId)i;
                break;
            }
        }
    }
}

fn void TypeRegistry.destroy(TypeRegistry* self) {
    if (self.types != null) { mem::free(self.types); self.types = null; }
    if (self.type_hash_index != null) { mem::free(self.type_hash_index); self.type_hash_index = null; }
}

fn TypeId TypeRegistry.register_type(TypeRegistry* self, TypeInfo info, SymbolTable* syms) {
    if (self.type_count >= self.capacity) {
        self.grow(syms);
    }
    TypeId id = (TypeId)self.type_count;
    self.types[self.type_count] = info;
    self.type_count++;

    // Insert into hash table
    char[] name = syms.get_name(info.name);
    usz h = hash_symbol(name);
    usz mask = self.hash_capacity - 1;
    for (usz probe = 0; probe < self.hash_capacity; probe++) {
        usz slot = (h + probe) & mask;
        if (self.type_hash_index[slot] == INVALID_TYPE_ID) {
            self.type_hash_index[slot] = id;
            break;
        }
    }
    return id;
}

fn TypeId TypeRegistry.lookup(TypeRegistry* self, SymbolId name, SymbolTable* syms) {
    char[] name_str = syms.get_name(name);
    usz h = hash_symbol(name_str);
    usz mask = self.hash_capacity - 1;
    for (usz probe = 0; probe < self.hash_capacity; probe++) {
        usz slot = (h + probe) & mask;
        TypeId tid = self.type_hash_index[slot];
        if (tid == INVALID_TYPE_ID) return INVALID_TYPE_ID;
        if (self.types[(usz)tid].name == name) return tid;
    }
    return INVALID_TYPE_ID;
}

fn TypeInfo* TypeRegistry.get(TypeRegistry* self, TypeId id) {
    if (id == INVALID_TYPE_ID || (usz)id >= self.type_count) return null;
    return &self.types[(usz)id];
}

fn bool TypeRegistry.is_subtype(TypeRegistry* self, TypeId child, TypeId parent) {
    if (child == parent) return true;
    // Walk parent chain (depth limit 16)
    TypeId current = child;
    for (usz depth = 0; depth < 16; depth++) {
        TypeInfo* info = self.get(current);
        if (info == null) return false;
        if (info.parent == INVALID_TYPE_ID) return false;
        if (info.parent == parent) return true;
        current = info.parent;
    }
    return false;
}

/**
 * Instance — User-defined type instance (heap-allocated like HashMap/Array).
 */
struct Instance {
    TypeId type_id;
    Value*[MAX_TYPE_FIELDS] fields;
    usz field_count;
    TypeId[MAX_TYPE_PARAMS] type_args;   // Inferred type arguments (e.g., T=Int for Box<Int>)
    usz type_arg_count;
}

/**
 * MethodConstraint — Type bound on a type parameter (e.g., T <: Number).
 */
struct MethodConstraint {
    SymbolId param_sym;   // The type parameter symbol (e.g., 'T)
    TypeId bound_type;    // The upper bound TypeId (e.g., Number)
}

/**
 * MethodSignature — Type signature for dispatch matching.
 */
struct MethodSignature {
    TypeId[32] param_types;     // INVALID_TYPE_ID = Any
    long[32] val_literals;      // For Val types
    bool[32] has_val_literal;
    usz param_count;
    MethodConstraint[MAX_TYPE_PARAMS] constraints;  // Type param constraints
    usz constraint_count;
}

/**
 * MethodEntry — Single dispatch entry in a method table.
 */
struct MethodEntry {
    MethodSignature sig;
    Value* implementation;  // Closure or Primitive
}

/**
 * MethodTable — Dispatch table for multiple dispatch (heap-allocated).
 */
const usz METHOD_INITIAL_CAPACITY = 8;

struct MethodTable {
    SymbolId name;
    MethodEntry* entries;
    usz entry_count;
    usz capacity;
    Value* fallback;  // untyped catch-all
}

/**
 * Value — Tagged union for all Lisp values.
 */
struct Value {
    ValueTag tag;
    uint scope_gen;  // Scope generation stamp — set at allocation, used for O(1) scope checks

    union {
        long          int_val;
        double        double_val;     // Floating-point value
        StringVal*    str_val;        // String value (heap-allocated)
        SymbolId      sym_val;
        Cons          cons_val;
        Closure*      closure_val;    // Closure (heap-allocated)
        Continuation* cont_val;      // From our continuation system
        Primitive*    prim_val;       // Primitive (heap-allocated)
        PartialPrim   partial_val;   // Curried primitive
        HashMap*      hashmap_val;   // Hash map
        FfiHandle*    ffi_val;       // FFI library handle (heap-allocated)
        Array*        array_val;     // Mutable array
        TypeInfo*     type_info_val; // Type descriptor
        Instance*     instance_val;  // User-defined type instance
        MethodTable*  method_table_val; // Dispatch table
        Module*       module_val;       // First-class module
        Value*        iterator_val;     // Iterator thunk closure (V_CLOSURE Value*)
        main::StackCtx*   coroutine_val;        // User-facing coroutine (StackCtx*)
    }
}

// =============================================================================
// SECTION 2: VALUE CONSTRUCTORS
// =============================================================================

// Global stack context pool pointer for scope_dtor_value — set by Interp.init()
main::StackPool* g_stack_ctx_pool = null;

// Scope destructor for Values with heap-allocated backing data.
// Mirrors destroy_value in eval.c3 but called from scope_region lifecycle.
fn void scope_dtor_value(void* ptr) {
    // Dtors re-enabled after fixing use-after-free (parser literals, module env)
    Value* v = (Value*)ptr;
    switch (v.tag) {
        case STRING:
        case ERROR:
            if (v.str_val != null) {
                if (v.str_val.chars != null) mem::free(v.str_val.chars);
                mem::free(v.str_val);
                v.str_val = null;
            }
        case CLOSURE:
            // Handled by scope_dtor_closure now
            break;
        case PRIMITIVE:
            if (v.prim_val != null) {
                mem::free(v.prim_val);
                v.prim_val = null;
            }
        case ARRAY:
            if (v.array_val != null) {
                if (v.array_val.items != null) mem::free(v.array_val.items);
                mem::free(v.array_val);
                v.array_val = null;
            }
        case INSTANCE:
            if (v.instance_val != null) {
                mem::free(v.instance_val);
                v.instance_val = null;
            }
        case METHOD_TABLE:
            if (v.method_table_val != null) {
                if (v.method_table_val.entries != null) mem::free(v.method_table_val.entries);
                mem::free(v.method_table_val);
                v.method_table_val = null;
            }
        case HASHMAP:
            if (v.hashmap_val != null) {
                if (v.hashmap_val.entries != null) mem::free(v.hashmap_val.entries);
                mem::free(v.hashmap_val);
                v.hashmap_val = null;
            }
        case FFI_HANDLE:
            if (v.ffi_val != null) {
                // Note: don't dlclose here — FFI handles are long-lived
                mem::free(v.ffi_val);
                v.ffi_val = null;
            }
        // COROUTINE and CONTINUATION dtors deferred — lifecycle managed by coroutine primitives.
        // See plan Phase 4c step 3 for details.
        default: {}
    }
}

// Scope destructor for Env objects — frees malloc'd bindings and hash table.
fn void scope_dtor_closure(void* ptr) {
    Value* v = (Value*)ptr;
    if (v.closure_val != null && v.closure_val.env_scope != null) {
        main::scope_release(v.closure_val.env_scope);
        v.closure_val.env_scope = null;
    }
}

fn void scope_dtor_env(void* ptr) {
    Env* env = (Env*)ptr;
    if (!env.is_inline && env.bindings != null) {
        mem::free(env.bindings);
        env.bindings = null;
    }
    if (env.hash_table != null) {
        mem::free(env.hash_table);
        env.hash_table = null;
    }
}

<* @ensure return != null *>
fn Value* make_nil(Interp* interp) @inline {
    Value* v = interp.alloc_value();
    v.tag = NIL;
    return v;
}

<* @ensure return != null *>
fn Value* make_int(Interp* interp, long n) @inline {
    Value* v = interp.alloc_value();
    v.tag = INT;
    v.int_val = n;
    return v;
}

<* @ensure return != null *>
fn Value* make_double(Interp* interp, double d) @inline {
    Value* v = interp.alloc_value();
    v.tag = DOUBLE;
    v.double_val = d;
    return v;
}

<* @ensure return != null *>
fn Value* make_string(Interp* interp, char[] str) {
    Value* v = interp.alloc_value();
    v.tag = STRING;
    main::scope_register_dtor(interp.current_scope, (void*)v, &scope_dtor_value);
    v.str_val = (StringVal*)mem::malloc(StringVal.sizeof);
    usz len = str.len;
    usz cap = len + 1;
    if (cap < 32) cap = 32;  // minimum allocation
    v.str_val.chars = (char*)mem::malloc(cap);
    v.str_val.capacity = cap;
    for (usz i = 0; i < len; i++) {
        v.str_val.chars[i] = str[i];
    }
    v.str_val.chars[len] = 0;
    v.str_val.len = len;

    return v;
}

<* @ensure return != null *>
fn Value* make_symbol(Interp* interp, SymbolId sym) @inline {
    Value* v = interp.alloc_value();
    v.tag = SYMBOL;
    v.sym_val = sym;
    return v;
}

/**
 * Allocate a cons cell in escape_scope (result_scope).
 * Promotes car/cdr from call_scope to escape_scope if needed.
 * Called when accumulator pattern detected (cdr is NIL or already in escape_scope).
 */
fn Value* make_cons_escape(Interp* interp, Value* car, Value* cdr) {
    main::ScopeRegion* call_scope = interp.current_scope;

    // Switch to escape_scope for allocation
    interp.current_scope = interp.escape_scope;

    // Promote car/cdr: copy from call_scope to escape_scope if needed.
    main::ScopeRegion* saved_releasing = interp.releasing_scope;
    interp.releasing_scope = call_scope;
    Value* safe_car = copy_to_parent(car, interp);
    Value* safe_cdr = copy_to_parent(cdr, interp);
    interp.releasing_scope = saved_releasing;

    // Allocate cons in escape_scope
    Value* v = interp.alloc_value();
    v.tag = CONS;
    v.cons_val.car = safe_car;
    v.cons_val.cdr = safe_cdr;

    // Restore call_scope
    interp.current_scope = call_scope;
    return v;
}

<* @ensure return != null *>
fn Value* make_cons(Interp* interp, Value* car, Value* cdr) @inline {
    // Escape-scope optimization: detect accumulator pattern (cons x acc)
    // where acc starts as NIL and grows by consing onto the front.
    // Trigger: cdr is NIL (start of accumulator) or cdr stamped with escape_scope's generation.
    if (interp.escape_scope != null &&
        interp.current_scope != interp.escape_scope &&
        (cdr.tag == NIL || cdr.scope_gen == interp.escape_scope.generation)) {
        return make_cons_escape(interp, car, cdr);
    }

    // Normal path: allocate in current_scope
    Value* v = interp.alloc_value();
    v.tag = CONS;
    v.cons_val.car = car;
    v.cons_val.cdr = cdr;
    return v;
}

fn Value* make_closure(Interp* interp, SymbolId param, Expr* body, Env* env) {
    Value* v = interp.alloc_value();
    v.tag = CLOSURE;
    main::scope_register_dtor(interp.current_scope, (void*)v, &scope_dtor_closure);
    v.closure_val = (Closure*)interp.current_scope.alloc(Closure.sizeof);
    v.closure_val.scope_gen = interp.current_scope.generation;
    v.closure_val.env_scope = null;
    v.closure_val.param = param;
    v.closure_val.params = (SymbolId*)interp.current_scope.alloc(SymbolId.sizeof * 1);
    v.closure_val.params[0] = param;
    v.closure_val.param_count = 1;
    v.closure_val.has_rest = false;
    v.closure_val.rest_param = 0;
    v.closure_val.body = body;
    v.closure_val.env = env;
    v.closure_val.has_param = true;
    v.closure_val.has_typed_params = false;
    v.closure_val.type_sig = null;
    return v;
}

fn Value* make_closure_no_param(Interp* interp, Expr* body, Env* env) {
    Value* v = interp.alloc_value();
    v.tag = CLOSURE;
    main::scope_register_dtor(interp.current_scope, (void*)v, &scope_dtor_closure);
    v.closure_val = (Closure*)interp.current_scope.alloc(Closure.sizeof);
    v.closure_val.scope_gen = interp.current_scope.generation;
    v.closure_val.env_scope = null;
    v.closure_val.param = 0;
    v.closure_val.params = null;
    v.closure_val.param_count = 0;
    v.closure_val.has_rest = false;
    v.closure_val.rest_param = 0;
    v.closure_val.body = body;
    v.closure_val.env = env;
    v.closure_val.has_param = false;
    v.closure_val.has_typed_params = false;
    v.closure_val.type_sig = null;

    return v;
}

fn Value* make_continuation(Interp* interp, Continuation* k) @inline {
    Value* v = interp.alloc_value();
    v.tag = CONTINUATION;
    // No dtor registered — lifecycle managed by effect handlers (reset/shift/handle).
    // Scope dtor would fire too early (before handler is done with the context).
    v.cont_val = k;
    return v;
}

fn Value* make_primitive(Interp* interp, char[] name, PrimitiveFn func, int arity) {
    // Allocate in root_scope — primitives are permanent (defined in global env)
    main::ScopeRegion* saved_scope = interp.current_scope;
    interp.current_scope = interp.root_scope;
    Value* v = interp.alloc_value();
    v.tag = PRIMITIVE;
    main::scope_register_dtor(interp.root_scope, (void*)v, &scope_dtor_value);
    v.prim_val = (Primitive*)mem::malloc(Primitive.sizeof);
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
    v.prim_val.tag = 0;
    v.prim_val.user_data = null;
    interp.current_scope = saved_scope;

    return v;
}

fn Value* make_ffi_handle(Interp* interp, void* handle, char[] name) {
    // Allocate in root_scope so handle survives REPL line reclamation
    main::ScopeRegion* saved_scope = interp.current_scope;
    interp.current_scope = interp.root_scope;
    Value* v = interp.alloc_value();
    v.tag = FFI_HANDLE;
    main::scope_register_dtor(interp.root_scope, (void*)v, &scope_dtor_value);
    v.ffi_val = (FfiHandle*)mem::malloc(FfiHandle.sizeof);
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

    interp.current_scope = saved_scope;
    return v;
}

fn Value* make_error(Interp* interp, char[] msg) {
    Value* v = interp.alloc_value();
    v.tag = ERROR;
    main::scope_register_dtor(interp.current_scope, (void*)v, &scope_dtor_value);
    v.str_val = (StringVal*)mem::malloc(StringVal.sizeof);
    usz len = msg.len;
    usz cap = len + 1;
    if (cap < 32) cap = 32;
    v.str_val.chars = (char*)mem::malloc(cap);
    v.str_val.capacity = cap;
    for (usz i = 0; i < len; i++) {
        v.str_val.chars[i] = msg[i];
    }
    v.str_val.chars[len] = 0;
    v.str_val.len = len;

    return v;
}

/**
 * raise_error — Recoverable error via algebraic effect system.
 *
 * If a `raise` handler is on the handler stack, sets raise_pending flag and
 * returns V_ERROR for stack unwinding. If no handler, falls back to V_ERROR
 * (same as make_error).
 *
 * Use make_error() for fatal/internal errors only (stack overflow, invariants).
 */
fn Value* raise_error(Interp* interp, char[] msg) {
    // Fast path: no handlers at all → just return error
    if (interp.handler_count == 0) {
        return make_error(interp, msg);
    }

    // If a raise is already pending, don't overwrite it (error re-propagation)
    if (interp.flags.raise_pending) {
        return make_error(interp, msg);
    }

    SymbolId raise_sym = interp.sym_raise;

    // Search handler stack from top (most recent) to bottom
    for (isz i = (isz)interp.handler_count - 1; i >= 0; i--) {
        EffectHandler* h = &interp.handler_stack[(usz)i];

        for (usz j = 0; j < h.clause_count; j++) {
            SymbolId ctag = h.clauses != null ? h.clauses[j].effect_tag : h.tags[j];
            if (ctag == raise_sym) {
                // Found a raise handler — set pending flag for jit_handle_impl to dispatch.
                // We can't safely call jit_eval from within a primitive call chain,
                // so we store the message and let the handle block dispatch.
                interp.flags.raise_pending = true;
                usz copy_len = msg.len;
                if (copy_len > 255) copy_len = 255;
                for (usz ci = 0; ci < copy_len; ci++) interp.raise_msg[ci] = msg[ci];
                interp.raise_msg[copy_len] = 0;
                interp.raise_msg_len = copy_len;
                return make_error(interp, msg);
            }
        }

        if (h.strict_mode) break;
    }

    // No raise handler found — fall back to V_ERROR
    return make_error(interp, msg);
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

fn bool is_double(Value* v) @inline {
    return v != null && v.tag == DOUBLE;
}

fn bool is_number(Value* v) @inline {
    return v != null && (v.tag == INT || v.tag == DOUBLE);
}

fn double to_double(Value* v) @inline {
    if (v.tag == DOUBLE) return v.double_val;
    return (double)v.int_val;
}

fn bool is_ffi_handle(Value* v) @inline {
    return v != null && v.tag == FFI_HANDLE;
}

// Convert a ^Type annotation base_type symbol to FfiTypeTag
fn FfiTypeTag type_ann_to_ffi_tag(SymbolId base_type, Interp* interp) {
    if (base_type == interp.sym_Int) return FFI_TYPE_INT;
    if (base_type == interp.sym_Double) return FFI_TYPE_DOUBLE;
    if (base_type == interp.sym_String) return FFI_TYPE_PTR;
    if (base_type == interp.sym_Ptr) return FFI_TYPE_PTR;
    if (base_type == interp.sym_Bool) return FFI_TYPE_BOOL;
    if (base_type == interp.sym_Void) return FFI_TYPE_VOID;
    // Unknown type — default to pointer (handles opaque types)
    return FFI_TYPE_PTR;
}

fn bool is_array(Value* v) @inline {
    return v != null && v.tag == ARRAY;
}

fn Value* make_array(Interp* interp, usz capacity) {
    // Allocate Value in root_scope so it persists across scope releases
    main::ScopeRegion* saved_scope = interp.current_scope;
    interp.current_scope = interp.root_scope;
    Value* v = interp.alloc_value();
    main::scope_register_dtor(interp.root_scope, (void*)v, &scope_dtor_value);
    interp.current_scope = saved_scope;
    v.tag = ARRAY;
    Array* arr = (Array*)mem::malloc(Array.sizeof);
    arr.capacity = capacity < 4 ? 4 : capacity;
    arr.length = 0;
    arr.items = (Value**)mem::malloc(Value*.sizeof * arr.capacity);
    v.array_val = arr;

    return v;
}

fn Value* make_module(Interp* interp, Module* mod) {
    // Allocate Value in root_scope so it persists across scope releases
    main::ScopeRegion* saved_scope = interp.current_scope;
    interp.current_scope = interp.root_scope;
    Value* v = interp.alloc_value();
    interp.current_scope = saved_scope;
    v.tag = MODULE;
    v.module_val = mod;
    return v;
}

fn Value* make_iterator(Interp* interp, Value* thunk) {
    Value* v = interp.alloc_value();
    v.tag = ITERATOR;
    v.iterator_val = thunk;
    return v;
}

fn Value* make_coroutine(Interp* interp, main::StackCtx* ctx) {
    Value* v = interp.alloc_value();
    v.tag = COROUTINE;
    // No dtor registered — lifecycle managed by coroutine primitives (resume/yield).
    // Coroutines that complete return their context to the pool in coroutine_thunk_entry.
    // Abandoned coroutines leak their stack (~64KB) until program exit — acceptable for now.
    // TODO: Track active coroutines in interp for cleanup on shutdown.
    v.coroutine_val = ctx;
    return v;
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

<* @require v != null && v.tag == INT : "get_int called on non-INT value" *>
fn long get_int(Value* v) @inline {
    return v.int_val;
}

<* @require v != null && v.tag == SYMBOL : "get_symbol called on non-SYMBOL value" *>
fn SymbolId get_symbol(Value* v) @inline {
    return v.sym_val;
}

<* @require v != null && v.tag == CONS : "car called on non-CONS value" *>
fn Value* car(Value* v) @inline {
    return v.cons_val.car;
}

<* @require v != null && v.tag == CONS : "cdr called on non-CONS value" *>
fn Value* cdr(Value* v) @inline {
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
// String conversion helpers (shared across primitives, eval, value printing)
// =============================================================================

/**
 * Convert integer to string in caller-provided buffer. Returns slice of written chars.
 */
fn char[] int_to_string(long val, char[] buffer) {
    if (val == 0) { buffer[0] = '0'; return buffer[:1]; }
    usz len = 0;
    bool negative = val < 0;
    if (negative) val = -val;
    while (val > 0 && len < buffer.len - 1) {
        buffer[len++] = (char)('0' + (val % 10));
        val /= 10;
    }
    if (negative) buffer[len++] = '-';
    // Reverse in-place
    for (usz j = 0; j < len / 2; j++) {
        char tmp = buffer[j];
        buffer[j] = buffer[len - 1 - j];
        buffer[len - 1 - j] = tmp;
    }
    return buffer[:len];
}

/**
 * Format double to string in caller-provided buffer, ensuring decimal point.
 * Handles NaN, +inf, -inf. Buffer must be at least 64 bytes.
 */
fn char[] double_to_string(double val, char[] buffer) {
    if (val != val) {
        char[] nan = "+nan.0";
        for (usz i = 0; i < nan.len; i++) buffer[i] = nan[i];
        return buffer[:nan.len];
    }
    if (val == 1.0/0.0) {
        char[] inf = "+inf.0";
        for (usz i = 0; i < inf.len; i++) buffer[i] = inf[i];
        return buffer[:inf.len];
    }
    if (val == -1.0/0.0) {
        char[] ninf = "-inf.0";
        for (usz i = 0; i < ninf.len; i++) buffer[i] = ninf[i];
        return buffer[:ninf.len];
    }
    char[64] fmt_buf;
    char[] s = io::bprintf(&fmt_buf, "%.15g", val)!!;
    for (usz i = 0; i < s.len; i++) buffer[i] = s[i];
    usz len = s.len;
    bool has_dot = false;
    for (usz i = 0; i < len; i++) {
        if (buffer[i] == '.' || buffer[i] == 'e' || buffer[i] == 'E') { has_dot = true; break; }
    }
    if (!has_dot && len + 2 <= buffer.len) {
        buffer[len] = '.';
        buffer[len + 1] = '0';
        len += 2;
    }
    return buffer[:len];
}

// =============================================================================
// SECTION 5: ENVIRONMENT
// =============================================================================

// Initial capacity for dynamically-sized Env binding arrays
const usz ENV_INITIAL_CAPACITY = 4;

// Threshold: build hash table when binding_count exceeds this
const usz ENV_HASH_THRESHOLD = 16;

// Sentinel for empty hash table slots
const SymbolId ENV_HASH_EMPTY = 0xFFFFFFFF;

/**
 * Binding — Single name-value binding.
 */
struct Binding {
    SymbolId name;
    Value*   value;
}

/**
 * EnvHashEntry — Open-addressing hash table entry for Env.
 */
struct EnvHashEntry {
    SymbolId key;    // ENV_HASH_EMPTY = empty slot
    usz      index;  // Index into bindings array
}

/**
 * Env — Linked list of environment frames.
 * Bindings array is heap-allocated and grows dynamically.
 * For frames with >ENV_HASH_THRESHOLD bindings, a hash table provides O(1) lookup.
 */
struct Env {
    Binding*              bindings;
    usz                   binding_count;
    usz                   capacity;
    Env*                  parent;
    EnvHashEntry*         hash_table;     // NULL if not using hash
    usz                   hash_capacity;  // Power of 2
    bool                  persistent;     // true = allocated in root_region, skip in deep_copy_env
    uint                  scope_gen;      // Scope generation stamp for O(1) membership check
    bool                  is_inline;
    Binding[4]            inline_bindings;
}

fn Env* make_env(Interp* interp, Env* parent) {
    // Escape-env optimization: allocate loop envs in escape_scope
    // so they survive TCO bounces without copy_tco_env_chain frame copy.
    bool use_escape = interp.escape_env_mode &&
                      interp.escape_scope != null &&
                      interp.current_scope != interp.escape_scope;

    main::ScopeRegion* target = use_escape ? interp.escape_scope : interp.current_scope;
    main::ScopeRegion* saved = interp.current_scope;
    if (use_escape) interp.current_scope = target;

    Env* env = interp.alloc_env();
    env.is_inline = true;
    env.bindings = &env.inline_bindings[0];
    env.binding_count = 0;
    env.capacity = 4;
    env.parent = parent;
    env.hash_table = null;
    env.hash_capacity = 0;
    env.persistent = false;
    env.scope_gen = target.generation;
    main::scope_register_dtor(target, (void*)env, &scope_dtor_env);

    if (use_escape) interp.current_scope = saved;
    return env;
}

/**
 * Build or rebuild the hash table for an Env frame.
 * Hash capacity is next power of 2 >= 2 * binding_count.
 */
fn void Env.build_hash_table(Env* self) {
    // Determine capacity (next power of 2 >= 2 * count)
    usz cap = 32;
    while (cap < self.binding_count * 2) cap *= 2;

    // Free old hash table if exists
    if (self.hash_table != null) {
        mem::free(self.hash_table);
    }

    self.hash_capacity = cap;
    self.hash_table = (EnvHashEntry*)mem::malloc(EnvHashEntry.sizeof * cap);
    // Initialize all slots to empty
    for (usz i = 0; i < cap; i++) {
        self.hash_table[i].key = ENV_HASH_EMPTY;
    }
    // Insert all existing bindings
    usz mask = cap - 1;
    for (usz i = 0; i < self.binding_count; i++) {
        usz slot = (usz)self.bindings[i].name & mask;
        while (self.hash_table[slot].key != ENV_HASH_EMPTY) {
            slot = (slot + 1) & mask;
        }
        self.hash_table[slot].key = self.bindings[i].name;
        self.hash_table[slot].index = i;
    }
}

/**
 * Look up a SymbolId in the hash table. Returns index into bindings or ~(usz)0 if not found.
 */
<* @require self.hash_table != null : "hash_lookup called without hash table" *>
fn usz Env.hash_lookup(Env* self, SymbolId name) {
    usz mask = self.hash_capacity - 1;
    usz slot = (usz)name & mask;
    for (;;) {
        SymbolId k = self.hash_table[slot].key;
        if (k == name) return self.hash_table[slot].index;
        if (k == ENV_HASH_EMPTY) return ~(usz)0;
        slot = (slot + 1) & mask;
    }
}

/**
 * Insert into the hash table (must have space — caller ensures load factor).
 */
<* @require self.hash_table != null : "hash_insert called without hash table" *>
fn void Env.hash_insert(Env* self, SymbolId name, usz index) {
    usz mask = self.hash_capacity - 1;
    usz slot = (usz)name & mask;
    while (self.hash_table[slot].key != ENV_HASH_EMPTY && self.hash_table[slot].key != name) {
        slot = (slot + 1) & mask;
    }
    self.hash_table[slot].key = name;
    self.hash_table[slot].index = index;
}

fn void Env.define(Env* self, SymbolId name, Value* value) {
    // Fast path: use hash table if available
    if (self.hash_table != null) {
        usz idx = self.hash_lookup(name);
        if (idx != ~(usz)0) {
            self.bindings[idx].value = value;
            return;
        }
    } else {
        // Linear scan for small frames
        for (usz i = 0; i < self.binding_count; i++) {
            if (self.bindings[i].name == name) {
                self.bindings[i].value = value;
                return;
            }
        }
    }
    // Grow if needed
    if (self.binding_count >= self.capacity) {
        usz new_cap = self.capacity * 2;
        Binding* new_bindings = (Binding*)mem::malloc(Binding.sizeof * new_cap);
        for (usz i = 0; i < self.binding_count; i++) {
            new_bindings[i] = self.bindings[i];
        }
        if (!self.is_inline) {
            mem::free(self.bindings);
        }
        self.is_inline = false;
        self.bindings = new_bindings;
        self.capacity = new_cap;
    }
    usz new_idx = self.binding_count;
    self.bindings[new_idx].name = name;
    self.bindings[new_idx].value = value;
    self.binding_count++;

    // Build hash table if we crossed the threshold
    if (self.hash_table == null && self.binding_count > ENV_HASH_THRESHOLD) {
        self.build_hash_table();
    } else if (self.hash_table != null) {
        // Check load factor: rebuild if > 70%
        if (self.binding_count * 10 > self.hash_capacity * 7) {
            self.build_hash_table();
        } else {
            self.hash_insert(name, new_idx);
        }
    }
}

fn Value* Env.lookup(Env* self, SymbolId name) {
    // Fast path: use hash table if available
    if (self.hash_table != null) {
        usz idx = self.hash_lookup(name);
        if (idx != ~(usz)0) {
            return self.bindings[idx].value;
        }
    } else {
        // Linear scan for small frames
        for (usz i = 0; i < self.binding_count; i++) {
            if (self.bindings[i].name == name) {
                return self.bindings[i].value;
            }
        }
    }
    // Search parent
    if (self.parent != null) {
        return self.parent.lookup(name);
    }
    return null;  // Not found
}

fn bool Env.set(Env* self, SymbolId name, Value* value) {
    if (self.hash_table != null) {
        usz idx = self.hash_lookup(name);
        if (idx != ~(usz)0) {
            self.bindings[idx].value = value;
            return true;
        }
    } else {
        for (usz i = 0; i < self.binding_count; i++) {
            if (self.bindings[i].name == name) {
                self.bindings[i].value = value;
                return true;
            }
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
const usz MAX_PATH_SEGMENTS = 32;

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
    E_PERFORM,    // (perform tag arg) or (signal tag arg)
    E_HANDLE,     // (handle body (tag arg body) ...) or (handle body ((tag k arg) body) ...)
    E_RESOLVE,    // (resolve value) — resume continuation in handler clause
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
    E_DEFTYPE,    // (define [type] Name fields...)
    E_DEFABSTRACT, // (define [abstract] Name [Parent])
    E_DEFUNION,   // (define [union] Name variants...)
    E_DEFALIAS,   // (define [alias] Name Target)
    E_DEFEFFECT,  // (define [effect] (tag (^Type arg)))
    E_EXPORT_FROM, // (export-from mod (sym1 sym2)) or (export-from mod :all)
    E_FFI_LIB,    // (define [ffi lib] name "path.so")
    E_FFI_FN,     // (define [ffi λ libname] (fname (^T arg)...) ^RetT)
}

struct ExprLit {
    Value* value;
}

struct ExprVar {
    SymbolId name;
}

struct ExprLambda {
    SymbolId param;           // First/only param (or unused if param_count==0)
    SymbolId* params;         // All fixed params (malloc'd)
    usz      param_count;     // Number of fixed params
    bool     has_rest;        // True if variadic (.. rest)
    SymbolId rest_param;      // Rest parameter name
    Expr*    body;
    TypeAnnotation* param_annotations;  // Type annotations for params (malloc'd, null if none)
    bool has_typed_params;    // True if any param has a type annotation
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

struct ExprResolve {
    Expr* value;   // the value to resolve with (resumes the continuation)
}

struct EffectClause {
    SymbolId effect_tag;    // Effect name
    SymbolId k_name;        // Continuation param
    SymbolId arg_name;      // Argument param
    Expr*    handler_body;
}

struct ExprHandle {
    Expr*          body;
    EffectClause*  clauses;
    usz            clause_count;
    bool           strict_mode;   // ^strict annotation
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
    PAT_CONSTRUCTOR, // (TypeName sub-patterns...) - matches type instance
    PAT_GUARD,      // (? pred) or (? pred sub-pattern) - guarded pattern
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
            Pattern** elements;
            usz elem_count;
            RestPosition rest_pos;
            SymbolId rest_binding;  // Name to bind rest to (if REST_MIDDLE)
        }

        // PAT_CONSTRUCTOR: (TypeName sub-patterns...)
        struct {
            SymbolId constructor_name;
            Pattern** ctor_sub_patterns;
            usz ctor_sub_count;
        }

        // PAT_GUARD: (? pred) or (? pred sub-pattern)
        struct {
            Expr* guard_pred;       // Predicate expression
            Pattern* guard_sub;     // Optional sub-pattern (null = match anything)
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
    MatchClause* clauses;
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
    Expr** args;
    usz arg_count;
}

/**
 * ExprBegin — Sequence expression (begin e1 e2 ... en).
 * Evaluates all expressions, returns the last.
 */
struct ExprBegin {
    Expr** exprs;
    usz expr_count;
}

/**
 * ExprSet — Variable mutation: (set! name value)
 */
struct ExprSet {
    SymbolId name;
    Expr*    value;
    bool     is_path;
    SymbolId[MAX_PATH_SEGMENTS] path_segments;
    usz      path_segment_count;
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
    MacroClause[32] clauses;
    usz clause_count;
}

/**
 * ExprModule — Module definition: (module name (export sym...) body...)
 */
struct ExprModule {
    SymbolId     name;
    SymbolId*    exports;        // heap-allocated
    usz          export_count;
    usz          export_capacity;
    Expr**       body;           // heap-allocated
    usz          body_count;
    usz          body_capacity;
}

/**
 * ExprImport — Module import: (import name) or (import "path")
 */
struct ExprImport {
    SymbolId name;        // module name (symbol)
    bool     has_path;    // true if importing by file path
    char[256] path;       // file path (if has_path)
    usz      path_len;
    // Selective imports: (import mod (sym1 (sym2 :as alias2)))
    SymbolId*    imports;       // Names to import into scope (heap-allocated)
    SymbolId*    aliases;       // Rename targets (0 = no rename) (heap-allocated)
    usz          import_count;  // 0 = qualified-only, >0 = selective
    usz          import_capacity;
    bool         import_all;    // true for :all
}

/**
 * ExprExportFrom — Re-export symbols from another module: (export-from mod (sym1 sym2)) or (export-from mod :all)
 */
struct ExprExportFrom {
    SymbolId     source_module;  // Module to re-export from
    SymbolId*    names;          // Specific names (heap-allocated, empty if :all)
    usz          name_count;
    usz          name_capacity;
    bool         all;            // true for :all
}

// =============================================================================
// SECTION 6.6: FFI AST NODES
// =============================================================================

// FFI type tag — must stay in sync with csrc/ffi_helpers.c OMNI_FFI_* enum
// Values: VOID=0, INT=1, DOUBLE=2, PTR=3, BOOL=4
enum FfiTypeTag : char {
    FFI_TYPE_VOID,
    FFI_TYPE_INT,
    FFI_TYPE_DOUBLE,
    FFI_TYPE_PTR,
    FFI_TYPE_BOOL,
}

struct ExprFfiLib {
    SymbolId name;           // e.g. "libc"
    Expr* path_expr;         // string literal "libc.so.6"
}

struct ExprFfiFn {
    SymbolId lib_name;       // which library (e.g. "libc")
    SymbolId fn_name;        // Omni name (auto kebab-cased)
    char[128] c_name;        // original C name for dlsym
    usz c_name_len;
    usz param_count;
    SymbolId[16] param_names;
    TypeAnnotation[16] param_types;
    TypeAnnotation return_type;
    bool has_return_type;
    bool is_variadic;
}

// FfiBoundFn — runtime state for a bound FFI function (used as Primitive.user_data)
struct FfiBoundFn {
    void* fn_ptr;           // null until first call (lazy dlsym)
    void* lib_handle;       // dlopen handle for lazy resolution
    char[128] c_name;       // C symbol name for lazy dlsym
    usz param_count;
    FfiTypeTag[16] param_types;
    FfiTypeTag return_type;
    bool has_return;
    bool is_variadic;
}

// =============================================================================
// SECTION 6.7: TYPE DEFINITION AST NODES
// =============================================================================

/**
 * TypeField — Field in a type definition with optional type annotation.
 */
struct TypeField {
    SymbolId name;
    TypeAnnotation type_ann;
}

/**
 * ExprDefType — (define [type] Name fields...) or (define [type] (Name Parent) fields...)
 */
struct ExprDefType {
    SymbolId name;
    SymbolId parent;
    bool has_parent;
    TypeField[MAX_TYPE_FIELDS] fields;
    usz field_count;
    SymbolId[MAX_TYPE_PARAMS] type_params;
    usz type_param_count;
}

/**
 * ExprDefAbstract — (define [abstract] Name) or (define [abstract] (Name Parent))
 */
struct ExprDefAbstract {
    SymbolId name;
    SymbolId parent;
    bool has_parent;
}

/**
 * ExprDefUnion — (define [union] (Name T...) Variant1 (Variant2 T) ...)
 */
struct ExprDefUnion {
    SymbolId name;
    SymbolId[MAX_TYPE_PARAMS] type_params;
    usz type_param_count;
    UnionVariant[64] variants;
    usz variant_count;
}

/**
 * ExprDefAlias — (define [alias] Name Target)
 */
struct ExprDefAlias {
    SymbolId name;
    TypeAnnotation target;
}

/**
 * ExprDefEffect — (define [effect] (tag (^Type arg)))
 */
struct ExprDefEffect {
    SymbolId name;           // Effect tag name (e.g., io/print)
    TypeAnnotation arg_type; // Declared argument type (^Any, ^String, etc.)
    bool has_arg_type;       // true if argument type annotation present
}

/**
 * Module — Loaded module record.
 */
struct Module {
    SymbolId     name;
    Env*         env;              // Module's internal environment
    SymbolId*    exports;          // Exported symbol names (heap-allocated)
    usz          export_count;
    usz          export_capacity;
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
        ExprLambda* lambda;       // Pointer-indirect (large struct)
        ExprApp     app;
        ExprIf      if_expr;
        ExprLet     let_expr;
        ExprDefine  define;
        ExprQuote   quote;
        ExprReset   reset;
        ExprShift   shift;
        ExprPerform perform;
        ExprResolve resolve;
        ExprHandle* handle;       // Pointer-indirect (large struct)
        ExprIndex   index;
        ExprPath    path;
        ExprMatch*  match;        // Pointer-indirect (large struct)
        ExprAnd     and_expr;
        ExprOr      or_expr;
        ExprCall*   call;         // Pointer-indirect (large struct)
        ExprBegin*  begin;        // Pointer-indirect (large struct)
        ExprSet     set_expr;
        ExprQuasiquote   quasiquote;
        ExprUnquote      unquote;
        ExprUnquoteSplicing unquote_splicing;
        ExprDefineMacro  define_macro;
        ExprModule*      module_expr;  // Pointer-indirect (large struct)
        ExprImport       import_expr;
        ExprDefType*     deftype;      // Pointer-indirect (large struct)
        ExprDefAbstract  defabstract;
        ExprDefUnion*    defunion;     // Pointer-indirect (large struct)
        ExprDefAlias     defalias;
        ExprDefEffect    defeffect;
        ExprExportFrom   export_from;
        ExprFfiLib       ffi_lib;
        ExprFfiFn*       ffi_fn;       // Pointer-indirect (large struct)
    }
}

// =============================================================================
// SECTION 7: EFFECT HANDLERS
// =============================================================================

/**
 * EffectHandler - Active effect handler in the handler stack.
 */
struct EffectHandler {
    SymbolId* tags;
    EffectClause* clauses;
    Value** closures;
    usz clause_count;
    Env* handler_env;
    Expr* body_expr;    // The handle body for continuation capture
    Env*  body_env;     // Environment at handle time
    bool  strict_mode;  // ^strict: reject unhandled effects in body
    void* effect_state;  // HandleEffectState* (set by stack-context-based handle, null otherwise)
}

// =============================================================================
// SECTION 8: INTERPRETER STATE
// =============================================================================

/**
 * InterpFlags — Packed boolean state flags for the interpreter.
 */
bitstruct InterpFlags : ushort {
    bool jit_enabled      : 0;  // true after init, enables JIT-first execution
    bool jit_tco_bounce   : 1;  // True = return sentinel, jit_eval loop continues
    bool raise_pending    : 2;  // raise_error found handler, pending dispatch
}

/**
 * Interp — Interpreter state.
 */
struct Interp {
    SymbolTable  symbols;
    Env*         global_env;

    // Region-based allocation (Expr/Pattern only)
    main::RegionHandle root_region;      // For Expr, Pattern allocation (AST nodes are permanent)

    // Scope-region allocation (Values, Envs, closures)
    main::ScopeRegion* root_scope;       // Permanent scope for defines (lives forever)
    main::ScopeRegion* current_scope;    // Current scope (changes at let/lambda/loop)

    // Pre-interned common symbols
    SymbolId sym_lambda;
    SymbolId sym_if;
    SymbolId sym_let;
    SymbolId sym_define;
    SymbolId sym_quote;
    SymbolId sym_reset;
    SymbolId sym_shift;
    SymbolId sym_perform;
    SymbolId sym_signal;
    SymbolId sym_handle;
    SymbolId sym_resolve;
    SymbolId sym_with_continuation;
    SymbolId sym__k;       // hidden continuation name for match-style handle clauses
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

    // Type system symbols
    SymbolId sym_type;      // "type" for [type] annotation
    SymbolId sym_abstract;  // "abstract" for [abstract] annotation
    SymbolId sym_union;     // "union" for [union] annotation
    SymbolId sym_alias;     // "alias" for [alias] annotation
    SymbolId sym_effect;    // "effect" for [effect] annotation

    // FFI annotation symbols
    SymbolId sym_ffi;       // "ffi" for [ffi ...] annotation
    SymbolId sym_lib;       // "lib" for [ffi lib]
    SymbolId sym_Ptr;       // "Ptr" for ^Ptr type
    SymbolId sym_Void;      // "Void" for ^Void type

    // Built-in type name symbols
    SymbolId sym_Int;
    SymbolId sym_Double;
    SymbolId sym_String;
    SymbolId sym_Symbol;
    SymbolId sym_List;
    SymbolId sym_Bool;
    SymbolId sym_Nil;
    SymbolId sym_Closure;
    SymbolId sym_Array;
    SymbolId sym_Dict;
    SymbolId sym_Any;
    SymbolId sym_Val;
    SymbolId sym_Number;
    SymbolId sym_Collection;
    SymbolId sym_Iterator;

    // Error-as-effect
    SymbolId sym_raise;

    // Cached cons path symbols
    SymbolId sym_car;
    SymbolId sym_cdr;

    // Placeholder/pipe/guard symbols
    SymbolId sym_placeholder;  // "__placeholder" sentinel for _ in expression context
    SymbolId sym_pipe;         // "|>" pipe operator
    SymbolId sym_question;     // "?" guard pattern

    // Effect fast-path dispatch table: maps effect tag → raw primitive
    // When a signal has no handler, the fast path looks up this table.
    FastPathEntry[32] fast_path_table;
    usz fast_path_count;
    bool jit_debug_trace;  // trace JIT compile/exec steps

    // Cached TypeIds for built-in types (avoid repeated hash lookups)
    TypeId tid_Int;
    TypeId tid_Double;
    TypeId tid_String;
    TypeId tid_Symbol;
    TypeId tid_List;
    TypeId tid_Bool;
    TypeId tid_Nil;
    TypeId tid_Closure;
    TypeId tid_Array;
    TypeId tid_Dict;
    TypeId tid_Any;
    TypeId tid_Number;
    TypeId tid_Collection;
    TypeId tid_Iterator;

    // Type registry
    TypeRegistry types;
    int constructor_type_id;  // Set before calling type constructor primitive
    void* prim_user_data;     // Set before calling primitive with user_data

    // Packed boolean state flags
    InterpFlags flags;

    // Pending raise error message (for raise_error → jit_handle_impl dispatch)
    char[256] raise_msg;
    usz raise_msg_len;

    // JIT execution mode
    Env* jit_env;      // Current local env for JIT sub-expression evaluation
    Env* match_env;    // Current env for evaluating guard predicates in match

    // JIT trampoline TCO state
    Expr* jit_tco_expr;        // Next expression to evaluate (set by tail-call helpers)
    Env*  jit_tco_env;         // Environment for next expression
    main::ScopeRegion* tco_recycle_scope; // Scope eligible for TCO recycling (set by jit_eval_in_call_scope)
    main::ScopeRegion* releasing_scope;   // Set during copy_to_parent to skip values not in this scope
    main::ScopeRegion* escape_scope;      // Result scope for escape-aware cons allocation (null when inactive)
    bool escape_env_mode;                 // When true, make_env allocates in escape_scope (for TCO loop envs)

    // Stack overflow protection
    usz eval_depth;
    usz max_eval_depth;

    // Macro table (dynamic)
    MacroDef* macro_table;
    usz macro_count;
    usz macro_capacity;
    usz gensym_counter;
    usz* macro_hash_index;
    usz macro_hash_capacity;

    // Module system (dynamic)
    Module* modules;
    usz module_count;
    usz module_capacity;
    usz* module_hash_index;
    usz module_hash_capacity;
    SymbolId sym_module;
    SymbolId sym_import;
    SymbolId sym_export;
    SymbolId sym_as;           // ":as" for (import mod (name :as alias))
    SymbolId sym_all;          // ":all" for (import mod :all)
    SymbolId sym_export_from;  // "export-from" for (export-from mod (sym...))

    // Continuation/reset depth (saved/restored across context boundaries)
    usz      reset_depth;

    // Effect handler stack (dynamic)
    EffectHandler* handler_stack;
    usz            handler_count;
    usz            handler_capacity;

    // Last call site symbol (for error messages)
    SymbolId last_call_name;

    // Source file directory stack (for relative import resolution)
    char[256][16] source_dirs;  // stack of directory paths (null-terminated)
    usz source_dir_count;

    // StackCtx-based continuation system (stack engine)
    main::StackPool stack_ctx_pool;
    Value* resume_value;           // Value passed to (k val) — read by shift on resume
    Value* yield_value;            // Value passed by (yield val) — read by resume
    void* current_reset_state;     // Pointer to current ResetState (in jit.c3)

}

fn void Interp.init(Interp* self) {
    self.symbols.init();
    self.global_env = null;

    // Region setup for Expr/Pattern allocation
    self.root_region = main::thread_root_region();

    // Scope-region setup for Values, Envs, closures
    self.root_scope = main::scope_create(null);
    self.current_scope = self.root_scope;

    // Effect handler stack (dynamic)
    self.handler_capacity = HANDLER_INITIAL_CAPACITY;
    self.handler_count = 0;
    self.handler_stack = (EffectHandler*)mem::malloc(EffectHandler.sizeof * self.handler_capacity);

    // Reset depth (saved/restored across context boundaries)
    self.reset_depth = 0;

    // Source directory stack
    self.source_dir_count = 0;

    // StackCtx-based continuation system
    main::stack_pool_init(&self.stack_ctx_pool);
    g_stack_ctx_pool = &self.stack_ctx_pool;
    self.resume_value = null;
    self.yield_value = null;
    self.current_reset_state = null;

    // Pre-intern keywords
    self.sym_lambda  = self.symbols.intern("lambda");
    self.sym_if      = self.symbols.intern("if");
    self.sym_let     = self.symbols.intern("let");
    self.sym_define  = self.symbols.intern("define");
    self.sym_quote   = self.symbols.intern("quote");
    self.sym_reset   = self.symbols.intern("reset");
    self.sym_shift   = self.symbols.intern("shift");
    self.sym_perform = self.symbols.intern("perform");
    self.sym_signal  = self.symbols.intern("signal");
    self.sym_handle  = self.symbols.intern("handle");
    self.sym_resolve = self.symbols.intern("resolve");
    self.sym_with_continuation = self.symbols.intern("with-continuation");
    self.sym__k      = self.symbols.intern("__k");
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

    // Type system symbols
    self.sym_type = self.symbols.intern("type");
    self.sym_abstract = self.symbols.intern("abstract");
    self.sym_union = self.symbols.intern("union");
    self.sym_alias = self.symbols.intern("alias");
    self.sym_effect = self.symbols.intern("effect");

    // FFI annotation symbols
    self.sym_ffi = self.symbols.intern("ffi");
    self.sym_lib = self.symbols.intern("lib");
    self.sym_Ptr = self.symbols.intern("Ptr");
    self.sym_Void = self.symbols.intern("Void");

    // Built-in type name symbols
    self.sym_Int = self.symbols.intern("Int");
    self.sym_Double = self.symbols.intern("Double");
    self.sym_String = self.symbols.intern("String");
    self.sym_Symbol = self.symbols.intern("Symbol");
    self.sym_List = self.symbols.intern("List");
    self.sym_Bool = self.symbols.intern("Bool");
    self.sym_Nil = self.symbols.intern("Nil");
    self.sym_Closure = self.symbols.intern("Closure");
    self.sym_Array = self.symbols.intern("Array");
    self.sym_Dict = self.symbols.intern("Dict");
    self.sym_Any = self.symbols.intern("Any");
    self.sym_Val = self.symbols.intern("Val");
    self.sym_Number = self.symbols.intern("Number");
    self.sym_Collection = self.symbols.intern("Collection");
    self.sym_Iterator = self.symbols.intern("Iterator");

    // I/O effect tags
    // Error-as-effect
    self.sym_raise = self.symbols.intern("raise");

    // Cons path symbols
    self.sym_car = self.symbols.intern("car");
    self.sym_cdr = self.symbols.intern("cdr");

    // Placeholder/pipe/guard symbols
    self.sym_placeholder = self.symbols.intern("__placeholder");
    self.sym_pipe = self.symbols.intern("|>");
    self.sym_question = self.symbols.intern("?");

    // Type registry
    self.types.init();

    // Module system (dynamic)
    self.sym_module = self.symbols.intern("module");
    self.sym_import = self.symbols.intern("import");
    self.sym_export = self.symbols.intern("export");
    self.sym_as = self.symbols.intern(":as");
    self.sym_all = self.symbols.intern(":all");
    self.sym_export_from = self.symbols.intern("export-from");
    self.module_count = 0;
    self.module_capacity = MODULE_INITIAL_CAPACITY;
    self.modules = (Module*)mem::malloc(Module.sizeof * self.module_capacity);
    self.module_hash_capacity = self.module_capacity * 2;
    self.module_hash_index = (usz*)mem::malloc(usz.sizeof * self.module_hash_capacity);
    for (usz i = 0; i < self.module_hash_capacity; i++) self.module_hash_index[i] = usz.max;

    // JIT disabled until after stdlib registration
    self.flags.jit_enabled = false;
    self.jit_env = null;
    self.jit_tco_expr = null;
    self.jit_tco_env = null;
    self.tco_recycle_scope = null;
    self.releasing_scope = null;
    self.escape_scope = null;
    self.escape_env_mode = false;
    self.flags.jit_tco_bounce = false;

    // Stack overflow protection
    self.eval_depth = 0;
    self.max_eval_depth = 5000;

    // Macro table (dynamic)
    self.macro_count = 0;
    self.macro_capacity = MACRO_INITIAL_CAPACITY;
    self.macro_table = (MacroDef*)mem::malloc(MacroDef.sizeof * self.macro_capacity);
    self.gensym_counter = 0;
    self.macro_hash_capacity = self.macro_capacity * 2;
    self.macro_hash_index = (usz*)mem::malloc(usz.sizeof * self.macro_hash_capacity);
    for (usz i = 0; i < self.macro_hash_capacity; i++) self.macro_hash_index[i] = usz.max;

    // Create global environment
    self.global_env = make_env(self, null);
}

fn void Interp.grow_macro_table(Interp* self) {
    usz new_cap = self.macro_capacity * 2;
    MacroDef* new_table = (MacroDef*)mem::malloc(MacroDef.sizeof * new_cap);
    for (usz i = 0; i < self.macro_count; i++) new_table[i] = self.macro_table[i];
    mem::free(self.macro_table);
    self.macro_table = new_table;
    self.macro_capacity = new_cap;

    // Rebuild hash
    usz new_hash_cap = new_cap * 2;
    usz* new_hash = (usz*)mem::malloc(usz.sizeof * new_hash_cap);
    for (usz i = 0; i < new_hash_cap; i++) new_hash[i] = usz.max;
    mem::free(self.macro_hash_index);
    self.macro_hash_index = new_hash;
    self.macro_hash_capacity = new_hash_cap;

    usz mask = new_hash_cap - 1;
    for (usz i = 0; i < self.macro_count; i++) {
        char[] name_str = self.symbols.get_name(self.macro_table[i].name);
        usz h = hash_symbol(name_str);
        for (usz probe = 0; probe < new_hash_cap; probe++) {
            usz slot = (h + probe) & mask;
            if (new_hash[slot] == usz.max) {
                new_hash[slot] = i;
                break;
            }
        }
    }
}

fn void Interp.grow_module_table(Interp* self) {
    usz new_cap = self.module_capacity * 2;
    Module* new_mods = (Module*)mem::malloc(Module.sizeof * new_cap);
    for (usz i = 0; i < self.module_count; i++) new_mods[i] = self.modules[i];
    mem::free(self.modules);
    self.modules = new_mods;
    self.module_capacity = new_cap;

    // Rebuild hash
    usz new_hash_cap = new_cap * 2;
    usz* new_hash = (usz*)mem::malloc(usz.sizeof * new_hash_cap);
    for (usz i = 0; i < new_hash_cap; i++) new_hash[i] = usz.max;
    mem::free(self.module_hash_index);
    self.module_hash_index = new_hash;
    self.module_hash_capacity = new_hash_cap;

    usz mask = new_hash_cap - 1;
    for (usz i = 0; i < self.module_count; i++) {
        char[] name_str = self.symbols.get_name(self.modules[i].name);
        usz h = hash_symbol(name_str);
        for (usz probe = 0; probe < new_hash_cap; probe++) {
            usz slot = (h + probe) & mask;
            if (new_hash[slot] == usz.max) {
                new_hash[slot] = i;
                break;
            }
        }
    }
}

fn void Interp.grow_handler_stack(Interp* self) {
    usz new_cap = self.handler_capacity * 2;
    EffectHandler* new_stack = (EffectHandler*)mem::malloc(EffectHandler.sizeof * new_cap);
    for (usz i = 0; i < self.handler_count; i++) new_stack[i] = self.handler_stack[i];
    mem::free(self.handler_stack);
    self.handler_stack = new_stack;
    self.handler_capacity = new_cap;
}

fn void Interp.destroy(Interp* self) {
    self.symbols.destroy();
    self.types.destroy();
    if (self.macro_table != null) { mem::free(self.macro_table); self.macro_table = null; }
    if (self.macro_hash_index != null) { mem::free(self.macro_hash_index); self.macro_hash_index = null; }
    if (self.modules != null) {
        for (usz i = 0; i < self.module_count; i++) {
            if (self.modules[i].exports != null) {
                mem::free(self.modules[i].exports);
            }
        }
        mem::free(self.modules);
        self.modules = null;
    }
    if (self.module_hash_index != null) { mem::free(self.module_hash_index); self.module_hash_index = null; }
    if (self.handler_stack != null) { mem::free(self.handler_stack); self.handler_stack = null; }
    main::stack_pool_shutdown(&self.stack_ctx_pool);
}

fn Continuation* Interp.alloc_lisp_continuation(Interp* self) {
    Continuation k;
    k.ctx = null;
    k.is_coroutine_based = false;
    k.handle_state = null;
    main::ObjectHandle h = main::allocate_in(self.root_region, Continuation, k);
    return main::dereference_as(Continuation, h);
}

fn Value* Interp.alloc_value(Interp* self) @inline {
    Value* v = (Value*)self.current_scope.alloc(Value.sizeof);
    v.tag = NIL;  // Safe default
    v.scope_gen = self.current_scope.generation;  // Stamp for O(1) scope checks
    // Dtor registration moved to heap-backed constructors (make_string, make_closure, etc.)
    // Flat types (NIL, INT, DOUBLE, SYMBOL, CONS, BOOL) skip dtor — no heap backing.
    return v;
}

// Allocate a Value in root_scope (for Expr literals and other permanent values).
// Root-scope dtors only run at program exit, so unconditional registration is fine here.
// The perf win is from removing dtors in alloc_value() (child scopes, per-call teardown).
fn Value* Interp.alloc_value_root(Interp* self) @inline {
    Value* v = (Value*)self.root_scope.alloc(Value.sizeof);
    v.tag = NIL;
    v.scope_gen = self.root_scope.generation;  // Stamp for O(1) scope checks
    main::scope_register_dtor(self.root_scope, (void*)v, &scope_dtor_value);
    return v;
}

fn Env* Interp.alloc_env(Interp* self) @inline {
    Env* e = (Env*)self.current_scope.alloc(Env.sizeof);
    e.bindings = null;
    e.binding_count = 0;
    e.capacity = 0;
    e.parent = null;
    e.hash_table = null;
    e.hash_capacity = 0;
    e.persistent = false;
    e.scope_gen = self.current_scope.generation;
    return e;
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
        case DOUBLE:
            char[64] dbuf;
            char[] dstr = double_to_string(v.double_val, &dbuf);
            io::printf("%s", dstr);
        case STRING:
            io::print("\"");
            io::print(v.str_val.chars[:v.str_val.len]);
            io::print("\"");
        case SYMBOL:
            io::print(syms.get_name(v.sym_val));
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
            io::print("#<λ");
            if ((uint)v.closure_val.name != 0) {
                io::printf(" %s", (ZString)syms.get_name(v.closure_val.name));
            }
            io::print(" (");
            for (usz i = 0; i < v.closure_val.param_count; i++) {
                if (i > 0) io::print(" ");
                io::printf("%s", (ZString)syms.get_name(v.closure_val.params[i]));
            }
            if (v.closure_val.has_rest) {
                if (v.closure_val.param_count > 0) io::print(" ");
                io::printf(".. %s", (ZString)syms.get_name(v.closure_val.rest_param));
            }
            io::print(")>");
        case CONTINUATION:
            io::print("#<continuation>");
        case PRIMITIVE:
            io::printf("#<primitive %s>", (ZString)&v.prim_val.name);
        case PARTIAL_PRIM:
            io::printf("#<partial %d more>", v.partial_val.remaining);
        case ERROR:
            io::printf("#<error: %s>", (ZString)v.str_val.chars);
        case HASHMAP:
            io::print("{");
            uint printed = 0;
            for (uint hi = 0; hi < v.hashmap_val.capacity; hi++) {
                if (v.hashmap_val.entries[hi].key != null) {
                    if (printed > 0) io::print(" ");
                    print_value(v.hashmap_val.entries[hi].key, syms);
                    io::print(" ");
                    print_value(v.hashmap_val.entries[hi].value, syms);
                    printed++;
                }
            }
            io::print("}");
        case FFI_HANDLE:
            io::printf("#<ffi-handle:%s>", (ZString)&v.ffi_val.lib_name);
        case ARRAY:
            io::print("[");
            if (v.array_val != null) {
                for (usz vi = 0; vi < v.array_val.length; vi++) {
                    if (vi > 0) io::print(" ");
                    print_value(v.array_val.items[vi], syms);
                }
            }
            io::print("]");
        case TYPE_INFO:
            io::printf("#<type %s>", (ZString)syms.get_name(v.type_info_val.name));
        case INSTANCE:
            if (v.instance_val != null) {
                io::print("(");
                // Print type name — need the registry to look up
                // For now just print the type_id
                io::printf("instance:%d", v.instance_val.type_id);
                for (usz fi = 0; fi < v.instance_val.field_count; fi++) {
                    io::print(" ");
                    print_value(v.instance_val.fields[fi], syms);
                }
                io::print(")");
            } else {
                io::print("#<instance:null>");
            }
        case METHOD_TABLE:
            if (v.method_table_val != null) {
                io::printf("#<dispatch %s", (ZString)syms.get_name(v.method_table_val.name));
                if (v.method_table_val.entry_count > 0) {
                    io::printf(" %d method%s", v.method_table_val.entry_count,
                        v.method_table_val.entry_count == 1 ? "" : "s");
                }
                if (v.method_table_val.fallback != null) {
                    io::print(" +default");
                }
                io::print(">");
            } else {
                io::print("#<dispatch:null>");
            }
        case MODULE:
            if (v.module_val != null) {
                io::printf("#<module %s>", (ZString)syms.get_name(v.module_val.name));
            } else {
                io::print("#<module:null>");
            }
        case ITERATOR:
            io::print("#<iterator>");
        case COROUTINE:
            io::print("#<coroutine>");
        default:
            io::print("#<unknown>");
    }
}

fn void println_value(Value* v, SymbolTable* syms) {
    print_value(v, syms);
    io::printn("");
}

// =============================================================================
// print_value_to_buf — capture print_value output to a char buffer
// =============================================================================

struct PrintBuf {
    char* buf;
    usz pos;
    usz capacity;
}

fn void PrintBuf.append_str(PrintBuf* self, char[] s) {
    for (usz i = 0; i < s.len && self.pos < self.capacity - 1; i++) {
        self.buf[self.pos++] = s[i];
    }
}

fn void PrintBuf.append_char(PrintBuf* self, char c) {
    if (self.pos < self.capacity - 1) {
        self.buf[self.pos++] = c;
    }
}

fn void PrintBuf.append_zstr(PrintBuf* self, ZString zs) {
    char* p = (char*)zs;
    while (*p != 0 && self.pos < self.capacity - 1) {
        self.buf[self.pos++] = *p;
        p++;
    }
}

fn void print_value_buf(Value* v, SymbolTable* syms, PrintBuf* pb) {
    if (v == null || v.tag == NIL) {
        pb.append_str("nil");
        return;
    }

    switch (v.tag) {
        case INT:
            char[32] ibuf;
            char[] is = io::bprintf(&ibuf, "%d", v.int_val)!!;
            pb.append_str(is);
        case DOUBLE:
            char[64] dbuf2;
            char[] dstr2 = double_to_string(v.double_val, &dbuf2);
            pb.append_str(dstr2);
        case STRING:
            pb.append_char('"');
            pb.append_zstr((ZString)v.str_val.chars);
            pb.append_char('"');
        case SYMBOL:
            pb.append_str(syms.get_name(v.sym_val));
        case CONS:
            pb.append_char('(');
            print_value_buf(v.cons_val.car, syms, pb);
            Value* rest = v.cons_val.cdr;
            while (is_cons(rest)) {
                pb.append_char(' ');
                print_value_buf(rest.cons_val.car, syms, pb);
                rest = rest.cons_val.cdr;
            }
            if (!is_nil(rest)) {
                pb.append_str(" . ");
                print_value_buf(rest, syms, pb);
            }
            pb.append_char(')');
        case CLOSURE:
            pb.append_str("#<\xCE\xBB");
            if ((uint)v.closure_val.name != 0) {
                pb.append_char(' ');
                pb.append_str((String)syms.get_name(v.closure_val.name));
            }
            pb.append_str(" (");
            for (usz i = 0; i < v.closure_val.param_count; i++) {
                if (i > 0) pb.append_char(' ');
                pb.append_str((String)syms.get_name(v.closure_val.params[i]));
            }
            if (v.closure_val.has_rest) {
                if (v.closure_val.param_count > 0) pb.append_char(' ');
                pb.append_str(".. ");
                pb.append_str((String)syms.get_name(v.closure_val.rest_param));
            }
            pb.append_str(")>");
        case CONTINUATION:
            pb.append_str("#<continuation>");
        case PRIMITIVE:
            pb.append_str("#<primitive>");
        case PARTIAL_PRIM:
            pb.append_str("#<partial>");
        case ERROR:
            pb.append_str("#<error>");
        case HASHMAP:
            pb.append_char('{');
            uint hprinted = 0;
            for (uint hi = 0; hi < v.hashmap_val.capacity; hi++) {
                if (v.hashmap_val.entries[hi].key != null) {
                    if (hprinted > 0) pb.append_char(' ');
                    print_value_buf(v.hashmap_val.entries[hi].key, syms, pb);
                    pb.append_char(' ');
                    print_value_buf(v.hashmap_val.entries[hi].value, syms, pb);
                    hprinted++;
                }
            }
            pb.append_char('}');
        case ARRAY:
            pb.append_char('[');
            if (v.array_val != null) {
                for (usz vi = 0; vi < v.array_val.length; vi++) {
                    if (vi > 0) pb.append_char(' ');
                    print_value_buf(v.array_val.items[vi], syms, pb);
                }
            }
            pb.append_char(']');
        case MODULE:
            pb.append_str("#<module>");
        case ITERATOR:
            pb.append_str("#<iterator>");
        case COROUTINE:
            pb.append_str("#<coroutine>");
        default:
            pb.append_str("#<unknown>");
    }
}

fn usz print_value_to_buf(Value* v, SymbolTable* syms, char* buf, usz buf_size) {
    PrintBuf pb;
    pb.buf = buf;
    pb.pos = 0;
    pb.capacity = buf_size;
    print_value_buf(v, syms, &pb);
    buf[pb.pos] = 0;
    return pb.pos;
}
