module lisp;

import std::io;
import std::collections::list;
import main;

// =============================================================================
// SECTION 1: COMPILER STATE
// =============================================================================

/**
 * Compiler — Translates Lisp AST to C3 source code.
 *
 * The compiler generates C3 code that uses a minimal runtime library
 * for value representation and function invocation.
 *
 * Region-based memory management:
 * - Objects are allocated in regions
 * - Closures track their home region
 * - Write barriers mark objects as escaped when stored across regions
 * - When a region dies, escaped objects are promoted to the parent
 */
struct Compiler {
    Interp*     interp;          // For symbol table access
    char*       output_buf;      // Generated C3 code buffer
    usz         output_len;      // Current length
    usz         output_cap;      // Buffer capacity
    usz         indent;          // Current indentation level
    usz         temp_counter;    // For generating unique temp names
    usz         lambda_counter;  // For generating lambda function names

    // Track captured variables for closure generation
    List{SymbolId} current_captures;

    // Track defined globals for forward declarations
    List{SymbolId} defined_globals;

    // Lambda definitions to emit after scanning
    List{LambdaDef} lambda_defs;

    // Track which lambdas create nested closures (for frame region optimization)
    bool[256] lambda_creates_closure;

    // Track mutable-captured variables (stored in interpreter env, not C3 locals)
    List{SymbolId} mutable_captures;

    // Track variables declared in current function scope (to avoid re-declaration/shadowing)
    List{SymbolId} declared_vars;

    // When true, emit rt_print_value for the last non-define expression
    bool print_last;

    // When true, emit rt_print_value for EVERY non-define expression
    bool print_all;

    // When true, find_free_vars uses is_builtin_primitive instead of is_primitive
    // (for delegation contexts where user globals need injection into interpreter env)
    bool for_delegation;

    // Compiled modules: track module name → exported symbols
    usz compiled_module_count;
    CompiledModule[32] compiled_modules;
}

/**
 * CompiledModule — Tracks a module compiled inline for import resolution.
 */
struct CompiledModule {
    SymbolId     name;
    SymbolId[64] exports;
    usz          export_count;
    bool         loaded;
}

/**
 * LambdaDef — Stored lambda definition for deferred emission.
 */
struct LambdaDef {
    usz         id;              // Lambda ID
    SymbolId    param;           // Parameter name
    Expr*       body;            // Body expression
    SymbolId[16] captures;       // Captured variables
    usz         capture_count;
    bool        creates_closure; // Does this lambda create nested closures?
}

// =============================================================================
// SECTION 1b: PRIMITIVE VARIABLE HASH TABLE
// =============================================================================

const usz PRIM_HASH_SIZE = 256;  // Power of 2, > 2*87
struct PrimHashEntry {
    SymbolId key;
    char[]   emit;
}

// Module-level hash table for primitive variable → C3 code mapping
PrimHashEntry[PRIM_HASH_SIZE] g_prim_hash;
bool g_prim_hash_initialized = false;

fn void prim_hash_insert(SymbolId key, char[] emit) {
    usz idx = ((usz)key) & (PRIM_HASH_SIZE - 1);
    for (usz i = 0; i < PRIM_HASH_SIZE; i++) {
        usz slot = (idx + i) & (PRIM_HASH_SIZE - 1);
        if ((uint)g_prim_hash[slot].key == 0xFFFFFFFF) {
            g_prim_hash[slot].key = key;
            g_prim_hash[slot].emit = emit;
            return;
        }
    }
}

fn char[] prim_hash_lookup(SymbolId key) {
    usz idx = ((usz)key) & (PRIM_HASH_SIZE - 1);
    for (usz i = 0; i < PRIM_HASH_SIZE; i++) {
        usz slot = (idx + i) & (PRIM_HASH_SIZE - 1);
        if ((uint)g_prim_hash[slot].key == 0xFFFFFFFF) return "";
        if ((uint)g_prim_hash[slot].key == (uint)key) return g_prim_hash[slot].emit;
    }
    return "";
}

fn void init_prim_hash(Interp* interp) {
    if (g_prim_hash_initialized) return;
    g_prim_hash_initialized = true;

    // Clear
    for (usz i = 0; i < PRIM_HASH_SIZE; i++) {
        g_prim_hash[i].key = (SymbolId)0xFFFFFFFF;
    }

    SymbolTable* st = &interp.symbols;

    // Arithmetic
    prim_hash_insert(st.intern("+"), "runtime::make_prim(&runtime::rt_add)");
    prim_hash_insert(st.intern("-"), "runtime::make_prim(&runtime::rt_sub)");
    prim_hash_insert(st.intern("*"), "runtime::make_prim(&runtime::rt_mul)");
    prim_hash_insert(st.intern("/"), "runtime::make_prim(&runtime::rt_div)");
    prim_hash_insert(st.intern("%"), "runtime::make_prim(&runtime::rt_mod)");

    // Comparison
    prim_hash_insert(st.intern("="), "runtime::make_prim(&runtime::rt_eq)");
    prim_hash_insert(st.intern("<"), "runtime::make_prim(&runtime::rt_lt)");
    prim_hash_insert(st.intern(">"), "runtime::make_prim(&runtime::rt_gt)");
    prim_hash_insert(st.intern("<="), "runtime::make_prim(&runtime::rt_le)");
    prim_hash_insert(st.intern(">="), "runtime::make_prim(&runtime::rt_ge)");

    // List ops
    prim_hash_insert(st.intern("cons"), "runtime::make_prim(&runtime::rt_cons_prim)");
    prim_hash_insert(st.intern("car"), "runtime::make_prim_unary(&runtime::rt_car)");
    prim_hash_insert(st.intern("cdr"), "runtime::make_prim_unary(&runtime::rt_cdr)");
    prim_hash_insert(st.intern("null?"), "runtime::make_prim_unary(&runtime::rt_null_p)");
    prim_hash_insert(st.intern("pair?"), "runtime::make_prim_unary(&runtime::rt_pair_p)");
    prim_hash_insert(st.intern("list"), "runtime::make_prim_unary(&runtime::rt_list)");
    prim_hash_insert(st.intern("length"), "runtime::make_prim_unary(&runtime::rt_length)");
    prim_hash_insert(st.intern("not"), "runtime::make_prim_unary(&runtime::rt_not)");

    // I/O
    prim_hash_insert(st.intern("print"), "runtime::make_prim_unary(&runtime::rt_print)");
    prim_hash_insert(st.intern("println"), "runtime::make_prim_unary(&runtime::rt_println)");
    prim_hash_insert(st.intern("newline"), "runtime::make_prim_unary(&runtime::rt_newline)");
    prim_hash_insert(st.intern("display"), "runtime::make_prim_unary(&runtime::rt_display)");

    // String ops
    prim_hash_insert(st.intern("string-append"), "runtime::make_prim(&runtime::rt_string_append)");
    prim_hash_insert(st.intern("string-join"), "runtime::make_prim(&runtime::rt_string_join)");
    prim_hash_insert(st.intern("substring"), "runtime::make_prim(&runtime::rt_substring_step1)");
    prim_hash_insert(st.intern("string-split"), "runtime::make_prim(&runtime::rt_string_split)");
    prim_hash_insert(st.intern("string-length"), "runtime::make_prim_unary(&runtime::rt_string_length)");
    prim_hash_insert(st.intern("string->list"), "runtime::make_prim_unary(&runtime::rt_string_to_list)");
    prim_hash_insert(st.intern("list->string"), "runtime::make_prim_unary(&runtime::rt_list_to_string)");
    prim_hash_insert(st.intern("string-upcase"), "runtime::make_prim_unary(&runtime::rt_string_upcase)");
    prim_hash_insert(st.intern("string-downcase"), "runtime::make_prim_unary(&runtime::rt_string_downcase)");
    prim_hash_insert(st.intern("string-trim"), "runtime::make_prim_unary(&runtime::rt_string_trim)");
    prim_hash_insert(st.intern("string->number"), "runtime::make_prim_unary(&runtime::rt_string_to_number)");
    prim_hash_insert(st.intern("number->string"), "runtime::make_prim_unary(&runtime::rt_number_to_string)");

    // Type predicates
    prim_hash_insert(st.intern("string?"), "runtime::make_prim_unary(&runtime::rt_string_p)");
    prim_hash_insert(st.intern("int?"), "runtime::make_prim_unary(&runtime::rt_int_p)");
    prim_hash_insert(st.intern("symbol?"), "runtime::make_prim_unary(&runtime::rt_symbol_p)");
    prim_hash_insert(st.intern("closure?"), "runtime::make_prim_unary(&runtime::rt_closure_p)");
    prim_hash_insert(st.intern("continuation?"), "runtime::make_prim_unary(&runtime::rt_continuation_p)");
    prim_hash_insert(st.intern("double?"), "runtime::make_prim_unary(&runtime::rt_double_p)");
    prim_hash_insert(st.intern("list?"), "runtime::make_prim_unary(&runtime::rt_list_p)");
    prim_hash_insert(st.intern("boolean?"), "runtime::make_prim_unary(&runtime::rt_boolean_p)");
    prim_hash_insert(st.intern("number?"), "runtime::make_prim_unary(&runtime::rt_number_p)");
    prim_hash_insert(st.intern("equal?"), "runtime::make_prim(&runtime::rt_equal_p)");
    prim_hash_insert(st.intern("type-of"), "runtime::make_prim_unary(&runtime::rt_type_of)");

    // File I/O
    prim_hash_insert(st.intern("read-file"), "runtime::make_prim_unary(&runtime::rt_read_file)");
    prim_hash_insert(st.intern("write-file"), "runtime::make_prim(&runtime::rt_write_file)");
    prim_hash_insert(st.intern("file-exists?"), "runtime::make_prim_unary(&runtime::rt_file_exists)");
    prim_hash_insert(st.intern("read-lines"), "runtime::make_prim_unary(&runtime::rt_read_lines)");

    // FFI
    prim_hash_insert(st.intern("ffi-open"), "runtime::make_prim_unary(&runtime::rt_ffi_open)");
    prim_hash_insert(st.intern("ffi-close"), "runtime::make_prim_unary(&runtime::rt_ffi_close)");
    prim_hash_insert(st.intern("ffi-sym"), "runtime::make_prim(&runtime::rt_ffi_sym)");
    prim_hash_insert(st.intern("ffi-call"), "runtime::make_prim(&runtime::rt_ffi_call)");

    // Misc
    prim_hash_insert(st.intern("gensym"), "runtime::make_prim_unary(&runtime::rt_gensym)");
    prim_hash_insert(st.intern("load"), "runtime::make_prim_unary(&runtime::rt_load)");
    prim_hash_insert(st.intern("apply"), "runtime::make_prim(&runtime::rt_apply_prim)");

    // Dict/collection ops
    prim_hash_insert(st.intern("dict"), "runtime::make_prim_unary(&runtime::rt_hash_map_create)");
    prim_hash_insert(st.intern("dict-set!"), "runtime::make_prim(&runtime::rt_hash_set)");
    prim_hash_insert(st.intern("dict?"), "runtime::make_prim_unary(&runtime::rt_hash_map_p)");
    prim_hash_insert(st.intern("ref"), "runtime::make_prim(&runtime::rt_hash_ref)");
    prim_hash_insert(st.intern("keys"), "runtime::make_prim_unary(&runtime::rt_hash_keys)");
    prim_hash_insert(st.intern("values"), "runtime::make_prim_unary(&runtime::rt_hash_values)");
    prim_hash_insert(st.intern("has?"), "runtime::make_prim(&runtime::rt_hash_has)");
    prim_hash_insert(st.intern("remove!"), "runtime::make_prim(&runtime::rt_hash_remove)");
    prim_hash_insert(st.intern("push!"), "runtime::make_prim(&runtime::rt_hash_set)");

    // Math
    prim_hash_insert(st.intern("abs"), "runtime::make_prim_unary(&runtime::rt_abs)");
    prim_hash_insert(st.intern("min"), "runtime::make_prim(&runtime::rt_min)");
    prim_hash_insert(st.intern("max"), "runtime::make_prim(&runtime::rt_max)");
    prim_hash_insert(st.intern("floor"), "runtime::make_prim_unary(&runtime::rt_floor)");
    prim_hash_insert(st.intern("ceiling"), "runtime::make_prim_unary(&runtime::rt_ceiling)");
    prim_hash_insert(st.intern("round"), "runtime::make_prim_unary(&runtime::rt_round)");
    prim_hash_insert(st.intern("truncate"), "runtime::make_prim_unary(&runtime::rt_truncate)");
    prim_hash_insert(st.intern("sqrt"), "runtime::make_prim_unary(&runtime::rt_sqrt)");
    prim_hash_insert(st.intern("even?"), "runtime::make_prim_unary(&runtime::rt_even_p)");
    prim_hash_insert(st.intern("odd?"), "runtime::make_prim_unary(&runtime::rt_odd_p)");
    prim_hash_insert(st.intern("zero?"), "runtime::make_prim_unary(&runtime::rt_zero_p)");
    prim_hash_insert(st.intern("positive?"), "runtime::make_prim_unary(&runtime::rt_positive_p)");
    prim_hash_insert(st.intern("negative?"), "runtime::make_prim_unary(&runtime::rt_negative_p)");
    prim_hash_insert(st.intern("gcd"), "runtime::make_prim(&runtime::rt_gcd)");

    // Bitwise
    prim_hash_insert(st.intern("bitwise-and"), "runtime::make_prim(&runtime::rt_bitwise_and)");
    prim_hash_insert(st.intern("bitwise-or"), "runtime::make_prim(&runtime::rt_bitwise_or)");
    prim_hash_insert(st.intern("bitwise-xor"), "runtime::make_prim(&runtime::rt_bitwise_xor)");
    prim_hash_insert(st.intern("bitwise-not"), "runtime::make_prim_unary(&runtime::rt_bitwise_not)");
    prim_hash_insert(st.intern("lshift"), "runtime::make_prim(&runtime::rt_lshift)");
    prim_hash_insert(st.intern("rshift"), "runtime::make_prim(&runtime::rt_rshift)");

    // Literals
    prim_hash_insert(st.intern("true"), "runtime::make_true()");
    prim_hash_insert(st.intern("false"), "runtime::make_nil()");
    prim_hash_insert(st.intern("nil"), "runtime::make_nil()");
}

// =============================================================================
// SECTION 2: COMPILER INITIALIZATION
// =============================================================================

fn void Compiler.init(Compiler* self, Interp* interp) {
    self.interp = interp;
    // Lists auto-initialize when declared
    self.indent = 0;
    self.temp_counter = 0;
    self.lambda_counter = 0;
    self.output_cap = 8192;
    self.output_buf = (char*)mem::malloc(self.output_cap);
    self.output_len = 0;
    init_prim_hash(interp);
}

fn void Compiler.free(Compiler* self) {
    if (self.output_buf != null) { mem::free(self.output_buf); self.output_buf = null; }
    self.current_captures.free();
    self.defined_globals.free();
    self.lambda_defs.free();
    self.mutable_captures.free();
    self.declared_vars.free();
}

fn bool Compiler.is_declared(Compiler* self, SymbolId sym) {
    foreach (s : self.declared_vars) {
        if ((uint)s == (uint)sym) return true;
    }
    return false;
}

fn void Compiler.mark_declared(Compiler* self, SymbolId sym) {
    if (!self.is_declared(sym)) {
        self.declared_vars.push(sym);
    }
}

// =============================================================================
// SECTION 3: OUTPUT HELPERS
// =============================================================================

fn void Compiler.ensure_capacity(Compiler* self, usz needed) {
    if (self.output_len + needed > self.output_cap) {
        usz new_cap = self.output_cap * 2;
        if (new_cap < self.output_len + needed) { new_cap = self.output_len + needed; }
        char* new_buf = (char*)mem::malloc(new_cap);
        mem::copy(new_buf, self.output_buf, self.output_len);
        mem::free(self.output_buf);
        self.output_buf = new_buf;
        self.output_cap = new_cap;
    }
}

fn void Compiler.emit(Compiler* self, char[] s) {
    self.ensure_capacity(s.len);
    mem::copy(&self.output_buf[self.output_len], s.ptr, s.len);
    self.output_len += s.len;
}

fn void Compiler.emit_char(Compiler* self, char c) {
    self.ensure_capacity(1);
    self.output_buf[self.output_len] = c;
    self.output_len++;
}

/**
 * Emit a string into the output with escaping for C3 string literal context.
 * Replaces \ with \\ and " with \" to prevent code injection via symbol names.
 */
fn void Compiler.emit_escaped(Compiler* self, char[] s) {
    foreach (c : s) {
        if (c == '"') {
            self.emit("\\\"");
        } else if (c == '\\') {
            self.emit("\\\\");
        } else if (c == '\n') {
            self.emit("\\n");
        } else if (c == '\t') {
            self.emit("\\t");
        } else {
            self.emit_char(c);
        }
    }
}

fn void Compiler.emit_line(Compiler* self, char[] s) {
    self.emit_indent();
    self.emit(s);
    self.emit_char('\n');
}

fn void Compiler.emit_indent(Compiler* self) {
    for (usz i = 0; i < self.indent; i++) {
        self.emit("    ");
    }
}

fn void Compiler.emit_newline(Compiler* self) {
    self.emit_char('\n');
}

fn void Compiler.emit_int(Compiler* self, long n) {
    // Convert integer to string manually
    if (n == 0) {
        self.emit_char('0');
        return;
    }

    bool negative = n < 0;
    if (negative) {
        self.emit_char('-');
        n = -n;
    }

    char[32] buf;
    usz pos = 0;
    while (n > 0 && pos < 31) {
        buf[pos++] = (char)('0' + (n % 10));
        n /= 10;
    }

    // Reverse
    for (isz i = (isz)pos - 1; i >= 0; i--) {
        self.emit_char(buf[(usz)i]);
    }
}

fn void Compiler.emit_usz(Compiler* self, usz n) {
    self.emit_int((long)n);
}

fn bool is_c3_reserved(char[] name) {
    char[][*] reserved = {"try", "var", "fn", "if", "else", "for", "while", "do",
        "switch", "case", "default", "return", "break", "continue", "import",
        "module", "struct", "enum", "union", "const", "static", "true", "false",
        "null", "void", "bool", "int", "uint", "char", "float", "double", "usz"};
    foreach (r : reserved) {
        if (name.len == r.len) {
            bool match = true;
            for (usz i = 0; i < name.len; i++) {
                if (name[i] != r[i]) { match = false; break; }
            }
            if (match) return true;
        }
    }
    return false;
}

fn void Compiler.emit_symbol_name(Compiler* self, SymbolId sym) {
    char[] name = self.interp.symbols.get_name(sym);
    // Escape C3 reserved words
    if (is_c3_reserved(name)) {
        self.emit("_pika_");
    }
    // Sanitize name for C3 (replace - with _)
    foreach (c : name) {
        if (c == '-') {
            self.emit_char('_');
        } else if (c == '?') {
            self.emit("_p");  // predicate suffix
        } else if (c == '!') {
            self.emit("_bang");
        } else if (c == '>') {
            self.emit("_to_");
        } else if (c == '<') {
            self.emit("_lt_");
        } else if (c == '=') {
            self.emit("_eq_");
        } else if (c == '+') {
            self.emit("_plus_");
        } else if (c == '*') {
            self.emit("_star_");
        } else if (c == '/') {
            self.emit("_slash_");
        } else if (c == '%') {
            self.emit("_mod_");
        } else {
            self.emit_char(c);
        }
    }
}

fn char[] Compiler.get_output(Compiler* self) {
    return self.output_buf[:self.output_len];
}

// =============================================================================
// SECTION 4: FREE VARIABLE ANALYSIS
// =============================================================================

/**
 * Find free variables in an expression that need to be captured.
 * bound_vars contains variables that are bound in the current scope.
 */
fn void Compiler.find_free_vars(Compiler* self, Expr* expr, List{SymbolId}* bound_vars, List{SymbolId}* free_vars, List{SymbolId}* enclosing_scope) {
    if (expr == null) return;

    switch (expr.tag) {
        case E_LIT:
            // Literals have no free variables
            return;

        case E_VAR:
            SymbolId name = expr.var_expr.name;
            // Check if bound locally (this lambda's own params/lets)
            foreach (b : *bound_vars) {
                if ((uint)b == (uint)name) return;
            }
            // Check if in enclosing scope (outer lambda params/lets) — must capture,
            // even if the name also exists as a global define
            if (enclosing_scope != null) {
                foreach (e : *enclosing_scope) {
                    if ((uint)e == (uint)name) {
                        foreach (f : *free_vars) {
                            if ((uint)f == (uint)name) return;
                        }
                        free_vars.push(name);
                        return;
                    }
                }
            }
            // Check if it's a global primitive (for delegation, only skip builtins)
            if (self.for_delegation) {
                if (self.is_builtin_primitive(name)) return;
            } else {
                if (self.is_primitive(name)) return;
            }
            // Check if it's a mutable-captured variable (accessed via interpreter env)
            if (self.is_mutable_captured_var(name)) return;
            // Check if already in free vars
            foreach (f : *free_vars) {
                if ((uint)f == (uint)name) return;
            }
            // It's a free variable
            free_vars.push(name);

        case E_LAMBDA:
            // Add ALL parameters to bound vars (multi-param lambdas have param_count > 1)
            List{SymbolId} new_bound;
            foreach (b : *bound_vars) {
                new_bound.push(b);
            }
            if (expr.lambda.param_count > 1) {
                for (usz pi = 0; pi < expr.lambda.param_count; pi++) {
                    new_bound.push(expr.lambda.params[pi]);
                }
            } else if ((uint)expr.lambda.param != 0xFFFFFFFF) {
                new_bound.push(expr.lambda.param);
            }
            self.find_free_vars(expr.lambda.body, &new_bound, free_vars, enclosing_scope);
            new_bound.free();

        case E_APP:
            self.find_free_vars(expr.app.func, bound_vars, free_vars, enclosing_scope);
            self.find_free_vars(expr.app.arg, bound_vars, free_vars, enclosing_scope);

        case E_IF:
            self.find_free_vars(expr.if_expr.test, bound_vars, free_vars, enclosing_scope);
            self.find_free_vars(expr.if_expr.then_branch, bound_vars, free_vars, enclosing_scope);
            self.find_free_vars(expr.if_expr.else_branch, bound_vars, free_vars, enclosing_scope);

        case E_LET:
            // For recursive let, name is bound in both init and body
            List{SymbolId} let_bound;
            foreach (b : *bound_vars) {
                let_bound.push(b);
            }
            if (expr.let_expr.is_recursive) {
                let_bound.push(expr.let_expr.name);
            }
            // Init expression
            self.find_free_vars(expr.let_expr.init, &let_bound, free_vars, enclosing_scope);
            // Body with name bound
            if (!expr.let_expr.is_recursive) {
                let_bound.push(expr.let_expr.name);
            }
            self.find_free_vars(expr.let_expr.body, &let_bound, free_vars, enclosing_scope);
            let_bound.free();

        case E_DEFINE:
            self.find_free_vars(expr.define.value, bound_vars, free_vars, enclosing_scope);

        case E_QUOTE:
            // Quoted data has no free variables
            return;

        case E_AND:
            self.find_free_vars(expr.and_expr.left, bound_vars, free_vars, enclosing_scope);
            self.find_free_vars(expr.and_expr.right, bound_vars, free_vars, enclosing_scope);

        case E_OR:
            self.find_free_vars(expr.or_expr.left, bound_vars, free_vars, enclosing_scope);
            self.find_free_vars(expr.or_expr.right, bound_vars, free_vars, enclosing_scope);

        case E_MATCH:
            self.find_free_vars(expr.match.scrutinee, bound_vars, free_vars, enclosing_scope);
            for (usz i = 0; i < expr.match.clause_count; i++) {
                // Add pattern bindings to bound vars
                List{SymbolId} clause_bound;
                foreach (b : *bound_vars) {
                    clause_bound.push(b);
                }
                self.collect_pattern_bindings(expr.match.clauses[i].pattern, &clause_bound);
                self.find_free_vars(expr.match.clauses[i].result, &clause_bound, free_vars, enclosing_scope);
                clause_bound.free();
            }

        case E_CALL:
            self.find_free_vars(expr.call.func, bound_vars, free_vars, enclosing_scope);
            for (usz i = 0; i < expr.call.arg_count; i++) {
                self.find_free_vars(expr.call.args[i], bound_vars, free_vars, enclosing_scope);
            }

        case E_INDEX:
            self.find_free_vars(expr.index.collection, bound_vars, free_vars, enclosing_scope);
            self.find_free_vars(expr.index.index, bound_vars, free_vars, enclosing_scope);

        case E_PATH:
            // First segment is a variable reference
            SymbolId path_name = expr.path.segments[0];
            foreach (b : *bound_vars) {
                if ((uint)b == (uint)path_name) return;
            }
            // Check enclosing scope before is_primitive (enclosing var shadows global)
            if (enclosing_scope != null) {
                foreach (e : *enclosing_scope) {
                    if ((uint)e == (uint)path_name) {
                        foreach (f : *free_vars) {
                            if ((uint)f == (uint)path_name) return;
                        }
                        free_vars.push(path_name);
                        return;
                    }
                }
            }
            if (self.is_primitive(path_name)) return;
            foreach (f : *free_vars) {
                if ((uint)f == (uint)path_name) return;
            }
            free_vars.push(path_name);

        case E_RESET:
            self.find_free_vars(expr.reset.body, bound_vars, free_vars, enclosing_scope);

        case E_SHIFT:
            // k_name is bound in the body
            List{SymbolId} shift_bound;
            foreach (b : *bound_vars) {
                shift_bound.push(b);
            }
            shift_bound.push(expr.shift.k_name);
            self.find_free_vars(expr.shift.body, &shift_bound, free_vars, enclosing_scope);
            shift_bound.free();

        case E_PERFORM:
            self.find_free_vars(expr.perform.arg, bound_vars, free_vars, enclosing_scope);

        case E_HANDLE:
            self.find_free_vars(expr.handle.body, bound_vars, free_vars, enclosing_scope);
            for (usz i = 0; i < expr.handle.clause_count; i++) {
                // k_name and arg_name are bound in handler body
                List{SymbolId} handle_bound;
                foreach (b : *bound_vars) {
                    handle_bound.push(b);
                }
                handle_bound.push(expr.handle.clauses[i].k_name);
                handle_bound.push(expr.handle.clauses[i].arg_name);
                self.find_free_vars(expr.handle.clauses[i].handler_body, &handle_bound, free_vars, enclosing_scope);
                handle_bound.free();
            }

        case E_BEGIN:
            for (usz i = 0; i < expr.begin.expr_count; i++) {
                self.find_free_vars(expr.begin.exprs[i], bound_vars, free_vars, enclosing_scope);
            }

        case E_QUASIQUOTE:
            // Only look for free vars inside unquote nodes — the rest is quoted (literal symbols)
            self.find_free_vars_in_qq(expr.quasiquote.body, bound_vars, free_vars, enclosing_scope);

        case E_UNQUOTE:
            self.find_free_vars(expr.unquote.body, bound_vars, free_vars, enclosing_scope);

        case E_UNQUOTE_SPLICING:
            self.find_free_vars(expr.unquote_splicing.body, bound_vars, free_vars, enclosing_scope);

        case E_DEFMACRO:
            // Macro templates may reference free vars at expansion time
            // Conservative: treat as having no free vars (macros expand at compile time)
            return;

        case E_MODULE:
            for (usz i = 0; i < expr.module_expr.body_count; i++) {
                self.find_free_vars(expr.module_expr.body[i], bound_vars, free_vars, enclosing_scope);
            }

        case E_IMPORT:
            // No free variables
            return;

        case E_SET:
            // The variable being set might be free
            SymbolId set_name = expr.set_expr.name;
            foreach (b : *bound_vars) {
                if ((uint)b == (uint)set_name) {
                    // Bound locally, just analyze the value
                    self.find_free_vars(expr.set_expr.value, bound_vars, free_vars, enclosing_scope);
                    return;
                }
            }
            // Check enclosing scope before is_primitive (enclosing var shadows global)
            if (enclosing_scope != null) {
                foreach (e : *enclosing_scope) {
                    if ((uint)e == (uint)set_name) {
                        bool set_enc_free = false;
                        foreach (f : *free_vars) {
                            if ((uint)f == (uint)set_name) { set_enc_free = true; break; }
                        }
                        if (!set_enc_free) {
                            free_vars.push(set_name);
                        }
                        self.find_free_vars(expr.set_expr.value, bound_vars, free_vars, enclosing_scope);
                        return;
                    }
                }
            }
            if (self.is_primitive(set_name)) {
                self.find_free_vars(expr.set_expr.value, bound_vars, free_vars, enclosing_scope);
                return;
            }
            // Check if already in free vars
            bool set_already_free = false;
            foreach (f : *free_vars) {
                if ((uint)f == (uint)set_name) { set_already_free = true; break; }
            }
            if (!set_already_free) {
                free_vars.push(set_name);
            }
            self.find_free_vars(expr.set_expr.value, bound_vars, free_vars, enclosing_scope);

        default:
            // Handle other expression types conservatively
            return;
    }
}

/**
 * Traverse quasiquote body, only descending into unquote/unquote-splicing for free vars.
 * In quasiquote, bare symbols are literals — only unquoted expressions reference variables.
 */
fn void Compiler.find_free_vars_in_qq(Compiler* self, Expr* expr, List{SymbolId}* bound_vars, List{SymbolId}* free_vars, List{SymbolId}* enclosing_scope) {
    if (expr == null) return;
    switch (expr.tag) {
        case E_UNQUOTE:
            // Unquoted expression — evaluate normally, so scan for free vars
            self.find_free_vars(expr.unquote.body, bound_vars, free_vars, enclosing_scope);
        case E_UNQUOTE_SPLICING:
            self.find_free_vars(expr.unquote_splicing.body, bound_vars, free_vars, enclosing_scope);
        case E_CALL:
            // Lists in qq template are parsed as E_CALL — traverse to find unquotes
            self.find_free_vars_in_qq(expr.call.func, bound_vars, free_vars, enclosing_scope);
            for (usz i = 0; i < expr.call.arg_count; i++) {
                self.find_free_vars_in_qq(expr.call.args[i], bound_vars, free_vars, enclosing_scope);
            }
        case E_APP:
            self.find_free_vars_in_qq(expr.app.func, bound_vars, free_vars, enclosing_scope);
            self.find_free_vars_in_qq(expr.app.arg, bound_vars, free_vars, enclosing_scope);
        case E_QUASIQUOTE:
            // Nested quasiquote — still only scan for unquotes
            self.find_free_vars_in_qq(expr.quasiquote.body, bound_vars, free_vars, enclosing_scope);
        default:
            // E_VAR, E_LIT, etc. in qq context are literal — no free vars
            return;
    }
}

fn void Compiler.collect_pattern_bindings(Compiler* self, Pattern* pat, List{SymbolId}* bindings) {
    if (pat == null) return;

    switch (pat.tag) {
        case PAT_VAR:
            bindings.push(pat.var_name);

        case PAT_CONS:
            self.collect_pattern_bindings(pat.car_pat, bindings);
            self.collect_pattern_bindings(pat.cdr_pat, bindings);

        case PAT_SEQ:
            for (usz i = 0; i < pat.elem_count; i++) {
                self.collect_pattern_bindings(pat.elements[i], bindings);
            }
            if (pat.rest_pos == REST_MIDDLE) {
                bindings.push(pat.rest_binding);
            }

        default:
            return;
    }
}

fn bool Compiler.is_primitive(Compiler* self, SymbolId sym) {
    char[] name = self.interp.symbols.get_name(sym);

    // Check known primitives
    char[][*] primitives = {
        "+", "-", "*", "/", "%",
        "=", "<", ">", "<=", ">=",
        "cons", "car", "cdr", "null?", "pair?",
        "list", "length", "not",
        "print", "println", "newline", "display",
        "string-append", "string-join", "substring",
        "string-split", "string-length", "string->list",
        "list->string", "string-upcase", "string-downcase",
        "string-trim", "string?", "int?", "symbol?",
        "closure?", "continuation?", "double?", "list?",
        "boolean?", "number?",
        "string->number", "number->string",
        "gensym", "load", "apply", "equal?",
        "dict", "dict-set!", "dict?",
        "ref", "push!", "keys", "values", "has?", "remove!",
        "read-file", "write-file", "file-exists?", "read-lines",
        "ffi-open", "ffi-call", "ffi-close", "ffi-sym",
        "type-of",
        "abs", "min", "max", "floor", "ceiling", "round",
        "truncate", "sqrt", "even?", "odd?", "zero?",
        "positive?", "negative?", "gcd",
        "bitwise-and", "bitwise-or", "bitwise-xor",
        "bitwise-not", "lshift", "rshift",
        "true", "false", "nil"
    };

    foreach (prim : primitives) {
        if (self.str_eq(name, prim)) return true;
    }

    // Check if it's a defined global
    foreach (g : self.defined_globals) {
        if ((uint)g == (uint)sym) return true;
    }

    return false;
}

fn bool Compiler.is_mutable_captured_var(Compiler* self, SymbolId sym) {
    foreach (mc : self.mutable_captures) {
        if ((uint)mc == (uint)sym) return true;
    }
    return false;
}

fn bool Compiler.str_eq(Compiler* self, char[] a, char[] b) {
    if (a.len != b.len) return false;
    for (usz i = 0; i < a.len; i++) {
        if (a[i] != b[i]) return false;
    }
    return true;
}

// =============================================================================
// SECTION 4a1: MUTABLE CAPTURE PRESCAN
// =============================================================================

/**
 * Pre-scan the entire expression tree to identify all mutable-captured variables.
 * Must be called before scan_lambdas so that lambda scanning can exclude
 * mutable-captured variables from closure struct captures.
 */
fn void Compiler.prescan_mutable_captures(Compiler* self, Expr* expr) {
    if (expr == null) return;
    switch (expr.tag) {
        case E_LET:
            // Check if this let-bound variable is mutable-captured
            if (self.is_mutable_capture(expr.let_expr.name, expr.let_expr.body)) {
                // Add to mutable captures if not already present
                bool already = false;
                foreach (mc : self.mutable_captures) {
                    if ((uint)mc == (uint)expr.let_expr.name) { already = true; break; }
                }
                if (!already) {
                    self.mutable_captures.push(expr.let_expr.name);
                }
            }
            self.prescan_mutable_captures(expr.let_expr.init);
            self.prescan_mutable_captures(expr.let_expr.body);
        case E_LAMBDA:
            self.prescan_mutable_captures(expr.lambda.body);
        case E_APP:
            self.prescan_mutable_captures(expr.app.func);
            self.prescan_mutable_captures(expr.app.arg);
        case E_IF:
            self.prescan_mutable_captures(expr.if_expr.test);
            self.prescan_mutable_captures(expr.if_expr.then_branch);
            self.prescan_mutable_captures(expr.if_expr.else_branch);
        case E_DEFINE:
            self.prescan_mutable_captures(expr.define.value);
        case E_BEGIN:
            for (usz i = 0; i < expr.begin.expr_count; i++) {
                self.prescan_mutable_captures(expr.begin.exprs[i]);
            }
        case E_CALL:
            self.prescan_mutable_captures(expr.call.func);
            for (usz i = 0; i < expr.call.arg_count; i++) {
                self.prescan_mutable_captures(expr.call.args[i]);
            }
        case E_AND:
            self.prescan_mutable_captures(expr.and_expr.left);
            self.prescan_mutable_captures(expr.and_expr.right);
        case E_OR:
            self.prescan_mutable_captures(expr.or_expr.left);
            self.prescan_mutable_captures(expr.or_expr.right);
        case E_MATCH:
            self.prescan_mutable_captures(expr.match.scrutinee);
            for (usz i = 0; i < expr.match.clause_count; i++) {
                self.prescan_mutable_captures(expr.match.clauses[i].result);
            }
        case E_SET:
            self.prescan_mutable_captures(expr.set_expr.value);
        case E_RESET:
            self.prescan_mutable_captures(expr.reset.body);
        case E_SHIFT:
            self.prescan_mutable_captures(expr.shift.body);
        case E_HANDLE:
            self.prescan_mutable_captures(expr.handle.body);
            for (usz i = 0; i < expr.handle.clause_count; i++) {
                self.prescan_mutable_captures(expr.handle.clauses[i].handler_body);
            }
        case E_PERFORM:
            self.prescan_mutable_captures(expr.perform.arg);
        default:
            return;
    }
}

// =============================================================================
// SECTION 4a2: MUTABLE CAPTURE DETECTION
// =============================================================================

/**
 * Check if a variable is both captured by a nested lambda AND mutated with set!.
 * This is done by scanning the expression tree for (set! name ...) where name
 * is a free variable of some nested lambda.
 *
 * We detect: is `name` set! inside any sub-expression that also contains
 * a lambda that captures `name`?
 */
fn bool Compiler.is_mutable_capture(Compiler* self, SymbolId name, Expr* body) {
    // Check if name is set! anywhere AND captured by any nested lambda
    bool has_set = self.has_set_on(name, body);
    bool has_capture = self.is_captured_by_nested_lambda(name, body);
    return has_set && has_capture;
}

fn bool Compiler.has_set_on(Compiler* self, SymbolId name, Expr* expr) {
    if (expr == null) return false;
    switch (expr.tag) {
        case E_SET:
            if ((uint)expr.set_expr.name == (uint)name) return true;
            return self.has_set_on(name, expr.set_expr.value);
        case E_APP:
            return self.has_set_on(name, expr.app.func) || self.has_set_on(name, expr.app.arg);
        case E_IF:
            return self.has_set_on(name, expr.if_expr.test) ||
                   self.has_set_on(name, expr.if_expr.then_branch) ||
                   self.has_set_on(name, expr.if_expr.else_branch);
        case E_LET:
            return self.has_set_on(name, expr.let_expr.init) || self.has_set_on(name, expr.let_expr.body);
        case E_LAMBDA:
            return self.has_set_on(name, expr.lambda.body);
        case E_BEGIN:
            for (usz i = 0; i < expr.begin.expr_count; i++) {
                if (self.has_set_on(name, expr.begin.exprs[i])) return true;
            }
            return false;
        case E_CALL:
            if (self.has_set_on(name, expr.call.func)) return true;
            for (usz i = 0; i < expr.call.arg_count; i++) {
                if (self.has_set_on(name, expr.call.args[i])) return true;
            }
            return false;
        case E_AND:
            return self.has_set_on(name, expr.and_expr.left) || self.has_set_on(name, expr.and_expr.right);
        case E_OR:
            return self.has_set_on(name, expr.or_expr.left) || self.has_set_on(name, expr.or_expr.right);
        case E_MATCH:
            if (self.has_set_on(name, expr.match.scrutinee)) return true;
            for (usz i = 0; i < expr.match.clause_count; i++) {
                if (self.has_set_on(name, expr.match.clauses[i].result)) return true;
            }
            return false;
        default:
            return false;
    }
}

fn bool Compiler.is_captured_by_nested_lambda(Compiler* self, SymbolId name, Expr* expr) {
    if (expr == null) return false;
    switch (expr.tag) {
        case E_LAMBDA:
            // Check if this lambda captures name as a free variable
            List{SymbolId} bound;
            if ((uint)expr.lambda.param != 0xFFFFFFFF) {
                bound.push(expr.lambda.param);
            }
            List{SymbolId} free;
            self.find_free_vars(expr.lambda.body, &bound, &free, null);
            bool captured = false;
            foreach (f : free) {
                if ((uint)f == (uint)name) { captured = true; break; }
            }
            bound.free();
            free.free();
            if (captured) return true;
            // Also check deeper nesting
            return self.is_captured_by_nested_lambda(name, expr.lambda.body);
        case E_APP:
            return self.is_captured_by_nested_lambda(name, expr.app.func) ||
                   self.is_captured_by_nested_lambda(name, expr.app.arg);
        case E_IF:
            return self.is_captured_by_nested_lambda(name, expr.if_expr.test) ||
                   self.is_captured_by_nested_lambda(name, expr.if_expr.then_branch) ||
                   self.is_captured_by_nested_lambda(name, expr.if_expr.else_branch);
        case E_LET:
            return self.is_captured_by_nested_lambda(name, expr.let_expr.init) ||
                   self.is_captured_by_nested_lambda(name, expr.let_expr.body);
        case E_BEGIN:
            for (usz i = 0; i < expr.begin.expr_count; i++) {
                if (self.is_captured_by_nested_lambda(name, expr.begin.exprs[i])) return true;
            }
            return false;
        case E_CALL:
            if (self.is_captured_by_nested_lambda(name, expr.call.func)) return true;
            for (usz i = 0; i < expr.call.arg_count; i++) {
                if (self.is_captured_by_nested_lambda(name, expr.call.args[i])) return true;
            }
            return false;
        case E_AND:
            return self.is_captured_by_nested_lambda(name, expr.and_expr.left) ||
                   self.is_captured_by_nested_lambda(name, expr.and_expr.right);
        case E_OR:
            return self.is_captured_by_nested_lambda(name, expr.or_expr.left) ||
                   self.is_captured_by_nested_lambda(name, expr.or_expr.right);
        case E_MATCH:
            if (self.is_captured_by_nested_lambda(name, expr.match.scrutinee)) return true;
            for (usz i = 0; i < expr.match.clause_count; i++) {
                if (self.is_captured_by_nested_lambda(name, expr.match.clauses[i].result)) return true;
            }
            return false;
        case E_SET:
            return self.is_captured_by_nested_lambda(name, expr.set_expr.value);
        default:
            return false;
    }
}

// =============================================================================
// SECTION 4b: CLOSURE CREATION ANALYSIS
// =============================================================================

/**
 * Check if an expression body creates closures that might escape.
 * Used to determine if a lambda needs frame region management.
 *
 * A lambda "creates closures" if its body contains lambda expressions.
 * This is conservative but correct - if a lambda is created, it might escape.
 */
// =============================================================================
// SECTION 4c: EXPRESSION SERIALIZER
// =============================================================================

/**
 * Serialize an Expr* back to Pika source text.
 * This is used to convert continuation forms (reset/shift/handle/perform)
 * into source strings that can be evaluated by the interpreter at runtime.
 *
 * The serialized output is appended to the compiler's output buffer temporarily
 * via a separate List{char} buffer.
 */
fn void Compiler.serialize_expr_to_buf(Compiler* self, Expr* expr, List{char}* buf) {
    if (expr == null) {
        self.buf_append(buf, "nil");
        return;
    }

    switch (expr.tag) {
        case E_LIT:
            self.serialize_value_to_buf(expr.lit.value, buf);

        case E_VAR:
            char[] name = self.interp.symbols.get_name(expr.var_expr.name);
            self.buf_append(buf, name);

        case E_LAMBDA:
            self.buf_append(buf, "(lambda (");
            // Check for zero-arg lambda
            if ((uint)expr.lambda.param == 0xFFFFFFFF) {
                // zero-arg
            } else {
                char[] pname = self.interp.symbols.get_name(expr.lambda.param);
                self.buf_append(buf, pname);
            }
            self.buf_append(buf, ") ");
            self.serialize_expr_to_buf(expr.lambda.body, buf);
            buf.push(')');

        case E_APP:
            buf.push('(');
            self.serialize_expr_to_buf(expr.app.func, buf);
            buf.push(' ');
            self.serialize_expr_to_buf(expr.app.arg, buf);
            buf.push(')');

        case E_IF:
            self.buf_append(buf, "(if ");
            self.serialize_expr_to_buf(expr.if_expr.test, buf);
            buf.push(' ');
            self.serialize_expr_to_buf(expr.if_expr.then_branch, buf);
            buf.push(' ');
            self.serialize_expr_to_buf(expr.if_expr.else_branch, buf);
            buf.push(')');

        case E_LET:
            if (expr.let_expr.is_recursive) {
                self.buf_append(buf, "(let ^rec ((");
            } else {
                self.buf_append(buf, "(let ((");
            }
            char[] let_name = self.interp.symbols.get_name(expr.let_expr.name);
            self.buf_append(buf, let_name);
            buf.push(' ');
            self.serialize_expr_to_buf(expr.let_expr.init, buf);
            self.buf_append(buf, ")) ");
            self.serialize_expr_to_buf(expr.let_expr.body, buf);
            buf.push(')');

        case E_DEFINE:
            self.buf_append(buf, "(define ");
            char[] def_name = self.interp.symbols.get_name(expr.define.name);
            self.buf_append(buf, def_name);
            buf.push(' ');
            self.serialize_expr_to_buf(expr.define.value, buf);
            buf.push(')');

        case E_QUOTE:
            buf.push('\'');
            self.serialize_value_to_buf(expr.quote.datum, buf);

        case E_RESET:
            self.buf_append(buf, "(reset ");
            self.serialize_expr_to_buf(expr.reset.body, buf);
            buf.push(')');

        case E_SHIFT:
            self.buf_append(buf, "(shift ");
            char[] k_name = self.interp.symbols.get_name(expr.shift.k_name);
            self.buf_append(buf, k_name);
            buf.push(' ');
            self.serialize_expr_to_buf(expr.shift.body, buf);
            buf.push(')');

        case E_PERFORM:
            self.buf_append(buf, "(perform ");
            char[] tag_name = self.interp.symbols.get_name(expr.perform.tag);
            self.buf_append(buf, tag_name);
            buf.push(' ');
            self.serialize_expr_to_buf(expr.perform.arg, buf);
            buf.push(')');

        case E_HANDLE:
            self.buf_append(buf, "(handle ");
            self.serialize_expr_to_buf(expr.handle.body, buf);
            for (usz i = 0; i < expr.handle.clause_count; i++) {
                self.buf_append(buf, " ((");
                char[] etag = self.interp.symbols.get_name(expr.handle.clauses[i].effect_tag);
                self.buf_append(buf, etag);
                buf.push(' ');
                char[] ek = self.interp.symbols.get_name(expr.handle.clauses[i].k_name);
                self.buf_append(buf, ek);
                buf.push(' ');
                char[] ea = self.interp.symbols.get_name(expr.handle.clauses[i].arg_name);
                self.buf_append(buf, ea);
                self.buf_append(buf, ") ");
                self.serialize_expr_to_buf(expr.handle.clauses[i].handler_body, buf);
                buf.push(')');
            }
            buf.push(')');

        case E_AND:
            self.buf_append(buf, "(and ");
            self.serialize_expr_to_buf(expr.and_expr.left, buf);
            buf.push(' ');
            self.serialize_expr_to_buf(expr.and_expr.right, buf);
            buf.push(')');

        case E_OR:
            self.buf_append(buf, "(or ");
            self.serialize_expr_to_buf(expr.or_expr.left, buf);
            buf.push(' ');
            self.serialize_expr_to_buf(expr.or_expr.right, buf);
            buf.push(')');

        case E_BEGIN:
            self.buf_append(buf, "(begin");
            for (usz i = 0; i < expr.begin.expr_count; i++) {
                buf.push(' ');
                self.serialize_expr_to_buf(expr.begin.exprs[i], buf);
            }
            buf.push(')');

        case E_CALL:
            buf.push('(');
            self.serialize_expr_to_buf(expr.call.func, buf);
            for (usz i = 0; i < expr.call.arg_count; i++) {
                buf.push(' ');
                self.serialize_expr_to_buf(expr.call.args[i], buf);
            }
            buf.push(')');

        case E_SET:
            self.buf_append(buf, "(set! ");
            char[] set_name = self.interp.symbols.get_name(expr.set_expr.name);
            self.buf_append(buf, set_name);
            buf.push(' ');
            self.serialize_expr_to_buf(expr.set_expr.value, buf);
            buf.push(')');

        case E_MATCH:
            self.buf_append(buf, "(match ");
            self.serialize_expr_to_buf(expr.match.scrutinee, buf);
            for (usz i = 0; i < expr.match.clause_count; i++) {
                buf.push(' ');
                self.serialize_pattern_to_buf(expr.match.clauses[i].pattern, buf);
                buf.push(' ');
                self.serialize_expr_to_buf(expr.match.clauses[i].result, buf);
            }
            buf.push(')');

        case E_INDEX:
            self.serialize_expr_to_buf(expr.index.collection, buf);
            self.buf_append(buf, ".[");
            self.serialize_expr_to_buf(expr.index.index, buf);
            buf.push(']');

        case E_PATH:
            for (usz i = 0; i < expr.path.segment_count; i++) {
                if (i > 0) buf.push('.');
                char[] seg = self.interp.symbols.get_name(expr.path.segments[i]);
                self.buf_append(buf, seg);
            }

        case E_QUASIQUOTE:
            buf.push('`');
            self.serialize_expr_to_buf(expr.quasiquote.body, buf);

        case E_UNQUOTE:
            buf.push(',');
            self.serialize_expr_to_buf(expr.unquote.body, buf);

        case E_UNQUOTE_SPLICING:
            self.buf_append(buf, ",@");
            self.serialize_expr_to_buf(expr.unquote_splicing.body, buf);

        case E_DEFMACRO:
            self.buf_append(buf, "(define [macro] ");
            char[] macro_name = self.interp.symbols.get_name(expr.define_macro.name);
            self.buf_append(buf, macro_name);
            for (usz i = 0; i < expr.define_macro.clause_count; i++) {
                self.buf_append(buf, " (");
                self.serialize_pattern_to_buf(expr.define_macro.clauses[i].pattern, buf);
                buf.push(' ');
                self.buf_append(buf, "(");
                self.serialize_value_to_buf(expr.define_macro.clauses[i].tmpl, buf);
                self.buf_append(buf, "))");
            }
            buf.push(')');

        case E_MODULE:
            self.buf_append(buf, "(module ");
            char[] mod_name = self.interp.symbols.get_name(expr.module_expr.name);
            self.buf_append(buf, mod_name);
            self.buf_append(buf, " (export");
            for (usz i = 0; i < expr.module_expr.export_count; i++) {
                buf.push(' ');
                char[] exp_name = self.interp.symbols.get_name(expr.module_expr.exports[i]);
                self.buf_append(buf, exp_name);
            }
            buf.push(')');
            for (usz i = 0; i < expr.module_expr.body_count; i++) {
                buf.push(' ');
                self.serialize_expr_to_buf(expr.module_expr.body[i], buf);
            }
            buf.push(')');

        case E_IMPORT:
            self.buf_append(buf, "(import ");
            char[] imp_name = self.interp.symbols.get_name(expr.import_expr.name);
            self.buf_append(buf, imp_name);
            buf.push(')');

        default:
            self.buf_append(buf, "nil");
    }
}

fn void Compiler.serialize_value_to_buf(Compiler* self, Value* v, List{char}* buf) {
    if (v == null || v.tag == NIL) {
        self.buf_append(buf, "nil");
        return;
    }

    switch (v.tag) {
        case INT:
            // Convert int to string
            long n = v.int_val;
            if (n < 0) {
                buf.push('-');
                n = -n;
            }
            if (n == 0) {
                buf.push('0');
            } else {
                char[32] digits;
                usz dpos = 0;
                while (n > 0 && dpos < 31) {
                    digits[dpos++] = (char)('0' + (n % 10));
                    n /= 10;
                }
                for (isz di = (isz)dpos - 1; di >= 0; di--) {
                    buf.push(digits[(usz)di]);
                }
            }

        case DOUBLE:
            char[64] dbuf;
            char[] dslice = io::bprintf(&dbuf, "%.15g", v.double_val)!!;
            self.buf_append(buf, dslice);

        case STRING:
            buf.push('"');
            for (usz i = 0; i < v.str_val.len; i++) {
                char c = v.str_val.chars[i];
                if (c == '"') {
                    buf.push('\\');
                    buf.push('"');
                } else if (c == '\\') {
                    buf.push('\\');
                    buf.push('\\');
                } else if (c == '\n') {
                    buf.push('\\');
                    buf.push('n');
                } else {
                    buf.push(c);
                }
            }
            buf.push('"');

        case SYMBOL:
            char[] sym_name = self.interp.symbols.get_name(v.sym_val);
            self.buf_append(buf, sym_name);

        case CONS:
            buf.push('(');
            self.serialize_value_to_buf(v.cons_val.car, buf);
            Value* rest = v.cons_val.cdr;
            while (rest != null && rest.tag == CONS) {
                buf.push(' ');
                self.serialize_value_to_buf(rest.cons_val.car, buf);
                rest = rest.cons_val.cdr;
            }
            if (rest != null && rest.tag != NIL) {
                self.buf_append(buf, " . ");
                self.serialize_value_to_buf(rest, buf);
            }
            buf.push(')');

        default:
            self.buf_append(buf, "nil");
    }
}

fn void Compiler.serialize_pattern_to_buf(Compiler* self, Pattern* pat, List{char}* buf) {
    if (pat == null) {
        buf.push('_');
        return;
    }

    switch (pat.tag) {
        case PAT_WILDCARD:
            buf.push('_');

        case PAT_VAR:
            char[] vname = self.interp.symbols.get_name(pat.var_name);
            self.buf_append(buf, vname);

        case PAT_LIT:
            self.serialize_value_to_buf(pat.lit_value, buf);

        case PAT_QUOTE:
            buf.push('\'');
            self.serialize_value_to_buf(pat.quote_datum, buf);

        case PAT_SEQ:
            buf.push('[');
            for (usz i = 0; i < pat.elem_count; i++) {
                if (i > 0) buf.push(' ');
                self.serialize_pattern_to_buf(pat.elements[i], buf);
            }
            if (pat.rest_pos == REST_MIDDLE) {
                self.buf_append(buf, " .. ");
                char[] rname = self.interp.symbols.get_name(pat.rest_binding);
                self.buf_append(buf, rname);
            }
            buf.push(']');

        case PAT_CONS:
            buf.push('(');
            self.serialize_pattern_to_buf(pat.car_pat, buf);
            self.buf_append(buf, " . ");
            self.serialize_pattern_to_buf(pat.cdr_pat, buf);
            buf.push(')');

        default:
            buf.push('_');
    }
}

fn void Compiler.buf_append(Compiler* self, List{char}* buf, char[] s) {
    foreach (c : s) {
        buf.push(c);
    }
}

/**
 * Emit the serialized form of an expression as a C3 string literal
 * directly into the compiler output. Escapes special characters.
 */
fn void Compiler.emit_serialized_expr(Compiler* self, Expr* expr) {
    List{char} buf;
    self.serialize_expr_to_buf(expr, &buf);

    // Emit the serialized source as a C3 string literal with escaping
    self.emit("\"");
    for (usz i = 0; i < buf.len(); i++) {
        char c = buf.entries[i];
        if (c == '"') {
            self.emit("\\\"");
        } else if (c == '\\') {
            self.emit("\\\\");
        } else if (c == '\n') {
            self.emit("\\n");
        } else if (c == '\t') {
            self.emit("\\t");
        } else {
            self.emit_char(c);
        }
    }
    self.emit("\"");

    buf.free();
}

/**
 * Collect free variables from a continuation expression (reset/shift/handle/perform).
 * These are variables that need to be injected into the interpreter before evaluation.
 * Returns the list of free variables (caller must free).
 */
fn List{SymbolId} Compiler.collect_cont_free_vars(Compiler* self, Expr* expr) {
    List{SymbolId} bound;
    List{SymbolId} free;
    self.for_delegation = true;
    self.find_free_vars(expr, &bound, &free, null);
    self.for_delegation = false;
    bound.free();
    return free;
}

// =============================================================================
// SECTION 4d: STDLIB PRELUDE
// =============================================================================

/**
 * Standard library definitions prepended to all compiled programs.
 * These are the same HOFs defined in register_stdlib() in eval.c3.
 */
const char[] STDLIB_PRELUDE =
`(define with-trampoline (lambda (thunk) (handle (thunk nil) ((bounce k next-thunk) (k (with-trampoline next-thunk))))))
(define map (lambda (f) (lambda (lst) (let loop ((xs lst) (acc nil)) (if (null? xs) (reverse acc) (loop (cdr xs) (cons (f (car xs)) acc)))))))
(define filter (lambda (pred) (lambda (lst) (let loop ((xs lst) (acc nil)) (if (null? xs) (reverse acc) (if (pred (car xs)) (loop (cdr xs) (cons (car xs) acc)) (loop (cdr xs) acc)))))))
(define foldl (lambda (f) (lambda (acc) (lambda (lst) (if (null? lst) acc (((foldl f) ((f acc) (car lst))) (cdr lst)))))))
(define foldr (lambda (f) (lambda (init) (lambda (lst) (if (null? lst) init ((f (car lst)) (((foldr f) init) (cdr lst))))))))
(define append (lambda (a) (lambda (b) (if (null? a) b (cons (car a) ((append (cdr a)) b))))))
(define reverse (lambda (lst) (((foldl (lambda (acc) (lambda (x) (cons x acc)))) nil) lst)))
(define compose (lambda (f) (lambda (g) (lambda (x) (f (g x))))))
(define id (lambda (x) x))
(define nth (lambda (n) (lambda (lst) (if (= n 0) (car lst) ((nth (- n 1)) (cdr lst))))))
(define take (lambda (n) (lambda (lst) (if (= n 0) nil (if (null? lst) nil (cons (car lst) ((take (- n 1)) (cdr lst))))))))
(define drop (lambda (n) (lambda (lst) (if (= n 0) lst (if (null? lst) nil ((drop (- n 1)) (cdr lst)))))))
(define zip (lambda (a) (lambda (b) (if (or (null? a) (null? b)) nil (cons (cons (car a) (car b)) ((zip (cdr a)) (cdr b)))))))
(define range (lambda (n) (let ^rec ((helper (lambda (i) (if (= i n) nil (cons i (helper (+ i 1))))))) (helper 0))))
(define for-each (lambda (f) (lambda (lst) (if (null? lst) nil (let ((_r (f (car lst)))) ((for-each f) (cdr lst)))))))
(define any? (lambda (pred) (lambda (lst) (if (null? lst) nil (if (pred (car lst)) true ((any? pred) (cdr lst)))))))
(define every? (lambda (pred) (lambda (lst) (if (null? lst) true (if (pred (car lst)) ((every? pred) (cdr lst)) nil)))))
(define try (lambda (thunk) (lambda (handler) (handle (thunk nil) ((raise k msg) (handler msg))))))
(define assert! (lambda (condition) (lambda (msg) (if condition true (perform raise msg)))))
(define assoc (lambda (key) (lambda (alist) (if (null? alist) nil (if (= (car (car alist)) key) (car alist) ((assoc key) (cdr alist)))))))
(define assoc-ref (lambda (key) (lambda (alist) (let ((pair ((assoc key) alist))) (if (null? pair) nil (cdr pair))))))
(define when (lambda (test) (lambda (body) (if test body nil))))
(define unless (lambda (test) (lambda (body) (if test nil body))))`;

// =============================================================================
// SECTION 5: MAIN COMPILATION
// =============================================================================

/**
 * Compile a complete program to C3 source code.
 * Prepends stdlib definitions so HOFs are available.
 */
fn char[] Compiler.compile_program(Compiler* self, char[] source) {
    // Prepend stdlib prelude to user source using bulk copy
    usz total = STDLIB_PRELUDE.len + 1 + source.len;
    char* full_buf = (char*)mem::malloc(total);
    mem::copy(full_buf, STDLIB_PRELUDE.ptr, STDLIB_PRELUDE.len);
    full_buf[STDLIB_PRELUDE.len] = ' ';
    mem::copy(&full_buf[STDLIB_PRELUDE.len + 1], source.ptr, source.len);
    char[] full = full_buf[:total];

    // Parse the program
    List{Expr*} exprs = parse_program(full, self.interp);

    // First pass: collect all top-level defines (including inside module bodies)
    foreach (expr : exprs) {
        if (expr.tag == E_DEFINE) {
            self.defined_globals.push(expr.define.name);
        } else if (expr.tag == E_MODULE) {
            // Module body defines become globals when compiled inline
            for (usz mi = 0; mi < expr.module_expr.body_count; mi++) {
                if (expr.module_expr.body[mi].tag == E_DEFINE) {
                    self.defined_globals.push(expr.module_expr.body[mi].define.name);
                }
            }
        }
    }

    // Prescan for mutable captures (before lambda scanning)
    foreach (expr : exprs) {
        self.prescan_mutable_captures(expr);
    }

    // Emit prelude
    self.emit_prelude();

    // Second pass: scan all lambdas to generate definitions
    foreach (expr : exprs) {
        self.scan_lambdas(expr);
    }

    // Emit lambda struct and function definitions
    self.emit_lambda_definitions();

    // Emit global variable declarations for defined globals
    self.emit_global_declarations();

    // Emit main function
    self.emit_main_start();

    // Clear declared vars for main scope
    self.declared_vars.free();

    // Compile each top-level expression
    // Find the last non-define expression index for print_last mode
    isz last_non_define = -1;
    if (self.print_last) {
        for (usz i = 0; i < exprs.len(); i++) {
            if (exprs[i].tag != E_DEFINE) {
                last_non_define = (isz)i;
            }
        }
    }

    for (usz i = 0; i < exprs.len(); i++) {
        bool should_print = (self.print_all && exprs[i].tag != E_DEFINE)
            || (self.print_last && (isz)i == last_non_define);
        if (should_print) {
            // Compile to temp, then print the result
            usz r = self.compile_to_temp(exprs[i]);
            self.emit_indent();
            self.emit("runtime::rt_print_value(");
            self.emit_temp_ref(r);
            self.emit("); io::printn(\"\");\n");
        } else if (exprs[i].tag == E_DEFINE) {
            // Define: compile init to temp, assign to global
            usz init_r = self.compile_to_temp(exprs[i].define.value);
            self.emit_indent();
            self.emit_symbol_name(exprs[i].define.name);
            self.emit(" = ");
            self.emit_temp_ref(init_r);
            self.emit(";\n");
        } else {
            // Other statements: compile to temp (discards result)
            self.compile_to_temp(exprs[i]);
        }
    }

    self.emit_main_end();

    exprs.free();
    mem::free(full_buf);
    return self.get_output();
}

/**
 * Scan expression tree for lambdas and register them.
 */
fn void Compiler.scan_lambdas(Compiler* self, Expr* expr) {
    List{SymbolId} empty;
    self.scan_lambdas_with_scope(expr, &empty);
    empty.free();
}

// Returns true if any E_LAMBDA was found in the subtree (replaces body_creates_closure)
fn bool Compiler.scan_lambdas_with_scope(Compiler* self, Expr* expr, List{SymbolId}* enclosing_bound) {
    if (expr == null) return false;

    switch (expr.tag) {
        case E_LAMBDA:
            // For multi-param lambdas, create synthetic nested single-param
            // lambda AST nodes for the compiler (which uses single-param calling convention).
            Expr* scan_expr = expr;
            if (expr.lambda.param_count > 1 && !expr.lambda.has_rest) {
                Expr* inner = expr.lambda.body;
                for (usz pi = expr.lambda.param_count; pi > 1; pi--) {
                    Expr* wrapper = self.interp.alloc_expr();
                    wrapper.tag = E_LAMBDA;
                    wrapper.lambda = mem::malloc(ExprLambda.sizeof);
                    wrapper.loc_line = expr.loc_line;
                    wrapper.loc_column = expr.loc_column;
                    wrapper.lambda.param = expr.lambda.params[pi - 1];
                    wrapper.lambda.param_count = 1;
                    wrapper.lambda.params[0] = expr.lambda.params[pi - 1];
                    wrapper.lambda.has_rest = false;
                    wrapper.lambda.rest_param = 0;
                    wrapper.lambda.has_typed_params = false;
                    wrapper.lambda.body = inner;
                    inner = wrapper;
                }
                expr.lambda.body = inner;
                expr.lambda.param = expr.lambda.params[0];
                expr.lambda.param_count = 1;
                expr.lambda.params[0] = expr.lambda.param;
            }

            List{SymbolId} own_bound;
            if ((uint)expr.lambda.param != 0xFFFFFFFF) {
                own_bound.push(expr.lambda.param);
            }
            List{SymbolId} free;
            self.find_free_vars(expr.lambda.body, &own_bound, &free, enclosing_bound);

            LambdaDef def;
            def.id = self.lambda_counter++;
            def.param = expr.lambda.param;
            def.body = expr.lambda.body;
            def.capture_count = 0;

            foreach (f : free) {
                if (def.capture_count < 16) {
                    def.captures[def.capture_count++] = f;
                }
            }

            // Recursively scan body — the return value tells us if body has nested lambdas
            List{SymbolId} new_enclosing;
            foreach (b : *enclosing_bound) {
                new_enclosing.push(b);
            }
            if ((uint)expr.lambda.param != 0xFFFFFFFF) {
                new_enclosing.push(expr.lambda.param);
            }
            bool body_has_lambda = self.scan_lambdas_with_scope(expr.lambda.body, &new_enclosing);

            // Set creates_closure from recursive scan (replaces body_creates_closure)
            def.creates_closure = body_has_lambda;
            if (def.id < 256) {
                self.lambda_creates_closure[def.id] = body_has_lambda;
            }

            self.lambda_defs.push(def);

            new_enclosing.free();
            own_bound.free();
            free.free();
            return true;  // This node IS a lambda

        case E_APP:
            bool app_f = self.scan_lambdas_with_scope(expr.app.func, enclosing_bound);
            return self.scan_lambdas_with_scope(expr.app.arg, enclosing_bound) || app_f;

        case E_IF:
            bool if_t = self.scan_lambdas_with_scope(expr.if_expr.test, enclosing_bound);
            bool if_th = self.scan_lambdas_with_scope(expr.if_expr.then_branch, enclosing_bound);
            return self.scan_lambdas_with_scope(expr.if_expr.else_branch, enclosing_bound) || if_t || if_th;

        case E_LET:
            List{SymbolId} let_scope;
            foreach (b : *enclosing_bound) {
                let_scope.push(b);
            }
            let_scope.push(expr.let_expr.name);
            bool let_found;
            if (expr.let_expr.is_recursive) {
                let_found = self.scan_lambdas_with_scope(expr.let_expr.init, &let_scope);
            } else {
                let_found = self.scan_lambdas_with_scope(expr.let_expr.init, enclosing_bound);
            }
            bool let_body = self.scan_lambdas_with_scope(expr.let_expr.body, &let_scope);
            let_scope.free();
            return let_found || let_body;

        case E_DEFINE:
            return self.scan_lambdas_with_scope(expr.define.value, enclosing_bound);

        case E_AND:
            bool and_l = self.scan_lambdas_with_scope(expr.and_expr.left, enclosing_bound);
            return self.scan_lambdas_with_scope(expr.and_expr.right, enclosing_bound) || and_l;

        case E_OR:
            bool or_l = self.scan_lambdas_with_scope(expr.or_expr.left, enclosing_bound);
            return self.scan_lambdas_with_scope(expr.or_expr.right, enclosing_bound) || or_l;

        case E_MATCH:
            bool m_found = self.scan_lambdas_with_scope(expr.match.scrutinee, enclosing_bound);
            for (usz i = 0; i < expr.match.clause_count; i++) {
                m_found |= self.scan_lambdas_with_scope(expr.match.clauses[i].result, enclosing_bound);
            }
            return m_found;

        case E_CALL:
            bool c_found = self.scan_lambdas_with_scope(expr.call.func, enclosing_bound);
            for (usz i = 0; i < expr.call.arg_count; i++) {
                c_found |= self.scan_lambdas_with_scope(expr.call.args[i], enclosing_bound);
            }
            return c_found;

        case E_INDEX:
            bool idx_c = self.scan_lambdas_with_scope(expr.index.collection, enclosing_bound);
            return self.scan_lambdas_with_scope(expr.index.index, enclosing_bound) || idx_c;

        case E_PATH:
            return false;

        case E_RESET: {
            // Wrap body as synthetic lambda: (lambda (_eff_dummy) body)
            SymbolId dummy_param = self.interp.symbols.intern("_eff_dummy");
            Expr* orig_body = expr.reset.body;
            Expr* wrapper = self.interp.alloc_expr();
            wrapper.tag = E_LAMBDA;
            wrapper.lambda = (ExprLambda*)mem::malloc(ExprLambda.sizeof);
            wrapper.loc_line = expr.loc_line;
            wrapper.loc_column = expr.loc_column;
            wrapper.lambda.param = dummy_param;
            wrapper.lambda.param_count = 1;
            wrapper.lambda.params[0] = dummy_param;
            wrapper.lambda.has_rest = false;
            wrapper.lambda.rest_param = 0;
            wrapper.lambda.has_typed_params = false;
            wrapper.lambda.body = orig_body;
            expr.reset.body = wrapper;
            return self.scan_lambdas_with_scope(wrapper, enclosing_bound);
        }

        case E_SHIFT: {
            // Wrap body as synthetic lambda: (lambda (k_name) body)
            Expr* orig_body = expr.shift.body;
            Expr* wrapper = self.interp.alloc_expr();
            wrapper.tag = E_LAMBDA;
            wrapper.lambda = (ExprLambda*)mem::malloc(ExprLambda.sizeof);
            wrapper.loc_line = expr.loc_line;
            wrapper.loc_column = expr.loc_column;
            wrapper.lambda.param = expr.shift.k_name;
            wrapper.lambda.param_count = 1;
            wrapper.lambda.params[0] = expr.shift.k_name;
            wrapper.lambda.has_rest = false;
            wrapper.lambda.rest_param = 0;
            wrapper.lambda.has_typed_params = false;
            wrapper.lambda.body = orig_body;
            expr.shift.body = wrapper;
            return self.scan_lambdas_with_scope(wrapper, enclosing_bound);
        }

        case E_PERFORM:
            return self.scan_lambdas_with_scope(expr.perform.arg, enclosing_bound);

        case E_HANDLE: {
            // Wrap body as synthetic lambda
            SymbolId dummy_param = self.interp.symbols.intern("_eff_dummy");
            Expr* orig_body = expr.handle.body;
            Expr* body_wrapper = self.interp.alloc_expr();
            body_wrapper.tag = E_LAMBDA;
            body_wrapper.lambda = (ExprLambda*)mem::malloc(ExprLambda.sizeof);
            body_wrapper.loc_line = expr.loc_line;
            body_wrapper.loc_column = expr.loc_column;
            body_wrapper.lambda.param = dummy_param;
            body_wrapper.lambda.param_count = 1;
            body_wrapper.lambda.params[0] = dummy_param;
            body_wrapper.lambda.has_rest = false;
            body_wrapper.lambda.rest_param = 0;
            body_wrapper.lambda.has_typed_params = false;
            body_wrapper.lambda.body = orig_body;
            expr.handle.body = body_wrapper;
            bool h_found = self.scan_lambdas_with_scope(body_wrapper, enclosing_bound);

            // Wrap each handler clause as synthetic lambda: (lambda (_pair) (let (k (car _pair)) (let (arg (cdr _pair)) body)))
            SymbolId pair_param = self.interp.symbols.intern("_eff_pair");
            SymbolId sym_car = self.interp.symbols.intern("car");
            SymbolId sym_cdr = self.interp.symbols.intern("cdr");
            for (usz i = 0; i < expr.handle.clause_count; i++) {
                Expr* orig_handler = expr.handle.clauses[i].handler_body;
                SymbolId k_name = expr.handle.clauses[i].k_name;
                SymbolId arg_name = expr.handle.clauses[i].arg_name;

                // Build (cdr _pair) for arg extraction
                Expr* pair_ref2 = self.interp.alloc_expr();
                pair_ref2.tag = E_VAR;
                pair_ref2.var_expr.name = pair_param;
                Expr* cdr_fn = self.interp.alloc_expr();
                cdr_fn.tag = E_VAR;
                cdr_fn.var_expr.name = sym_cdr;
                Expr* cdr_call = self.interp.alloc_expr();
                cdr_call.tag = E_APP;
                cdr_call.app.func = cdr_fn;
                cdr_call.app.arg = pair_ref2;

                // Build: (let (arg_name (cdr _pair)) orig_handler)
                Expr* let_arg = self.interp.alloc_expr();
                let_arg.tag = E_LET;
                let_arg.let_expr.name = arg_name;
                let_arg.let_expr.init = cdr_call;
                let_arg.let_expr.body = orig_handler;
                let_arg.let_expr.is_recursive = false;

                // Build (car _pair) for k extraction
                Expr* pair_ref1 = self.interp.alloc_expr();
                pair_ref1.tag = E_VAR;
                pair_ref1.var_expr.name = pair_param;
                Expr* car_fn = self.interp.alloc_expr();
                car_fn.tag = E_VAR;
                car_fn.var_expr.name = sym_car;
                Expr* car_call = self.interp.alloc_expr();
                car_call.tag = E_APP;
                car_call.app.func = car_fn;
                car_call.app.arg = pair_ref1;

                // Build: (let (k_name (car _pair)) (let (arg_name ...) ...))
                Expr* let_k = self.interp.alloc_expr();
                let_k.tag = E_LET;
                let_k.let_expr.name = k_name;
                let_k.let_expr.init = car_call;
                let_k.let_expr.body = let_arg;
                let_k.let_expr.is_recursive = false;

                // Lambda wrapper: (lambda (_pair) (let (k ...) (let (arg ...) body)))
                Expr* handler_wrapper = self.interp.alloc_expr();
                handler_wrapper.tag = E_LAMBDA;
                handler_wrapper.lambda = (ExprLambda*)mem::malloc(ExprLambda.sizeof);
                handler_wrapper.loc_line = expr.loc_line;
                handler_wrapper.loc_column = expr.loc_column;
                handler_wrapper.lambda.param = pair_param;
                handler_wrapper.lambda.param_count = 1;
                handler_wrapper.lambda.params[0] = pair_param;
                handler_wrapper.lambda.has_rest = false;
                handler_wrapper.lambda.rest_param = 0;
                handler_wrapper.lambda.has_typed_params = false;
                handler_wrapper.lambda.body = let_k;
                expr.handle.clauses[i].handler_body = handler_wrapper;
                h_found |= self.scan_lambdas_with_scope(handler_wrapper, enclosing_bound);
            }
            return h_found;
        }

        case E_BEGIN:
            bool b_found = false;
            for (usz i = 0; i < expr.begin.expr_count; i++) {
                b_found |= self.scan_lambdas_with_scope(expr.begin.exprs[i], enclosing_bound);
            }
            return b_found;

        case E_SET:
            return self.scan_lambdas_with_scope(expr.set_expr.value, enclosing_bound);

        case E_QUASIQUOTE:
            return self.scan_lambdas_with_scope(expr.quasiquote.body, enclosing_bound);

        case E_UNQUOTE:
            return self.scan_lambdas_with_scope(expr.unquote.body, enclosing_bound);

        case E_UNQUOTE_SPLICING:
            return self.scan_lambdas_with_scope(expr.unquote_splicing.body, enclosing_bound);

        case E_DEFMACRO:
            return false;

        case E_MODULE:
            bool mod_found = false;
            for (usz i = 0; i < expr.module_expr.body_count; i++) {
                List{SymbolId} mod_empty;
                mod_found |= self.scan_lambdas_with_scope(expr.module_expr.body[i], &mod_empty);
                mod_empty.free();
            }
            return mod_found;

        case E_IMPORT:
            return false;

        default:
            return false;
    }
}

// =============================================================================
// SECTION 6: CODE EMISSION
// =============================================================================

fn void Compiler.emit_prelude(Compiler* self) {
    self.emit("// Generated by Pika Lisp Compiler\n");
    self.emit("// Do not edit manually\n\n");
    self.emit("import std::io;\n");
    self.emit("import main;\n");
    self.emit("import lisp::runtime;\n\n");
}

fn void Compiler.emit_lambda_definitions(Compiler* self) {
    // Emit struct definitions for closures with captures
    foreach (def : self.lambda_defs) {
        if (def.capture_count > 0) {
            self.emit("struct Lambda_");
            self.emit_usz(def.id);
            self.emit(" {\n");

            // Add home_region for region tracking
            self.emit("    main::RegionHandle home_region;\n");

            for (usz i = 0; i < def.capture_count; i++) {
                self.emit("    runtime::Value captured_");
                self.emit_symbol_name(def.captures[i]);
                self.emit(";\n");
            }

            self.emit("}\n\n");
        }
    }

    // Emit function definitions for lambdas
    foreach (def : self.lambda_defs) {
        // Check if this is a zero-arg lambda (sentinel param)
        bool is_zero_arg = (uint)def.param == 0xFFFFFFFF;

        self.emit("fn runtime::Value invoke_lambda_");
        self.emit_usz(def.id);
        if (is_zero_arg) {
            self.emit("(void* _self, runtime::Value _unused) {\n");
        } else {
            self.emit("(void* _self, runtime::Value ");
            self.emit_symbol_name(def.param);
            self.emit(") {\n");
        }

        self.indent++;

        // Clear declared vars for this function scope
        self.declared_vars.free();
        // Mark parameter as declared
        if (!is_zero_arg) {
            self.mark_declared(def.param);
        }

        // If this lambda creates closures, push a frame region
        if (def.creates_closure) {
            self.emit_indent();
            self.emit("main::RegionHandle _frame = runtime::rt_push_frame();\n");
        }

        // Extract captures if any
        if (def.capture_count > 0) {
            self.emit_indent();
            self.emit("Lambda_");
            self.emit_usz(def.id);
            self.emit("* self = (Lambda_");
            self.emit_usz(def.id);
            self.emit("*)_self;\n");

            for (usz i = 0; i < def.capture_count; i++) {
                self.mark_declared(def.captures[i]);
                self.emit_indent();
                self.emit("runtime::Value ");
                self.emit_symbol_name(def.captures[i]);
                self.emit(" = self.captured_");
                self.emit_symbol_name(def.captures[i]);
                self.emit(";\n");
            }
        }

        // Compile body and return using statement-level compilation
        if (def.body.tag == E_LAMBDA) {
            self.emit_lambda_return_with_frame(def.body, def.creates_closure);
        } else {
            usz body_r = self.compile_to_temp_tail(def.body);
            if (def.creates_closure) {
                self.emit_indent();
                self.emit("runtime::rt_pop_frame(_frame);\n");
            }
            self.emit_indent();
            self.emit("return ");
            self.emit_temp_ref(body_r);
            self.emit(";\n");
        }

        self.indent--;
        self.emit("}\n\n");
    }
}

fn void Compiler.emit_lambda_return(Compiler* self, Expr* expr) {
    // Find the lambda definition for this lambda
    usz lambda_id = 0;
    LambdaDef* found_def = null;

    foreach (&def : self.lambda_defs) {
        if (def.body == expr.lambda.body && (uint)def.param == (uint)expr.lambda.param) {
            lambda_id = def.id;
            found_def = def;
            break;
        }
    }

    if (found_def != null && found_def.capture_count > 0) {
        // Allocate closure struct in current region
        self.emit_indent();
        self.emit("Lambda_");
        self.emit_usz(lambda_id);
        self.emit(" _closure_data;\n");

        // Set home region
        self.emit_indent();
        self.emit("_closure_data.home_region = runtime::rt_current_region();\n");

        // Set captured values with write barriers
        for (usz i = 0; i < found_def.capture_count; i++) {
            self.emit_indent();
            self.emit("_closure_data.captured_");
            self.emit_symbol_name(found_def.captures[i]);
            self.emit(" = ");
            self.emit_symbol_name(found_def.captures[i]);
            self.emit(";\n");

            // Emit write barrier for captured value
            self.emit_indent();
            self.emit("runtime::rt_capture_value(_closure_data.home_region, ");
            self.emit_symbol_name(found_def.captures[i]);
            self.emit(");\n");
        }

        // Allocate in region
        self.emit_indent();
        self.emit("main::ObjectHandle _closure_handle = main::allocate_in(runtime::rt_current_region(), Lambda_");
        self.emit_usz(lambda_id);
        self.emit(", _closure_data);\n");

        self.emit_indent();
        self.emit("return runtime::make_closure(main::dereference(_closure_handle), &invoke_lambda_");
        self.emit_usz(lambda_id);
        self.emit(");\n");
    } else {
        // Simple closure without captures
        self.emit_indent();
        self.emit("return runtime::make_closure(null, &invoke_lambda_");
        self.emit_usz(lambda_id);
        self.emit(");\n");
    }
}

fn void Compiler.emit_lambda_return_with_frame(Compiler* self, Expr* expr, bool has_frame) {
    // Find the lambda definition for this lambda
    usz lambda_id = 0;
    LambdaDef* found_def = null;

    foreach (&def : self.lambda_defs) {
        if (def.body == expr.lambda.body && (uint)def.param == (uint)expr.lambda.param) {
            lambda_id = def.id;
            found_def = def;
            break;
        }
    }

    if (found_def != null && found_def.capture_count > 0) {
        // Allocate closure struct in current region
        self.emit_indent();
        self.emit("Lambda_");
        self.emit_usz(lambda_id);
        self.emit(" _closure_data;\n");

        // Set home region
        self.emit_indent();
        self.emit("_closure_data.home_region = runtime::rt_current_region();\n");

        // Set captured values with write barriers
        for (usz i = 0; i < found_def.capture_count; i++) {
            self.emit_indent();
            self.emit("_closure_data.captured_");
            self.emit_symbol_name(found_def.captures[i]);
            self.emit(" = ");
            self.emit_symbol_name(found_def.captures[i]);
            self.emit(";\n");

            // Emit write barrier for captured value
            self.emit_indent();
            self.emit("runtime::rt_capture_value(_closure_data.home_region, ");
            self.emit_symbol_name(found_def.captures[i]);
            self.emit(");\n");
        }

        // Allocate in region
        self.emit_indent();
        self.emit("main::ObjectHandle _closure_handle = main::allocate_in(runtime::rt_current_region(), Lambda_");
        self.emit_usz(lambda_id);
        self.emit(", _closure_data);\n");

        // Make closure value
        self.emit_indent();
        self.emit("runtime::Value _result = runtime::make_closure(main::dereference(_closure_handle), &invoke_lambda_");
        self.emit_usz(lambda_id);
        self.emit(");\n");

        // Pop frame if we have one
        if (has_frame) {
            self.emit_indent();
            self.emit("runtime::rt_pop_frame(_frame);\n");
        }

        self.emit_indent();
        self.emit("return _result;\n");
    } else {
        // Simple closure without captures
        self.emit_indent();
        self.emit("runtime::Value _result = runtime::make_closure(null, &invoke_lambda_");
        self.emit_usz(lambda_id);
        self.emit(");\n");

        // Pop frame if we have one
        if (has_frame) {
            self.emit_indent();
            self.emit("runtime::rt_pop_frame(_frame);\n");
        }

        self.emit_indent();
        self.emit("return _result;\n");
    }
}

fn void Compiler.emit_global_declarations(Compiler* self) {
    if (self.defined_globals.len() == 0) return;

    self.emit("// Global variables\n");
    foreach (name : self.defined_globals) {
        self.emit("runtime::Value ");
        self.emit_symbol_name(name);
        self.emit(";\n");
    }
    self.emit_newline();
}

fn void Compiler.emit_main_start(Compiler* self) {
    self.emit("fn int main() {\n");
    self.indent++;

    // Initialize runtime
    self.emit_line("runtime::rt_init();");
    self.emit_newline();

    // Macros (when, unless, cond) are expanded at compile time — no runtime registration needed
    self.emit_newline();
}

fn void Compiler.emit_main_end(Compiler* self) {
    self.emit_newline();
    // Shutdown runtime to clean up regions
    self.emit_line("runtime::rt_shutdown();");
    self.emit_line("return 0;");
    self.indent--;
    self.emit("}\n");
}

// =============================================================================
// SECTION 7: STATEMENT COMPILATION
// =============================================================================

fn void Compiler.compile_statement(Compiler* self, Expr* expr) {
    if (expr == null) return;

    switch (expr.tag) {
        case E_DEFINE:
            // (define name value) => name = <compile value>;
            usz init_r = self.compile_to_temp(expr.define.value);
            self.emit_indent();
            self.emit_symbol_name(expr.define.name);
            self.emit(" = ");
            self.emit_temp_ref(init_r);
            self.emit(";\n");

        default:
            // Other expressions: evaluate and discard result
            self.compile_to_temp(expr);
    }
}

// =============================================================================
// SECTION 8: EXPRESSION COMPILATION
// =============================================================================

fn void Compiler.compile_expr(Compiler* self, Expr* expr) {
    if (expr == null) {
        self.emit("runtime::make_nil()");
        return;
    }

    switch (expr.tag) {
        case E_LIT:
            self.compile_literal(expr.lit.value);

        case E_VAR:
            self.compile_var(expr);

        case E_LAMBDA:
            self.compile_lambda(expr);

        case E_APP:
            self.compile_application(expr);

        case E_IF:
            self.compile_if(expr);

        case E_LET:
            self.compile_let(expr);

        case E_DEFINE:
            // Define in expression context
            self.emit("(");
            self.emit_symbol_name(expr.define.name);
            self.emit(" = ");
            self.compile_expr(expr.define.value);
            self.emit(")");

        case E_QUOTE:
            self.compile_quote(expr.quote.datum);

        case E_AND:
            self.compile_and(expr);

        case E_OR:
            self.compile_or(expr);

        case E_MATCH:
            self.compile_match(expr);

        case E_CALL:
            self.compile_call(expr);

        case E_INDEX:
            self.compile_index(expr);

        case E_PATH:
            self.compile_path(expr);

        case E_RESET:
            self.compile_reset(expr);

        case E_SHIFT:
            self.compile_shift(expr);

        case E_PERFORM:
            self.compile_perform(expr);

        case E_HANDLE:
            self.compile_handle(expr);

        case E_BEGIN:
            self.compile_begin(expr);

        case E_SET:
            self.compile_set(expr);

        case E_QUASIQUOTE:
            self.compile_quasiquote(expr);

        case E_DEFMACRO:
            self.compile_defmacro(expr);

        case E_MODULE:
            self.compile_module(expr);

        case E_IMPORT:
            self.compile_import(expr);

        default:
            io::printfn("WARNING: compiler: unsupported expr tag %d", expr.tag.ordinal);
            self.emit("runtime::make_nil() /* WARNING: unsupported expr type */");
    }
}

// =============================================================================
// SECTION 8a: STATEMENT-LEVEL COMPILATION (compile_to_temp)
//
// compile_to_temp emits statements that compute an expression's value
// and store it in a temp variable _rN. Returns N.
// This avoids GCC-style statement expressions ({ ... }) which C3 doesn't support.
// =============================================================================

fn usz Compiler.next_result(Compiler* self) {
    usz id = self.temp_counter;
    self.temp_counter++;
    return id;
}

fn void Compiler.emit_temp_decl(Compiler* self, usz id) {
    self.emit_indent();
    self.emit("runtime::Value _r");
    self.emit_usz(id);
}

fn void Compiler.emit_temp_ref(Compiler* self, usz id) {
    self.emit("_r");
    self.emit_usz(id);
}

/**
 * Compile an expression as statements, storing result in _rN.
 * Returns N. All temp declarations are emitted as proper C3 statements.
 */
fn usz Compiler.compile_to_temp(Compiler* self, Expr* expr) {
    if (expr == null) {
        usz id = self.next_result();
        self.emit_temp_decl(id);
        self.emit(" = runtime::make_nil();\n");
        return id;
    }

    switch (expr.tag) {
        case E_CALL:
            return self.compile_call_flat(expr);

        case E_LET:
            return self.compile_let_flat(expr);

        case E_AND:
            return self.compile_and_flat(expr);

        case E_OR:
            return self.compile_or_flat(expr);

        case E_BEGIN:
            return self.compile_begin_flat(expr);

        case E_SET:
            return self.compile_set_flat(expr);

        case E_RESET:
            return self.compile_reset_flat(expr);

        case E_SHIFT:
            return self.compile_shift_flat(expr);

        case E_PERFORM:
            return self.compile_perform_flat(expr);

        case E_HANDLE:
            return self.compile_handle_flat(expr);

        case E_QUASIQUOTE:
            return self.compile_qq_flat(expr.quasiquote.body, 0);

        case E_DEFMACRO:
            // Macros expanded at compile time — no-op at runtime
            usz defmacro_id = self.next_result();
            self.emit_temp_decl(defmacro_id);
            self.emit(" = runtime::make_nil(); // macro: expanded at compile time\n");
            return defmacro_id;

        case E_MODULE:
            return self.compile_module_flat(expr);

        case E_IMPORT:
            return self.compile_import_flat(expr);

        case E_IF:
            return self.compile_if_flat(expr);

        case E_APP:
            return self.compile_app_flat(expr);

        case E_LAMBDA:
            return self.compile_lambda_flat(expr);

        case E_MATCH:
            return self.compile_match_flat(expr);

        case E_INDEX:
            usz idx_coll_r = self.compile_to_temp(expr.index.collection);
            usz idx_idx_r = self.compile_to_temp(expr.index.index);
            usz idx_id = self.next_result();
            self.emit_temp_decl(idx_id);
            self.emit(" = runtime::rt_index(");
            self.emit_temp_ref(idx_coll_r);
            self.emit(", ");
            self.emit_temp_ref(idx_idx_r);
            self.emit(");\n");
            return idx_id;

        case E_DEFINE:
            usz def_val_r = self.compile_to_temp(expr.define.value);
            self.emit_indent();
            self.emit_symbol_name(expr.define.name);
            self.emit(" = ");
            self.emit_temp_ref(def_val_r);
            self.emit(";\n");
            return def_val_r;

        default:
            // Leaf expressions (E_LIT, E_VAR, E_QUOTE, E_PATH):
            // compile_expr produces inline code without ({ }) or recursion
            usz id = self.next_result();
            self.emit_temp_decl(id);
            self.emit(" = ");
            self.compile_expr(expr);
            self.emit(";\n");
            return id;
    }
}

/**
 * compile_to_temp for tail position. For calls, emits make_thunk instead of rt_invoke.
 */
fn usz Compiler.compile_to_temp_tail(Compiler* self, Expr* expr) {
    if (expr == null) {
        usz id = self.next_result();
        self.emit_temp_decl(id);
        self.emit(" = runtime::make_nil();\n");
        return id;
    }

    switch (expr.tag) {
        case E_APP:
            return self.compile_app_tail_flat(expr);

        case E_CALL:
            return self.compile_call_tail_flat(expr);

        case E_IF:
            // Propagate tail to branches
            usz cond = self.compile_to_temp(expr.if_expr.test);
            usz id = self.next_result();
            self.emit_temp_decl(id);
            self.emit(";\n");
            self.emit_indent();
            self.emit("if (runtime::rt_is_truthy(");
            self.emit_temp_ref(cond);
            self.emit(")) {\n");
            self.indent++;
            usz then_r = self.compile_to_temp_tail(expr.if_expr.then_branch);
            self.emit_indent();
            self.emit_temp_ref(id);
            self.emit(" = ");
            self.emit_temp_ref(then_r);
            self.emit(";\n");
            self.indent--;
            self.emit_indent();
            self.emit("} else {\n");
            self.indent++;
            usz else_r = self.compile_to_temp_tail(expr.if_expr.else_branch);
            self.emit_indent();
            self.emit_temp_ref(id);
            self.emit(" = ");
            self.emit_temp_ref(else_r);
            self.emit(";\n");
            self.indent--;
            self.emit_indent();
            self.emit("}\n");
            return id;

        case E_BEGIN:
            usz bcount = expr.begin.expr_count;
            if (bcount == 0) {
                usz bid = self.next_result();
                self.emit_temp_decl(bid);
                self.emit(" = runtime::make_nil();\n");
                return bid;
            }
            for (usz i = 0; i < bcount - 1; i++) {
                self.compile_to_temp(expr.begin.exprs[i]);
            }
            return self.compile_to_temp_tail(expr.begin.exprs[bcount - 1]);

        case E_LET:
            return self.compile_let_flat_tail(expr);

        case E_AND:
            usz and_left = self.compile_to_temp(expr.and_expr.left);
            usz and_id = self.next_result();
            self.emit_temp_decl(and_id);
            self.emit(";\n");
            self.emit_indent();
            self.emit("if (runtime::rt_is_truthy(");
            self.emit_temp_ref(and_left);
            self.emit(")) {\n");
            self.indent++;
            usz and_right = self.compile_to_temp_tail(expr.and_expr.right);
            self.emit_indent();
            self.emit_temp_ref(and_id);
            self.emit(" = ");
            self.emit_temp_ref(and_right);
            self.emit(";\n");
            self.indent--;
            self.emit_indent();
            self.emit("} else {\n");
            self.indent++;
            self.emit_indent();
            self.emit_temp_ref(and_id);
            self.emit(" = ");
            self.emit_temp_ref(and_left);
            self.emit(";\n");
            self.indent--;
            self.emit_indent();
            self.emit("}\n");
            return and_id;

        case E_OR:
            usz or_left = self.compile_to_temp(expr.or_expr.left);
            usz or_id = self.next_result();
            self.emit_temp_decl(or_id);
            self.emit(";\n");
            self.emit_indent();
            self.emit("if (runtime::rt_is_truthy(");
            self.emit_temp_ref(or_left);
            self.emit(")) {\n");
            self.indent++;
            self.emit_indent();
            self.emit_temp_ref(or_id);
            self.emit(" = ");
            self.emit_temp_ref(or_left);
            self.emit(";\n");
            self.indent--;
            self.emit_indent();
            self.emit("} else {\n");
            self.indent++;
            usz or_right = self.compile_to_temp_tail(expr.or_expr.right);
            self.emit_indent();
            self.emit_temp_ref(or_id);
            self.emit(" = ");
            self.emit_temp_ref(or_right);
            self.emit(";\n");
            self.indent--;
            self.emit_indent();
            self.emit("}\n");
            return or_id;

        default:
            // Non-tail forms: just use compile_to_temp
            return self.compile_to_temp(expr);
    }
}

// --- Flat (statement-level) compilation of individual forms ---

fn usz Compiler.compile_call_flat(Compiler* self, Expr* expr) {
    usz arg_count = expr.call.arg_count;

    // Special case: (dict ...) is variadic — use rt_dict_from_args directly
    if (expr.call.func.tag == E_VAR) {
        char[] fname = self.interp.symbols.get_name(expr.call.func.var_expr.name);
        if (self.str_eq(fname, "dict")) {
            if (arg_count == 0) {
                usz id = self.next_result();
                self.emit_temp_decl(id);
                self.emit(" = runtime::rt_dict_from_args(runtime::make_nil());\n");
                return id;
            }
            // Compile args and build cons list
            usz[32] arg_temps;
            for (usz i = 0; i < arg_count && i < 32; i++) {
                arg_temps[i] = self.compile_to_temp(expr.call.args[i]);
            }
            usz args_list = self.next_result();
            self.emit_temp_decl(args_list);
            self.emit(" = runtime::make_nil();\n");
            for (isz i = (isz)arg_count - 1; i >= 0; i--) {
                self.emit_indent();
                self.emit_temp_ref(args_list);
                self.emit(" = runtime::rt_cons(");
                self.emit_temp_ref(arg_temps[(usz)i]);
                self.emit(", ");
                self.emit_temp_ref(args_list);
                self.emit(");\n");
            }
            usz id = self.next_result();
            self.emit_temp_decl(id);
            self.emit(" = runtime::rt_dict_from_args(");
            self.emit_temp_ref(args_list);
            self.emit(");\n");
            return id;
        }
    }

    if (arg_count == 0) {
        usz func_r = self.compile_to_temp(expr.call.func);
        usz id = self.next_result();
        self.emit_temp_decl(id);
        self.emit(" = runtime::rt_invoke(");
        self.emit_temp_ref(func_r);
        self.emit(", runtime::make_nil());\n");
        return id;
    }

    // Compile each arg to a temp
    usz[32] arg_temps;
    for (usz i = 0; i < arg_count && i < 32; i++) {
        arg_temps[i] = self.compile_to_temp(expr.call.args[i]);
    }

    // Compile func to temp
    usz func_r = self.compile_to_temp(expr.call.func);

    // Apply args one at a time (inline currying):
    // rt_invoke_once for intermediates, rt_invoke for last arg (with trampoline)
    usz tf = func_r;
    for (usz i = 0; i < arg_count; i++) {
        usz next_r = self.next_result();
        self.emit_temp_decl(next_r);
        if (i == arg_count - 1) {
            self.emit(" = runtime::rt_invoke(");
        } else {
            self.emit(" = runtime::rt_invoke_once(");
        }
        self.emit_temp_ref(tf);
        self.emit(", ");
        self.emit_temp_ref(arg_temps[i]);
        self.emit(");\n");
        tf = next_r;
    }
    return tf;
}

fn usz Compiler.compile_call_tail_flat(Compiler* self, Expr* expr) {
    usz arg_count = expr.call.arg_count;

    if (arg_count == 0) {
        usz func_r = self.compile_to_temp(expr.call.func);
        usz id = self.next_result();
        self.emit_temp_decl(id);
        self.emit(";\n");
        self.emit_indent();
        self.emit("if (");
        self.emit_temp_ref(func_r);
        self.emit(".tag == runtime::ValueTag.V_CLOSURE) {\n");
        self.indent++;
        self.emit_indent();
        self.emit_temp_ref(id);
        self.emit(" = runtime::make_thunk(");
        self.emit_temp_ref(func_r);
        self.emit(".closure_val.invoke, ");
        self.emit_temp_ref(func_r);
        self.emit(".closure_val.data, runtime::make_nil());\n");
        self.indent--;
        self.emit_indent();
        self.emit("} else {\n");
        self.indent++;
        self.emit_indent();
        self.emit_temp_ref(id);
        self.emit(" = runtime::rt_invoke_once(");
        self.emit_temp_ref(func_r);
        self.emit(", runtime::make_nil());\n");
        self.indent--;
        self.emit_indent();
        self.emit("}\n");
        return id;
    }

    // Compile each arg to a temp
    usz[32] arg_temps;
    for (usz i = 0; i < arg_count && i < 32; i++) {
        arg_temps[i] = self.compile_to_temp(expr.call.args[i]);
    }

    // Compile func
    usz func_r = self.compile_to_temp(expr.call.func);

    // Apply args one at a time; last one is tail position
    usz tf = func_r;
    for (usz i = 0; i < arg_count; i++) {
        if (i == arg_count - 1) {
            // Last arg: tail position → emit thunk for closures
            usz id = self.next_result();
            self.emit_temp_decl(id);
            self.emit(";\n");
            self.emit_indent();
            self.emit("if (");
            self.emit_temp_ref(tf);
            self.emit(".tag == runtime::ValueTag.V_CLOSURE) {\n");
            self.indent++;
            self.emit_indent();
            self.emit_temp_ref(id);
            self.emit(" = runtime::make_thunk(");
            self.emit_temp_ref(tf);
            self.emit(".closure_val.invoke, ");
            self.emit_temp_ref(tf);
            self.emit(".closure_val.data, ");
            self.emit_temp_ref(arg_temps[i]);
            self.emit(");\n");
            self.indent--;
            self.emit_indent();
            self.emit("} else {\n");
            self.indent++;
            self.emit_indent();
            self.emit_temp_ref(id);
            self.emit(" = runtime::rt_invoke_once(");
            self.emit_temp_ref(tf);
            self.emit(", ");
            self.emit_temp_ref(arg_temps[i]);
            self.emit(");\n");
            self.indent--;
            self.emit_indent();
            self.emit("}\n");
            return id;
        } else {
            // Intermediate: non-tail, no trampoline needed
            usz next_tf = self.next_result();
            self.emit_temp_decl(next_tf);
            self.emit(" = runtime::rt_invoke_once(");
            self.emit_temp_ref(tf);
            self.emit(", ");
            self.emit_temp_ref(arg_temps[i]);
            self.emit(");\n");
            tf = next_tf;
        }
    }
    // Should not reach here
    return tf;
}

fn usz Compiler.compile_app_flat(Compiler* self, Expr* expr) {
    usz func_r = self.compile_to_temp(expr.app.func);
    usz arg_r = self.compile_to_temp(expr.app.arg);
    usz id = self.next_result();
    self.emit_temp_decl(id);
    self.emit(" = runtime::rt_invoke(");
    self.emit_temp_ref(func_r);
    self.emit(", ");
    self.emit_temp_ref(arg_r);
    self.emit(");\n");
    return id;
}

fn usz Compiler.compile_app_tail_flat(Compiler* self, Expr* expr) {
    usz func_r = self.compile_to_temp(expr.app.func);
    usz arg_r = self.compile_to_temp(expr.app.arg);
    usz id = self.next_result();
    self.emit_temp_decl(id);
    self.emit(";\n");
    self.emit_indent();
    self.emit("if (");
    self.emit_temp_ref(func_r);
    self.emit(".tag == runtime::ValueTag.V_CLOSURE) {\n");
    self.indent++;
    self.emit_indent();
    self.emit_temp_ref(id);
    self.emit(" = runtime::make_thunk(");
    self.emit_temp_ref(func_r);
    self.emit(".closure_val.invoke, ");
    self.emit_temp_ref(func_r);
    self.emit(".closure_val.data, ");
    self.emit_temp_ref(arg_r);
    self.emit(");\n");
    self.indent--;
    self.emit_indent();
    self.emit("} else {\n");
    self.indent++;
    self.emit_indent();
    self.emit_temp_ref(id);
    self.emit(" = runtime::rt_invoke_once(");
    self.emit_temp_ref(func_r);
    self.emit(", ");
    self.emit_temp_ref(arg_r);
    self.emit(");\n");
    self.indent--;
    self.emit_indent();
    self.emit("}\n");
    return id;
}

fn usz Compiler.compile_if_flat(Compiler* self, Expr* expr) {
    usz cond = self.compile_to_temp(expr.if_expr.test);
    usz id = self.next_result();
    self.emit_temp_decl(id);
    self.emit(";\n");
    self.emit_indent();
    self.emit("if (runtime::rt_is_truthy(");
    self.emit_temp_ref(cond);
    self.emit(")) {\n");
    self.indent++;
    usz then_r = self.compile_to_temp(expr.if_expr.then_branch);
    self.emit_indent();
    self.emit_temp_ref(id);
    self.emit(" = ");
    self.emit_temp_ref(then_r);
    self.emit(";\n");
    self.indent--;
    self.emit_indent();
    self.emit("} else {\n");
    self.indent++;
    usz else_r = self.compile_to_temp(expr.if_expr.else_branch);
    self.emit_indent();
    self.emit_temp_ref(id);
    self.emit(" = ");
    self.emit_temp_ref(else_r);
    self.emit(";\n");
    self.indent--;
    self.emit_indent();
    self.emit("}\n");
    return id;
}

fn usz Compiler.compile_let_flat(Compiler* self, Expr* expr) {
    bool is_mc = self.is_mutable_captured_var(expr.let_expr.name);

    if (is_mc) {
        // Mutable-captured: define in interpreter env
        usz init_r = self.compile_to_temp(expr.let_expr.init);
        self.emit_indent();
        self.emit("runtime::rt_define_var(\"");
        char[] mc_name = self.interp.symbols.get_name(expr.let_expr.name);
        self.emit_escaped(mc_name);
        self.emit("\", ");
        self.emit_temp_ref(init_r);
        self.emit(");\n");
        return self.compile_to_temp(expr.let_expr.body);
    }

    bool already = self.is_declared(expr.let_expr.name);

    if (expr.let_expr.is_recursive) {
        // Recursive let: declare first (if needed), then assign
        if (!already) {
            self.emit_indent();
            self.emit("runtime::Value ");
            self.emit_symbol_name(expr.let_expr.name);
            self.emit(";\n");
            self.mark_declared(expr.let_expr.name);
        }
        usz init_r = self.compile_to_temp(expr.let_expr.init);
        self.emit_indent();
        self.emit_symbol_name(expr.let_expr.name);
        self.emit(" = ");
        self.emit_temp_ref(init_r);
        self.emit(";\n");

        // Patch self-reference: the closure captured an uninitialized copy of name.
        // Now that name holds the closure, update the capture in the region-allocated data.
        Expr* rec_init = expr.let_expr.init;
        if (rec_init.tag == E_LAMBDA) {
            foreach (&def : self.lambda_defs) {
                if (def.body == rec_init.lambda.body && (uint)def.param == (uint)rec_init.lambda.param) {
                    for (usz ci = 0; ci < def.capture_count; ci++) {
                        if ((uint)def.captures[ci] == (uint)expr.let_expr.name) {
                            // Patch: Lambda_N* _sp_N = (Lambda_N*)name.closure_val.data; _sp_N.captured_name = name;
                            self.emit_indent();
                            self.emit("Lambda_");
                            self.emit_usz(def.id);
                            self.emit("* _sp_");
                            self.emit_usz(def.id);
                            self.emit(" = (Lambda_");
                            self.emit_usz(def.id);
                            self.emit("*)");
                            self.emit_symbol_name(expr.let_expr.name);
                            self.emit(".closure_val.data;\n");
                            self.emit_indent();
                            self.emit("_sp_");
                            self.emit_usz(def.id);
                            self.emit(".captured_");
                            self.emit_symbol_name(expr.let_expr.name);
                            self.emit(" = ");
                            self.emit_symbol_name(expr.let_expr.name);
                            self.emit(";\n");
                            break;
                        }
                    }
                    break;
                }
            }
        }
    } else {
        // Non-recursive let
        usz init_r = self.compile_to_temp(expr.let_expr.init);
        self.emit_indent();
        if (!already) {
            self.emit("runtime::Value ");
            self.mark_declared(expr.let_expr.name);
        }
        self.emit_symbol_name(expr.let_expr.name);
        self.emit(" = ");
        self.emit_temp_ref(init_r);
        self.emit(";\n");
    }

    return self.compile_to_temp(expr.let_expr.body);
}

fn usz Compiler.compile_let_flat_tail(Compiler* self, Expr* expr) {
    bool is_mc = self.is_mutable_captured_var(expr.let_expr.name);

    if (is_mc) {
        usz init_r = self.compile_to_temp(expr.let_expr.init);
        self.emit_indent();
        self.emit("runtime::rt_define_var(\"");
        char[] mc_name = self.interp.symbols.get_name(expr.let_expr.name);
        self.emit_escaped(mc_name);
        self.emit("\", ");
        self.emit_temp_ref(init_r);
        self.emit(");\n");
        return self.compile_to_temp_tail(expr.let_expr.body);
    }

    bool already = self.is_declared(expr.let_expr.name);

    if (expr.let_expr.is_recursive) {
        if (!already) {
            self.emit_indent();
            self.emit("runtime::Value ");
            self.emit_symbol_name(expr.let_expr.name);
            self.emit(";\n");
            self.mark_declared(expr.let_expr.name);
        }
        usz init_r = self.compile_to_temp(expr.let_expr.init);
        self.emit_indent();
        self.emit_symbol_name(expr.let_expr.name);
        self.emit(" = ");
        self.emit_temp_ref(init_r);
        self.emit(";\n");

        // Patch self-reference for recursive closure
        Expr* rec_init = expr.let_expr.init;
        if (rec_init.tag == E_LAMBDA) {
            foreach (&def : self.lambda_defs) {
                if (def.body == rec_init.lambda.body && (uint)def.param == (uint)rec_init.lambda.param) {
                    for (usz ci = 0; ci < def.capture_count; ci++) {
                        if ((uint)def.captures[ci] == (uint)expr.let_expr.name) {
                            // Patch: Lambda_N* _sp_N = (Lambda_N*)name.closure_val.data; _sp_N.captured_name = name;
                            self.emit_indent();
                            self.emit("Lambda_");
                            self.emit_usz(def.id);
                            self.emit("* _sp_");
                            self.emit_usz(def.id);
                            self.emit(" = (Lambda_");
                            self.emit_usz(def.id);
                            self.emit("*)");
                            self.emit_symbol_name(expr.let_expr.name);
                            self.emit(".closure_val.data;\n");
                            self.emit_indent();
                            self.emit("_sp_");
                            self.emit_usz(def.id);
                            self.emit(".captured_");
                            self.emit_symbol_name(expr.let_expr.name);
                            self.emit(" = ");
                            self.emit_symbol_name(expr.let_expr.name);
                            self.emit(";\n");
                            break;
                        }
                    }
                    break;
                }
            }
        }
    } else {
        usz init_r = self.compile_to_temp(expr.let_expr.init);
        self.emit_indent();
        if (!already) {
            self.emit("runtime::Value ");
            self.mark_declared(expr.let_expr.name);
        }
        self.emit_symbol_name(expr.let_expr.name);
        self.emit(" = ");
        self.emit_temp_ref(init_r);
        self.emit(";\n");
    }

    return self.compile_to_temp_tail(expr.let_expr.body);
}

fn usz Compiler.compile_and_flat(Compiler* self, Expr* expr) {
    usz left = self.compile_to_temp(expr.and_expr.left);
    usz id = self.next_result();
    self.emit_temp_decl(id);
    self.emit(";\n");
    self.emit_indent();
    self.emit("if (runtime::rt_is_truthy(");
    self.emit_temp_ref(left);
    self.emit(")) {\n");
    self.indent++;
    usz right = self.compile_to_temp(expr.and_expr.right);
    self.emit_indent();
    self.emit_temp_ref(id);
    self.emit(" = ");
    self.emit_temp_ref(right);
    self.emit(";\n");
    self.indent--;
    self.emit_indent();
    self.emit("} else {\n");
    self.indent++;
    self.emit_indent();
    self.emit_temp_ref(id);
    self.emit(" = ");
    self.emit_temp_ref(left);
    self.emit(";\n");
    self.indent--;
    self.emit_indent();
    self.emit("}\n");
    return id;
}

fn usz Compiler.compile_or_flat(Compiler* self, Expr* expr) {
    usz left = self.compile_to_temp(expr.or_expr.left);
    usz id = self.next_result();
    self.emit_temp_decl(id);
    self.emit(";\n");
    self.emit_indent();
    self.emit("if (runtime::rt_is_truthy(");
    self.emit_temp_ref(left);
    self.emit(")) {\n");
    self.indent++;
    self.emit_indent();
    self.emit_temp_ref(id);
    self.emit(" = ");
    self.emit_temp_ref(left);
    self.emit(";\n");
    self.indent--;
    self.emit_indent();
    self.emit("} else {\n");
    self.indent++;
    usz right = self.compile_to_temp(expr.or_expr.right);
    self.emit_indent();
    self.emit_temp_ref(id);
    self.emit(" = ");
    self.emit_temp_ref(right);
    self.emit(";\n");
    self.indent--;
    self.emit_indent();
    self.emit("}\n");
    return id;
}

fn usz Compiler.compile_begin_flat(Compiler* self, Expr* expr) {
    usz count = expr.begin.expr_count;
    if (count == 0) {
        usz id = self.next_result();
        self.emit_temp_decl(id);
        self.emit(" = runtime::make_nil();\n");
        return id;
    }
    // Compile all but last as discarded statements
    for (usz i = 0; i < count - 1; i++) {
        self.compile_to_temp(expr.begin.exprs[i]);
    }
    // Last expression is the result
    return self.compile_to_temp(expr.begin.exprs[count - 1]);
}

fn usz Compiler.compile_set_flat(Compiler* self, Expr* expr) {
    if (self.is_mutable_captured_var(expr.set_expr.name)) {
        usz val_r = self.compile_to_temp(expr.set_expr.value);
        usz id = self.next_result();
        self.emit_temp_decl(id);
        self.emit(" = runtime::rt_set_var(\"");
        char[] set_name = self.interp.symbols.get_name(expr.set_expr.name);
        self.emit_escaped(set_name);
        self.emit("\", ");
        self.emit_temp_ref(val_r);
        self.emit(");\n");
        return id;
    } else {
        usz val_r = self.compile_to_temp(expr.set_expr.value);
        self.emit_indent();
        self.emit_symbol_name(expr.set_expr.name);
        self.emit(" = ");
        self.emit_temp_ref(val_r);
        self.emit(";\n");
        // Result is the variable itself
        usz id = self.next_result();
        self.emit_temp_decl(id);
        self.emit(" = ");
        self.emit_symbol_name(expr.set_expr.name);
        self.emit(";\n");
        return id;
    }
}

fn bool Compiler.is_builtin_primitive(Compiler* self, SymbolId sym) {
    // Check ONLY the hardcoded primitives list, NOT defined_globals.
    // Used for delegation injection — defined globals still need injection
    // into the interpreter env since they're C3 locals, not interpreter vars.
    char[] name = self.interp.symbols.get_name(sym);
    char[][*] primitives = {
        "+", "-", "*", "/", "%",
        "=", "<", ">", "<=", ">=",
        "cons", "car", "cdr", "null?", "pair?",
        "list", "length", "not",
        "print", "println", "newline", "display",
        "string-append", "string-join", "substring",
        "string-split", "string-length", "string->list",
        "list->string", "string-upcase", "string-downcase",
        "string-trim", "string?", "int?", "symbol?",
        "closure?", "continuation?", "double?", "list?",
        "boolean?", "number?",
        "string->number", "number->string",
        "gensym", "load", "apply", "equal?",
        "dict", "dict-set!", "dict?",
        "ref", "push!", "keys", "values", "has?", "remove!",
        "read-file", "write-file", "file-exists?", "read-lines",
        "ffi-open", "ffi-call", "ffi-close", "ffi-sym",
        "type-of",
        "abs", "min", "max", "floor", "ceiling", "round",
        "truncate", "sqrt", "even?", "odd?", "zero?",
        "positive?", "negative?", "gcd",
        "bitwise-and", "bitwise-or", "bitwise-xor",
        "bitwise-not", "lshift", "rshift",
        "true", "false", "nil"
    };
    foreach (prim : primitives) {
        if (self.str_eq(name, prim)) return true;
    }
    return false;
}

// compile_delegate_flat — REMOVED: All expression types now compiled natively.
// Delegation via rt_eval_source() is no longer used.

/**
 * compile_qq_flat — Flat-style quasiquote compilation.
 * Emits statements that build the quoted structure, storing result in _rN.
 * At depth 0, unquote sub-expressions are compiled as normal expressions.
 */
fn usz Compiler.compile_qq_flat(Compiler* self, Expr* tmpl, usz depth) {
    if (tmpl == null) {
        usz id = self.next_result();
        self.emit_temp_decl(id);
        self.emit(" = runtime::make_nil();\n");
        return id;
    }

    switch (tmpl.tag) {
        case E_UNQUOTE: {
            if (depth == 0) {
                // Evaluate the unquoted expression
                return self.compile_to_temp(tmpl.unquote.body);
            }
            // Nested: build (unquote <inner>)
            usz inner_r = self.compile_qq_flat(tmpl.unquote.body, depth - 1);
            usz nil_r = self.next_result();
            self.emit_temp_decl(nil_r);
            self.emit(" = runtime::make_nil();\n");

            usz rest_r = self.next_result();
            self.emit_temp_decl(rest_r);
            self.emit(" = runtime::rt_cons(");
            self.emit_temp_ref(inner_r);
            self.emit(", ");
            self.emit_temp_ref(nil_r);
            self.emit(");\n");

            usz sym_r = self.next_result();
            self.emit_temp_decl(sym_r);
            self.emit(" = runtime::make_symbol(\"unquote\");\n");

            usz id = self.next_result();
            self.emit_temp_decl(id);
            self.emit(" = runtime::rt_cons(");
            self.emit_temp_ref(sym_r);
            self.emit(", ");
            self.emit_temp_ref(rest_r);
            self.emit(");\n");
            return id;
        }
        case E_UNQUOTE_SPLICING: {
            if (depth == 0) {
                // Splicing at top level is an error
                usz id = self.next_result();
                self.emit_temp_decl(id);
                self.emit(" = runtime::make_nil(); /* ERROR: ,@ in non-list context */\n");
                return id;
            }
            // Nested: build (unquote-splicing <inner>)
            usz inner_r = self.compile_qq_flat(tmpl.unquote_splicing.body, depth - 1);
            usz nil_r = self.next_result();
            self.emit_temp_decl(nil_r);
            self.emit(" = runtime::make_nil();\n");

            usz rest_r = self.next_result();
            self.emit_temp_decl(rest_r);
            self.emit(" = runtime::rt_cons(");
            self.emit_temp_ref(inner_r);
            self.emit(", ");
            self.emit_temp_ref(nil_r);
            self.emit(");\n");

            usz sym_r = self.next_result();
            self.emit_temp_decl(sym_r);
            self.emit(" = runtime::make_symbol(\"unquote-splicing\");\n");

            usz id = self.next_result();
            self.emit_temp_decl(id);
            self.emit(" = runtime::rt_cons(");
            self.emit_temp_ref(sym_r);
            self.emit(", ");
            self.emit_temp_ref(rest_r);
            self.emit(");\n");
            return id;
        }
        case E_QUASIQUOTE: {
            // Nested quasiquote: build (quasiquote <inner>)
            usz inner_r = self.compile_qq_flat(tmpl.quasiquote.body, depth + 1);
            usz nil_r = self.next_result();
            self.emit_temp_decl(nil_r);
            self.emit(" = runtime::make_nil();\n");

            usz rest_r = self.next_result();
            self.emit_temp_decl(rest_r);
            self.emit(" = runtime::rt_cons(");
            self.emit_temp_ref(inner_r);
            self.emit(", ");
            self.emit_temp_ref(nil_r);
            self.emit(");\n");

            usz sym_r = self.next_result();
            self.emit_temp_decl(sym_r);
            self.emit(" = runtime::make_symbol(\"quasiquote\");\n");

            usz id = self.next_result();
            self.emit_temp_decl(id);
            self.emit(" = runtime::rt_cons(");
            self.emit_temp_ref(sym_r);
            self.emit(", ");
            self.emit_temp_ref(rest_r);
            self.emit(");\n");
            return id;
        }
        case E_LIT: {
            usz id = self.next_result();
            self.emit_temp_decl(id);
            self.emit(" = ");
            self.compile_literal(tmpl.lit.value);
            self.emit(";\n");
            return id;
        }
        case E_VAR: {
            usz id = self.next_result();
            self.emit_temp_decl(id);
            self.emit(" = runtime::make_symbol(\"");
            char[] name = self.interp.symbols.get_name(tmpl.var_expr.name);
            self.emit_escaped(name);
            self.emit("\");\n");
            return id;
        }
        case E_QUOTE: {
            usz id = self.next_result();
            self.emit_temp_decl(id);
            self.emit(" = ");
            self.compile_literal(tmpl.quote.datum);
            self.emit(";\n");
            return id;
        }
        case E_APP: {
            // Two-element list: (func arg)
            usz func_r = self.compile_qq_flat(tmpl.app.func, depth);
            usz arg_r = self.compile_qq_flat(tmpl.app.arg, depth);

            usz nil_r = self.next_result();
            self.emit_temp_decl(nil_r);
            self.emit(" = runtime::make_nil();\n");

            usz inner_r = self.next_result();
            self.emit_temp_decl(inner_r);
            self.emit(" = runtime::rt_cons(");
            self.emit_temp_ref(arg_r);
            self.emit(", ");
            self.emit_temp_ref(nil_r);
            self.emit(");\n");

            usz id = self.next_result();
            self.emit_temp_decl(id);
            self.emit(" = runtime::rt_cons(");
            self.emit_temp_ref(func_r);
            self.emit(", ");
            self.emit_temp_ref(inner_r);
            self.emit(");\n");
            return id;
        }
        case E_CALL:
            return self.compile_qq_call_flat(tmpl, depth);
        default: {
            usz id = self.next_result();
            self.emit_temp_decl(id);
            self.emit(" = runtime::make_nil();\n");
            return id;
        }
    }
}

/**
 * compile_qq_call_flat — Flat-style compilation of E_CALL in QQ context.
 * Builds cons chain from elements, handling ,@ splicing via rt_list_append.
 */
fn usz Compiler.compile_qq_call_flat(Compiler* self, Expr* tmpl, usz depth) {
    usz total = tmpl.call.arg_count + 1;
    Expr*[17] elements;
    elements[0] = tmpl.call.func;
    for (usz i = 0; i < tmpl.call.arg_count && i < 16; i++) {
        elements[i + 1] = tmpl.call.args[i];
    }

    // Check for splicing
    bool has_splice = false;
    if (depth == 0) {
        for (usz i = 0; i < total; i++) {
            if (elements[i].tag == E_UNQUOTE_SPLICING) {
                has_splice = true;
                break;
            }
        }
    }

    if (!has_splice) {
        // No splicing: build cons chain right to left
        // Start with nil, then cons each element from the end
        usz result_r = self.next_result();
        self.emit_temp_decl(result_r);
        self.emit(" = runtime::make_nil();\n");

        for (usz i = total; i > 0; i--) {
            usz elem_r = self.compile_qq_flat(elements[i - 1], depth);
            usz new_r = self.next_result();
            self.emit_temp_decl(new_r);
            self.emit(" = runtime::rt_cons(");
            self.emit_temp_ref(elem_r);
            self.emit(", ");
            self.emit_temp_ref(result_r);
            self.emit(");\n");
            result_r = new_r;
        }
        return result_r;
    }

    // Has splicing: process elements right to left
    // For each element:
    //   - if ,@ at depth 0: compile inner, append to accumulated result
    //   - otherwise: compile qq, cons to accumulated result
    usz result_r = self.next_result();
    self.emit_temp_decl(result_r);
    self.emit(" = runtime::make_nil();\n");

    for (usz i = total; i > 0; i--) {
        Expr* elem = elements[i - 1];
        if (elem.tag == E_UNQUOTE_SPLICING && depth == 0) {
            // Splice: compile the inner expression and append it before current result
            usz splice_r = self.compile_to_temp(elem.unquote_splicing.body);
            usz new_r = self.next_result();
            self.emit_temp_decl(new_r);
            self.emit(" = runtime::rt_list_append(");
            self.emit_temp_ref(splice_r);
            self.emit(", ");
            self.emit_temp_ref(result_r);
            self.emit(");\n");
            result_r = new_r;
        } else {
            // Normal element: compile qq and cons
            usz elem_r = self.compile_qq_flat(elem, depth);
            usz new_r = self.next_result();
            self.emit_temp_decl(new_r);
            self.emit(" = runtime::rt_cons(");
            self.emit_temp_ref(elem_r);
            self.emit(", ");
            self.emit_temp_ref(result_r);
            self.emit(");\n");
            result_r = new_r;
        }
    }
    return result_r;
}

// =============================================================================
// SECTION: NATIVE EFFECT COMPILATION (flat style)
// =============================================================================

fn usz Compiler.compile_reset_flat(Compiler* self, Expr* expr) {
    // Body has been wrapped as a lambda during prescan
    usz body_r = self.compile_to_temp(expr.reset.body);
    usz id = self.next_result();
    self.emit_temp_decl(id);
    self.emit(" = runtime::rt_compiled_reset(");
    self.emit_temp_ref(body_r);
    self.emit(");\n");
    return id;
}

fn usz Compiler.compile_shift_flat(Compiler* self, Expr* expr) {
    // Body has been wrapped as lambda (lambda (k) ...) during prescan
    usz body_r = self.compile_to_temp(expr.shift.body);
    usz id = self.next_result();
    self.emit_temp_decl(id);
    self.emit(" = runtime::rt_compiled_shift(");
    self.emit_temp_ref(body_r);
    self.emit(");\n");
    return id;
}

fn usz Compiler.compile_perform_flat(Compiler* self, Expr* expr) {
    // Compile the argument
    usz arg_r = self.compile_to_temp(expr.perform.arg);
    // Create tag symbol
    usz tag_r = self.next_result();
    self.emit_temp_decl(tag_r);
    self.emit(" = runtime::make_symbol(\"");
    char[] tag_name = self.interp.symbols.get_name(expr.perform.tag);
    self.emit_escaped(tag_name);
    self.emit("\");\n");

    usz id = self.next_result();
    self.emit_temp_decl(id);
    self.emit(" = runtime::rt_compiled_perform(");
    self.emit_temp_ref(tag_r);
    self.emit(", ");
    self.emit_temp_ref(arg_r);
    self.emit(");\n");
    return id;
}

fn usz Compiler.compile_handle_flat(Compiler* self, Expr* expr) {
    usz clause_count = expr.handle.clause_count;

    // Compile all handler closures and tags
    usz[8] handler_rs;
    usz[8] tag_rs;
    for (usz i = 0; i < clause_count && i < 8; i++) {
        // Tag symbol
        tag_rs[i] = self.next_result();
        self.emit_temp_decl(tag_rs[i]);
        self.emit(" = runtime::make_symbol(\"");
        char[] tag_name = self.interp.symbols.get_name(expr.handle.clauses[i].effect_tag);
        self.emit_escaped(tag_name);
        self.emit("\");\n");

        // Handler closure (synthetic lambda from prescan)
        handler_rs[i] = self.compile_to_temp(expr.handle.clauses[i].handler_body);
    }

    // Compile body closure (synthetic lambda from prescan)
    usz body_r = self.compile_to_temp(expr.handle.body);

    // Build tags and handlers arrays (C3 array syntax: Type[N] name)
    usz tags_arr = self.next_result();
    self.emit_indent();
    self.emit("runtime::Value[");
    self.emit_usz(clause_count > 0 ? clause_count : 1);
    self.emit("] _eff_tags_");
    self.emit_usz(tags_arr);
    self.emit(";\n");

    usz hdlrs_arr = self.next_result();
    self.emit_indent();
    self.emit("runtime::Value[");
    self.emit_usz(clause_count > 0 ? clause_count : 1);
    self.emit("] _eff_hdlrs_");
    self.emit_usz(hdlrs_arr);
    self.emit(";\n");

    for (usz i = 0; i < clause_count && i < 8; i++) {
        self.emit_indent();
        self.emit("_eff_tags_");
        self.emit_usz(tags_arr);
        self.emit("[");
        self.emit_usz(i);
        self.emit("] = ");
        self.emit_temp_ref(tag_rs[i]);
        self.emit(";\n");

        self.emit_indent();
        self.emit("_eff_hdlrs_");
        self.emit_usz(hdlrs_arr);
        self.emit("[");
        self.emit_usz(i);
        self.emit("] = ");
        self.emit_temp_ref(handler_rs[i]);
        self.emit(";\n");
    }

    usz id = self.next_result();
    self.emit_temp_decl(id);
    self.emit(" = runtime::rt_compiled_handle(&_eff_tags_");
    self.emit_usz(tags_arr);
    self.emit(", &_eff_hdlrs_");
    self.emit_usz(hdlrs_arr);
    self.emit(", ");
    self.emit_usz(clause_count);
    self.emit(", ");
    self.emit_temp_ref(body_r);
    self.emit(");\n");
    return id;
}

fn usz Compiler.compile_lambda_flat(Compiler* self, Expr* expr) {
    // Find the lambda definition we created earlier
    usz lambda_id = 0;
    LambdaDef* found_def = null;

    foreach (&def : self.lambda_defs) {
        if (def.body == expr.lambda.body && (uint)def.param == (uint)expr.lambda.param) {
            lambda_id = def.id;
            found_def = def;
            break;
        }
    }

    if (found_def != null && found_def.capture_count > 0) {
        // Closure with captures - emit as statements
        self.emit_indent();
        self.emit("Lambda_");
        self.emit_usz(lambda_id);
        self.emit(" _closure_data_");
        self.emit_usz(lambda_id);
        self.emit(";\n");

        // Set home region
        self.emit_indent();
        self.emit("_closure_data_");
        self.emit_usz(lambda_id);
        self.emit(".home_region = runtime::rt_current_region();\n");

        // Set captured values with write barriers
        for (usz i = 0; i < found_def.capture_count; i++) {
            self.emit_indent();
            self.emit("_closure_data_");
            self.emit_usz(lambda_id);
            self.emit(".captured_");
            self.emit_symbol_name(found_def.captures[i]);
            self.emit(" = ");
            self.emit_symbol_name(found_def.captures[i]);
            self.emit(";\n");

            // Write barrier
            self.emit_indent();
            self.emit("runtime::rt_capture_value(_closure_data_");
            self.emit_usz(lambda_id);
            self.emit(".home_region, ");
            self.emit_symbol_name(found_def.captures[i]);
            self.emit(");\n");
        }

        // Allocate in region
        self.emit_indent();
        self.emit("main::ObjectHandle _closure_handle_");
        self.emit_usz(lambda_id);
        self.emit(" = main::allocate_in(runtime::rt_current_region(), Lambda_");
        self.emit_usz(lambda_id);
        self.emit(", _closure_data_");
        self.emit_usz(lambda_id);
        self.emit(");\n");

        usz id = self.next_result();
        self.emit_temp_decl(id);
        self.emit(" = runtime::make_closure(main::dereference(_closure_handle_");
        self.emit_usz(lambda_id);
        self.emit("), &invoke_lambda_");
        self.emit_usz(lambda_id);
        self.emit(");\n");
        return id;
    } else {
        // Simple closure without captures
        usz id = self.next_result();
        self.emit_temp_decl(id);
        self.emit(" = runtime::make_closure(null, &invoke_lambda_");
        self.emit_usz(lambda_id);
        self.emit(");\n");
        return id;
    }
}

fn usz Compiler.compile_match_flat(Compiler* self, Expr* expr) {
    // Compile scrutinee to temp
    usz scrutinee_r = self.compile_to_temp(expr.match.scrutinee);

    // Declare result temp
    usz id = self.next_result();
    self.emit_temp_decl(id);
    self.emit(" = runtime::make_nil();\n");

    for (usz i = 0; i < expr.match.clause_count; i++) {
        self.emit_indent();
        if (i > 0) {
            self.emit("else ");
        }
        self.emit("if (");
        // Use temp ref name for pattern checks
        char[16] ref_name;
        usz ref_len = io::bprintf(&ref_name, "_r%d", (int)scrutinee_r)!!.len;
        self.compile_pattern_check(expr.match.clauses[i].pattern, ref_name[:ref_len]);
        self.emit(") {\n");
        self.indent++;

        // Bind pattern variables
        self.emit_indent();
        self.compile_pattern_bindings(expr.match.clauses[i].pattern, ref_name[:ref_len]);

        // Compile result
        usz clause_r = self.compile_to_temp(expr.match.clauses[i].result);
        self.emit_indent();
        self.emit_temp_ref(id);
        self.emit(" = ");
        self.emit_temp_ref(clause_r);
        self.emit(";\n");
        self.indent--;
        self.emit_indent();
        self.emit("} ");
    }

    self.emit("\n");
    return id;
}

fn void Compiler.compile_literal(Compiler* self, Value* v) {
    if (v == null || v.tag == NIL) {
        self.emit("runtime::make_nil()");
        return;
    }

    switch (v.tag) {
        case INT:
            self.emit("runtime::make_int(");
            self.emit_int(v.int_val);
            self.emit(")");

        case DOUBLE:
            self.emit("runtime::rt_make_double(");
            char[64] dbuf;
            char[] dslice = io::bprintf(&dbuf, "%.17g", v.double_val)!!;
            self.emit(dslice);
            self.emit(")");

        case STRING:
            self.emit("runtime::make_string(\"");
            // Emit string with escaping
            for (usz i = 0; i < v.str_val.len; i++) {
                char c = v.str_val.chars[i];
                if (c == '"') {
                    self.emit("\\\"");
                } else if (c == '\\') {
                    self.emit("\\\\");
                } else if (c == '\n') {
                    self.emit("\\n");
                } else if (c == '\t') {
                    self.emit("\\t");
                } else {
                    self.emit_char(c);
                }
            }
            self.emit("\")");

        case SYMBOL:
            self.emit("runtime::make_symbol(\"");
            char[] name = self.interp.symbols.get_name(v.sym_val);
            self.emit_escaped(name);
            self.emit("\")");

        case CONS:
            self.emit("runtime::rt_cons(");
            self.compile_literal(v.cons_val.car);
            self.emit(", ");
            self.compile_literal(v.cons_val.cdr);
            self.emit(")");

        default:
            self.emit("runtime::make_nil()");
    }
}

fn void Compiler.compile_var(Compiler* self, Expr* expr) {
    SymbolId name = expr.var_expr.name;

    // O(1) hash lookup for known primitives and literals
    char[] emit_code = prim_hash_lookup(name);
    if (emit_code.len > 0) {
        self.emit(emit_code);
        return;
    }

    // Mutable-captured variable: look up from interpreter env
    if (self.is_mutable_captured_var(name)) {
        char[] sym_name = self.interp.symbols.get_name(name);
        self.emit("runtime::rt_lookup_var(\"");
        self.emit_escaped(sym_name);
        self.emit("\")");
        return;
    }

    // Regular variable reference
    self.emit_symbol_name(name);
}

fn void Compiler.compile_lambda(Compiler* self, Expr* expr) {
    // Find the lambda definition we created earlier
    usz lambda_id = 0;
    LambdaDef* found_def = null;

    foreach (&def : self.lambda_defs) {
        if (def.body == expr.lambda.body && (uint)def.param == (uint)expr.lambda.param) {
            lambda_id = def.id;
            found_def = def;
            break;
        }
    }

    if (found_def != null && found_def.capture_count > 0) {
        // Closure with captures - allocate struct in region and set captures
        self.emit("({ Lambda_");
        self.emit_usz(lambda_id);
        self.emit(" _closure_data; ");

        // Set home region
        self.emit("_closure_data.home_region = runtime::rt_current_region(); ");

        // Set captured values with write barriers
        for (usz i = 0; i < found_def.capture_count; i++) {
            self.emit("_closure_data.captured_");
            self.emit_symbol_name(found_def.captures[i]);
            self.emit(" = ");
            self.emit_symbol_name(found_def.captures[i]);
            self.emit("; ");

            // Emit write barrier for captured value
            self.emit("runtime::rt_capture_value(_closure_data.home_region, ");
            self.emit_symbol_name(found_def.captures[i]);
            self.emit("); ");
        }

        // Allocate in region
        self.emit("main::ObjectHandle _closure_handle = main::allocate_in(runtime::rt_current_region(), Lambda_");
        self.emit_usz(lambda_id);
        self.emit(", _closure_data); ");

        self.emit("runtime::make_closure(main::dereference(_closure_handle), &invoke_lambda_");
        self.emit_usz(lambda_id);
        self.emit("); })");
    } else {
        // Simple closure without captures
        self.emit("runtime::make_closure(null, &invoke_lambda_");
        self.emit_usz(lambda_id);
        self.emit(")");
    }
}

fn void Compiler.compile_application(Compiler* self, Expr* expr) {
    // Check for direct primitive calls that can be optimized
    if (expr.app.func.tag == E_VAR) {
        char[] name = self.interp.symbols.get_name(expr.app.func.var_expr.name);

        // Check if this is a curried binary operation
        if (expr.app.arg != null && self.is_binary_prim(name)) {
            // Check if the function is already partially applied
            // For (+ 1 2), this would be (app (app + 1) 2)
            if (expr.app.func.tag == E_APP) {
                // Already partial
            }
        }
    }

    // General case: invoke(func, arg)
    self.emit("runtime::rt_invoke(");
    self.compile_expr(expr.app.func);
    self.emit(", ");
    self.compile_expr(expr.app.arg);
    self.emit(")");
}

fn bool Compiler.is_binary_prim(Compiler* self, char[] name) {
    char[][*] binary_ops = {"+", "-", "*", "/", "%", "=", "<", ">", "<=", ">=", "cons"};
    foreach (op : binary_ops) {
        if (self.str_eq(name, op)) return true;
    }
    return false;
}

fn void Compiler.compile_if(Compiler* self, Expr* expr) {
    // Use C3's ternary operator
    self.emit("(runtime::rt_is_truthy(");
    self.compile_expr(expr.if_expr.test);
    self.emit(") ? ");
    self.compile_expr(expr.if_expr.then_branch);
    self.emit(" : ");
    self.compile_expr(expr.if_expr.else_branch);
    self.emit(")");
}

fn void Compiler.compile_let(Compiler* self, Expr* expr) {
    // Check if this let-bound variable was identified as mutable-captured in prescan
    bool is_mc = self.is_mutable_captured_var(expr.let_expr.name);

    if (is_mc) {
        // Mutable-captured: define in interpreter env instead of C3 local
        self.emit("({ ");
        self.emit("runtime::rt_define_var(\"");
        char[] mc_name = self.interp.symbols.get_name(expr.let_expr.name);
        self.emit_escaped(mc_name);
        self.emit("\", ");
        self.compile_expr(expr.let_expr.init);
        self.emit("); ");
        self.compile_expr(expr.let_expr.body);
        self.emit("; })");
    } else if (expr.let_expr.is_recursive) {
        // Recursive let: declare first, then assign (name visible in initializer)
        self.emit("({ Value ");
        self.emit_symbol_name(expr.let_expr.name);
        self.emit("; ");
        self.emit_symbol_name(expr.let_expr.name);
        self.emit(" = ");
        self.compile_expr(expr.let_expr.init);
        self.emit("; ");
        self.compile_expr(expr.let_expr.body);
        self.emit("; })");
    } else {
        // Non-recursive let: declare and assign in one statement
        self.emit("({ Value ");
        self.emit_symbol_name(expr.let_expr.name);
        self.emit(" = ");
        self.compile_expr(expr.let_expr.init);
        self.emit("; ");
        self.compile_expr(expr.let_expr.body);
        self.emit("; })");
    }
}

fn void Compiler.compile_quote(Compiler* self, Value* datum) {
    // Quote compiles to literal value construction
    self.compile_literal(datum);
}

fn void Compiler.compile_and(Compiler* self, Expr* expr) {
    // (and a b) => rt_is_truthy(a) ? b : a
    self.emit("({ Value _and_left = ");
    self.compile_expr(expr.and_expr.left);
    self.emit("; runtime::rt_is_truthy(_and_left) ? ");
    self.compile_expr(expr.and_expr.right);
    self.emit(" : _and_left; })");
}

fn void Compiler.compile_or(Compiler* self, Expr* expr) {
    // (or a b) => rt_is_truthy(a) ? a : b
    self.emit("({ Value _or_left = ");
    self.compile_expr(expr.or_expr.left);
    self.emit("; runtime::rt_is_truthy(_or_left) ? _or_left : ");
    self.compile_expr(expr.or_expr.right);
    self.emit("; })");
}

fn void Compiler.compile_match(Compiler* self, Expr* expr) {
    // Compile match to a series of if-else checks
    self.emit("({ Value _match_val = ");
    self.compile_expr(expr.match.scrutinee);
    self.emit("; Value _match_result = runtime::make_nil(); ");

    for (usz i = 0; i < expr.match.clause_count; i++) {
        if (i > 0) {
            self.emit("else ");
        }
        self.emit("if (");
        self.compile_pattern_check(expr.match.clauses[i].pattern, "_match_val");
        self.emit(") { ");

        // Bind pattern variables
        self.compile_pattern_bindings(expr.match.clauses[i].pattern, "_match_val");

        self.emit("_match_result = ");
        self.compile_expr(expr.match.clauses[i].result);
        self.emit("; } ");
    }

    self.emit("_match_result; })");
}

fn void Compiler.compile_pattern_check(Compiler* self, Pattern* pat, char[] val_name) {
    if (pat == null) {
        self.emit("true");
        return;
    }

    switch (pat.tag) {
        case PAT_WILDCARD:
            self.emit("true");

        case PAT_VAR:
            self.emit("true");

        case PAT_LIT:
            self.emit("runtime::rt_values_equal(");
            self.emit(val_name);
            self.emit(", ");
            self.compile_literal(pat.lit_value);
            self.emit(")");

        case PAT_QUOTE:
            self.emit("runtime::rt_values_equal(");
            self.emit(val_name);
            self.emit(", ");
            self.compile_literal(pat.quote_datum);
            self.emit(")");

        case PAT_SEQ:
            // Check list length and elements
            self.emit("runtime::rt_is_list(");
            self.emit(val_name);
            self.emit(")");

            if (pat.rest_pos == REST_NONE) {
                // Exact length match
                self.emit(" && runtime::rt_list_length(");
                self.emit(val_name);
                self.emit(") == ");
                self.emit_usz(pat.elem_count);
            } else if (pat.rest_pos == REST_START || pat.rest_pos == REST_MIDDLE || pat.rest_pos == REST_END) {
                // At least N elements
                self.emit(" && runtime::rt_list_length(");
                self.emit(val_name);
                self.emit(") >= ");
                self.emit_usz(pat.elem_count);
            }

        default:
            self.emit("true");
    }
}

fn void Compiler.emit_pattern_var_decl(Compiler* self, SymbolId name) {
    if (!self.is_declared(name)) {
        self.emit("runtime::Value ");
        self.mark_declared(name);
    }
}

fn void Compiler.compile_pattern_bindings(Compiler* self, Pattern* pat, char[] val_name) {
    if (pat == null) return;

    switch (pat.tag) {
        case PAT_VAR:
            self.emit_pattern_var_decl(pat.var_name);
            self.emit_symbol_name(pat.var_name);
            self.emit(" = ");
            self.emit(val_name);
            self.emit("; ");

        case PAT_SEQ:
            // Bind sequence elements
            for (usz i = 0; i < pat.elem_count; i++) {
                // Generate element access — index depends on rest position
                self.emit("runtime::Value _elem_");
                self.emit_usz(i);
                if (pat.rest_pos == REST_START) {
                    // [.. a b] — elements are at end of list
                    self.emit(" = runtime::rt_list_nth(");
                    self.emit(val_name);
                    self.emit(", runtime::rt_list_length(");
                    self.emit(val_name);
                    self.emit(") - ");
                    self.emit_usz(pat.elem_count - i);
                    self.emit("); ");
                } else {
                    // REST_NONE, REST_END, REST_MIDDLE — elements at start
                    self.emit(" = runtime::rt_list_nth(");
                    self.emit(val_name);
                    self.emit(", ");
                    self.emit_usz(i);
                    self.emit("); ");
                }

                // Recursively bind
                if (pat.elements[i].tag == PAT_VAR) {
                    self.emit_pattern_var_decl(pat.elements[i].var_name);
                    self.emit_symbol_name(pat.elements[i].var_name);
                    self.emit(" = _elem_");
                    self.emit_usz(i);
                    self.emit("; ");
                }
            }

            // Bind rest if REST_MIDDLE
            if (pat.rest_pos == REST_MIDDLE) {
                self.emit_pattern_var_decl(pat.rest_binding);
                self.emit_symbol_name(pat.rest_binding);
                self.emit(" = runtime::rt_list_rest(");
                self.emit(val_name);
                self.emit(", ");
                self.emit_usz(pat.elem_count);
                self.emit("); ");
            }

        default:
            return;
    }
}

// =============================================================================
// SECTION 8b: NEW EXPRESSION COMPILATION
// =============================================================================

/**
 * compile_call — Multi-argument function call.
 * (f a b c) => rt_apply_multi(f, cons(a, cons(b, cons(c, nil))), 3)
 * Uses temp vars to evaluate args left-to-right, then builds cons list right-to-left.
 */
fn void Compiler.compile_call(Compiler* self, Expr* expr) {
    usz arg_count = expr.call.arg_count;

    if (arg_count == 0) {
        // Zero-arg call: rt_apply_multi(func, make_nil(), 0)
        self.emit("runtime::rt_apply_multi(");
        self.compile_expr(expr.call.func);
        self.emit(", runtime::make_nil(), 0)");
        return;
    }

    // Multi-arg: evaluate args into temps, build cons list, call rt_apply_multi
    self.emit("({ ");

    // Evaluate each arg into a temp variable (left-to-right evaluation order)
    // Reserve temp names upfront to avoid collisions with nested compile_expr
    usz base_temp = self.temp_counter;
    self.temp_counter += arg_count;
    for (usz i = 0; i < arg_count; i++) {
        self.emit("runtime::Value _t");
        self.emit_usz(base_temp + i);
        self.emit(" = ");
        self.compile_expr(expr.call.args[i]);
        self.emit("; ");
    }

    // Build cons list right-to-left
    self.emit("runtime::Value _args");
    self.emit_usz(base_temp);
    self.emit(" = runtime::make_nil(); ");

    for (isz i = (isz)arg_count - 1; i >= 0; i--) {
        self.emit("_args");
        self.emit_usz(base_temp);
        self.emit(" = runtime::rt_cons(_t");
        self.emit_usz(base_temp + (usz)i);
        self.emit(", _args");
        self.emit_usz(base_temp);
        self.emit("); ");
    }

    // Call rt_apply_multi
    self.emit("runtime::rt_apply_multi(");
    self.compile_expr(expr.call.func);
    self.emit(", _args");
    self.emit_usz(base_temp);
    self.emit(", ");
    self.emit_usz(arg_count);
    self.emit("); })");
}

/**
 * compile_index — Dot-bracket access: arr.[0]
 */
fn void Compiler.compile_index(Compiler* self, Expr* expr) {
    self.emit("runtime::rt_index(");
    self.compile_expr(expr.index.collection);
    self.emit(", ");
    self.compile_expr(expr.index.index);
    self.emit(")");
}

/**
 * compile_path — Field access: point.x, person.address.city
 * person.address.city => rt_field_access(rt_field_access(person, "address"), "city")
 */
fn void Compiler.compile_path(Compiler* self, Expr* expr) {
    if (expr.path.segment_count == 0) {
        self.emit("runtime::make_nil()");
        return;
    }

    // First segment is a variable, remaining segments are field lookups
    for (usz i = 1; i < expr.path.segment_count; i++) {
        self.emit("runtime::rt_field_access(");
    }

    // Emit first segment as variable reference
    self.emit_symbol_name(expr.path.segments[0]);

    // Emit remaining segments as string field names (wrapped in make_string for Value type)
    for (usz i = 1; i < expr.path.segment_count; i++) {
        self.emit(", runtime::make_string(\"");
        char[] name = self.interp.symbols.get_name(expr.path.segments[i]);
        self.emit_escaped(name);
        self.emit("\"))");
    }
}

/**
 * compile_reset — Native compilation. Body is a synthetic lambda (created during prescan).
 * Compiles to: rt_compiled_reset(body_closure)
 */
fn void Compiler.compile_reset(Compiler* self, Expr* expr) {
    self.emit("runtime::rt_compiled_reset(");
    self.compile_expr(expr.reset.body);  // The synthetic lambda
    self.emit(")");
}

/**
 * compile_shift — Native compilation. Body is a synthetic lambda taking k.
 * Compiles to: rt_compiled_shift(k_body_closure)
 */
fn void Compiler.compile_shift(Compiler* self, Expr* expr) {
    self.emit("runtime::rt_compiled_shift(");
    self.compile_expr(expr.shift.body);  // The synthetic lambda (lambda (k) ...)
    self.emit(")");
}

/**
 * compile_perform — Native compilation. Compile tag and arg directly.
 * Compiles to: rt_compiled_perform(tag_symbol, compiled_arg)
 */
fn void Compiler.compile_perform(Compiler* self, Expr* expr) {
    self.emit("runtime::rt_compiled_perform(runtime::make_symbol(\"");
    char[] tag_name = self.interp.symbols.get_name(expr.perform.tag);
    self.emit_escaped(tag_name);
    self.emit("\"), ");
    self.compile_expr(expr.perform.arg);
    self.emit(")");
}

/**
 * compile_handle — Native compilation. Body and handlers are synthetic lambdas.
 * Compiles to: rt_compiled_handle(tags, handlers, count, body_closure)
 */
fn void Compiler.compile_handle(Compiler* self, Expr* expr) {
    usz clause_count = expr.handle.clause_count;
    usz base = self.temp_counter;
    self.temp_counter += 2;  // tags array + handlers array

    self.emit("({ ");

    // Declare tags and handlers arrays (C3 syntax: Type[N] name)
    self.emit("runtime::Value[");
    self.emit_usz(clause_count > 0 ? clause_count : 1);
    self.emit("] _eff_tags");
    self.emit_usz(base);
    self.emit("; ");

    self.emit("runtime::Value[");
    self.emit_usz(clause_count > 0 ? clause_count : 1);
    self.emit("] _eff_hdlrs");
    self.emit_usz(base);
    self.emit("; ");

    // Fill in tags and handlers
    for (usz i = 0; i < clause_count; i++) {
        self.emit("_eff_tags");
        self.emit_usz(base);
        self.emit("[");
        self.emit_usz(i);
        self.emit("] = runtime::make_symbol(\"");
        char[] tag_name = self.interp.symbols.get_name(expr.handle.clauses[i].effect_tag);
        self.emit_escaped(tag_name);
        self.emit("\"); ");

        self.emit("_eff_hdlrs");
        self.emit_usz(base);
        self.emit("[");
        self.emit_usz(i);
        self.emit("] = ");
        self.compile_expr(expr.handle.clauses[i].handler_body);  // The synthetic lambda
        self.emit("; ");
    }

    self.emit("runtime::rt_compiled_handle(&_eff_tags");
    self.emit_usz(base);
    self.emit(", &_eff_hdlrs");
    self.emit_usz(base);
    self.emit(", ");
    self.emit_usz(clause_count);
    self.emit(", ");
    self.compile_expr(expr.handle.body);  // The synthetic body lambda
    self.emit("); })");
}

// emit_cont_var_injection — REMOVED: No longer needed since all expressions
// are compiled natively (no delegation to interpreter).

/**
 * compile_begin — Sequence expression (begin e1 e2 ... en).
 * Evaluates all, returns the last.
 */
fn void Compiler.compile_begin(Compiler* self, Expr* expr) {
    usz count = expr.begin.expr_count;
    if (count == 0) {
        self.emit("runtime::make_nil()");
        return;
    }
    if (count == 1) {
        self.compile_expr(expr.begin.exprs[0]);
        return;
    }
    // Multi-expr: ({ e1; e2; ...; eN; })
    self.emit("({ ");
    for (usz i = 0; i < count - 1; i++) {
        self.compile_expr(expr.begin.exprs[i]);
        self.emit("; ");
    }
    // Last expression is the result
    self.compile_expr(expr.begin.exprs[count - 1]);
    self.emit("; })");
}

/**
 * compile_set — Variable mutation: (set! name value)
 * For compiled code, globals are module-level C3 variables that can be assigned.
 * For local variables in lambdas, C3 allows reassignment of local vars.
 * The result of set! is the new value.
 */
fn void Compiler.compile_set(Compiler* self, Expr* expr) {
    if (self.is_mutable_captured_var(expr.set_expr.name)) {
        // Mutable-captured variable: set in interpreter env
        self.emit("runtime::rt_set_var(\"");
        char[] set_name = self.interp.symbols.get_name(expr.set_expr.name);
        self.emit_escaped(set_name);
        self.emit("\", ");
        self.compile_expr(expr.set_expr.value);
        self.emit(")");
    } else {
        // (set! name value) => ({ name = <value>; name; })
        self.emit("({ ");
        self.emit_symbol_name(expr.set_expr.name);
        self.emit(" = ");
        self.compile_expr(expr.set_expr.value);
        self.emit("; ");
        self.emit_symbol_name(expr.set_expr.name);
        self.emit("; })");
    }
}

// =============================================================================
// SECTION 8b2: TAIL-POSITION COMPILATION (TCO)
// =============================================================================

// =============================================================================
// SECTION 8c: QUASIQUOTE / MACRO / MODULE COMPILATION
// =============================================================================

/**
 * compile_quasiquote — Native compilation (no interpreter delegation).
 * Recursively walks the QQ template, emitting rt_cons/make_symbol/etc calls.
 * Unquoted sub-expressions at depth 0 are compiled normally.
 */
fn void Compiler.compile_quasiquote(Compiler* self, Expr* expr) {
    // For inline context, delegate to compile_qq which emits inline expressions.
    // This handles simple cases; complex cases use compile_qq_flat via compile_to_temp.
    self.compile_qq(expr.quasiquote.body, 0);
}

/**
 * compile_qq — Emit inline expression for a quasiquote template node.
 * Recursively produces an expression that builds the quoted list structure.
 */
fn void Compiler.compile_qq(Compiler* self, Expr* tmpl, usz depth) {
    if (tmpl == null) {
        self.emit("runtime::make_nil()");
        return;
    }

    switch (tmpl.tag) {
        case E_UNQUOTE: {
            if (depth == 0) {
                // At depth 0, evaluate the unquoted expression
                self.compile_expr(tmpl.unquote.body);
            } else {
                // Nested: build (unquote <inner>)
                self.emit("runtime::rt_cons(runtime::make_symbol(\"unquote\"), runtime::rt_cons(");
                self.compile_qq(tmpl.unquote.body, depth - 1);
                self.emit(", runtime::make_nil()))");
            }
        }
        case E_UNQUOTE_SPLICING: {
            if (depth == 0) {
                // Splicing at top level is an error (should be inside a list)
                self.emit("runtime::make_nil() /* ERROR: ,@ in non-list context */");
            } else {
                // Nested: build (unquote-splicing <inner>)
                self.emit("runtime::rt_cons(runtime::make_symbol(\"unquote-splicing\"), runtime::rt_cons(");
                self.compile_qq(tmpl.unquote_splicing.body, depth - 1);
                self.emit(", runtime::make_nil()))");
            }
        }
        case E_QUASIQUOTE: {
            // Nested quasiquote: build (quasiquote <inner>) and increment depth
            self.emit("runtime::rt_cons(runtime::make_symbol(\"quasiquote\"), runtime::rt_cons(");
            self.compile_qq(tmpl.quasiquote.body, depth + 1);
            self.emit(", runtime::make_nil()))");
        }
        case E_LIT:
            self.compile_literal(tmpl.lit.value);
        case E_VAR: {
            // In QQ context, vars are treated as symbols (not looked up)
            self.emit("runtime::make_symbol(\"");
            char[] name = self.interp.symbols.get_name(tmpl.var_expr.name);
            self.emit_escaped(name);
            self.emit("\")");
        }
        case E_QUOTE:
            self.compile_literal(tmpl.quote.datum);
        case E_APP: {
            // Two-element list: (func arg)
            self.emit("runtime::rt_cons(");
            self.compile_qq(tmpl.app.func, depth);
            self.emit(", runtime::rt_cons(");
            self.compile_qq(tmpl.app.arg, depth);
            self.emit(", runtime::make_nil()))");
        }
        case E_CALL: {
            // Multi-element list: (func arg1 arg2 ...)
            // Build cons chain from right to left inline
            // This can get deeply nested, but it's correct for inline context.
            // For flat context, compile_qq_flat handles this better.
            self.compile_qq_call_inline(tmpl, depth);
        }
        default:
            self.emit("runtime::make_nil()");
    }
}

/**
 * compile_qq_call_inline — Emit inline cons chain for E_CALL in QQ context.
 * Handles splicing by using rt_list_append.
 */
fn void Compiler.compile_qq_call_inline(Compiler* self, Expr* tmpl, usz depth) {
    // Collect all elements: func + args
    usz total = tmpl.call.arg_count + 1;

    // Check if any element is a splice at depth 0
    bool has_splice = false;
    if (depth == 0) {
        for (usz i = 0; i < tmpl.call.arg_count; i++) {
            if (tmpl.call.args[i].tag == E_UNQUOTE_SPLICING) {
                has_splice = true;
                break;
            }
        }
        if (tmpl.call.func.tag == E_UNQUOTE_SPLICING) has_splice = true;
    }

    if (!has_splice) {
        // No splicing — simple cons chain from right to left
        // We need to build: rt_cons(e0, rt_cons(e1, ... rt_cons(eN, nil)...))
        // Emit it using nested calls
        for (usz i = 0; i < total; i++) {
            self.emit("runtime::rt_cons(");
        }
        // Now emit each element followed by ", " and closing parens
        // First element is func
        self.compile_qq(tmpl.call.func, depth);
        self.emit(", ");
        for (usz i = 0; i < tmpl.call.arg_count; i++) {
            self.compile_qq(tmpl.call.args[i], depth);
            if (i < tmpl.call.arg_count - 1) {
                self.emit(", ");
            } else {
                self.emit(", runtime::make_nil()");
            }
        }
        // If no args, close with nil
        if (tmpl.call.arg_count == 0) {
            self.emit("runtime::make_nil()");
        }
        // Close all parens
        for (usz i = 0; i < total; i++) {
            self.emit(")");
        }
    } else {
        // Has splicing — too complex for inline, fall back to a helper comment
        // This case is handled properly by compile_qq_flat (via compile_to_temp)
        // For inline context, we just emit a delegation call as a fallback
        // Actually, let's handle it properly by building with rt_list_append
        // We group consecutive non-splice elements into cons chains, then append splice results
        self.compile_qq_call_with_splice_inline(tmpl, depth);
    }
}

/**
 * compile_qq_call_with_splice_inline — Handle E_CALL with ,@ splice in inline context.
 * Groups elements into segments separated by splices.
 * Result: rt_list_append(segment1, rt_list_append(splice1, rt_list_append(segment2, ...nil)))
 */
fn void Compiler.compile_qq_call_with_splice_inline(Compiler* self, Expr* tmpl, usz depth) {
    // Collect all elements: func + args
    usz total = tmpl.call.arg_count + 1;
    Expr*[17] elements;
    elements[0] = tmpl.call.func;
    for (usz i = 0; i < tmpl.call.arg_count && i < 16; i++) {
        elements[i + 1] = tmpl.call.args[i];
    }

    // Build from right to left: start with nil, prepend segments/splices
    // Actually, build from left to right using rt_list_append chains.
    // The structure is: append(segment1, append(splice_result, append(segment2, nil)))
    // But it's easier to build from right to left.

    // Collect segments: each is either a single splice or a run of non-splice elements
    // Then build: rt_list_append(seg[0], rt_list_append(seg[1], ... nil))

    // Count segments (for opening parens)
    usz seg_count = 0;
    usz i = 0;
    while (i < total) {
        seg_count++;
        if (elements[i].tag == E_UNQUOTE_SPLICING && depth == 0) {
            i++;
        } else {
            while (i < total && !(elements[i].tag == E_UNQUOTE_SPLICING && depth == 0)) {
                i++;
            }
        }
    }

    // Emit opening rt_list_append calls (seg_count - 1 appends + inner nil)
    for (usz s = 0; s < seg_count - 1; s++) {
        self.emit("runtime::rt_list_append(");
    }

    // Emit segments left to right
    i = 0;
    usz seg_idx = 0;
    while (i < total) {
        if (seg_idx > 0) {
            self.emit(", ");
        }

        if (elements[i].tag == E_UNQUOTE_SPLICING && depth == 0) {
            // Splice segment: compile the inner expression
            self.compile_expr(elements[i].unquote_splicing.body);
            i++;
        } else {
            // Non-splice segment: build a cons chain of consecutive non-splice elements
            usz start = i;
            while (i < total && !(elements[i].tag == E_UNQUOTE_SPLICING && depth == 0)) {
                i++;
            }
            usz seg_len = i - start;
            // Build cons chain for elements[start..i)
            for (usz j = 0; j < seg_len; j++) {
                self.emit("runtime::rt_cons(");
            }
            for (usz j = start; j < i; j++) {
                self.compile_qq(elements[j], depth);
                self.emit(", ");
            }
            self.emit("runtime::make_nil()");
            for (usz j = 0; j < seg_len; j++) {
                self.emit(")");
            }
        }

        seg_idx++;
        if (seg_idx < seg_count) {
            // Close this rt_list_append level
        }
    }

    // If only one segment, no append wrappers needed
    // Otherwise close the append calls with trailing nil for last
    if (seg_count > 1) {
        // We need to close (seg_count - 1) rt_list_append calls
        // The last segment was emitted without append, then we close backwards
        for (usz s = 0; s < seg_count - 1; s++) {
            self.emit(")");
        }
    }
}

/**
 * compile_defmacro — No-op: macros are expanded at compile time.
 * The E_DEFMACRO node is a compile-time registration; at runtime it's a no-op.
 */
fn void Compiler.compile_defmacro(Compiler* self, Expr* expr) {
    self.emit("runtime::make_nil() /* macro: expanded at compile time */");
}

/**
 * compile_module — Compile module body inline and track exports.
 * Module definitions become regular globals; the module name + exports
 * are recorded so import can verify they exist.
 */
fn void Compiler.compile_module(Compiler* self, Expr* expr) {
    // Register the module in compiled_modules table
    if (self.compiled_module_count < 32) {
        CompiledModule* mod = &self.compiled_modules[self.compiled_module_count];
        mod.name = expr.module_expr.name;
        mod.export_count = expr.module_expr.export_count;
        for (usz i = 0; i < expr.module_expr.export_count; i++) {
            mod.exports[i] = expr.module_expr.exports[i];
        }
        mod.loaded = true;
        self.compiled_module_count++;
    }

    // Compile body expressions inline — they become top-level code
    self.emit("runtime::make_nil() /* module compiled inline */");
}

/**
 * compile_import — No-op for compiled modules (all definitions already global).
 */
fn void Compiler.compile_import(Compiler* self, Expr* expr) {
    self.emit("runtime::make_nil() /* import: inline module */");
}

/**
 * compile_module_flat — Flat-style module compilation.
 * Compiles module body expressions inline, registers module in table.
 */
fn usz Compiler.compile_module_flat(Compiler* self, Expr* expr) {
    // Register the module in compiled_modules table
    if (self.compiled_module_count < 32) {
        CompiledModule* mod = &self.compiled_modules[self.compiled_module_count];
        mod.name = expr.module_expr.name;
        mod.export_count = expr.module_expr.export_count;
        for (usz i = 0; i < expr.module_expr.export_count; i++) {
            mod.exports[i] = expr.module_expr.exports[i];
        }
        mod.loaded = true;
        self.compiled_module_count++;
    }

    // Compile body expressions inline
    usz last_id = 0;
    for (usz i = 0; i < expr.module_expr.body_count; i++) {
        Expr* body_expr = expr.module_expr.body[i];
        if (body_expr.tag == E_DEFINE) {
            // Define: compile init, assign to global
            usz init_r = self.compile_to_temp(body_expr.define.value);
            self.emit_indent();
            self.emit_symbol_name(body_expr.define.name);
            self.emit(" = ");
            self.emit_temp_ref(init_r);
            self.emit(";\n");
            last_id = init_r;
        } else {
            // Non-define: compile inline
            last_id = self.compile_to_temp(body_expr);
        }
    }

    // Module evaluates to nil
    usz id = self.next_result();
    self.emit_temp_decl(id);
    self.emit(" = runtime::make_nil();\n");
    return id;
}

/**
 * compile_import_flat — Flat-style import compilation.
 * For inline-compiled modules, all definitions are already globals — no-op.
 */
fn usz Compiler.compile_import_flat(Compiler* self, Expr* expr) {
    usz id = self.next_result();
    self.emit_temp_decl(id);
    self.emit(" = runtime::make_nil(); // import: inline module\n");
    return id;
}

// =============================================================================
// SECTION 9: TOP-LEVEL COMPILE FUNCTION
// =============================================================================

/**
 * Compile Lisp source code to C3 source code.
 * Returns the generated C3 code as a string.
 */
fn char[] compile_to_c3(char[] source, Interp* interp) {
    Compiler compiler;
    compiler.init(interp);

    char[] result = compiler.compile_program(source);

    // Note: caller must copy result before compiler is freed
    return result;
}

/**
 * Compile Lisp source code to C3 source code with print_last mode.
 * The last non-define expression is wrapped with rt_print_value + printn
 * so the compiled binary prints its result.
 */
fn char[] compile_to_c3_with_print(char[] source, Interp* interp) {
    Compiler compiler;
    compiler.init(interp);
    compiler.print_last = true;

    char[] result = compiler.compile_program(source);

    // Note: caller must copy result before compiler is freed
    return result;
}

/**
 * Compile Lisp source code to C3 source code with print_all mode.
 * Every non-define expression is wrapped with rt_print_value + printn
 * so the compiled binary prints all results (for e2e testing).
 */
fn char[] compile_to_c3_print_all(char[] source, Interp* interp) {
    Compiler compiler;
    compiler.init(interp);
    compiler.print_all = true;

    char[] result = compiler.compile_program(source);

    // Note: caller must copy result before compiler is freed
    return result;
}
