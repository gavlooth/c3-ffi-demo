module lisp;


import std::io;
import std::collections::list;
import main;
import pika;

// =============================================================================
// REPLXX FFI DECLARATIONS
// =============================================================================
// replxx is a readline replacement with syntax highlighting, hints, and completions.
// replxx_input returns a library-managed pointer (do NOT free it). null on EOF.

// Opaque replxx handle — use void* since C3 requires uppercase for type defs
alias ReplxxHighlighterCb = fn void(char*, CInt*, CInt, void*);
alias ReplxxCompletionCb = fn void(char*, void*, CInt*, void*);

extern fn void* replxx_init() @extern("replxx_init");
extern fn void replxx_end(void* rx) @extern("replxx_end");
extern fn char* replxx_input(void* rx, char* prompt) @extern("replxx_input");
extern fn void replxx_history_add(void* rx, char* line) @extern("replxx_history_add");
extern fn int replxx_history_load(void* rx, char* filename) @extern("replxx_history_load");
extern fn int replxx_history_save(void* rx, char* filename) @extern("replxx_history_save");
extern fn void replxx_set_max_history_size(void* rx, CInt len) @extern("replxx_set_max_history_size");
extern fn void replxx_set_highlighter_callback(void* rx, ReplxxHighlighterCb cb, void* ud) @extern("replxx_set_highlighter_callback");
extern fn void replxx_set_completion_callback(void* rx, ReplxxCompletionCb cb, void* ud) @extern("replxx_set_completion_callback");
extern fn void replxx_add_completion(void* lc, char* str) @extern("replxx_add_completion");
extern fn void replxx_set_word_break_characters(void* rx, char* wb) @extern("replxx_set_word_break_characters");
extern fn void replxx_set_unique_history(void* rx, CInt val) @extern("replxx_set_unique_history");
extern fn int replxx_install_window_change_handler(void* rx) @extern("replxx_install_window_change_handler");
extern fn void replxx_enable_bracketed_paste(void* rx) @extern("replxx_enable_bracketed_paste");
extern fn void replxx_set_indent_multiline(void* rx, CInt val) @extern("replxx_set_indent_multiline");

// Replxx color constants
const CInt RXCOLOR_BLACK         = 0;
const CInt RXCOLOR_RED           = 1;
const CInt RXCOLOR_GREEN         = 2;
const CInt RXCOLOR_BROWN         = 3;
const CInt RXCOLOR_BLUE          = 4;
const CInt RXCOLOR_MAGENTA       = 5;
const CInt RXCOLOR_CYAN          = 6;
const CInt RXCOLOR_LIGHTGRAY     = 7;
const CInt RXCOLOR_GRAY          = 8;
const CInt RXCOLOR_BRIGHTRED     = 9;
const CInt RXCOLOR_BRIGHTGREEN   = 10;
const CInt RXCOLOR_YELLOW        = 11;
const CInt RXCOLOR_BRIGHTBLUE    = 12;
const CInt RXCOLOR_BRIGHTMAGENTA = 13;
const CInt RXCOLOR_BRIGHTCYAN    = 14;
const CInt RXCOLOR_WHITE         = 15;
const CInt RXCOLOR_DEFAULT       = 1 << 16;
const CInt RXCOLOR_BOLD          = 1 << 17;

// Global replxx handle for use in callbacks
void* g_replxx = null;
// Global interp for completion callback
Interp* g_repl_interp = null;

struct CopyToParentStats {
    usz total_calls;
    usz fast_reuse_hits;
    usz defensive_copies;

    usz nil_calls;
    usz int_calls;
    usz double_calls;
    usz string_calls;
    usz symbol_calls;
    usz cons_calls;
    usz closure_calls;
    usz other_calls;

    usz[20] site_calls;
}

CopyToParentStats g_copy_to_parent_stats;

const uint COPY_SITE_GENERIC = 0;
const uint COPY_SITE_PROMOTE_TO_ROOT = 1;
const uint COPY_SITE_COPY_ENV_TO_SCOPE = 2;
const uint COPY_SITE_RUN_JIT_RESULT = 3;
const uint COPY_SITE_REPL_LINE_RESULT = 4;
const uint COPY_SITE_PRIM_RESUME = 5;
const uint COPY_SITE_CONS_BARRIER_CAR = 6;
const uint COPY_SITE_CONS_BARRIER_CDR = 7;
const uint COPY_SITE_ENV_PROMOTE_FOR_TARGET = 8;
const uint COPY_SITE_JIT_SINGLE_SCOPE_RETURN = 9;
const uint COPY_SITE_JIT_CALL_SCOPE_STEP2 = 10;
const uint COPY_SITE_JIT_TCO_FRAME_COPY = 11;
const uint COPY_SITE_JIT_EVAL_SET = 12;
const uint COPY_SITE_JIT_EVAL_DEFINE = 13;
const uint COPY_SITE_JIT_ENV_EXTEND_ROOT = 14;
const uint COPY_SITE_COUNT = 15;
const usz PROMOTION_CTX_BUDGET_RETURN = 4096;
const usz PROMOTION_CTX_BUDGET_TCO = 2048;

fn void copy_to_parent_note_tag(ValueTag tag) @inline {
    switch (tag) {
        case NIL:
            g_copy_to_parent_stats.nil_calls++;
        case INT:
            g_copy_to_parent_stats.int_calls++;
        case DOUBLE:
            g_copy_to_parent_stats.double_calls++;
        case STRING:
            g_copy_to_parent_stats.string_calls++;
        case SYMBOL:
            g_copy_to_parent_stats.symbol_calls++;
        case CONS:
            g_copy_to_parent_stats.cons_calls++;
        case CLOSURE:
            g_copy_to_parent_stats.closure_calls++;
        default:
            g_copy_to_parent_stats.other_calls++;
    }
}

fn void print_memory_promotion_stats() {
    io::printn("=== copy_to_parent Stats ===");
    io::printfn("copy_to_parent calls: %d", (long)g_copy_to_parent_stats.total_calls);
    io::printfn("copy_to_parent fast reuse hits: %d", (long)g_copy_to_parent_stats.fast_reuse_hits);
    io::printfn("copy_to_parent defensive copies: %d", (long)g_copy_to_parent_stats.defensive_copies);
    io::printfn("copy_to_parent by tag: nil=%d int=%d double=%d string=%d symbol=%d cons=%d closure=%d other=%d",
        (long)g_copy_to_parent_stats.nil_calls,
        (long)g_copy_to_parent_stats.int_calls,
        (long)g_copy_to_parent_stats.double_calls,
        (long)g_copy_to_parent_stats.string_calls,
        (long)g_copy_to_parent_stats.symbol_calls,
        (long)g_copy_to_parent_stats.cons_calls,
        (long)g_copy_to_parent_stats.closure_calls,
        (long)g_copy_to_parent_stats.other_calls);
    io::printfn("copy_to_parent by site: promote_to_root=%d copy_env=%d run_jit=%d repl=%d prim_resume=%d",
        (long)g_copy_to_parent_stats.site_calls[COPY_SITE_PROMOTE_TO_ROOT],
        (long)g_copy_to_parent_stats.site_calls[COPY_SITE_COPY_ENV_TO_SCOPE],
        (long)g_copy_to_parent_stats.site_calls[COPY_SITE_RUN_JIT_RESULT],
        (long)g_copy_to_parent_stats.site_calls[COPY_SITE_REPL_LINE_RESULT],
        (long)g_copy_to_parent_stats.site_calls[COPY_SITE_PRIM_RESUME]);
    io::printfn("copy_to_parent by site: cons_barrier_car=%d cons_barrier_cdr=%d env_promote=%d",
        (long)g_copy_to_parent_stats.site_calls[COPY_SITE_CONS_BARRIER_CAR],
        (long)g_copy_to_parent_stats.site_calls[COPY_SITE_CONS_BARRIER_CDR],
        (long)g_copy_to_parent_stats.site_calls[COPY_SITE_ENV_PROMOTE_FOR_TARGET]);
    io::printfn("copy_to_parent by site: jit_single=%d jit_call_step2=%d",
        (long)g_copy_to_parent_stats.site_calls[COPY_SITE_JIT_SINGLE_SCOPE_RETURN],
        (long)g_copy_to_parent_stats.site_calls[COPY_SITE_JIT_CALL_SCOPE_STEP2]);
    io::printfn("copy_to_parent by site: jit_tco_frame=%d jit_set=%d jit_define=%d",
        (long)g_copy_to_parent_stats.site_calls[COPY_SITE_JIT_TCO_FRAME_COPY],
        (long)g_copy_to_parent_stats.site_calls[COPY_SITE_JIT_EVAL_SET],
        (long)g_copy_to_parent_stats.site_calls[COPY_SITE_JIT_EVAL_DEFINE]);
    io::printfn("copy_to_parent by site: jit_env_extend_root=%d",
        (long)g_copy_to_parent_stats.site_calls[COPY_SITE_JIT_ENV_EXTEND_ROOT]);
}

fn void reset_memory_promotion_stats() {
    g_copy_to_parent_stats = {};
}

fn usz copy_to_parent_total_calls() @inline {
    return g_copy_to_parent_stats.total_calls;
}

fn usz copy_to_parent_site_calls(uint site) @inline {
    if (site >= COPY_SITE_COUNT) return 0;
    return g_copy_to_parent_stats.site_calls[(usz)site];
}

// =============================================================================
// POSIX SIGNAL HANDLING
// =============================================================================

const int SIGINT_VAL = 2;
alias SignalHandler = fn void(CInt);
extern fn void* signal(int signum, SignalHandler handler) @extern("signal");

// Global interrupt flag — set by SIGINT handler, checked by eval loop
bool g_interrupted = false;

fn void sigint_handler(CInt sig) {
    g_interrupted = true;
}

// =============================================================================
// LIBDL DECLARATIONS (used by declarative FFI: define [ffi lib] / define [ffi λ])
// =============================================================================

extern fn void* dlopen(char* filename, int flags) @extern("dlopen");
extern fn void* dlsym(void* handle, char* symbol) @extern("dlsym");
extern fn int dlclose(void* handle) @extern("dlclose");
extern fn char* dlerror() @extern("dlerror");
const int RTLD_LAZY = 1;


// =============================================================================
// SECTION 1: EVALUATOR CORE
// =============================================================================

/**
 * EvalError - Error result from evaluation.
 */
struct EvalError {
    bool        has_error;
    char[256]   message;
    usz         line;     // Source line where error occurred (0 if unknown)
    usz         column;   // Source column where error occurred (0 if unknown)
}

/**
 * EvalResult - Result of evaluation (value or error).
 */
struct EvalResult {
    Value*     value;
    EvalError  error;
}

fn EvalResult eval_ok(Value* v) @inline {
    return { .value = v, .error = { .has_error = false } };
}

fn EvalResult eval_error(char[] msg) {
    EvalResult r;
    r.value = null;
    r.error.has_error = true;
    r.error.line = 0;
    r.error.column = 0;
    usz len = msg.len;
    if (len > 255) len = 255;
    for (usz i = 0; i < len; i++) {
        r.error.message[i] = msg[i];
    }
    r.error.message[len] = 0;
    return r;
}

/**
 * Create an error with source location from an expression.
 */
fn EvalResult eval_error_expr(char[] msg, Expr* expr) {
    EvalResult r;
    r.value = null;
    r.error.has_error = true;
    if (expr != null) {
        r.error.line = expr.loc_line;
        r.error.column = expr.loc_column;
    } else {
        r.error.line = 0;
        r.error.column = 0;
    }
    usz len = msg.len;
    if (len > 255) len = 255;
    for (usz i = 0; i < len; i++) {
        r.error.message[i] = msg[i];
    }
    r.error.message[len] = 0;
    return r;
}

// Expression evaluation — all handled by JIT (jit.c3)

// =============================================================================
// SECTION 2.34: MULTIPLE DISPATCH
// =============================================================================

/**
 * Find the best matching method in a method table for the given args.
 * Scoring: Val match (1000) > exact type (100) > subtype (10) > any (1)
 */
fn Value* find_best_method(MethodTable* mt, Value*[] args, Interp* interp) {
    long best_score = -1;
    Value* best_method = null;
    usz best_count = 0;

    // Pre-compute arg TypeIds once (avoid repeated infer_value_type calls)
    TypeId[8] arg_types;
    usz arg_count = args.len;
    if (arg_count > 8) { arg_count = 8; }
    for (usz a = 0; a < arg_count; a++) {
        arg_types[a] = infer_value_type(args[a], interp);
    }

    for (usz i = 0; i < mt.entry_count; i++) {
        MethodEntry* entry = &mt.entries[i];

        // Check arity
        if (entry.sig.param_count != args.len) continue;

        long score = 0;
        bool matches = true;

        for (usz j = 0; j < arg_count && j < entry.sig.param_count; j++) {
            TypeId expected = entry.sig.param_types[j];

            // Check Val literal match FIRST (before any-type escape)
            if (entry.sig.has_val_literal[j]) {
                if (args[j] != null && args[j].tag == INT && args[j].int_val == entry.sig.val_literals[j]) {
                    score += 1000;  // Val match is strongest
                    continue;
                }
                matches = false;
                break;
            }

            if (expected == INVALID_TYPE_ID) {
                // Any type — weak match
                score += 1;
                continue;
            }

            TypeId actual = arg_types[j];
            if (actual == INVALID_TYPE_ID) {
                matches = false;
                break;
            }

            if (actual == expected) {
                score += 100;  // Exact match
            } else if (interp.types.is_subtype(actual, expected)) {
                score += 10;   // Subtype match
            } else {
                matches = false;
                break;
            }
        }

        // Enforce type constraints (e.g., ^{'T Number} requires arg's type <: Number)
        if (matches && entry.sig.constraint_count > 0) {
            for (usz c = 0; c < entry.sig.constraint_count; c++) {
                TypeId bound = entry.sig.constraints[c].bound_type;
                if (bound == INVALID_TYPE_ID) continue;  // unresolved bound — skip

                // The constraint applies to the param with dict annotation.
                // Find which arg position has the constrained type variable.
                // For now: check ALL args against the bound — if any arg's type
                // is not a subtype of the bound, the constraint fails.
                // More precise: match param_sym to the annotation on a specific position.
                // We use a simple heuristic: scan for the arg position that has
                // INVALID_TYPE_ID param_type (the dict-annotated one)
                bool constraint_satisfied = false;
                for (usz p = 0; p < arg_count && p < entry.sig.param_count; p++) {
                    if (entry.sig.param_types[p] == INVALID_TYPE_ID && !entry.sig.has_val_literal[p]) {
                        TypeId actual = arg_types[p];
                        if (actual != INVALID_TYPE_ID &&
                            (interp.types.is_subtype(actual, bound))) {
                            constraint_satisfied = true;
                            break;
                        }
                    }
                }
                if (!constraint_satisfied) {
                    matches = false;
                    break;
                }
            }
        }

        if (matches && score > best_score) {
            best_score = score;
            best_method = entry.implementation;
            best_count = 1;
        } else if (matches && score == best_score) {
            best_count++;
        }
    }

    if (best_count > 1) {
        return raise_error(interp, "ambiguous method call: multiple methods match with equal specificity");
    }

    return best_method;
}

// =============================================================================
// SECTION 2.35: TYPE SYSTEM EVALUATORS
// =============================================================================

/**
 * Register built-in types in the type registry.
 * Called once during init, after symbol interning.
 */
fn void register_builtin_types(Interp* interp) {
    // Register built-in types with TK_BUILTIN kind
    SymbolId[12] builtin_names = {
        interp.sym_Int, interp.sym_Double, interp.sym_String,
        interp.sym_Symbol, interp.sym_List, interp.sym_Bool,
        interp.sym_Nil, interp.sym_Closure, interp.sym_Array,
        interp.sym_Dict, interp.sym_Any, interp.sym_Iterator
    };
    for (usz i = 0; i < 12; i++) {
        TypeInfo info;
        info.name = builtin_names[i];
        info.kind = TK_BUILTIN;
        info.parent = INVALID_TYPE_ID;
        info.field_count = 0;
        info.type_param_count = 0;
        info.constraint_count = 0;
        info.variant_count = 0;
        info.alias_target = INVALID_TYPE_ID;
        interp.types.register_type(info, &interp.symbols);
    }

    // Register abstract types: Number, Collection
    TypeInfo abs_info;
    abs_info.kind = TK_ABSTRACT;
    abs_info.parent = INVALID_TYPE_ID;
    abs_info.field_count = 0;
    abs_info.type_param_count = 0;
    abs_info.constraint_count = 0;
    abs_info.variant_count = 0;
    abs_info.alias_target = INVALID_TYPE_ID;

    abs_info.name = interp.sym_Number;
    interp.types.register_type(abs_info, &interp.symbols);

    abs_info.name = interp.sym_Collection;
    interp.types.register_type(abs_info, &interp.symbols);

    // Cache TypeIds for O(1) dispatch lookups
    interp.tid_Int = interp.types.lookup(interp.sym_Int, &interp.symbols);
    interp.tid_Double = interp.types.lookup(interp.sym_Double, &interp.symbols);
    interp.tid_String = interp.types.lookup(interp.sym_String, &interp.symbols);
    interp.tid_Symbol = interp.types.lookup(interp.sym_Symbol, &interp.symbols);
    interp.tid_List = interp.types.lookup(interp.sym_List, &interp.symbols);
    interp.tid_Bool = interp.types.lookup(interp.sym_Bool, &interp.symbols);
    interp.tid_Nil = interp.types.lookup(interp.sym_Nil, &interp.symbols);
    interp.tid_Closure = interp.types.lookup(interp.sym_Closure, &interp.symbols);
    interp.tid_Array = interp.types.lookup(interp.sym_Array, &interp.symbols);
    interp.tid_Dict = interp.types.lookup(interp.sym_Dict, &interp.symbols);
    interp.tid_Any = interp.types.lookup(interp.sym_Any, &interp.symbols);
    interp.tid_Number = interp.types.lookup(interp.sym_Number, &interp.symbols);
    interp.tid_Collection = interp.types.lookup(interp.sym_Collection, &interp.symbols);
    interp.tid_Iterator = interp.types.lookup(interp.sym_Iterator, &interp.symbols);

    // Set parent chains: Int/Double → Number, List/Array/Dict → Collection, Nil → List
    interp.types.types[(usz)interp.tid_Int].parent = interp.tid_Number;
    interp.types.types[(usz)interp.tid_Double].parent = interp.tid_Number;
    interp.types.types[(usz)interp.tid_List].parent = interp.tid_Collection;
    interp.types.types[(usz)interp.tid_Array].parent = interp.tid_Collection;
    interp.types.types[(usz)interp.tid_Dict].parent = interp.tid_Collection;
    interp.types.types[(usz)interp.tid_Nil].parent = interp.tid_List;  // nil IS the empty list
}

/**
 * Infer the TypeId for a runtime value.
 */
fn TypeId infer_value_type(Value* v, Interp* interp) {
    if (v == null || v.tag == NIL) return interp.tid_Nil;
    switch (v.tag) {
        case INT:       return interp.tid_Int;
        case DOUBLE:    return interp.tid_Double;
        case STRING:    return interp.tid_String;
        case SYMBOL:    return interp.tid_Symbol;
        case CONS:      return interp.tid_List;
        case CLOSURE:   return interp.tid_Closure;
        case ARRAY:     return interp.tid_Array;
        case HASHMAP:   return interp.tid_Dict;
        case ITERATOR:  return interp.tid_Iterator;
        case INSTANCE:
            if (v.instance_val != null) return v.instance_val.type_id;
            return INVALID_TYPE_ID;
        default:
            return INVALID_TYPE_ID;
    }
}

/**
 * Get the type name symbol for a runtime value (for type-of primitive).
 */
fn SymbolId value_type_name(Value* v, Interp* interp) {
    if (v == null || v.tag == NIL) return interp.sym_Nil;
    switch (v.tag) {
        case INT:       return interp.sym_Int;
        case DOUBLE:    return interp.sym_Double;
        case STRING:    return interp.sym_String;
        case SYMBOL:
            // true/false are symbols but semantically Bool
            if (v.sym_val == interp.sym_true || v.sym_val == interp.sym_false) { return interp.sym_Bool; }
            return interp.sym_Symbol;
        case CONS:      return interp.sym_List;
        case CLOSURE:   return interp.sym_Closure;
        case ARRAY:     return interp.sym_Array;
        case HASHMAP:   return interp.sym_Dict;
        case ITERATOR:  return interp.sym_Iterator;
        case COROUTINE:     return interp.symbols.intern("Coroutine");
        case INSTANCE:
            if (v.instance_val != null) {
                TypeInfo* ti = interp.types.get(v.instance_val.type_id);
                if (ti != null) return ti.name;
            }
            return interp.sym_Any;
        default:
            return interp.sym_Any;
    }
}

/**
 * Format a detailed dispatch error showing method name, arg types, and arity hints.
 */
fn Value* format_dispatch_error(MethodTable* mt, Value*[] args, Interp* interp) {
    char[] name = interp.symbols.get_name(mt.name);

    // Build "Int, String" arg type list
    DString ds;
    ds.init(mem);
    defer ds.free();

    for (usz i = 0; i < args.len; i++) {
        if (i > 0) ds.append_string(", ");
        ds.append_string((String)interp.symbols.get_name(value_type_name(args[i], interp)));
    }
    ZString arg_types = ds.zstr_view();

    // Check if arity mismatch is the issue
    bool has_arity_match = false;
    for (usz i = 0; i < mt.entry_count; i++) {
        if (mt.entries[i].sig.param_count == args.len) { has_arity_match = true; break; }
    }

    char[512] buf;
    char[] msg;
    if (mt.entry_count > 0 && !has_arity_match) {
        // Collect unique arities
        DString arities;
        arities.init(mem);
        defer arities.free();
        usz[8] seen_arities;
        usz seen_count = 0;
        for (usz i = 0; i < mt.entry_count && seen_count < 8; i++) {
            usz pc = mt.entries[i].sig.param_count;
            bool dup = false;
            for (usz s = 0; s < seen_count; s++) { if (seen_arities[s] == pc) { dup = true; break; } }
            if (!dup) {
                seen_arities[seen_count++] = pc;
                if (arities.len() > 0) arities.append_string(", ");
                char[4] nbuf;
                arities.append_string((String)int_to_string((long)pc, &nbuf));
            }
        }
        msg = io::bprintf(&buf, "no matching method '%s': got %d arg(s) (%s), expected %s arg(s)",
            (ZString)name, (int)args.len, arg_types, arities.zstr_view())!!;
    } else {
        msg = io::bprintf(&buf, "no matching method '%s' for (%s)",
            (ZString)name, arg_types)!!;
    }
    return raise_error(interp, msg);
}

/**
 * Format a detailed match error showing type info and missing union variants.
 */
fn Value* format_match_error(Value* scrutinee, Expr* expr, Interp* interp) {
    char[512] buf;
    char[] type_name = interp.symbols.get_name(value_type_name(scrutinee, interp));

    // Check if scrutinee is an Instance of a union type
    if (scrutinee != null && scrutinee.tag == INSTANCE && scrutinee.instance_val != null) {
        TypeId tid = scrutinee.instance_val.type_id;
        TypeInfo* ti = interp.types.get(tid);
        if (ti != null && ti.parent != INVALID_TYPE_ID) {
            TypeInfo* parent = interp.types.get(ti.parent);
            if (parent != null && parent.kind == TK_UNION) {
                DString missing;
                missing.init(mem);
                defer missing.free();
                usz missing_count = 0;
                for (usz v = 0; v < parent.variant_count; v++) {
                    char[] vname = interp.symbols.get_name(parent.variants[v].name);
                    bool covered = false;
                    for (usz c = 0; c < expr.match.clause_count; c++) {
                        Pattern* pat = expr.match.clauses[c].pattern;
                        if (pat.tag == PAT_CONSTRUCTOR &&
                            (uint)pat.constructor_name == (uint)parent.variants[v].name) {
                            covered = true; break;
                        }
                        if (pat.tag == PAT_WILDCARD) { covered = true; break; }
                        if (pat.tag == PAT_VAR) {
                            TypeId vt = interp.types.lookup(pat.var_name, &interp.symbols);
                            if (vt != INVALID_TYPE_ID && (uint)pat.var_name == (uint)parent.variants[v].name) {
                                covered = true; break;
                            }
                        }
                    }
                    if (!covered) {
                        if (missing_count > 0) missing.append_string(", ");
                        missing.append_string((String)vname);
                        missing_count++;
                    }
                }
                if (missing_count > 0) {
                    char[] parent_name = interp.symbols.get_name(parent.name);
                    char[] msg = io::bprintf(&buf, "no pattern matched: %s value '%s', missing variants: %s",
                        (ZString)parent_name, (ZString)type_name, missing.zstr_view())!!;
                    return raise_error(interp, msg);
                }
            }
        }
    }

    // Fallback for non-union types
    char[] msg = io::bprintf(&buf, "no pattern matched for value of type %s", (ZString)type_name)!!;
    return raise_error(interp, msg);
}

/**
 * Create an Instance value (heap-allocated).
 */
fn Value* make_instance(Interp* interp, TypeId type_id, Value** fields, usz field_count) {
    Instance* inst = (Instance*)mem::malloc(Instance.sizeof);
    inst.type_id = type_id;
    inst.field_count = field_count;
    inst.type_arg_count = 0;
    for (usz i = 0; i < field_count && i < MAX_TYPE_FIELDS; i++) {
        // Instance wrapper lives in root_scope, so field values must also be rooted.
        inst.fields[i] = promote_to_root(fields[i], interp);
    }
    // Allocate Value in root_scope so it persists across scope releases
    main::ScopeRegion* saved_scope = interp.current_scope;
    interp.current_scope = interp.root_scope;
    Value* v = interp.alloc_value();
    main::scope_register_dtor(interp.root_scope, (void*)v, &scope_dtor_value);
    interp.current_scope = saved_scope;
    v.tag = INSTANCE;
    v.instance_val = inst;
    return v;
}

fn EvalResult eval_deftype(Expr* expr, Env* env, Interp* interp) {
    ExprDefType* dt = expr.deftype;

    // Build TypeInfo
    TypeInfo info;
    info.name = dt.name;
    info.kind = TK_CONCRETE;
    info.field_count = dt.field_count;
    info.type_param_count = dt.type_param_count;
    info.variant_count = 0;
    info.alias_target = INVALID_TYPE_ID;

    // Disambiguate parent vs type-params from collected symbols.
    // If the first symbol is a registered type, it's the parent; rest are type params.
    // Otherwise, all symbols are type params and there is no parent.
    info.parent = INVALID_TYPE_ID;
    info.type_param_count = 0;
    if (dt.type_param_count > 0) {
        TypeId maybe_parent = interp.types.lookup(dt.type_params[0], &interp.symbols);
        if (maybe_parent != INVALID_TYPE_ID) {
            // First symbol is a known type — it's the parent
            info.parent = maybe_parent;
            // Remaining symbols are actual type params
            info.type_param_count = dt.type_param_count - 1;
            for (usz i = 0; i < info.type_param_count; i++) {
                info.type_params[i] = dt.type_params[i + 1];
            }
        } else {
            // Not a known type — all are type params, no parent
            info.type_param_count = dt.type_param_count;
            for (usz i = 0; i < info.type_param_count; i++) {
                info.type_params[i] = dt.type_params[i];
            }
        }
    }

    // Copy fields and store original annotation symbol for type-param mapping
    info.constraint_count = 0;
    for (usz i = 0; i < dt.field_count; i++) {
        info.fields[i].name = dt.fields[i].name;
        if (dt.fields[i].type_ann.has_annotation) {
            if (dt.fields[i].type_ann.is_dict) {
                // Dict annotation like ^{'T Number} — extract constraints
                info.fields[i].field_type = INVALID_TYPE_ID;
                info.fields[i].annotation_sym = 0;
                // Extract constraint entries from dict metadata
                for (usz m = 0; m < dt.fields[i].type_ann.meta_count && info.constraint_count < MAX_TYPE_PARAMS; m++) {
                    if (!dt.fields[i].type_ann.meta[m].is_int) {
                        info.constraints[info.constraint_count].param_sym = dt.fields[i].type_ann.meta[m].key;
                        info.constraints[info.constraint_count].bound_type =
                            interp.types.lookup(dt.fields[i].type_ann.meta[m].sym_value, &interp.symbols);
                        // Also store the param_sym as annotation_sym for type-arg inference
                        info.fields[i].annotation_sym = dt.fields[i].type_ann.meta[m].key;
                        info.constraint_count++;
                    }
                }
            } else {
                info.fields[i].field_type = interp.types.lookup(dt.fields[i].type_ann.base_type, &interp.symbols);
                info.fields[i].annotation_sym = dt.fields[i].type_ann.base_type;
            }
        } else {
            info.fields[i].field_type = INVALID_TYPE_ID;  // Any
            info.fields[i].annotation_sym = 0;
        }
    }

    // Register in type registry
    TypeId type_id = interp.types.register_type(info, &interp.symbols);

    // Create constructor primitive in root_scope (lives permanently in global env)
    char[] name_str = interp.symbols.get_name(dt.name);
    Value* constructor = make_primitive(interp, name_str, &prim_type_constructor, (int)dt.field_count);
    constructor.prim_val.tag = (int)type_id;

    // Define constructor in global env
    env_define_with_barrier(interp.global_env, dt.name, constructor, interp);

    // Also make type info available as a value
    Value* type_val = interp.alloc_value();
    type_val.tag = TYPE_INFO;
    type_val.type_info_val = &interp.types.types[(usz)type_id];

    return eval_ok(constructor);
}

/**
 * Generic type constructor primitive.
 * Uses interp.constructor_type_id (set before each call) to know which type to construct.
 */
fn Value* prim_type_constructor(Value*[] args, Env* env, Interp* interp) {
    TypeId type_id = (TypeId)interp.constructor_type_id;
    TypeInfo* ti = interp.types.get(type_id);
    if (ti == null) return make_error(interp, "internal error: type not found in constructor");

    if (args.len != ti.field_count) {
        char[256] ebuf;
        char[] type_n = interp.symbols.get_name(ti.name);
        char[] msg = io::bprintf(&ebuf, "%s constructor expects %d argument(s), got %d",
            (ZString)type_n, (int)ti.field_count, (int)args.len)!!;
        return raise_error(interp, msg);
    }

    // Validate field types (Julia-style boundary enforcement)
    for (usz i = 0; i < args.len && i < ti.field_count; i++) {
        TypeId expected = ti.fields[i].field_type;
        if (expected == INVALID_TYPE_ID) continue;  // untyped field — allow any
        // Check if annotation_sym is a type param (e.g., T in Box)
        if (ti.fields[i].annotation_sym != 0) {
            bool is_param = false;
            for (usz p = 0; p < ti.type_param_count; p++) {
                if (ti.type_params[p] == ti.fields[i].annotation_sym) {
                    is_param = true; break;
                }
            }
            if (is_param) continue;  // parametric field — validated by constraint check
        }
        TypeId actual = infer_value_type(args[i], interp);
        if (actual == INVALID_TYPE_ID) continue;  // can't infer — allow
        if (actual != expected && !interp.types.is_subtype(actual, expected)) {
            char[128] ebuf;
            char[] expected_name = interp.symbols.get_name(ti.fields[i].annotation_sym);
            char[] field_name = interp.symbols.get_name(ti.fields[i].name);
            char[] actual_name = interp.symbols.get_name(value_type_name(args[i], interp));
            char[] emsg = io::bprintf(&ebuf, "type error: field '%s' expects %s, got %s",
                (ZString)field_name, (ZString)expected_name, (ZString)actual_name)!!;
            return raise_error(interp, emsg);
        }
    }

    // Copy args into a fixed array for make_instance
    Value*[MAX_TYPE_FIELDS] fields;
    for (usz i = 0; i < args.len && i < MAX_TYPE_FIELDS; i++) {
        fields[i] = args[i];
    }
    Value* result = make_instance(interp, type_id, &fields, args.len);

    // Infer type arguments from field values for parametric types
    if (ti.type_param_count > 0 && result.instance_val != null) {
        Instance* inst = result.instance_val;
        inst.type_arg_count = ti.type_param_count;
        for (usz p = 0; p < ti.type_param_count && p < MAX_TYPE_PARAMS; p++) {
            SymbolId param_sym = ti.type_params[p];
            inst.type_args[p] = INVALID_TYPE_ID;
            // Find the first field whose annotation_sym matches this type param
            for (usz f = 0; f < ti.field_count; f++) {
                if (ti.fields[f].annotation_sym == param_sym && f < args.len) {
                    inst.type_args[p] = infer_value_type(args[f], interp);
                    break;
                }
            }
        }

        // Validate parametric constraints (e.g., T <: Number)
        for (usz c = 0; c < ti.constraint_count; c++) {
            SymbolId param = ti.constraints[c].param_sym;
            TypeId bound = ti.constraints[c].bound_type;
            if (bound == INVALID_TYPE_ID) continue;
            // Find the inferred type_arg for this param
            for (usz p = 0; p < ti.type_param_count; p++) {
                if (ti.type_params[p] == param) {
                    TypeId inferred = inst.type_args[p];
                    if (inferred != INVALID_TYPE_ID) {
                        if (inferred != bound && !interp.types.is_subtype(inferred, bound)) {
                            char[128] ebuf;
                            char[] param_name = interp.symbols.get_name(param);
                            char[] bound_name = interp.symbols.get_name(interp.types.types[(usz)bound].name);
                            char[] actual_name = interp.symbols.get_name(interp.types.types[(usz)inferred].name);
                            char[] emsg = io::bprintf(&ebuf, "type constraint violated: %s must be subtype of %s, got %s",
                                (ZString)param_name, (ZString)bound_name, (ZString)actual_name)!!;
                            return raise_error(interp, emsg);
                        }
                    }
                    break;
                }
            }
        }
    }

    return result;
}

fn EvalResult eval_defabstract(Expr* expr, Env* env, Interp* interp) {
    ExprDefAbstract* da = &expr.defabstract;

    TypeInfo info;
    info.name = da.name;
    info.kind = TK_ABSTRACT;
    info.field_count = 0;
    info.type_param_count = 0;
    info.constraint_count = 0;
    info.variant_count = 0;
    info.alias_target = INVALID_TYPE_ID;

    if (da.has_parent) {
        info.parent = interp.types.lookup(da.parent, &interp.symbols);
        if (info.parent == INVALID_TYPE_ID) {
            char[256] ebuf;
            char[] msg = io::bprintf(&ebuf, "unknown parent type '%s'",
                (ZString)interp.symbols.get_name(da.parent))!!;
            return eval_error(msg);
        }
    } else {
        info.parent = INVALID_TYPE_ID;
    }

    TypeId type_id = interp.types.register_type(info, &interp.symbols);

    // Return type name as symbol
    Value* result = make_symbol(interp, da.name);
    return eval_ok(result);
}

fn EvalResult eval_defunion(Expr* expr, Env* env, Interp* interp) {
    ExprDefUnion* du = expr.defunion;

    // Register the union type itself
    TypeInfo union_info;
    union_info.name = du.name;
    union_info.kind = TK_UNION;
    union_info.parent = INVALID_TYPE_ID;
    union_info.field_count = 0;
    union_info.type_param_count = du.type_param_count;
    union_info.constraint_count = 0;
    union_info.variant_count = du.variant_count;
    union_info.alias_target = INVALID_TYPE_ID;
    for (usz i = 0; i < du.type_param_count; i++) {
        union_info.type_params[i] = du.type_params[i];
    }
    for (usz i = 0; i < du.variant_count; i++) {
        union_info.variants[i] = du.variants[i];
    }

    TypeId union_id = interp.types.register_type(union_info, &interp.symbols);

    // Register each variant as its own type (with union as parent)
    for (usz i = 0; i < du.variant_count; i++) {
        UnionVariant* variant = &du.variants[i];

        TypeInfo var_info;
        var_info.name = variant.name;
        var_info.kind = TK_CONCRETE;
        var_info.parent = union_id;  // variant is subtype of union
        var_info.field_count = variant.field_count;
        var_info.type_param_count = 0;
        var_info.constraint_count = 0;
        var_info.variant_count = 0;
        var_info.alias_target = INVALID_TYPE_ID;
        for (usz j = 0; j < variant.field_count; j++) {
            var_info.fields[j].name = variant.fields[j];
            var_info.fields[j].field_type = INVALID_TYPE_ID;
        }

        TypeId var_id = interp.types.register_type(var_info, &interp.symbols);

        // Create constructor for this variant
        if (variant.field_count == 0) {
            // Nullary variant: define as a constant instance (make_instance uses root_scope)
            Value* inst = make_instance(interp, var_id, null, 0);
            env_define_with_barrier(interp.global_env, variant.name, inst, interp);
        } else {
            // Variant with fields: define as constructor primitive (make_primitive uses root_scope)
            char[] vname = interp.symbols.get_name(variant.name);
            Value* ctor = make_primitive(interp, vname, &prim_type_constructor, (int)variant.field_count);
            ctor.prim_val.tag = (int)var_id;
            env_define_with_barrier(interp.global_env, variant.name, ctor, interp);
        }
    }

    Value* result = make_symbol(interp, du.name);
    return eval_ok(result);
}

fn EvalResult eval_defalias(Expr* expr, Env* env, Interp* interp) {
    ExprDefAlias* da = &expr.defalias;

    TypeInfo info;
    info.name = da.name;
    info.kind = TK_ALIAS;
    info.parent = INVALID_TYPE_ID;
    info.field_count = 0;
    info.type_param_count = 0;
    info.constraint_count = 0;
    info.variant_count = 0;

    // Resolve target
    if (da.target.has_annotation) {
        info.alias_target = interp.types.lookup(da.target.base_type, &interp.symbols);
    } else {
        info.alias_target = INVALID_TYPE_ID;
    }

    TypeId type_id = interp.types.register_type(info, &interp.symbols);

    Value* result = make_symbol(interp, da.name);
    return eval_ok(result);
}

fn EvalResult eval_defeffect(Expr* expr, Env* env, Interp* interp) {
    ExprDefEffect* de = &expr.defeffect;

    TypeInfo info;
    info.name = de.name;
    info.kind = TK_EFFECT;
    info.parent = INVALID_TYPE_ID;
    info.type_param_count = 0;
    info.constraint_count = 0;
    info.variant_count = 0;
    info.alias_target = INVALID_TYPE_ID;

    if (de.has_arg_type) {
        info.field_count = 1;
        info.fields[0].name = interp.symbols.intern("arg");
        if (de.arg_type.has_annotation) {
            TypeId resolved = interp.types.lookup(de.arg_type.base_type, &interp.symbols);
            info.fields[0].field_type = resolved;
            info.fields[0].annotation_sym = de.arg_type.base_type;
        } else {
            info.fields[0].field_type = INVALID_TYPE_ID;
            info.fields[0].annotation_sym = (SymbolId)0;
        }
    } else {
        info.field_count = 0;
    }

    TypeId type_id = interp.types.register_type(info, &interp.symbols);

    return eval_ok(make_nil(interp));
}

// =============================================================================
// SECTION 2.3.1: FFI EVALUATION (define [ffi lib], define [ffi λ])
// =============================================================================

// C helper from csrc/ffi_helpers.c
extern fn int omni_ffi_call(void* fn_ptr, int nargs, int* arg_types, void** arg_values,
                            int ret_type, void* ret_value) @extern("omni_ffi_call");

// eval_ffi_lib — (define [ffi lib] name "path.so")
fn Value* eval_ffi_lib(Expr* expr, Env* env, Interp* interp) {
    ExprFfiLib* fl = &expr.ffi_lib;

    // Evaluate path expression
    Value* path_val = jit_eval(fl.path_expr, env, interp);
    if (path_val == null || path_val.tag == ERROR) return path_val;
    if (path_val.tag != STRING) return raise_error(interp, "ffi lib: path must be a string");

    // dlopen
    void* handle = dlopen((ZString)path_val.str_chars, RTLD_LAZY);
    if (handle == null) {
        char[256] ebuf;
        char[] msg = io::bprintf(&ebuf, "ffi lib: dlopen failed for '%s'",
            (ZString)path_val.str_chars)!!;
        return raise_error(interp, msg);
    }

    // Create FFI handle value and bind in global env
    Value* ffi_handle = make_ffi_handle(interp, handle, path_val.str_chars[:path_val.str_len]);
    env_define_with_barrier(interp.global_env, fl.name, ffi_handle, interp);
    return ffi_handle;
}

// prim_ffi_bound_call — primitive handler for bound FFI functions.
// user_data points to a heap-allocated FfiBoundFn.
fn Value* prim_ffi_bound_call(Value*[] args, Env* env, Interp* interp) {
    FfiBoundFn* bound = (FfiBoundFn*)interp.prim_user_data;
    if (bound == null) return raise_error(interp, "ffi: internal error — no bound function");

    // Lazy dlsym: resolve on first call
    if (bound.fn_ptr == null) {
        bound.fn_ptr = dlsym(bound.lib_handle, (ZString)&bound.c_name);
        if (bound.fn_ptr == null) {
            char[256] ebuf;
            char[] msg = io::bprintf(&ebuf, "ffi: dlsym failed for '%s'",
                (ZString)&bound.c_name)!!;
            return raise_error(interp, msg);
        }
    }

    // Validate arg count
    if (args.len < bound.param_count) {
        char[256] ebuf;
        char[] msg = io::bprintf(&ebuf, "ffi: expected %d args, got %d",
            (int)bound.param_count, (int)args.len)!!;
        return raise_error(interp, msg);
    }

    // Prepare C argument storage
    long[16] int_store;
    double[16] dbl_store;
    void*[16] ptr_store;
    int[16] arg_types;
    void*[16] arg_values;

    for (usz i = 0; i < bound.param_count; i++) {
        Value* arg = args[i];
        switch (bound.param_types[i]) {
            case FFI_TYPE_INT:
            case FFI_TYPE_BOOL:
                if (arg.tag == INT) {
                    int_store[i] = arg.int_val;
                } else if (arg.tag == DOUBLE) {
                    int_store[i] = (long)arg.double_val;
                } else if (!is_falsy(arg, interp)) {
                    int_store[i] = 1;
                } else {
                    int_store[i] = 0;
                }
                arg_types[i] = (int)bound.param_types[i];
                arg_values[i] = (void*)&int_store[i];
            case FFI_TYPE_DOUBLE:
                if (arg.tag == DOUBLE) {
                    dbl_store[i] = arg.double_val;
                } else if (arg.tag == INT) {
                    dbl_store[i] = (double)arg.int_val;
                } else {
                    dbl_store[i] = 0.0;
                }
                arg_types[i] = (int)bound.param_types[i];
                arg_values[i] = (void*)&dbl_store[i];
            case FFI_TYPE_PTR:
                if (arg.tag == STRING) {
                    ptr_store[i] = (void*)arg.str_chars;
                } else if (arg.tag == INT) {
                    ptr_store[i] = (void*)(uptr)arg.int_val;
                } else if (arg.tag == NIL) {
                    ptr_store[i] = null;
                } else if (arg.tag == FFI_HANDLE) {
                    ptr_store[i] = arg.ffi_val.lib_handle;
                } else {
                    ptr_store[i] = (void*)(uptr)arg.int_val;
                }
                arg_types[i] = (int)bound.param_types[i];
                arg_values[i] = (void*)&ptr_store[i];
            default:
                arg_types[i] = (int)FfiTypeTag.FFI_TYPE_PTR;
                ptr_store[i] = null;
                arg_values[i] = (void*)&ptr_store[i];
        }
    }

    // Call via libffi
    int ret_type = (int)bound.return_type;
    long ret_int = 0;
    double ret_dbl = 0.0;
    void* ret_ptr = null;
    void* ret_storage;

    switch (bound.return_type) {
        case FFI_TYPE_INT:
        case FFI_TYPE_BOOL:
            ret_storage = (void*)&ret_int;
        case FFI_TYPE_DOUBLE:
            ret_storage = (void*)&ret_dbl;
        case FFI_TYPE_PTR:
            ret_storage = (void*)&ret_ptr;
        case FFI_TYPE_VOID:
            ret_storage = (void*)&ret_int;  // unused but must be valid
        default:
            ret_storage = (void*)&ret_int;
    }

    int rc = omni_ffi_call(bound.fn_ptr, (int)bound.param_count,
                           &arg_types, &arg_values, ret_type, ret_storage);
    if (rc != 0) return raise_error(interp, "ffi: libffi call failed");

    // Convert result
    switch (bound.return_type) {
        case FFI_TYPE_INT:
            return make_int(interp, ret_int);
        case FFI_TYPE_BOOL:
            if (ret_int != 0) return make_symbol(interp, interp.sym_true);
            return make_symbol(interp, interp.sym_false);
        case FFI_TYPE_DOUBLE:
            return make_double(interp, ret_dbl);
        case FFI_TYPE_PTR:
            if (ret_ptr == null) return make_nil(interp);
            return make_int(interp, (long)(uptr)ret_ptr);
        case FFI_TYPE_VOID:
            return make_nil(interp);
        default:
            return make_nil(interp);
    }
}

// eval_ffi_fn — (define [ffi λ libname] (fname (^T arg)...) ^RetT)
fn Value* eval_ffi_fn(Expr* expr, Env* env, Interp* interp) {
    ExprFfiFn* ff = expr.ffi_fn;

    // Look up library handle
    Value* lib_val = env.lookup(ff.lib_name);
    if (lib_val == null) lib_val = interp.global_env.lookup(ff.lib_name);
    if (lib_val == null || lib_val.tag != FFI_HANDLE) {
        char[256] ebuf;
        char[] msg = io::bprintf(&ebuf, "ffi λ: library '%s' not found",
            (ZString)interp.symbols.get_name(ff.lib_name))!!;
        return raise_error(interp, msg);
    }
    if (lib_val.ffi_val.lib_handle == null) {
        return raise_error(interp, "ffi λ: library handle is closed");
    }

    // Build FfiBoundFn — dlsym is deferred to first call (lazy resolution)
    FfiBoundFn* bound = (FfiBoundFn*)mem::malloc(FfiBoundFn.sizeof);
    bound.fn_ptr = null;  // resolved lazily on first call
    bound.lib_handle = lib_val.ffi_val.lib_handle;
    // Copy C symbol name for lazy dlsym
    for (usz i = 0; i < ff.c_name.len && i < 127; i++) {
        bound.c_name[i] = ff.c_name[i];
    }
    bound.c_name[ff.c_name.len < 127 ? ff.c_name.len : 127] = 0;
    bound.param_count = ff.param_count;
    bound.is_variadic = ff.is_variadic;

    for (usz i = 0; i < ff.param_count; i++) {
        bound.param_types[i] = type_ann_to_ffi_tag(ff.param_types[i].base_type, interp);
    }

    if (ff.has_return_type) {
        bound.return_type = type_ann_to_ffi_tag(ff.return_type.base_type, interp);
        bound.has_return = true;
    } else {
        bound.return_type = FFI_TYPE_VOID;
        bound.has_return = false;
    }

    // Create a primitive value with user_data pointing to bound
    Value* prim = make_primitive(interp,
        interp.symbols.get_name(ff.fn_name),
        &prim_ffi_bound_call,
        (int)ff.param_count);
    prim.prim_val.user_data = (void*)bound;

    // Bind in global env
    env_define_with_barrier(interp.global_env, ff.fn_name, prim, interp);
    return prim;
}

// =============================================================================
// SECTION 2.4: FRAME PUSH/POP (ESCAPE-COPY)
// =============================================================================

// Frame push/pop is now active: run() wraps each evaluation in a child region.
// Temporary Values (ints, cons cells, intermediate strings) die with the child
// scope. Long-lived values are promoted to root_scope by existing infrastructure:
// - jit_make_closure_from_expr: allocates closures + env in per-closure env_scope
// - jit_eval_define: copies defined values to root_scope via copy_to_parent
// - jit_eval_let_rec: creates weak self-reference in closure's env_scope
// - alloc_expr/alloc_pattern: always use root_region (AST nodes survive)

// Old region-system destructors removed (Phase 7).
// Scope-region destructors are scope_dtor_value and scope_dtor_env in value.c3.

/**
 * Copy a Value and everything it transitively references to current_scope.
 * Used to promote return values out of a child scope before releasing it.
 * Caller must set current_scope to the target scope before calling.
 */
<*
 @require interp != null : "copy_to_parent: null interp"
 @require interp.current_scope != null : "copy_to_parent: null current_scope"
*>
fn bool in_target_scope_chain(void* ptr, Interp* interp) {
    main::ScopeRegion* s = interp.current_scope;
    while (s != null) {
        if (main::is_in_scope(ptr, s)) return true;
        s = s.parent;
    }
    return false;
}

fn Value* copy_to_parent(Value* v, Interp* interp) {
    if (v == null) return null;
    g_copy_to_parent_stats.total_calls++;
    copy_to_parent_note_tag(v.tag);

    // Scope-aware optimization: if value is NOT in the scope being released,
    // reuse only when it is already in the target scope chain.
    // Note: uses is_in_scope (chunk walk) rather than scope_gen stamp because
    // chunk-transfer paths (scope_splice_escapes) move allocations without
    // rewriting per-value scope_gen stamps.
    if (interp.releasing_scope != null && !main::is_in_scope((void*)v, interp.releasing_scope)) {
        if (in_target_scope_chain((void*)v, interp)) {
            g_copy_to_parent_stats.fast_reuse_hits++;
            return v;
        }
        // Not in releasing_scope, but also not in a known surviving scope.
        // Copy defensively to avoid dangling pointers from transient env scopes.
        g_copy_to_parent_stats.defensive_copies++;
    }

    Value* result;
    switch (v.tag) {
        case NIL:
            result = make_nil(interp);
        case INT:
            result = make_int(interp, v.int_val);
        case DOUBLE:
            result = make_double(interp, v.double_val);
        case STRING:
            result = make_string(interp, v.str_chars[:v.str_len]);
        case SYMBOL:
            result = make_symbol(interp, v.sym_val);
        case CONS: {
            // Iterative copy to prevent C stack overflow on long lists
            Value* head = interp.alloc_value();
            head.tag = CONS;
            Value* current_new = head;
            Value* current_old = v;
            
            while (true) {
                current_new.cons_val.car = copy_to_parent(current_old.cons_val.car, interp);
                
                Value* old_cdr = current_old.cons_val.cdr;
                if (old_cdr != null && old_cdr.tag == CONS) {
                    if (interp.releasing_scope != null &&
                        !main::is_in_scope((void*)old_cdr, interp.releasing_scope) &&
                        in_target_scope_chain((void*)old_cdr, interp)) {
                        // Safe fast-path: tail already lives in a surviving scope chain.
                        current_new.cons_val.cdr = old_cdr;
                        break;
                    }
                    // Deep-copy tail iteratively to avoid recursive overflow on long lists.
                    Value* next_new = interp.alloc_value();
                    next_new.tag = CONS;
                    current_new.cons_val.cdr = next_new;
                    current_new = next_new;
                    current_old = old_cdr;
                } else {
                    current_new.cons_val.cdr = copy_to_parent(old_cdr, interp);
                    break;
                }
            }
            result = head;
            break;
        }
        case CLOSURE: {
            bool closure_in_releasing_scope =
                interp.releasing_scope != null &&
                (v.closure_val.scope_gen == interp.releasing_scope.generation ||
                 main::scope_contains(interp.releasing_scope, (void*)v.closure_val));
            if (closure_in_releasing_scope || !in_target_scope_chain((void*)v, interp)) {
                // Deep copy: new Value and new Closure in target scope
                result = interp.alloc_value();
                result.tag = CLOSURE;
                result.closure_val = (Closure*)interp.current_scope.alloc(Closure.sizeof);
                *result.closure_val = *v.closure_val;
                result.closure_val.scope_gen = interp.current_scope.generation;
                
                // Copy params array to new scope
                if (v.closure_val.param_count > 0) {
                    result.closure_val.params = (SymbolId*)interp.current_scope.alloc(SymbolId.sizeof * v.closure_val.param_count);
                    for (usz i = 0; i < v.closure_val.param_count; i++) {
                        result.closure_val.params[i] = v.closure_val.params[i];
                    }
                } else {
                    result.closure_val.params = null;
                }
                
                // Copy type_sig if present
                if (v.closure_val.type_sig != null) {
                    result.closure_val.type_sig = (MethodSignature*)interp.current_scope.alloc(MethodSignature.sizeof);
                    *result.closure_val.type_sig = *v.closure_val.type_sig;
                }

                // Refcount env_scope
                if (result.closure_val.env_scope != null) {
                    result.closure_val.env_scope.refcount++;
                }
                main::scope_register_dtor(interp.current_scope, (void*)result, &scope_dtor_closure);
            } else {
                result = v; // Return as-is
            }
        }
        case PRIMITIVE:
            result = v;
        case PARTIAL_PRIM: {
            // Deep copy: Value wrapper may be in child scope, and first_arg/second_arg may too
            result = interp.alloc_value();
            result.tag = PARTIAL_PRIM;
            result.partial_val.func = v.partial_val.func;
            result.partial_val.remaining = v.partial_val.remaining;
            result.partial_val.first_arg = copy_to_parent(v.partial_val.first_arg, interp);
            result.partial_val.second_arg = copy_to_parent(v.partial_val.second_arg, interp);
        }
        case CONTINUATION:
            // Shallow copy: Value wrapper may be in child scope, cont_val itself is in root_region
            result = interp.alloc_value();
            result.tag = CONTINUATION;
            result.cont_val = v.cont_val;
        case HASHMAP:
        case ARRAY:
        case INSTANCE:
        case METHOD_TABLE:
            result = v;  // Value allocated in root_scope; backing data is malloc'd with registered destructors
        case FFI_HANDLE:
            result = v;  // FfiHandle is inline in Value; Value allocated in root_scope
        case TYPE_INFO:
            result = v;  // type info lives in registry
        case ITERATOR: {
            // Phase 4b: iterator_val is a Value* to a CLOSURE that may be in a child scope.
            // Promote the underlying closure too so it outlives the ITERATOR.
            Value* promoted_thunk = copy_to_parent(v.iterator_val, interp);
            result = interp.alloc_value();
            result.tag = ITERATOR;
            result.iterator_val = promoted_thunk;
            break;
        }
        case COROUTINE:
            // Shallow copy: Value wrapper may be in child scope, coroutine_val (Coro*) managed by stack pool
            result = interp.alloc_value();
            result.tag = COROUTINE;
            result.coroutine_val = v.coroutine_val;
        case MODULE:
            result = v;  // Module lives in interp.modules array
        case ERROR:
            result = make_error(interp, v.str_chars[:v.str_len]);
        default:
            result = v;
    }
    return result;
}

fn Value* copy_to_parent_site(Value* v, Interp* interp, uint site) {
    if (site < COPY_SITE_COUNT) g_copy_to_parent_stats.site_calls[(usz)site]++;
    return copy_to_parent(v, interp);
}

fn void promotion_context_begin(Interp* interp, PromotionContext* ctx, usz budget = usz.max) {
    if (interp == null || ctx == null) return;
    interp.promotion_epoch_counter++;
    if (interp.promotion_epoch_counter == 0) interp.promotion_epoch_counter = 1;
    ctx.epoch = interp.promotion_epoch_counter;
    ctx.budget = budget;
    ctx.aborted = false;
    ctx.memo_head = null;
    ctx.prev = interp.active_promotion_ctx;
    interp.active_promotion_ctx = ctx;
}

fn void promotion_context_end(Interp* interp, PromotionContext* ctx) {
    if (interp == null || ctx == null) return;
    if (interp.active_promotion_ctx == ctx) {
        interp.active_promotion_ctx = ctx.prev;
    }
}

fn bool promotion_context_consume(PromotionContext* ctx, usz units = 1) @inline {
    if (ctx == null) return true;
    if (ctx.aborted) return false;
    if (ctx.budget == usz.max) return true;
    if (ctx.budget < units) {
        ctx.aborted = true;
        return false;
    }
    ctx.budget -= units;
    return true;
}

fn Value* promotion_context_lookup(PromotionContext* ctx, Value* src) @inline {
    if (ctx == null || src == null) return null;
    PromotionMemoEntry* e = ctx.memo_head;
    while (e != null) {
        if (e.src == src) return e.dst;
        e = e.next;
    }
    return null;
}

fn void promotion_context_remember(PromotionContext* ctx, Value* src, Value* dst, Interp* interp) {
    if (ctx == null || src == null || dst == null || interp == null || interp.current_scope == null) return;
    if (ctx.aborted) return;
    PromotionMemoEntry* e = (PromotionMemoEntry*)interp.current_scope.alloc(PromotionMemoEntry.sizeof);
    e.src = src;
    e.dst = dst;
    e.next = ctx.memo_head;
    ctx.memo_head = e;
}

fn Value* copy_to_parent_site_ctx(Value* v, Interp* interp, uint site, PromotionContext* ctx) {
    if (v == null) return null;
    if (ctx != null) {
        Value* cached = promotion_context_lookup(ctx, v);
        if (cached != null) return cached;
    }
    bool can_track = promotion_context_consume(ctx, 1);
    Value* copied = copy_to_parent_site(v, interp, site);
    if (can_track && ctx != null && copied != null) {
        promotion_context_remember(ctx, v, copied, interp);
    }
    return copied;
}

// Boundary-aware copy helper:
// only copy when value belongs to source_scope; otherwise reuse as-is.
fn Value* copy_value_if_owned_by_scope(Value* v, Interp* interp, main::ScopeRegion* source_scope, uint site) {
    if (v == null) return null;
    if (source_scope != null && !main::is_in_scope((void*)v, source_scope)) return v;

    switch (v.tag) {
        case NIL:
            return make_nil(interp);
        case INT:
            return make_int(interp, v.int_val);
        case DOUBLE:
            return make_double(interp, v.double_val);
        case SYMBOL:
            return make_symbol(interp, v.sym_val);
        case STRING:
            return make_string(interp, v.str_chars[:v.str_len]);
        case ERROR:
            return make_error(interp, v.str_chars[:v.str_len]);
        case PRIMITIVE:
        case HASHMAP:
        case ARRAY:
        case INSTANCE:
        case METHOD_TABLE:
        case FFI_HANDLE:
        case TYPE_INFO:
        case MODULE:
            return v;
        default:
            return copy_to_parent_site_ctx(v, interp, site, interp.active_promotion_ctx);
    }
}

/**
 * Promote a value from current scope TEMP lane to current scope ESCAPE lane.
 * This is an intra-scope promotion primitive for dual-lane rollout.
 * Values not owned by current TEMP lane are returned unchanged.
 */
fn Value* promote_to_escape(Value* v, Interp* interp) {
    if (v == null || interp == null || interp.current_scope == null) return v;
    PromotionContext* ctx = interp.active_promotion_ctx;
    if (ctx != null && ctx.aborted) return v;

    // Fast paths:
    // - already in current ESCAPE lane
    // - in current TEMP lane (normal case for lane promotion)
    // - in a surviving scope chain (root/parent/shared lifetime)
    if (v.scope_gen == interp.current_scope.escape_generation) return v;
    if (v.scope_gen != interp.current_scope.generation) {
        if (in_target_scope_chain((void*)v, interp)) return v;
        // Defensive fallback: disjoint lifetime object (e.g. standalone env_scope).
        // Copy into current TEMP lane first, then continue promotion into ESCAPE.
        PromotionContext local_ctx = {};
        PromotionContext* local_ptr = interp.active_promotion_ctx;
        bool owns_ctx = false;
        if (local_ptr == null) {
            promotion_context_begin(interp, &local_ctx);
            local_ptr = &local_ctx;
            owns_ctx = true;
        }
        Value* copied = copy_to_parent_site_ctx(v, interp, COPY_SITE_GENERIC, local_ptr);
        if (owns_ctx) promotion_context_end(interp, local_ptr);
        if (copied == null || copied == v) return copied;
        return promote_to_escape(copied, interp);
    }

    Value* result;
    switch (v.tag) {
        case NIL:
            if (!promotion_context_consume(ctx, 1)) return v;
            result = interp.alloc_value_escape();
            result.tag = NIL;
        case INT:
            if (!promotion_context_consume(ctx, 1)) return v;
            result = interp.alloc_value_escape();
            result.tag = INT;
            result.int_val = v.int_val;
        case DOUBLE:
            if (!promotion_context_consume(ctx, 1)) return v;
            result = interp.alloc_value_escape();
            result.tag = DOUBLE;
            result.double_val = v.double_val;
        case SYMBOL:
            if (!promotion_context_consume(ctx, 1)) return v;
            result = interp.alloc_value_escape();
            result.tag = SYMBOL;
            result.sym_val = v.sym_val;
        case STRING:
        case ERROR: {
            if (!promotion_context_consume(ctx, 1)) return v;
            result = interp.alloc_value_escape();
            result.tag = v.tag;
            main::scope_register_dtor_escape(interp.current_scope, (void*)result, &scope_dtor_value);
            usz len = v.str_len;
            usz cap = len + 1;
            if (cap < 32) cap = 32;
            result.str_chars = (char*)mem::malloc(cap);
            for (usz i = 0; i < len; i++) {
                result.str_chars[i] = v.str_chars[i];
            }
            result.str_chars[len] = 0;
            result.str_len = len;
            break;
        }
        case CONS: {
            if (!promotion_context_consume(ctx, 1)) return v;
            // Iterative spine copy to avoid recursion blowups on long lists.
            Value* head = interp.alloc_value_escape();
            head.tag = CONS;
            Value* current_new = head;
            Value* current_old = v;

            while (true) {
                current_new.cons_val.car = promote_to_escape(current_old.cons_val.car, interp);
                if (ctx != null && ctx.aborted) return v;
                Value* old_cdr = current_old.cons_val.cdr;
                if (old_cdr != null && old_cdr.tag == CONS &&
                    old_cdr.scope_gen == interp.current_scope.generation) {
                    if (!promotion_context_consume(ctx, 1)) return v;
                    Value* next_new = interp.alloc_value_escape();
                    next_new.tag = CONS;
                    current_new.cons_val.cdr = next_new;
                    current_new = next_new;
                    current_old = old_cdr;
                } else {
                    current_new.cons_val.cdr = promote_to_escape(old_cdr, interp);
                    if (ctx != null && ctx.aborted) return v;
                    break;
                }
            }
            result = head;
            break;
        }
        case CLOSURE: {
            if (!promotion_context_consume(ctx, 1)) return v;
            result = interp.alloc_value_escape();
            result.tag = CLOSURE;
            main::scope_register_dtor_escape(interp.current_scope, (void*)result, &scope_dtor_closure);
            result.closure_val = (Closure*)interp.current_scope.alloc_escape(Closure.sizeof);
            *result.closure_val = *v.closure_val;
            result.closure_val.scope_gen = interp.current_scope.escape_generation;

            if (v.closure_val.param_count > 0) {
                result.closure_val.params = (SymbolId*)interp.current_scope.alloc_escape(SymbolId.sizeof * v.closure_val.param_count);
                for (usz i = 0; i < v.closure_val.param_count; i++) {
                    result.closure_val.params[i] = v.closure_val.params[i];
                }
            } else {
                result.closure_val.params = null;
            }

            if (v.closure_val.type_sig != null) {
                result.closure_val.type_sig = (MethodSignature*)interp.current_scope.alloc_escape(MethodSignature.sizeof);
                *result.closure_val.type_sig = *v.closure_val.type_sig;
            }

            if (result.closure_val.env_scope != null) {
                main::scope_retain(result.closure_val.env_scope);
            }
            break;
        }
        case PARTIAL_PRIM: {
            if (!promotion_context_consume(ctx, 1)) return v;
            result = interp.alloc_value_escape();
            result.tag = PARTIAL_PRIM;
            result.partial_val.func = v.partial_val.func;
            result.partial_val.remaining = v.partial_val.remaining;
            result.partial_val.first_arg = promote_to_escape(v.partial_val.first_arg, interp);
            result.partial_val.second_arg = promote_to_escape(v.partial_val.second_arg, interp);
            break;
        }
        case CONTINUATION:
            if (!promotion_context_consume(ctx, 1)) return v;
            result = interp.alloc_value_escape();
            result.tag = CONTINUATION;
            result.cont_val = v.cont_val;
        case ITERATOR:
            if (!promotion_context_consume(ctx, 1)) return v;
            result = interp.alloc_value_escape();
            result.tag = ITERATOR;
            result.iterator_val = promote_to_escape(v.iterator_val, interp);
        case COROUTINE:
            if (!promotion_context_consume(ctx, 1)) return v;
            result = interp.alloc_value_escape();
            result.tag = COROUTINE;
            result.coroutine_val = v.coroutine_val;
        case PRIMITIVE:
        case HASHMAP:
        case ARRAY:
        case INSTANCE:
        case METHOD_TABLE:
        case FFI_HANDLE:
        case TYPE_INFO:
        case MODULE:
            // Wrapper promotion into ESCAPE lane; backing data/ownership remains unchanged.
            if (!promotion_context_consume(ctx, 1)) return v;
            result = interp.alloc_value_escape();
            *result = *v;
            result.scope_gen = interp.current_scope.escape_generation;
        default:
            result = v;
    }
    if (ctx != null && result != null && result != v) {
        promotion_context_remember(ctx, v, result, interp);
    }
    return result;
}

// Boundary helper: promote a child-scope value into root while preserving
// releasing_scope context so copy_to_parent keeps its defensive-copy behavior
// for disjoint transient lifetimes.
fn Value* promote_to_root_site(Value* v, Interp* interp, uint site) {
    if (v == null || interp == null || interp.current_scope == interp.root_scope) return v;
    main::ScopeRegion* saved_scope = interp.current_scope;
    main::ScopeRegion* saved_releasing = interp.releasing_scope;
    interp.current_scope = interp.root_scope;
    interp.releasing_scope = saved_scope;
    Value* result = copy_to_parent_site(v, interp, site);
    interp.releasing_scope = saved_releasing;
    interp.current_scope = saved_scope;
    return result;
}

/**
 * Promote a value to root_scope so it survives child scope release.
 * No-op when already in root_scope (fast path for stdlib loading, etc.).
 * Used when storing values into root-scope data structures (hashmap, array).
 */
fn Value* promote_to_root(Value* v, Interp* interp) {
    return promote_to_root_site(v, interp, COPY_SITE_PROMOTE_TO_ROOT);
}

fn Value* copy_env_value_fast(Value* v, Interp* interp) {
    if (v == null) return null;
    switch (v.tag) {
        case NIL:
            return make_nil(interp);
        case INT:
            return make_int(interp, v.int_val);
        case DOUBLE:
            return make_double(interp, v.double_val);
        case SYMBOL:
            return make_symbol(interp, v.sym_val);
        case STRING:
            return make_string(interp, v.str_chars[:v.str_len]);
        case ERROR:
            return make_error(interp, v.str_chars[:v.str_len]);
        case PRIMITIVE:
        case HASHMAP:
        case ARRAY:
        case INSTANCE:
        case METHOD_TABLE:
        case FFI_HANDLE:
        case TYPE_INFO:
        case MODULE:
            // Root-lifetime or external-lifetime objects can be shared.
            return v;
        default:
            return copy_to_parent_site(v, interp, COPY_SITE_COPY_ENV_TO_SCOPE);
    }
}

/**
 * Copy an env chain into interp.current_scope (caller must set this to the target scope,
 * typically a closure's env_scope). Uses copy_to_parent for each binding value.
 * Stops at global_env (already in root_region). Skips persistent env nodes (mutable boxes).
 * Replaces deep_copy_env — allocates into any scope, not just root_scope.
 */
fn Env* copy_env_to_scope(Env* env, Interp* interp, usz depth = 0) {
    if (env == null || env == interp.global_env) return env;
    if (depth >= 256) return null;

    // Persistent env nodes (mutable boxes allocated in root_region) are kept
    // as-is to preserve sharing between JIT stack slots and closure captures.
    // Their parent chain still needs fixing up since it may point to child-scope envs.
    if (env.persistent) {
        env.parent = copy_env_to_scope(env.parent, interp, depth + 1);
        return env;
    }

    Env* new_env = interp.alloc_env();
    new_env.binding_count = env.binding_count;
    
    if (env.binding_count <= 4) {
        new_env.is_inline = true;
        new_env.bindings = &new_env.inline_bindings[0];
        new_env.capacity = 4;
    } else {
        new_env.is_inline = false;
        new_env.bindings = (Binding*)mem::malloc(Binding.sizeof * env.binding_count);
        new_env.capacity = env.binding_count;
    }

    for (usz i = 0; i < env.binding_count; i++) {
        new_env.bindings[i].name = env.bindings[i].name;
        Value* val = env.bindings[i].value;
        // Deep copy closures and iterators to ensure they survive if they were in a dying scope
        if (val != null && val.tag == CLOSURE && val.closure_val.env_scope != null) {
            if (val.closure_val.scope_gen != interp.current_scope.generation) {
                Value* result = interp.alloc_value();
                result.tag = CLOSURE;
                result.closure_val = (Closure*)interp.current_scope.alloc(Closure.sizeof);
                *result.closure_val = *val.closure_val;
                result.closure_val.scope_gen = interp.current_scope.generation;
                if (val.closure_val.param_count > 0) {
                    result.closure_val.params = (SymbolId*)interp.current_scope.alloc(SymbolId.sizeof * val.closure_val.param_count);
                    for (usz p = 0; p < val.closure_val.param_count; p++) {
                        result.closure_val.params[p] = val.closure_val.params[p];
                    }
                } else {
                    result.closure_val.params = null;
                }
                if (val.closure_val.type_sig != null) {
                    result.closure_val.type_sig = (MethodSignature*)interp.current_scope.alloc(MethodSignature.sizeof);
                    *result.closure_val.type_sig = *val.closure_val.type_sig;
                }
                if (result.closure_val.env_scope != null) {
                    result.closure_val.env_scope.refcount++;
                }
                main::scope_register_dtor(interp.current_scope, (void*)result, &scope_dtor_closure);
                val = result;
            }
        } else if (val != null && val.tag == ITERATOR) {
            // ITERATOR contains a closure, deep copy it!
            Value* inner = val.iterator_val;
            if (inner != null && inner.tag == CLOSURE && inner.closure_val.env_scope != null) {
                if (inner.closure_val.scope_gen != interp.current_scope.generation) {
                    Value* inner_result = interp.alloc_value();
                    inner_result.tag = CLOSURE;
                    inner_result.closure_val = (Closure*)interp.current_scope.alloc(Closure.sizeof);
                    *inner_result.closure_val = *inner.closure_val;
                    inner_result.closure_val.scope_gen = interp.current_scope.generation;
                    if (inner.closure_val.param_count > 0) {
                        inner_result.closure_val.params = (SymbolId*)interp.current_scope.alloc(SymbolId.sizeof * inner.closure_val.param_count);
                        for (usz p = 0; p < inner.closure_val.param_count; p++) {
                            inner_result.closure_val.params[p] = inner.closure_val.params[p];
                        }
                    } else {
                        inner_result.closure_val.params = null;
                    }
                    if (inner.closure_val.type_sig != null) {
                        inner_result.closure_val.type_sig = (MethodSignature*)interp.current_scope.alloc(MethodSignature.sizeof);
                        *inner_result.closure_val.type_sig = *inner.closure_val.type_sig;
                    }
                    if (inner_result.closure_val.env_scope != null) {
                        inner_result.closure_val.env_scope.refcount++;
                    }
                    main::scope_register_dtor(interp.current_scope, (void*)inner_result, &scope_dtor_closure);
                    inner = inner_result;
                }
            }
            Value* iter_result = interp.alloc_value();
            iter_result.tag = ITERATOR;
            iter_result.iterator_val = inner;
            val = iter_result;
        } else {
            val = copy_env_value_fast(val, interp);
        }
        new_env.bindings[i].value = val;
    }
    new_env.parent = copy_env_to_scope(env.parent, interp, depth + 1);
    // Rebuild hash table if source had one
    if (env.hash_table != null) {
        new_env.build_hash_table();
    }
    // Register destructor for the newly malloc'd bindings
    main::scope_register_dtor(interp.current_scope, (void*)new_env, &scope_dtor_env);

    return new_env;
}

// Continuations, effects, index — all handled by JIT (jit.c3)

/**
 * Look up a key in an association list.
 * An alist is a list of pairs: ((key1 . val1) (key2 . val2) ...)
 * Returns the value associated with the key, or null if not found.
 */
fn Value* alist_lookup(Value* alist, SymbolId key, Interp* interp) {
    // Iterate through the alist
    while (is_cons(alist)) {
        Value* pair = car(alist);

        // Each element should be a cons cell (key . value)
        if (is_cons(pair)) {
            Value* pair_key = car(pair);

            // Check if the key matches (compare as symbols)
            if (is_symbol(pair_key) && pair_key.sym_val == key) {
                return cdr(pair);
            }
        }

        alist = cdr(alist);
    }

    return null;  // Key not found
}

/**
 * Evaluate a path expression: point.x, person.address.city
 * Field access notation.
 *
 * For multi-segment paths like person.address.city:
 * 1. Look up the first segment as a variable
 * 2. For each subsequent segment, look it up as a key in an association list
 */
fn EvalResult eval_path(Expr* expr, Env* env, Interp* interp) {
    if (expr.path.segment_count == 0) {
        return eval_error("empty field access path (e.g., point.x)");
    }

    // Look up the first segment as a variable
    Value* current = env.lookup(expr.path.segments[0]);
    if (current == null) {
        current = interp.global_env.lookup(expr.path.segments[0]);
    }
    if (current == null) {
        return eval_error("unbound path root variable");
    }

    // For single-segment paths, just return the value
    if (expr.path.segment_count == 1) {
        return eval_ok(current);
    }

    // For each remaining segment, look up field in Instance, Module, or alist
    for (usz i = 1; i < expr.path.segment_count; i++) {
        SymbolId key = expr.path.segments[i];

        // Module member access: math.add
        if (current != null && current.tag == MODULE && current.module_val != null) {
            Module* mod = current.module_val;
            // Check if member is exported
            bool is_exported = false;
            for (usz ei = 0; ei < mod.export_count; ei++) {
                if ((uint)mod.exports[ei] == (uint)key) {
                    is_exported = true;
                    break;
                }
            }
            if (!is_exported) {
                char[256] ebuf;
                char[] msg = io::bprintf(&ebuf, "'%s' is not exported from module",
                    (ZString)interp.symbols.get_name(key))!!;
                return eval_error(msg);
            }
            // Look up in module's env
            current = mod.env.lookup(key);
            if (current == null) {
                char[256] ebuf;
                char[] msg = io::bprintf(&ebuf, "symbol '%s' not found in module",
                    (ZString)interp.symbols.get_name(key))!!;
                return eval_error(msg);
            }
            continue;
        }

        if (current != null && current.tag == INSTANCE && current.instance_val != null) {
            // Instance field access
            TypeInfo* ti = interp.types.get(current.instance_val.type_id);
            if (ti != null) {
                bool found_field = false;
                for (usz fi = 0; fi < ti.field_count; fi++) {
                    if (ti.fields[fi].name == key) {
                        if (fi < current.instance_val.field_count) {
                            current = current.instance_val.fields[fi];
                            found_field = true;
                            break;
                        }
                    }
                }
                if (found_field) continue;
            }
            char[256] ebuf;
            char[] type_n = interp.symbols.get_name(ti.name);
            char[] field_n = interp.symbols.get_name(key);
            char[] msg = io::bprintf(&ebuf, "field '%s' not found in type '%s'",
                (ZString)field_n, (ZString)type_n)!!;
            return eval_error(msg);
        }

        // Cons cell .car / .cdr access
        if (current != null && current.tag == CONS) {
            if ((uint)key == (uint)interp.sym_car) {
                current = current.cons_val.car;
                continue;
            }
            if ((uint)key == (uint)interp.sym_cdr) {
                current = current.cons_val.cdr;
                continue;
            }
            // Fall through to alist lookup for cons-based association lists
        }

        // Alist fallback
        Value* found = alist_lookup(current, key, interp);
        if (found == null) {
            char[256] ebuf;
            char[] seg_n = interp.symbols.get_name(key);
            char[] msg = io::bprintf(&ebuf, "cannot access '.%s' on %s value",
                (ZString)seg_n, (ZString)interp.symbols.get_name(value_type_name(current, interp)))!!;
            return eval_error(msg);
        }
        current = found;
    }

    return eval_ok(current);
}

// =============================================================================
// SECTION 4.5: PATTERN MATCHING
// =============================================================================

/**
 * MatchResult - Result of attempting to match a pattern against a value.
 * If successful, contains bindings produced by the match.
 * Bindings are heap-allocated and grow dynamically.
 */
const usz MATCH_INITIAL_CAPACITY = 32;

struct MatchResult {
    bool matched;
    Binding* bindings;
    usz binding_count;
    usz capacity;
}

/**
 * Auto-gensym support for pattern macros.
 * Symbols ending with # are auto-replaced with unique gensyms.
 * The mapping ensures the same #-symbol maps to the same gensym within one expansion.
 */
struct GensymMapping {
    SymbolId original;   // The foo# symbol
    SymbolId generated;  // The g#N gensym
}

struct GensymTable {
    GensymMapping[64] mappings;
    usz count;
}

fn MatchResult match_fail() @inline {
    return { .matched = false, .bindings = null, .binding_count = 0, .capacity = 0 };
}

fn MatchResult match_ok() @inline {
    Binding* b = (Binding*)mem::malloc(Binding.sizeof * MATCH_INITIAL_CAPACITY);
    return { .matched = true, .bindings = b, .binding_count = 0, .capacity = MATCH_INITIAL_CAPACITY };
}

fn void MatchResult.add_binding(MatchResult* self, SymbolId name, Value* value) {
    if (self.binding_count >= self.capacity) {
        usz new_cap = self.capacity < MATCH_INITIAL_CAPACITY ? MATCH_INITIAL_CAPACITY : self.capacity * 2;
        Binding* new_bindings = (Binding*)mem::malloc(Binding.sizeof * new_cap);
        for (usz i = 0; i < self.binding_count; i++) {
            new_bindings[i] = self.bindings[i];
        }
        if (self.bindings != null) mem::free(self.bindings);
        self.bindings = new_bindings;
        self.capacity = new_cap;
    }
    self.bindings[self.binding_count].name = name;
    self.bindings[self.binding_count].value = value;
    self.binding_count++;
}

fn void MatchResult.merge(MatchResult* self, MatchResult* other) {
    for (usz i = 0; i < other.binding_count; i++) {
        self.add_binding(other.bindings[i].name, other.bindings[i].value);
    }
}

fn void MatchResult.cleanup(MatchResult* self) {
    if (self.bindings != null) {
        mem::free(self.bindings);
        self.bindings = null;
    }
}

/**
 * Check if two values are equal (for literal pattern matching).
 */
fn bool values_equal(Value* a, Value* b, usz depth = 0) {
    if (a == null && b == null) return true;
    if (a == null || b == null) return false;
    if (depth >= 256) return false;
    // Allow cross-type numeric comparison: (= 1 1.0) => true
    if (is_number(a) && is_number(b)) {
        return to_double(a) == to_double(b);
    }
    if (a.tag != b.tag) return false;

    switch (a.tag) {
        case NIL:
            return true;
        case INT:
            return a.int_val == b.int_val;
        case DOUBLE:
            return a.double_val == b.double_val;
        case STRING:
            if (a.str_len != b.str_len) return false;
            for (usz i = 0; i < a.str_len; i++) {
                if (a.str_chars[i] != b.str_chars[i]) return false;
            }
            return true;
        case SYMBOL:
            return a.sym_val == b.sym_val;
        case CONS:
            return values_equal(a.cons_val.car, b.cons_val.car, depth + 1) &&
                   values_equal(a.cons_val.cdr, b.cons_val.cdr, depth + 1);
        case HASHMAP:
            return a == b;  // Pointer equality for mutable containers
        case ARRAY:
            if (a.array_val.length != b.array_val.length) return false;
            for (usz i = 0; i < a.array_val.length; i++) {
                if (!values_equal(a.array_val.items[i], b.array_val.items[i], depth + 1)) return false;
            }
            return true;
        default:
            return a == b;  // Pointer equality for closures, etc.
    }
}

/**
 * Get the length of a list.
 */
fn usz get_list_length(Value* v) {
    usz len = 0;
    while (is_cons(v)) {
        len++;
        v = cdr(v);
    }
    return len;
}

/**
 * Get the nth element of a list.
 */
fn Value* get_list_nth(Value* v, usz n) {
    for (usz i = 0; i < n; i++) {
        if (!is_cons(v)) return null;
        v = cdr(v);
    }
    if (!is_cons(v)) return null;
    return car(v);
}

/**
 * Get the rest of a list starting at index n.
 */
fn Value* get_list_rest(Value* v, usz n, Interp* interp) {
    for (usz i = 0; i < n; i++) {
        if (!is_cons(v)) return make_nil(interp);
        v = cdr(v);
    }
    return v;
}

/**
 * Match a pattern against a value.
 */
fn MatchResult match_pattern(Pattern* pat, Value* val, Interp* interp) {
    switch (pat.tag) {
        case PAT_WILDCARD:
            // Wildcard matches anything
            return match_ok();

        case PAT_VAR: {
            // Check if this symbol is a nullary constructor (e.g., None)
            TypeId tid = interp.types.lookup(pat.var_name, &interp.symbols);
            if (tid != INVALID_TYPE_ID) {
                TypeInfo* ti = interp.types.get(tid);
                if (ti != null && (ti.kind == TK_CONCRETE || ti.kind == TK_UNION) && ti.field_count == 0) {
                    // Nullary constructor pattern: value must be INSTANCE of this type
                    if (val != null && val.tag == INSTANCE && val.instance_val != null &&
                        val.instance_val.type_id == tid) {
                        return match_ok();
                    }
                    return match_fail();
                }
            }
            // Regular variable: matches anything and binds
            MatchResult r = match_ok();
            r.add_binding(pat.var_name, val);
            return r;
        }

        case PAT_LIT:
            // Literal matches if values are equal
            if (values_equal(pat.lit_value, val)) {
                return match_ok();
            }
            return match_fail();

        case PAT_QUOTE:
            // Quoted pattern matches quoted datum
            if (values_equal(pat.quote_datum, val)) {
                return match_ok();
            }
            return match_fail();

        case PAT_CONS:
            // Cons pattern matches cons cells
            if (!is_cons(val)) {
                return match_fail();
            }
            MatchResult car_result = match_pattern(pat.car_pat, car(val), interp);
            if (!car_result.matched) {
                return match_fail();
            }
            MatchResult cdr_result = match_pattern(pat.cdr_pat, cdr(val), interp);
            if (!cdr_result.matched) {
                car_result.cleanup();
                return match_fail();
            }
            car_result.merge(&cdr_result);
            cdr_result.cleanup();
            return car_result;

        case PAT_SEQ:
            // Sequence pattern matches lists
            return match_seq_pattern(pat, val, interp);

        case PAT_CONSTRUCTOR: {
            // Constructor pattern: (ConstructorName sub-patterns...)
            // Value must be INSTANCE of the named type
            if (val == null || val.tag != INSTANCE || val.instance_val == null) {
                return match_fail();
            }
            TypeId tid = interp.types.lookup(pat.constructor_name, &interp.symbols);
            if (tid == INVALID_TYPE_ID || val.instance_val.type_id != tid) {
                return match_fail();
            }
            // Match sub-patterns against fields
            if (pat.ctor_sub_count != val.instance_val.field_count) {
                return match_fail();
            }
            MatchResult result = match_ok();
            for (usz ci = 0; ci < pat.ctor_sub_count; ci++) {
                MatchResult sub = match_pattern(pat.ctor_sub_patterns[ci], val.instance_val.fields[ci], interp);
                if (!sub.matched) { result.cleanup(); return match_fail(); }
                result.merge(&sub);
                sub.cleanup();
            }
            return result;
        }

        case PAT_GUARD: {
            // Guard pattern: evaluate pred, apply to val
            if (pat.guard_sub != null) {
                MatchResult sub_r = match_pattern(pat.guard_sub, val, interp);
                if (!sub_r.matched) return match_fail();
                // Evaluate guard with sub bindings in scope
                Env* guard_env = make_env(interp, interp.match_env);
                for (usz j = 0; j < sub_r.binding_count; j++) {
                    env_define_with_barrier(guard_env, sub_r.bindings[j].name, sub_r.bindings[j].value, interp);
                }
                Value* pred = jit_eval(pat.guard_pred, guard_env, interp);
                if (pred == null || pred.tag == ERROR) { sub_r.cleanup(); return match_fail(); }
                Value* result = jit_apply_value(pred, val, interp);
                if (result == null || result.tag == ERROR || jit_is_falsy(result, interp)) {
                    sub_r.cleanup(); return match_fail();
                }
                return sub_r;
            }
            // No sub-pattern: just evaluate pred and apply to val
            Value* pred = jit_eval(pat.guard_pred, interp.match_env, interp);
            if (pred == null || pred.tag == ERROR) return match_fail();
            Value* result = jit_apply_value(pred, val, interp);
            if (result == null || result.tag == ERROR || jit_is_falsy(result, interp)) {
                return match_fail();
            }
            return match_ok();
        }

        default:
            return match_fail();
    }
}

/**
 * Match a sequence pattern against a value.
 */
fn MatchResult match_seq_pattern(Pattern* pat, Value* val, Interp* interp) {
    usz elem_count = pat.elem_count;
    RestPosition rest_pos = pat.rest_pos;

    // Collect list elements into flat array in one pass (O(n) instead of O(n²))
    Value*[64] elems;
    usz list_len = 0;
    Value* cursor = val;
    while (is_cons(cursor) && list_len < 64) {
        elems[list_len] = car(cursor);
        list_len++;
        cursor = cdr(cursor);
    }

    switch (rest_pos) {
        case REST_NONE:
            if (list_len != elem_count) {
                return match_fail();
            }
            MatchResult result = match_ok();
            for (usz i = 0; i < elem_count; i++) {
                MatchResult elem_result = match_pattern(pat.elements[i], elems[i], interp);
                if (!elem_result.matched) { result.cleanup(); return match_fail(); }
                result.merge(&elem_result);
                elem_result.cleanup();
            }
            return result;

        case REST_START:
            if (list_len < elem_count) {
                return match_fail();
            }
            usz skip = list_len - elem_count;
            MatchResult start_result = match_ok();
            for (usz i = 0; i < elem_count; i++) {
                MatchResult elem_result = match_pattern(pat.elements[i], elems[skip + i], interp);
                if (!elem_result.matched) { start_result.cleanup(); return match_fail(); }
                start_result.merge(&elem_result);
                elem_result.cleanup();
            }
            return start_result;

        case REST_MIDDLE:
            if (list_len < elem_count) {
                return match_fail();
            }
            MatchResult mid_result = match_ok();
            for (usz i = 0; i < elem_count; i++) {
                MatchResult elem_result = match_pattern(pat.elements[i], elems[i], interp);
                if (!elem_result.matched) { mid_result.cleanup(); return match_fail(); }
                mid_result.merge(&elem_result);
                elem_result.cleanup();
            }
            Value* rest = get_list_rest(val, elem_count, interp);
            mid_result.add_binding(pat.rest_binding, rest);
            return mid_result;

        case REST_END:
            if (list_len < elem_count) {
                return match_fail();
            }
            MatchResult end_result = match_ok();
            for (usz i = 0; i < elem_count; i++) {
                MatchResult elem_result = match_pattern(pat.elements[i], elems[i], interp);
                if (!elem_result.matched) { end_result.cleanup(); return match_fail(); }
                end_result.merge(&elem_result);
                elem_result.cleanup();
            }
            return end_result;

        default:
            return match_fail();
    }
}

// =============================================================================
// SECTION 5: APPLICATION
// =============================================================================

// apply/apply_closure — handled by JIT (jit.c3)

fn EvalResult apply_primitive(Value* prim, Value* arg, Interp* interp) {
    int arity = prim.prim_val.arity;

    // For unary or variadic primitives, apply immediately
    if (arity <= 1 || arity == -1) {
        Value*[8] args;
        args[0] = arg;
        interp.constructor_type_id = prim.prim_val.tag;
        interp.prim_user_data = prim.prim_val.user_data;
        Value* result = prim.prim_val.func(args[:1], null, interp);
        if (is_error(result)) {
            return eval_error(result.str_chars[:result.str_len]);
        }
        return eval_ok(result);
    }

    // For multi-arity primitives, create a partial application
    Value* partial = interp.alloc_value();
    partial.tag = PARTIAL_PRIM;
    partial.partial_val.func = prim.prim_val.func;
    partial.partial_val.first_arg = arg;
    partial.partial_val.second_arg = null;
    partial.partial_val.remaining = arity - 1;

    return eval_ok(partial);
}

fn EvalResult apply_partial(Value* partial, Value* arg, Interp* interp) {
    if (partial.partial_val.remaining > 1) {
        // Still need more args — chain another partial
        Value* next = interp.alloc_value();
        next.tag = PARTIAL_PRIM;
        next.partial_val.func = partial.partial_val.func;
        next.partial_val.first_arg = partial.partial_val.first_arg;
        next.partial_val.second_arg = arg;
        next.partial_val.remaining = partial.partial_val.remaining - 1;
        return eval_ok(next);
    }

    // All args collected — call the function
    Value*[8] args;
    args[0] = partial.partial_val.first_arg;
    if (partial.partial_val.second_arg != null) {
        args[1] = partial.partial_val.second_arg;
        args[2] = arg;
        Value* result = partial.partial_val.func(args[:3], null, interp);
        if (is_error(result)) { return eval_error(result.str_chars[:result.str_len]); }
        return eval_ok(result);
    }
    args[1] = arg;
    Value* result = partial.partial_val.func(args[:2], null, interp);
    if (is_error(result)) {
        return eval_error(result.str_chars[:result.str_len]);
    }
    return eval_ok(result);
}

// =============================================================================
// SECTION 6: HELPER FUNCTIONS
// =============================================================================

/**
 * Check if a value is falsy (nil or false symbol).
 */
fn bool is_falsy(Value* v, Interp* interp) {
    if (v == null) return true;
    if (v.tag == NIL) return true;
    if (v.tag == SYMBOL && v.sym_val == interp.sym_false) return true;
    return false;
}

// =============================================================================
// SECTION 8: INTERPRETER INITIALIZATION
// =============================================================================

// Primitive registration table entry
struct PrimReg {
    char[] name;
    PrimitiveFn func;
    int arity;
}

/**
 * Register all primitive functions in the interpreter.
 * Uses data-driven tables to separate declaration from registration logic.
 */
fn void register_primitives(Interp* interp) {
    // Register true, false, and nil as symbols bound to their values
    Value* true_val = make_symbol(interp, interp.sym_true);
    Value* false_val = make_nil(interp);  // false is nil
    Value* nil_val = make_nil(interp);
    env_define_with_barrier(interp.global_env, interp.sym_true, true_val, interp);
    env_define_with_barrier(interp.global_env, interp.sym_false, false_val, interp);
    SymbolId sym_nil = interp.symbols.intern("nil");
    env_define_with_barrier(interp.global_env, sym_nil, nil_val, interp);

    // --- Dispatched primitives (have MethodTable for user-defined type extension) ---
    const DISPATCHED_PRIM_COUNT = 31;
    PrimReg[DISPATCHED_PRIM_COUNT] dispatched_prims = {
        // Arithmetic
        { "+", &prim_add, 2 }, { "-", &prim_sub, 2 }, { "*", &prim_mul, 2 },
        { "/", &prim_div, 2 }, { "%", &prim_mod, 2 },
        // Comparison
        { "=", &prim_eq, 2 }, { "<", &prim_lt, 2 }, { ">", &prim_gt, 2 },
        { "<=", &prim_le, 2 }, { ">=", &prim_ge, 2 },
        // Collections
        { "length", &prim_length, 1 }, { "ref", &prim_ref, 2 },
        { "push!", &prim_push, 2 }, { "keys", &prim_keys, 1 },
        { "values", &prim_values, 1 }, { "has?", &prim_has, 2 },
        { "remove!", &prim_remove, 2 },
        // Sorting
        { "sort", &prim_sort, 1 }, { "sort-by", &prim_sort_by, 2 },
        // String ops (extensible for user-defined string-like types)
        { "string-append", &prim_string_append, -1 },
        { "string-contains?", &prim_string_contains, 2 },
        { "string-upcase", &prim_string_upcase_unicode, 1 },
        { "string-downcase", &prim_string_downcase_unicode, 1 },
        // Math (extensible for user-defined numeric types)
        { "abs", &prim_abs, 1 }, { "floor", &prim_floor, 1 },
        { "ceiling", &prim_ceiling, 1 }, { "round", &prim_round, 1 },
        { "truncate", &prim_truncate, 1 }, { "sqrt", &prim_sqrt, 1 },
        { "min", &prim_min, 2 }, { "max", &prim_max, 2 },
    };
    $assert(dispatched_prims.len == DISPATCHED_PRIM_COUNT);
    foreach (&d : dispatched_prims) {
        register_dispatched_prim(interp, d.name, d.func, d.arity);
    }

    // --- Regular primitives ---
    const REGULAR_PRIM_COUNT = 137;
    PrimReg[REGULAR_PRIM_COUNT] regular_prims = {
        // List operations
        { "cons", &prim_cons, 2 }, { "car", &prim_car, 1 }, { "cdr", &prim_cdr, 1 },
        { "null?", &prim_null, 1 }, { "pair?", &prim_pair, 1 }, { "list", &prim_list, -1 },
        // Boolean
        { "not", &prim_not, 1 },
        // I/O (raw primitives for effect fast path)
        { "__raw-print", &prim_print, 1 }, { "__raw-println", &prim_println, 1 },
        { "__raw-newline", &prim_newline, 0 }, { "__raw-display", &prim_display, 1 },
        // String primitives
        { "string-join", &prim_string_join, 2 },
        { "substring", &prim_substring, 3 }, { "string-split", &prim_string_split, 2 },
        { "string-length", &prim_string_length, 1 }, { "string-byte-length", &prim_string_byte_length, 1 },
        { "string->list", &prim_string_to_list, 1 },
        { "list->string", &prim_list_to_string, 1 },
        { "string-trim", &prim_string_trim, 1 },
        { "continuation?", &prim_is_continuation, 1 },
        // File I/O
        { "__raw-read-file", &prim_read_file, 1 }, { "__raw-write-file", &prim_write_file, 2 },
        { "__raw-file-exists?", &prim_file_exists, 1 }, { "__raw-read-lines", &prim_read_lines, 1 },
        { "load", &prim_load, 1 },
        // Conversion
        { "string->number", &prim_string_to_number, 1 }, { "number->string", &prim_number_to_string, 1 },
        { "exact->inexact", &prim_exact_to_inexact, 1 }, { "inexact->exact", &prim_inexact_to_exact, 1 },
        // Gensym
        { "gensym", &prim_gensym, 0 },
        // Dict
        { "dict", &prim_dict, -1 }, { "dict-set!", &prim_dict_set, 3 },
        // Math library
        { "sin", &prim_sin, 1 }, { "cos", &prim_cos, 1 }, { "tan", &prim_tan, 1 },
        { "asin", &prim_asin, 1 }, { "acos", &prim_acos, 1 }, { "atan", &prim_atan, 1 },
        { "atan2", &prim_atan2, 2 }, { "exp", &prim_exp, 1 }, { "log", &prim_log, 1 },
        { "log10", &prim_log10, 1 }, { "pow", &prim_pow, 2 },
        { "gcd", &prim_gcd, 2 }, { "lcm", &prim_lcm, 2 },
        // Bitwise
        { "bitwise-and", &prim_bitwise_and, 2 }, { "bitwise-or", &prim_bitwise_or, 2 },
        { "bitwise-xor", &prim_bitwise_xor, 2 }, { "bitwise-not", &prim_bitwise_not, 1 },
        { "lshift", &prim_lshift, 2 }, { "rshift", &prim_rshift, 2 },
        // String operations & predicates
        { "string-index-of", &prim_string_index_of, 2 },
        { "string-replace", &prim_string_replace, 3 }, { "char-at", &prim_char_at, 2 },
        { "string-repeat", &prim_string_repeat, 2 }, { "procedure?", &prim_is_procedure, 1 },
        { "format", &prim_format, -1 }, { "cl-format", &prim_cl_format, -1 },
        // Introspection & metaprogramming
        { "macroexpand", &prim_macroexpand, 1 }, { "eval", &prim_eval, 1 },
        { "apply", &prim_apply, 2 }, { "bound?", &prim_bound, 1 },
        { "error", &prim_error, 1 }, { "error-message", &prim_error_message, 1 },
        // Arrays
        { "array", &prim_array, -1 }, { "array-set!", &prim_array_set, 3 },
        // Sets
        { "set", &prim_set, -1 }, { "set-add", &prim_set_add, 2 },
        { "set-remove", &prim_set_remove, 2 }, { "set-contains?", &prim_set_contains, 2 },
        { "set-size", &prim_set_size, 1 }, { "set->list", &prim_set_to_list, 1 },
        // Additional I/O and convenience
        { "read-string", &prim_read_string, 1 }, { "string->symbol", &prim_string_to_symbol, 1 },
        { "symbol->string", &prim_symbol_to_string, 1 },
        // Type system
        { "type-of", &prim_type_of, 1 }, { "is?", &prim_is_type, 2 },
        { "instance?", &prim_is_instance, 1 }, { "type-args", &prim_type_args, 1 },
        // Memory reclamation
        { "unsafe-free!", &prim_free_bang, 1 },
        // Iterators
        { "iterator?", &prim_iterator_p, 1 }, { "make-iterator", &prim_make_iterator, 1 },
        { "next", &prim_next, 1 }, { "collect", &prim_collect, 1 },
        { "to-array", &prim_to_array, 1 },
        // Coroutines
        { "coroutine", &prim_coroutine, 1 },
        { "resume", &prim_resume, -1 },
        { "yield", &prim_yield, -1 },
        { "coroutine?", &prim_coroutine_p, 1 },
        // System / OS
        { "shell", &prim_shell, 1 },
        { "random", &prim_random, 0 },
        { "random-int", &prim_random_int, 1 },
        { "getenv", &prim_getenv, 1 },
        { "time", &prim_time, 0 },
        { "time-ms", &prim_time_ms, 0 },
        { "exit", &prim_exit, -1 },
        { "sleep", &prim_sleep, 1 },
        // Unicode
        { "string-normalize", &prim_string_normalize, 2 },
        { "string-graphemes", &prim_string_graphemes, 1 },
        { "string-codepoints", &prim_string_codepoints, 1 },
        { "char-category", &prim_char_category, 1 },
        // Compression
        { "gzip", &prim_gzip, 1 },
        { "gunzip", &prim_gunzip, 1 },
        { "deflate", &prim_deflate, 1 },
        { "inflate", &prim_inflate, -1 },
        // JSON
        { "json-parse", &prim_json_parse, 1 },
        { "json-emit", &prim_json_emit, 1 },
        { "json-emit-pretty", &prim_json_emit_pretty, 1 },
        // Networking / async I/O (raw primitives for effect fast path)
        { "__raw-tcp-connect", &prim_tcp_connect, 2 },
        { "__raw-tcp-read", &prim_tcp_read, -1 },
        { "__raw-tcp-write", &prim_tcp_write, 2 },
        { "__raw-tcp-close", &prim_tcp_close, 1 },
        { "__raw-dns-resolve", &prim_dns_resolve, 1 },
        { "__raw-async-sleep", &prim_async_sleep, 1 },
        // TLS
        { "__raw-tls-connect", &prim_tls_connect, 2 },
        { "__raw-tls-read", &prim_tls_read, -1 },
        { "__raw-tls-write", &prim_tls_write, 2 },
        { "__raw-tls-close", &prim_tls_close, 1 },
        // Schema validation
        { "validate", &prim_validate, 2 },
        { "explain", &prim_explain, 2 },
        // Deduce
        { "deduce-open", &prim_deduce_open, 1 },
        { "__define-relation", &prim_define_relation, 1 },
        { "fact!", &prim_assert_fact, -1 },
        { "retract!", &prim_retract_fact, -1 },
        { "deduce-scan", &prim_deduce_scan, 1 },
        { "deduce-query", &prim_deduce_query, 2 },
        { "deduce-count", &prim_deduce_count, 1 },
        { "deduce-match", &prim_deduce_match, 2 },
        // Scheduler
        { "spawn", &prim_spawn, 1 },
        { "await", &prim_await, 1 },
        { "run-fibers", &prim_run_fibers, 0 },
        // HTTP
        { "__raw-http-get", &prim_http_get, 1 },
        { "__raw-http-request", &prim_http_request, -1 },
        // Atomics
        { "atomic", &prim_atomic, 1 },
        { "atomic-add!", &prim_atomic_add, 2 },
        { "atomic-read", &prim_atomic_read, 1 },
        { "atomic-cas!", &prim_atomic_cas, 3 },
    };
    $assert(regular_prims.len == REGULAR_PRIM_COUNT);
    foreach (&r : regular_prims) {
        register_prim(interp, r.name, r.func, r.arity);
    }

    // Register built-in types
    register_builtin_types(interp);

    // Register Pika parser/regex primitives
    pika::register_pika_primitives(interp);

    // Register effect fast-path table: maps effect tag → raw primitive
    // When a signal has no handler, jit_signal_impl looks up this table.
    interp.fast_path_count = 0;
    register_fast_path(interp, "io/print",      "__raw-print");
    register_fast_path(interp, "io/println",     "__raw-println");
    register_fast_path(interp, "io/display",     "__raw-display");
    register_fast_path(interp, "io/newline",     "__raw-newline");
    register_fast_path(interp, "io/read-file",   "__raw-read-file");
    register_fast_path(interp, "io/write-file",  "__raw-write-file");
    register_fast_path(interp, "io/file-exists?","__raw-file-exists?");
    register_fast_path(interp, "io/read-lines",  "__raw-read-lines");
    register_fast_path(interp, "io/tcp-connect", "__raw-tcp-connect");
    register_fast_path(interp, "io/tcp-read",    "__raw-tcp-read");
    register_fast_path(interp, "io/tcp-write",   "__raw-tcp-write");
    register_fast_path(interp, "io/tcp-close",   "__raw-tcp-close");
    register_fast_path(interp, "io/dns-resolve", "__raw-dns-resolve");
    register_fast_path(interp, "io/async-sleep", "__raw-async-sleep");
    register_fast_path(interp, "io/tls-connect", "__raw-tls-connect");
    register_fast_path(interp, "io/tls-read",    "__raw-tls-read");
    register_fast_path(interp, "io/tls-write",   "__raw-tls-write");
    register_fast_path(interp, "io/tls-close",   "__raw-tls-close");
    register_fast_path(interp, "io/http-get",    "__raw-http-get");
    register_fast_path(interp, "io/http-request","__raw-http-request");
}

fn void register_fast_path(Interp* interp, char[] effect_name, char[] raw_prim_name) {
    Value* prim = interp.global_env.lookup(interp.symbols.intern(raw_prim_name));
    if (prim != null && interp.fast_path_count < 32) {
        interp.fast_path_table[interp.fast_path_count].tag = interp.symbols.intern(effect_name);
        interp.fast_path_table[interp.fast_path_count].prim = prim;
        interp.fast_path_count++;
    }
}

fn void register_prim(Interp* interp, char[] name, PrimitiveFn func, int arity) {
    SymbolId sym = interp.symbols.intern(name);
    Value* prim = make_primitive(interp, name, func, arity);
    env_define_with_barrier(interp.global_env, sym, prim, interp);
}

fn void register_dispatched_prim(Interp* interp, char[] name, PrimitiveFn func, int arity) {
    SymbolId sym = interp.symbols.intern(name);
    Value* prim = make_primitive(interp, name, func, arity);

    // Create method table with no typed entries, primitive as fallback
    MethodTable* mt = (MethodTable*)mem::malloc(MethodTable.sizeof);
    mt.name = sym;
    mt.entries = (MethodEntry*)mem::malloc(MethodEntry.sizeof * METHOD_INITIAL_CAPACITY);
    mt.entry_count = 0;
    mt.capacity = METHOD_INITIAL_CAPACITY;
    mt.fallback = prim;

    main::ScopeRegion* saved_scope = interp.current_scope;
    interp.current_scope = interp.root_scope;
    Value* mt_val = interp.alloc_value();
    main::scope_register_dtor(interp.root_scope, (void*)mt_val, &scope_dtor_value);
    interp.current_scope = saved_scope;
    mt_val.tag = METHOD_TABLE;
    mt_val.method_table_val = mt;

    env_define_with_barrier(interp.global_env, sym, mt_val, interp);
}

/**
 * Run a complete program.
 */
fn EvalResult run_program(char[] source, Interp* interp) {
    Lexer lex;
    lex.init(source);
    Parser p;
    p.init(&lex, interp);

    List{Expr*} exprs;
    while (!lex.at_end() && !p.has_error) {
        exprs.push(p.parse_expr());
    }

    if (p.has_error) {
        exprs.free();
        EvalResult r;
        r.value = null;
        r.error.has_error = true;
        r.error.line = p.error_line;
        r.error.column = p.error_col;
        usz len = p.error_msg_len;
        if (len > 255) len = 255;
        for (usz i = 0; i < len; i++) {
            r.error.message[i] = p.error_msg[i];
        }
        r.error.message[len] = 0;
        return r;
    }

    EvalResult result = eval_ok(make_nil(interp));

    foreach (expr : exprs) {
        // GC JIT states between top-level expressions (safe: no JIT code on stack)
        jit_gc();

        // Expand macros before JIT compilation
        expr = expand_macros_in_expr(expr, interp);
        JitFn f = jit_compile(expr, interp);
        if (f != null) {
            Value* jit_result = jit_exec(f, interp);
            if (jit_result != null && jit_result.tag == ERROR) {
                result = eval_error_expr(jit_result.str_chars[:jit_result.str_len], expr);
            } else {
                result = eval_ok(jit_result);
            }
            if (result.error.has_error) break;
            continue;
        }
        result = eval_error_expr("JIT compilation failed", expr);
        break;
    }

    exprs.free();
    return result;
}

/**
 * Run a single expression.
 */
fn EvalResult run(char[] source, Interp* interp) {
    // GC JIT states between top-level evaluations (safe: no JIT code on stack)
    jit_gc();

    Lexer lex;
    lex.init(source);
    Parser p;
    p.init(&lex, interp);

    Expr* expr = p.parse_expr();

    if (p.has_error) {
        EvalResult r;
        r.value = null;
        r.error.has_error = true;
        r.error.line = p.error_line;
        r.error.column = p.error_col;
        usz len = p.error_msg_len;
        if (len > 255) len = 255;
        for (usz i = 0; i < len; i++) {
            r.error.message[i] = p.error_msg[i];
        }
        r.error.message[len] = 0;
        return r;
    }

    // JIT path: compile + execute in child region + child scope
    expr = expand_macros_in_expr(expr, interp);

    // Push child scope — temporaries freed after eval
    main::ScopeRegion* saved_scope = interp.current_scope;
    main::ScopeRegion* child_scope = main::scope_create(saved_scope);
    interp.current_scope = child_scope;

    JitFn f = jit_compile(expr, interp);
    EvalResult result;
    if (f != null) {
        Value* jit_result = jit_exec(f, interp);
        bool child_spliced = false;
        if (jit_result != null) {
            PromotionContext boundary_ctx = {};
            promotion_context_begin(interp, &boundary_ctx, PROMOTION_CTX_BUDGET_RETURN);
            Value* original_result = jit_result;
            interp.current_scope = saved_scope;

            if (child_scope.refcount == 1) {
                // Try lane-promotion + splice first (zero-copy return path).
                if (jit_result.scope_gen != child_scope.escape_generation) {
                    interp.current_scope = child_scope;
                    jit_result = promote_to_escape(jit_result, interp);
                    interp.current_scope = saved_scope;
                }
                if (boundary_ctx.aborted) {
                    main::ScopeRegion* saved_releasing = interp.releasing_scope;
                    interp.releasing_scope = child_scope;
                    jit_result = copy_value_if_owned_by_scope(original_result, interp, child_scope, COPY_SITE_RUN_JIT_RESULT);
                    interp.releasing_scope = saved_releasing;
                }
                if (!boundary_ctx.aborted &&
                    jit_result.scope_gen == child_scope.escape_generation) {
                    main::scope_splice_escapes(saved_scope, child_scope);
                    child_scope = null;
                    child_spliced = true;
                }
            }

            if (!child_spliced) {
                if (main::is_in_scope((void*)jit_result, child_scope)) {
                    main::ScopeRegion* saved_releasing = interp.releasing_scope;
                    interp.releasing_scope = child_scope;
                    jit_result = copy_value_if_owned_by_scope(jit_result, interp, child_scope, COPY_SITE_RUN_JIT_RESULT);
                    interp.releasing_scope = saved_releasing;
                }
            }
            promotion_context_end(interp, &boundary_ctx);
        }
        if (jit_result != null && jit_result.tag == ERROR) {
            result = eval_error_expr(jit_result.str_chars[:jit_result.str_len], expr);
        } else {
            result = eval_ok(jit_result);
        }
    } else {
        result = eval_error_expr("JIT compilation failed", expr);
    }

    // Pop child scope — frees all temporaries from this eval
    interp.current_scope = saved_scope;
    if (child_scope != null) {
        main::scope_release(child_scope);
    }

    return result;
}

/**
 * Register standard library functions defined in Pika itself.
 * Called after register_primitives().
 */
fn void register_stdlib(Interp* interp) {
    // Standard library loaded from embedded stdlib/stdlib.lisp at compile time.
    // Edit that file (with syntax highlighting) instead of inline strings here.
    char[] stdlib_src = $embed("../../stdlib/stdlib.lisp");
    usz pos = 0;
    while (pos < stdlib_src.len) {
        usz start = pos;
        while (pos < stdlib_src.len && stdlib_src[pos] != '\n') pos++;
        usz line_len = pos - start;
        if (pos < stdlib_src.len) pos++; // skip newline
        if (line_len == 0 || stdlib_src[start] == ';') continue;
        run(stdlib_src[start:line_len], interp);
    }
}

// =============================================================================
// SECTION 9: REPL
// =============================================================================

/**
 * Read a line of input from stdin.
 * Returns the number of characters read (not including null terminator).
 */
fn usz read_line(char[] buffer) {
    usz len = 0;
    usz max_len = buffer.len - 1;
    while (len < max_len) {
        if (try c = io::stdin().read_byte()) {
            if (c == '\n') {
                break;
            }
            buffer[len] = c;
            len++;
        } else {
            break;  // EOF or error
        }
    }
    buffer[len] = 0;  // Null terminate
    return len;
}

// =============================================================================
// REPLXX CALLBACKS — Syntax Highlighting + Completion
// =============================================================================

// Check if a symbol name is a keyword (special form)
fn bool is_keyword(char[] name) {
    // Special forms
    char[][] keywords = {
        "lambda", "define", "let", "if", "begin", "set!", "quote",
        "and", "or", "match", "reset", "shift", "signal", "handle",
        "resolve", "module", "import", "export", "with-continuation"
    };
    foreach (kw : keywords) {
        if (kw.len == name.len) {
            bool eq = true;
            for (usz i = 0; i < kw.len; i++) {
                if (kw[i] != name[i]) { eq = false; break; }
            }
            if (eq) return true;
        }
    }
    return false;
}

// Check if a symbol is a builtin constant
fn bool is_builtin_const(char[] name) {
    char[][] builtins = { "true", "false", "nil", "pi", "e" };
    foreach (b : builtins) {
        if (b.len == name.len) {
            bool eq = true;
            for (usz i = 0; i < b.len; i++) {
                if (b[i] != name[i]) { eq = false; break; }
            }
            if (eq) return true;
        }
    }
    return false;
}

// Helper: classify a symbol name and apply color to the range [start, end) in colors array
fn void flush_symbol(char* input, CInt* colors, isz start, CInt end_pos) {
    if (start < 0 || start >= (isz)end_pos) return;
    usz slen = (usz)end_pos - (usz)start;
    char[128] sbuf;
    if (slen > 127) slen = 127;
    for (usz k = 0; k < slen; k++) sbuf[k] = input[(usz)start + k];
    char[] sym = sbuf[:slen];

    CInt col = RXCOLOR_DEFAULT;
    if (is_keyword(sym)) {
        col = RXCOLOR_BRIGHTCYAN | RXCOLOR_BOLD;
    } else if (is_builtin_const(sym)) {
        col = RXCOLOR_BRIGHTMAGENTA;
    }
    for (isz j = start; j < (isz)end_pos; j++) {
        colors[(CInt)j] = col;
    }
}

// Syntax highlighter callback for replxx — colors individual code points
fn void lisp_highlighter(char* input, CInt* colors, CInt size, void* ud) {
    if (input == null || size <= 0) return;

    bool in_string = false;
    bool in_comment = false;
    bool escape_next = false;
    int paren_depth = 0;
    CInt[6] paren_colors = { RXCOLOR_BRIGHTCYAN, RXCOLOR_BRIGHTMAGENTA, RXCOLOR_YELLOW, RXCOLOR_BRIGHTGREEN, RXCOLOR_BRIGHTBLUE, RXCOLOR_BRIGHTRED };
    isz sym_start = -1;

    for (CInt i = 0; i < size; i++) {
        char c = input[i];

        if (in_comment) {
            colors[i] = RXCOLOR_GRAY;
            if (c == '\n') { in_comment = false; }
            continue;
        }

        if (in_string) {
            colors[i] = RXCOLOR_GREEN;
            if (escape_next) { escape_next = false; continue; }
            if (c == '\\') { escape_next = true; continue; }
            if (c == '"') { in_string = false; }
            continue;
        }

        // Normal mode
        if (c == ';') {
            flush_symbol(input, colors, sym_start, i); sym_start = -1;
            in_comment = true;
            colors[i] = RXCOLOR_GRAY;
        } else if (c == '"') {
            flush_symbol(input, colors, sym_start, i); sym_start = -1;
            in_string = true;
            colors[i] = RXCOLOR_GREEN;
        } else if (c == '(' || c == ')') {
            flush_symbol(input, colors, sym_start, i); sym_start = -1;
            if (c == '(') {
                colors[i] = paren_colors[paren_depth % 6];
                paren_depth++;
            } else {
                paren_depth--;
                if (paren_depth < 0) { paren_depth = 0; }
                colors[i] = paren_colors[paren_depth % 6];
            }
        } else if (c == '[' || c == ']') {
            flush_symbol(input, colors, sym_start, i); sym_start = -1;
            colors[i] = RXCOLOR_YELLOW;
        } else if (c == '{' || c == '}') {
            flush_symbol(input, colors, sym_start, i); sym_start = -1;
            colors[i] = RXCOLOR_BROWN;
        } else if (c == '\'' && (i == 0 || input[i-1] == '(' || input[i-1] == ' ' || input[i-1] == '\t')) {
            colors[i] = RXCOLOR_CYAN;
        } else if (c >= '0' && c <= '9' && sym_start < 0) {
            colors[i] = RXCOLOR_BRIGHTMAGENTA;
        } else if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            flush_symbol(input, colors, sym_start, i); sym_start = -1;
        } else {
            if (sym_start < 0) { sym_start = (isz)i; }
        }
    }

    // Flush trailing symbol
    flush_symbol(input, colors, sym_start, size);
}

// Completion callback — complete defined symbols
fn void lisp_completion(char* input, void* completions, CInt* context_len, void* ud) {
    if (input == null || g_repl_interp == null) return;

    // Find the start of the current word
    usz len = 0;
    while (input[len] != 0) { len++; }
    if (len == 0) return;

    // Walk backward to find word start
    isz word_start = (isz)len - 1;
    while (word_start >= 0) {
        char c = input[word_start];
        if (c == ' ' || c == '\t' || c == '(' || c == ')' || c == '[' || c == ']' || c == '{' || c == '}' || c == '\'' || c == '"') {
            word_start++;
            break;
        }
        if (word_start == 0) { break; }
        word_start--;
    }
    if (word_start < 0) { word_start = 0; }

    usz prefix_len = len - (usz)word_start;
    if (prefix_len == 0) return;
    *context_len = (CInt)prefix_len;
    char* prefix = &input[word_start];

    // Search global env bindings
    Env* env = g_repl_interp.global_env;
    for (usz i = 0; i < env.binding_count; i++) {
        char[] name = g_repl_interp.symbols.get_name(env.bindings[i].name);
        if (name.len >= prefix_len) {
            bool match = true;
            for (usz k = 0; k < prefix_len; k++) {
                if (name[k] != prefix[k]) { match = false; break; }
            }
            if (match) {
                char[256] nbuf;
                usz copy = name.len;
                if (copy > 255) { copy = 255; }
                for (usz k = 0; k < copy; k++) { nbuf[k] = name[k]; }
                nbuf[copy] = 0;
                replxx_add_completion(completions, &nbuf[0]);
            }
        }
    }
}

/**
 * Read-Eval-Print-Loop.
 */
// Count net parenthesis depth in a string, skipping chars inside "..." strings
// and ; line comments. Returns the net depth (opens - closes).
fn int count_paren_depth(char[] input) {
    int depth = 0;
    bool in_string = false;
    bool in_comment = false;
    bool escape_next = false;

    for (usz i = 0; i < input.len; i++) {
        char c = input[i];

        // Handle escape sequences inside strings
        if (in_string) {
            if (escape_next) {
                escape_next = false;
                continue;
            }
            if (c == '\\') {
                escape_next = true;
                continue;
            }
            if (c == '"') {
                in_string = false;
            }
            continue;
        }

        // Comments run to end of line
        if (in_comment) {
            if (c == '\n') {
                in_comment = false;
            }
            continue;
        }

        // Normal mode
        if (c == ';') {
            in_comment = true;
        } else if (c == '"') {
            in_string = true;
        } else if (c == '(') {
            depth++;
        } else if (c == ')') {
            depth--;
        }
    }
    return depth;
}

fn void repl(Interp* interp) {
    // Install SIGINT handler so Ctrl+C interrupts eval instead of killing process
    signal(SIGINT_VAL, &sigint_handler);

    // Initialize replxx
    void* rx = replxx_init();
    if (rx == null) {
        io::eprintn("Failed to initialize replxx");
        return;
    }
    g_replxx = rx;
    g_repl_interp = interp;

    // Configure replxx
    replxx_set_max_history_size(rx, 1000);
    replxx_set_unique_history(rx, 1);
    replxx_install_window_change_handler(rx);
    replxx_set_word_break_characters(rx, " \t\n()[]{}';\"");
    replxx_set_indent_multiline(rx, 1);

    // Register callbacks
    replxx_set_highlighter_callback(rx, &lisp_highlighter, null);
    replxx_set_completion_callback(rx, &lisp_completion, null);

    // Load history from ~/.omni_history
    char* history_file = ".omni_history";
    replxx_history_load(rx, history_file);

    // ANSI color codes for output
    char[] ansi_red     = "\x1b[31m";
    char[] ansi_green   = "\x1b[32m";
    char[] ansi_reset   = "\x1b[0m";

    io::printn("Omni Lisp REPL (type 'quit' or 'exit' to leave, Ctrl-D for EOF)");
    io::printn("---");

    // Buffer for accumulating multi-line input
    char[8192] buf;
    usz buf_len = 0;

    while (true) {
        // Determine prompt: primary or continuation
        char* prompt;
        if (buf_len == 0) {
            prompt = "\x1b[1;34momni>\x1b[0m ";
        } else {
            prompt = "\x1b[1;34m ....\x1b[0m ";
        }

        // Use replxx for line editing with syntax highlighting
        char* line = (char*)replxx_input(rx, prompt);

        // null on EOF (Ctrl-D)
        if (line == null) {
            if (buf_len > 0) {
                // Cancel incomplete expression on Ctrl-D
                io::printn("");
                buf_len = 0;
                continue;
            }
            io::printn("\nGoodbye!");
            break;
        }

        // Compute length of the line
        usz len = 0;
        while (line[len] != 0) {
            len++;
        }

        // replxx manages the returned pointer — do NOT free it

        // If we're in continuation mode and get an empty line, cancel
        if (buf_len > 0 && len == 0) {
            io::printn("Input cancelled.");
            buf_len = 0;
            continue;
        }

        // Skip empty lines on primary prompt (don't add to history)
        if (buf_len == 0 && len == 0) {
            continue;
        }

        // Check for quit/exit on primary prompt only
        if (buf_len == 0) {
            if (len >= 4 && line[0] == 'q' && line[1] == 'u' &&
                line[2] == 'i' && line[3] == 't') {
                io::printn("Goodbye!");
                break;
            }

            if (len >= 4 && line[0] == 'e' && line[1] == 'x' &&
                line[2] == 'i' && line[3] == 't') {
                io::printn("Goodbye!");
                break;
            }
        }

        // Append line to buffer (with newline separator if continuing)
        if (buf_len > 0) {
            if (buf_len < 8191) {
                buf[buf_len] = '\n';
                buf_len++;
            }
        }

        // Copy line into buffer
        usz copy_len = len;
        if (buf_len + copy_len > 8191) {
            copy_len = 8191 - buf_len;
        }
        for (usz i = 0; i < copy_len; i++) {
            buf[buf_len + i] = line[i];
        }
        buf_len += copy_len;

        // Check paren depth to see if expression is complete
        char[] accumulated = buf[:buf_len];
        int depth = count_paren_depth(accumulated);

        if (depth > 0) {
            // Unmatched open parens — prompt for more input
            continue;
        }

        // Expression looks complete (balanced or no parens)
        // Add the full accumulated expression to history
        buf[buf_len] = 0;
        replxx_history_add(rx, &buf[0]);

        // GC JIT states between REPL lines (safe: no JIT code on stack)
        jit_gc();

        // Parse and evaluate in child scope
        char[] input = buf[:buf_len];
        g_interrupted = false;

        // Push child scope — REPL-line temporaries freed after print
        main::ScopeRegion* saved_scope = interp.current_scope;
        main::ScopeRegion* repl_child_scope = main::scope_create(saved_scope);
        interp.current_scope = repl_child_scope;

        EvalResult r = run(input, interp);

        if (!r.error.has_error && r.value != null) {
            r.value = copy_to_parent_site(r.value, interp, COPY_SITE_REPL_LINE_RESULT);
        }

        if (r.error.has_error) {
            // Print error in red
            usz msg_len = 0;
            while (msg_len < 256 && r.error.message[msg_len] != 0) {
                msg_len++;
            }

            io::print(ansi_red);
            if (r.error.line > 0) {
                io::printf("Error at line %d, column %d: ", (int)r.error.line, (int)r.error.column);
            } else {
                io::print("Error: ");
            }
            for (usz i = 0; i < msg_len; i++) {
                io::printf("%c", r.error.message[i]);
            }
            io::print(ansi_reset);
            io::printn("");
        } else {
            // Print result in green
            io::print(ansi_green);
            print_value(r.value, &interp.symbols);
            io::print(ansi_reset);
            io::printn("");
        }

        // Pop REPL child scope — frees all REPL-line temporaries
        interp.current_scope = saved_scope;
        main::scope_release(repl_child_scope);

        // Reset buffer for next expression
        buf_len = 0;
    }

    // Save history and cleanup
    replxx_history_save(rx, history_file);
    replxx_end(rx);
    g_replxx = null;
    g_repl_interp = null;
}
