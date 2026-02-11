module lisp;

import std::collections::list;
import std::io;

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
}

/**
 * Symbol — Interned string identifier.
 */
typedef SymbolId = uint;

// Maximum symbol name length
const usz MAX_SYMBOL_LEN = 64;
// Maximum number of symbols
const usz MAX_SYMBOLS = 256;

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
}

fn void SymbolTable.init(SymbolTable* self) {
    self.count = 0;
}

fn SymbolId SymbolTable.intern(SymbolTable* self, char[] name) {
    // Check if already interned
    for (usz i = 0; i < self.count; i++) {
        if (self.entries[i].len == name.len) {
            bool match = true;
            for (usz j = 0; j < name.len; j++) {
                if (self.entries[i].name[j] != name[j]) {
                    match = false;
                    break;
                }
            }
            if (match) {
                return (SymbolId)i;
            }
        }
    }
    // New symbol
    assert(self.count < MAX_SYMBOLS, "symbol table exhausted");
    SymbolId id = (SymbolId)self.count;
    usz len = name.len;
    if (len > MAX_SYMBOL_LEN - 1) len = MAX_SYMBOL_LEN - 1;
    for (usz i = 0; i < len; i++) {
        self.entries[self.count].name[i] = name[i];
    }
    self.entries[self.count].name[len] = 0;
    self.entries[self.count].len = len;
    self.count++;
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
 */
struct Closure {
    SymbolId  param;      // Parameter name (single arg for simplicity)
    Expr*     body;       // Body expression
    Env*      env;        // Captured environment
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
    char[MAX_SYMBOL_LEN] chars;
    usz len;
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
    if (len > MAX_SYMBOL_LEN - 1) len = MAX_SYMBOL_LEN - 1;
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
    v.closure_val.body = body;
    v.closure_val.env = env;
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
    if (len > 31) len = 31;
    for (usz i = 0; i < len; i++) {
        v.prim_val.name[i] = name[i];
    }
    v.prim_val.name[len] = 0;
    v.prim_val.func = func;
    v.prim_val.arity = arity;
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
const usz MAX_BINDINGS = 64;

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
    assert(self.binding_count < MAX_BINDINGS, "environment frame exhausted");
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
    E_DEF,        // (def name val)
    E_QUOTE,      // (quote datum)
    E_RESET,      // (reset body)
    E_SHIFT,      // (shift k body)
    E_PERFORM,    // (perform tag arg)
    E_HANDLE,     // (handle body ((tag k x) handler) ...)
    E_INDEX,      // arr.[0], dict.['key] - OmniLisp index access
    E_PATH,       // point.x, person.address.city - field access
}

struct ExprLit {
    Value* value;
}

struct ExprVar {
    SymbolId name;
}

struct ExprLambda {
    SymbolId param;
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
}

struct ExprDef {
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
        ExprDef     def;
        ExprQuote   quote;
        ExprReset   reset;
        ExprShift   shift;
        ExprPerform perform;
        ExprHandle  handle;
        ExprIndex   index;
        ExprPath    path;
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

    // Simple bump allocator for values (for now)
    Value[4096]  value_pool;
    usz          value_count;

    Env[256]     env_pool;
    usz          env_count;

    Expr[1024]   expr_pool;
    usz          expr_count;

    Continuation[64] cont_pool;
    usz              cont_count;

    // Pre-interned common symbols
    SymbolId sym_lambda;
    SymbolId sym_if;
    SymbolId sym_let;
    SymbolId sym_def;
    SymbolId sym_quote;
    SymbolId sym_reset;
    SymbolId sym_shift;
    SymbolId sym_perform;
    SymbolId sym_handle;
    SymbolId sym_true;
    SymbolId sym_false;

    // Continuation/shift state
    usz      reset_depth;
    bool     shift_occurred;
    usz      shift_target_depth;
    Value*   shift_result;

    // Effect handler stack
    EffectHandler[16] handler_stack;
    usz               handler_count;
}

fn void Interp.init(Interp* self) {
    self.symbols.init();
    self.global_env = null;
    self.value_count = 0;
    self.env_count = 0;
    self.expr_count = 0;
    self.cont_count = 0;

    // Continuation/shift state
    self.reset_depth = 0;
    self.shift_occurred = false;
    self.shift_target_depth = 0;
    self.shift_result = null;
    self.handler_count = 0;

    // Pre-intern keywords
    self.sym_lambda  = self.symbols.intern("lambda");
    self.sym_if      = self.symbols.intern("if");
    self.sym_let     = self.symbols.intern("let");
    self.sym_def     = self.symbols.intern("def");
    self.sym_quote   = self.symbols.intern("quote");
    self.sym_reset   = self.symbols.intern("reset");
    self.sym_shift   = self.symbols.intern("shift");
    self.sym_perform = self.symbols.intern("perform");
    self.sym_handle  = self.symbols.intern("handle");
    self.sym_true    = self.symbols.intern("true");
    self.sym_false   = self.symbols.intern("false");

    // Create global environment
    self.global_env = make_env(self, null);
}

fn Continuation* Interp.alloc_lisp_continuation(Interp* self) {
    assert(self.cont_count < 64, "continuation pool exhausted");
    Continuation* k = &self.cont_pool[self.cont_count];
    self.cont_count++;
    k.data = null;
    return k;
}

fn Value* Interp.alloc_value(Interp* self) {
    assert(self.value_count < 4096, "value pool exhausted");
    Value* v = &self.value_pool[self.value_count];
    self.value_count++;
    return v;
}

fn Env* Interp.alloc_env(Interp* self) {
    assert(self.env_count < 256, "env pool exhausted");
    Env* e = &self.env_pool[self.env_count];
    self.env_count++;
    return e;
}

fn Expr* Interp.alloc_expr(Interp* self) {
    assert(self.expr_count < 1024, "expr pool exhausted");
    Expr* e = &self.expr_pool[self.expr_count];
    self.expr_count++;
    return e;
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
        default:
            io::print("#<unknown>");
    }
}

fn void println_value(Value* v, SymbolTable* syms) {
    print_value(v, syms);
    io::printn("");
}
