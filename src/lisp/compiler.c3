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
    List{char}  output;          // Generated C3 code buffer
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
// SECTION 2: COMPILER INITIALIZATION
// =============================================================================

fn void Compiler.init(Compiler* self, Interp* interp) {
    self.interp = interp;
    // Lists auto-initialize when declared
    self.indent = 0;
    self.temp_counter = 0;
    self.lambda_counter = 0;
}

fn void Compiler.free(Compiler* self) {
    self.output.free();
    self.current_captures.free();
    self.defined_globals.free();
    self.lambda_defs.free();
}

// =============================================================================
// SECTION 3: OUTPUT HELPERS
// =============================================================================

fn void Compiler.emit(Compiler* self, char[] s) {
    foreach (c : s) {
        self.output.push(c);
    }
}

fn void Compiler.emit_char(Compiler* self, char c) {
    self.output.push(c);
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

fn void Compiler.emit_symbol_name(Compiler* self, SymbolId sym) {
    char[] name = self.interp.symbols.get_name(sym);
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
    // Return the output as a char slice
    return self.output.entries[:self.output.len()];
}

// =============================================================================
// SECTION 4: FREE VARIABLE ANALYSIS
// =============================================================================

/**
 * Find free variables in an expression that need to be captured.
 * bound_vars contains variables that are bound in the current scope.
 */
fn void Compiler.find_free_vars(Compiler* self, Expr* expr, List{SymbolId}* bound_vars, List{SymbolId}* free_vars) {
    if (expr == null) return;

    switch (expr.tag) {
        case E_LIT:
            // Literals have no free variables
            return;

        case E_VAR:
            SymbolId name = expr.var_expr.name;
            // Check if bound locally
            foreach (b : *bound_vars) {
                if ((uint)b == (uint)name) return;
            }
            // Check if it's a global primitive
            if (self.is_primitive(name)) return;
            // Check if already in free vars
            foreach (f : *free_vars) {
                if ((uint)f == (uint)name) return;
            }
            // It's a free variable
            free_vars.push(name);

        case E_LAMBDA:
            // Add parameter to bound vars (skip sentinel for zero-arg lambdas)
            List{SymbolId} new_bound;
            foreach (b : *bound_vars) {
                new_bound.push(b);
            }
            if ((uint)expr.lambda.param != 0xFFFFFFFF) {
                new_bound.push(expr.lambda.param);
            }
            self.find_free_vars(expr.lambda.body, &new_bound, free_vars);
            new_bound.free();

        case E_APP:
            self.find_free_vars(expr.app.func, bound_vars, free_vars);
            self.find_free_vars(expr.app.arg, bound_vars, free_vars);

        case E_IF:
            self.find_free_vars(expr.if_expr.test, bound_vars, free_vars);
            self.find_free_vars(expr.if_expr.then_branch, bound_vars, free_vars);
            self.find_free_vars(expr.if_expr.else_branch, bound_vars, free_vars);

        case E_LET:
            // Init expression
            self.find_free_vars(expr.let_expr.init, bound_vars, free_vars);
            // Body with name bound
            List{SymbolId} let_bound;
            foreach (b : *bound_vars) {
                let_bound.push(b);
            }
            let_bound.push(expr.let_expr.name);
            self.find_free_vars(expr.let_expr.body, &let_bound, free_vars);
            let_bound.free();

        case E_DEFINE:
            self.find_free_vars(expr.define.value, bound_vars, free_vars);

        case E_QUOTE:
            // Quoted data has no free variables
            return;

        case E_AND:
            self.find_free_vars(expr.and_expr.left, bound_vars, free_vars);
            self.find_free_vars(expr.and_expr.right, bound_vars, free_vars);

        case E_OR:
            self.find_free_vars(expr.or_expr.left, bound_vars, free_vars);
            self.find_free_vars(expr.or_expr.right, bound_vars, free_vars);

        case E_MATCH:
            self.find_free_vars(expr.match.scrutinee, bound_vars, free_vars);
            for (usz i = 0; i < expr.match.clause_count; i++) {
                // Add pattern bindings to bound vars
                List{SymbolId} clause_bound;
                foreach (b : *bound_vars) {
                    clause_bound.push(b);
                }
                self.collect_pattern_bindings(expr.match.clauses[i].pattern, &clause_bound);
                self.find_free_vars(expr.match.clauses[i].result, &clause_bound, free_vars);
                clause_bound.free();
            }

        case E_CALL:
            self.find_free_vars(expr.call.func, bound_vars, free_vars);
            for (usz i = 0; i < expr.call.arg_count; i++) {
                self.find_free_vars(expr.call.args[i], bound_vars, free_vars);
            }

        case E_INDEX:
            self.find_free_vars(expr.index.collection, bound_vars, free_vars);
            self.find_free_vars(expr.index.index, bound_vars, free_vars);

        case E_PATH:
            // First segment is a variable reference
            SymbolId path_name = expr.path.segments[0];
            foreach (b : *bound_vars) {
                if ((uint)b == (uint)path_name) return;
            }
            if (self.is_primitive(path_name)) return;
            foreach (f : *free_vars) {
                if ((uint)f == (uint)path_name) return;
            }
            free_vars.push(path_name);

        case E_RESET:
            self.find_free_vars(expr.reset.body, bound_vars, free_vars);

        case E_SHIFT:
            // k_name is bound in the body
            List{SymbolId} shift_bound;
            foreach (b : *bound_vars) {
                shift_bound.push(b);
            }
            shift_bound.push(expr.shift.k_name);
            self.find_free_vars(expr.shift.body, &shift_bound, free_vars);
            shift_bound.free();

        case E_PERFORM:
            self.find_free_vars(expr.perform.arg, bound_vars, free_vars);

        case E_HANDLE:
            self.find_free_vars(expr.handle.body, bound_vars, free_vars);
            for (usz i = 0; i < expr.handle.clause_count; i++) {
                // k_name and arg_name are bound in handler body
                List{SymbolId} handle_bound;
                foreach (b : *bound_vars) {
                    handle_bound.push(b);
                }
                handle_bound.push(expr.handle.clauses[i].k_name);
                handle_bound.push(expr.handle.clauses[i].arg_name);
                self.find_free_vars(expr.handle.clauses[i].handler_body, &handle_bound, free_vars);
                handle_bound.free();
            }

        case E_BEGIN:
            for (usz i = 0; i < expr.begin.expr_count; i++) {
                self.find_free_vars(expr.begin.exprs[i], bound_vars, free_vars);
            }

        case E_SET:
            // The variable being set might be free
            SymbolId set_name = expr.set_expr.name;
            foreach (b : *bound_vars) {
                if ((uint)b == (uint)set_name) {
                    // Bound locally, just analyze the value
                    self.find_free_vars(expr.set_expr.value, bound_vars, free_vars);
                    return;
                }
            }
            if (self.is_primitive(set_name)) {
                self.find_free_vars(expr.set_expr.value, bound_vars, free_vars);
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
            self.find_free_vars(expr.set_expr.value, bound_vars, free_vars);

        default:
            // Handle other expression types conservatively
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
        "print", "println", "newline",
        "string-append", "string-join", "substring",
        "string-split", "string-length", "string->list",
        "list->string", "string-upcase", "string-downcase",
        "string-trim", "string?", "int?", "symbol?",
        "closure?", "continuation?",
        "read-file", "write-file", "file-exists?", "read-lines",
        "ffi-open", "ffi-call", "ffi-close", "ffi-sym",
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

fn bool Compiler.str_eq(Compiler* self, char[] a, char[] b) {
    if (a.len != b.len) return false;
    for (usz i = 0; i < a.len; i++) {
        if (a[i] != b[i]) return false;
    }
    return true;
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
fn bool Compiler.body_creates_closure(Compiler* self, Expr* body) {
    if (body == null) return false;

    switch (body.tag) {
        case E_LAMBDA:
            // Body directly is a lambda - definitely creates closure
            return true;

        case E_APP:
            // Check both function and argument
            return self.body_creates_closure(body.app.func) ||
                   self.body_creates_closure(body.app.arg);

        case E_IF:
            return self.body_creates_closure(body.if_expr.test) ||
                   self.body_creates_closure(body.if_expr.then_branch) ||
                   self.body_creates_closure(body.if_expr.else_branch);

        case E_LET:
            return self.body_creates_closure(body.let_expr.init) ||
                   self.body_creates_closure(body.let_expr.body);

        case E_DEFINE:
            return self.body_creates_closure(body.define.value);

        case E_AND:
            return self.body_creates_closure(body.and_expr.left) ||
                   self.body_creates_closure(body.and_expr.right);

        case E_OR:
            return self.body_creates_closure(body.or_expr.left) ||
                   self.body_creates_closure(body.or_expr.right);

        case E_MATCH:
            if (self.body_creates_closure(body.match.scrutinee)) return true;
            for (usz i = 0; i < body.match.clause_count; i++) {
                if (self.body_creates_closure(body.match.clauses[i].result)) return true;
            }
            return false;

        case E_CALL:
            if (self.body_creates_closure(body.call.func)) return true;
            for (usz i = 0; i < body.call.arg_count; i++) {
                if (self.body_creates_closure(body.call.args[i])) return true;
            }
            return false;

        case E_INDEX:
            return self.body_creates_closure(body.index.collection) ||
                   self.body_creates_closure(body.index.index);

        case E_RESET:
            return self.body_creates_closure(body.reset.body);

        case E_SHIFT:
            return self.body_creates_closure(body.shift.body);

        case E_PERFORM:
            return self.body_creates_closure(body.perform.arg);

        case E_HANDLE:
            if (self.body_creates_closure(body.handle.body)) return true;
            for (usz i = 0; i < body.handle.clause_count; i++) {
                if (self.body_creates_closure(body.handle.clauses[i].handler_body)) return true;
            }
            return false;

        case E_BEGIN:
            for (usz i = 0; i < body.begin.expr_count; i++) {
                if (self.body_creates_closure(body.begin.exprs[i])) return true;
            }
            return false;

        case E_SET:
            return self.body_creates_closure(body.set_expr.value);

        default:
            return false;
    }
}

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
    self.find_free_vars(expr, &bound, &free);
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
(define map (lambda (f) (lambda (lst) (if (null? lst) nil (cons (f (car lst)) ((map f) (cdr lst)))))))
(define filter (lambda (pred) (lambda (lst) (if (null? lst) nil (if (pred (car lst)) (cons (car lst) ((filter pred) (cdr lst))) ((filter pred) (cdr lst)))))))
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
(define assoc-ref (lambda (key) (lambda (alist) (let ((pair ((assoc key) alist))) (if (null? pair) nil (cdr pair))))))`;

// =============================================================================
// SECTION 5: MAIN COMPILATION
// =============================================================================

/**
 * Compile a complete program to C3 source code.
 * Prepends stdlib definitions so HOFs are available.
 */
fn char[] Compiler.compile_program(Compiler* self, char[] source) {
    // Prepend stdlib prelude to user source
    List{char} full_source;
    foreach (c : STDLIB_PRELUDE) {
        full_source.push(c);
    }
    full_source.push(' ');
    foreach (c : source) {
        full_source.push(c);
    }
    char[] full = full_source.entries[:full_source.len()];

    // Parse the program
    List{Expr*} exprs = parse_program(full, self.interp);

    // First pass: collect all top-level defines
    foreach (expr : exprs) {
        if (expr.tag == E_DEFINE) {
            self.defined_globals.push(expr.define.name);
        }
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

    // Compile each top-level expression
    foreach (expr : exprs) {
        self.emit_indent();
        self.compile_statement(expr);
    }

    self.emit_main_end();

    exprs.free();
    full_source.free();
    return self.get_output();
}

/**
 * Scan expression tree for lambdas and register them.
 */
fn void Compiler.scan_lambdas(Compiler* self, Expr* expr) {
    if (expr == null) return;

    switch (expr.tag) {
        case E_LAMBDA:
            // Find free variables
            List{SymbolId} bound;
            // Don't push sentinel for zero-arg lambdas
            if ((uint)expr.lambda.param != 0xFFFFFFFF) {
                bound.push(expr.lambda.param);
            }
            List{SymbolId} free;
            self.find_free_vars(expr.lambda.body, &bound, &free);

            // Register lambda
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

            // Check if this lambda's body creates nested closures
            def.creates_closure = self.body_creates_closure(expr.lambda.body);
            if (def.id < 256) {
                self.lambda_creates_closure[def.id] = def.creates_closure;
            }

            self.lambda_defs.push(def);

            // Recursively scan body
            self.scan_lambdas(expr.lambda.body);

            bound.free();
            free.free();

        case E_APP:
            self.scan_lambdas(expr.app.func);
            self.scan_lambdas(expr.app.arg);

        case E_IF:
            self.scan_lambdas(expr.if_expr.test);
            self.scan_lambdas(expr.if_expr.then_branch);
            self.scan_lambdas(expr.if_expr.else_branch);

        case E_LET:
            self.scan_lambdas(expr.let_expr.init);
            self.scan_lambdas(expr.let_expr.body);

        case E_DEFINE:
            self.scan_lambdas(expr.define.value);

        case E_AND:
            self.scan_lambdas(expr.and_expr.left);
            self.scan_lambdas(expr.and_expr.right);

        case E_OR:
            self.scan_lambdas(expr.or_expr.left);
            self.scan_lambdas(expr.or_expr.right);

        case E_MATCH:
            self.scan_lambdas(expr.match.scrutinee);
            for (usz i = 0; i < expr.match.clause_count; i++) {
                self.scan_lambdas(expr.match.clauses[i].result);
            }

        case E_CALL:
            self.scan_lambdas(expr.call.func);
            for (usz i = 0; i < expr.call.arg_count; i++) {
                self.scan_lambdas(expr.call.args[i]);
            }

        case E_INDEX:
            self.scan_lambdas(expr.index.collection);
            self.scan_lambdas(expr.index.index);

        case E_PATH:
            // Paths are just variable references, no sub-expressions
            return;

        case E_RESET:
            self.scan_lambdas(expr.reset.body);

        case E_SHIFT:
            self.scan_lambdas(expr.shift.body);

        case E_PERFORM:
            self.scan_lambdas(expr.perform.arg);

        case E_HANDLE:
            self.scan_lambdas(expr.handle.body);
            for (usz i = 0; i < expr.handle.clause_count; i++) {
                self.scan_lambdas(expr.handle.clauses[i].handler_body);
            }

        case E_BEGIN:
            for (usz i = 0; i < expr.begin.expr_count; i++) {
                self.scan_lambdas(expr.begin.exprs[i]);
            }

        case E_SET:
            self.scan_lambdas(expr.set_expr.value);

        default:
            return;
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
                self.emit_indent();
                self.emit("runtime::Value ");
                self.emit_symbol_name(def.captures[i]);
                self.emit(" = self.captured_");
                self.emit_symbol_name(def.captures[i]);
                self.emit(";\n");
            }
        }

        // Compile body and return
        // Special case: if body is a lambda with captures, emit setup statements first
        if (def.body.tag == E_LAMBDA) {
            self.emit_lambda_return_with_frame(def.body, def.creates_closure);
        } else {
            if (def.creates_closure) {
                // Store result, pop frame, then return
                self.emit_indent();
                self.emit("runtime::Value _result = ");
                self.compile_expr(def.body);
                self.emit(";\n");
                self.emit_indent();
                self.emit("runtime::rt_pop_frame(_frame);\n");
                self.emit_indent();
                self.emit("return _result;\n");
            } else {
                self.emit_indent();
                self.emit("return ");
                self.compile_expr(def.body);
                self.emit(";\n");
            }
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
            // (globals are declared at module level, just assign here)
            self.emit_symbol_name(expr.define.name);
            self.emit(" = ");
            self.compile_expr(expr.define.value);
            self.emit(";\n");

        default:
            // Other expressions: evaluate and discard result
            self.compile_expr(expr);
            self.emit(";\n");
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

        default:
            self.emit("runtime::make_nil() /* unsupported expr type */");
    }
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
    char[] sym_name = self.interp.symbols.get_name(name);

    // Check if it's a known primitive
    if (self.str_eq(sym_name, "+")) {
        self.emit("runtime::make_prim(&runtime::rt_add)");
    } else if (self.str_eq(sym_name, "-")) {
        self.emit("runtime::make_prim(&runtime::rt_sub)");
    } else if (self.str_eq(sym_name, "*")) {
        self.emit("runtime::make_prim(&runtime::rt_mul)");
    } else if (self.str_eq(sym_name, "/")) {
        self.emit("runtime::make_prim(&runtime::rt_div)");
    } else if (self.str_eq(sym_name, "%")) {
        self.emit("runtime::make_prim(&runtime::rt_mod)");
    } else if (self.str_eq(sym_name, "=")) {
        self.emit("runtime::make_prim(&runtime::rt_eq)");
    } else if (self.str_eq(sym_name, "<")) {
        self.emit("runtime::make_prim(&runtime::rt_lt)");
    } else if (self.str_eq(sym_name, ">")) {
        self.emit("runtime::make_prim(&runtime::rt_gt)");
    } else if (self.str_eq(sym_name, "<=")) {
        self.emit("runtime::make_prim(&runtime::rt_le)");
    } else if (self.str_eq(sym_name, ">=")) {
        self.emit("runtime::make_prim(&runtime::rt_ge)");
    } else if (self.str_eq(sym_name, "cons")) {
        self.emit("runtime::make_prim(&runtime::rt_cons_prim)");
    } else if (self.str_eq(sym_name, "car")) {
        self.emit("runtime::make_prim_unary(&runtime::rt_car)");
    } else if (self.str_eq(sym_name, "cdr")) {
        self.emit("runtime::make_prim_unary(&runtime::rt_cdr)");
    } else if (self.str_eq(sym_name, "null?")) {
        self.emit("runtime::make_prim_unary(&runtime::rt_null_p)");
    } else if (self.str_eq(sym_name, "pair?")) {
        self.emit("runtime::make_prim_unary(&runtime::rt_pair_p)");
    } else if (self.str_eq(sym_name, "list")) {
        self.emit("runtime::make_prim_unary(&runtime::rt_list)");
    } else if (self.str_eq(sym_name, "length")) {
        self.emit("runtime::make_prim_unary(&runtime::rt_length)");
    } else if (self.str_eq(sym_name, "not")) {
        self.emit("runtime::make_prim_unary(&runtime::rt_not)");
    } else if (self.str_eq(sym_name, "print")) {
        self.emit("runtime::make_prim_unary(&runtime::rt_print)");
    } else if (self.str_eq(sym_name, "println")) {
        self.emit("runtime::make_prim_unary(&runtime::rt_println)");
    } else if (self.str_eq(sym_name, "newline")) {
        self.emit("runtime::make_prim_unary(&runtime::rt_newline)");
    } else if (self.str_eq(sym_name, "string-append")) {
        self.emit("runtime::make_prim(&runtime::rt_string_append)");
    } else if (self.str_eq(sym_name, "string-join")) {
        self.emit("runtime::make_prim(&runtime::rt_string_join)");
    } else if (self.str_eq(sym_name, "substring")) {
        self.emit("runtime::make_prim(&runtime::rt_substring)");
    } else if (self.str_eq(sym_name, "string-split")) {
        self.emit("runtime::make_prim(&runtime::rt_string_split)");
    } else if (self.str_eq(sym_name, "string-length")) {
        self.emit("runtime::make_prim_unary(&runtime::rt_string_length)");
    } else if (self.str_eq(sym_name, "string->list")) {
        self.emit("runtime::make_prim_unary(&runtime::rt_string_to_list)");
    } else if (self.str_eq(sym_name, "list->string")) {
        self.emit("runtime::make_prim_unary(&runtime::rt_list_to_string)");
    } else if (self.str_eq(sym_name, "string-upcase")) {
        self.emit("runtime::make_prim_unary(&runtime::rt_string_upcase)");
    } else if (self.str_eq(sym_name, "string-downcase")) {
        self.emit("runtime::make_prim_unary(&runtime::rt_string_downcase)");
    } else if (self.str_eq(sym_name, "string-trim")) {
        self.emit("runtime::make_prim_unary(&runtime::rt_string_trim)");
    } else if (self.str_eq(sym_name, "string?")) {
        self.emit("runtime::make_prim_unary(&runtime::rt_string_p)");
    } else if (self.str_eq(sym_name, "int?")) {
        self.emit("runtime::make_prim_unary(&runtime::rt_int_p)");
    } else if (self.str_eq(sym_name, "symbol?")) {
        self.emit("runtime::make_prim_unary(&runtime::rt_symbol_p)");
    } else if (self.str_eq(sym_name, "closure?")) {
        self.emit("runtime::make_prim_unary(&runtime::rt_closure_p)");
    } else if (self.str_eq(sym_name, "continuation?")) {
        self.emit("runtime::make_prim_unary(&runtime::rt_continuation_p)");
    } else if (self.str_eq(sym_name, "read-file")) {
        self.emit("runtime::make_prim_unary(&runtime::rt_read_file)");
    } else if (self.str_eq(sym_name, "write-file")) {
        self.emit("runtime::make_prim(&runtime::rt_write_file)");
    } else if (self.str_eq(sym_name, "file-exists?")) {
        self.emit("runtime::make_prim_unary(&runtime::rt_file_exists)");
    } else if (self.str_eq(sym_name, "read-lines")) {
        self.emit("runtime::make_prim_unary(&runtime::rt_read_lines)");
    } else if (self.str_eq(sym_name, "ffi-open")) {
        self.emit("runtime::make_prim_unary(&runtime::rt_ffi_open)");
    } else if (self.str_eq(sym_name, "ffi-close")) {
        self.emit("runtime::make_prim_unary(&runtime::rt_ffi_close)");
    } else if (self.str_eq(sym_name, "ffi-sym")) {
        self.emit("runtime::make_prim(&runtime::rt_ffi_sym)");
    } else if (self.str_eq(sym_name, "ffi-call")) {
        self.emit("runtime::make_prim(&runtime::rt_ffi_call)");
    } else if (self.str_eq(sym_name, "true")) {
        self.emit("runtime::make_true()");
    } else if (self.str_eq(sym_name, "false") || self.str_eq(sym_name, "nil")) {
        self.emit("runtime::make_nil()");
    } else {
        // Regular variable reference
        self.emit_symbol_name(name);
    }
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
    if (expr.let_expr.is_recursive) {
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

fn void Compiler.compile_pattern_bindings(Compiler* self, Pattern* pat, char[] val_name) {
    if (pat == null) return;

    switch (pat.tag) {
        case PAT_VAR:
            self.emit("runtime::Value ");
            self.emit_symbol_name(pat.var_name);
            self.emit(" = ");
            self.emit(val_name);
            self.emit("; ");

        case PAT_SEQ:
            // Bind sequence elements
            for (usz i = 0; i < pat.elem_count; i++) {
                char[32] elem_name;
                // Generate element access
                self.emit("runtime::Value _elem_");
                self.emit_usz(i);
                self.emit(" = runtime::rt_list_nth(");
                self.emit(val_name);
                self.emit(", ");
                self.emit_usz(i);
                self.emit("); ");

                // Recursively bind
                if (pat.elements[i].tag == PAT_VAR) {
                    self.emit("runtime::Value ");
                    self.emit_symbol_name(pat.elements[i].var_name);
                    self.emit(" = _elem_");
                    self.emit_usz(i);
                    self.emit("; ");
                }
            }

            // Bind rest if REST_MIDDLE
            if (pat.rest_pos == REST_MIDDLE) {
                self.emit("runtime::Value ");
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
 * (f a b c) => rt_invoke(rt_invoke(rt_invoke(f, a), b), c)
 */
fn void Compiler.compile_call(Compiler* self, Expr* expr) {
    usz arg_count = expr.call.arg_count;

    if (arg_count == 0) {
        // Zero-arg call: rt_invoke(func, make_nil())
        self.emit("runtime::rt_invoke(");
        self.compile_expr(expr.call.func);
        self.emit(", runtime::make_nil())");
        return;
    }

    // Multi-arg: chain invocations
    // (f a b c) => rt_invoke(rt_invoke(rt_invoke(f, a), b), c)
    for (usz i = 0; i < arg_count; i++) {
        self.emit("runtime::rt_invoke(");
    }
    self.compile_expr(expr.call.func);
    for (usz i = 0; i < arg_count; i++) {
        self.emit(", ");
        self.compile_expr(expr.call.args[i]);
        self.emit(")");
    }
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
 * compile_reset — Delimited continuation reset.
 *
 * Delegates to the interpreter via rt_eval_source(). The entire reset
 * expression is serialized back to Pika source text, and any free
 * variables from the compiled scope are injected into the interpreter
 * before evaluation. This is slower than native compilation but correct,
 * since the interpreter fully implements the replay-based continuation system.
 */
fn void Compiler.compile_reset(Compiler* self, Expr* expr) {
    self.emit("({ ");
    // Inject free variables into interpreter
    self.emit_cont_var_injection(expr);
    // Evaluate the entire reset expression via interpreter
    self.emit("runtime::rt_eval_source(");
    self.emit_serialized_expr(expr);
    self.emit("); })");
}

/**
 * compile_shift — Continuation capture via interpreter delegation.
 */
fn void Compiler.compile_shift(Compiler* self, Expr* expr) {
    // shift only makes sense inside a reset, which delegates to interpreter.
    // If shift appears at the top level (outside a compiled reset), we still
    // delegate to the interpreter. The interpreter will catch "shift outside reset".
    self.emit("({ ");
    self.emit_cont_var_injection(expr);
    self.emit("runtime::rt_eval_source(");
    self.emit_serialized_expr(expr);
    self.emit("); })");
}

/**
 * compile_perform — Effect signaling via interpreter delegation.
 */
fn void Compiler.compile_perform(Compiler* self, Expr* expr) {
    // Like shift, perform makes sense inside a handle block.
    // We delegate to the interpreter for correct handler dispatch.
    self.emit("({ ");
    self.emit_cont_var_injection(expr);
    self.emit("runtime::rt_eval_source(");
    self.emit_serialized_expr(expr);
    self.emit("); })");
}

/**
 * compile_handle — Effect handler installation via interpreter delegation.
 */
fn void Compiler.compile_handle(Compiler* self, Expr* expr) {
    self.emit("({ ");
    // Inject free variables into interpreter
    self.emit_cont_var_injection(expr);
    // Evaluate the entire handle expression via interpreter
    self.emit("runtime::rt_eval_source(");
    self.emit_serialized_expr(expr);
    self.emit("); })");
}

/**
 * Emit code to inject free variables from the compiled scope into the
 * interpreter before evaluating a continuation expression.
 */
fn void Compiler.emit_cont_var_injection(Compiler* self, Expr* expr) {
    List{SymbolId} free_vars = self.collect_cont_free_vars(expr);

    for (usz i = 0; i < free_vars.len(); i++) {
        SymbolId sym = free_vars.entries[i];
        char[] name = self.interp.symbols.get_name(sym);

        // Skip primitives — these are already registered in the interpreter
        if (self.is_primitive(sym)) continue;

        self.emit("runtime::rt_define_var(\"");
        self.emit_escaped(name);
        self.emit("\", ");
        self.emit_symbol_name(sym);
        self.emit("); ");
    }

    free_vars.free();
}

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
    // (set! name value) => ({ name = <value>; name; })
    self.emit("({ ");
    self.emit_symbol_name(expr.set_expr.name);
    self.emit(" = ");
    self.compile_expr(expr.set_expr.value);
    self.emit("; ");
    self.emit_symbol_name(expr.set_expr.name);
    self.emit("; })");
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
