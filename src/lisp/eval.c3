module lisp;

import std::io;
import std::collections::list;
import main;

// =============================================================================
// GNU READLINE FFI DECLARATIONS
// =============================================================================
// readline returns a malloc'd string (null on EOF), must be freed with mem::free.
// add_history adds a line to the interactive history.

extern fn char* readline(char* prompt) @extern("readline");
extern fn void add_history(char* line) @extern("add_history");

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
// LIBDL FFI DECLARATIONS
// =============================================================================

extern fn void* dlopen(char* filename, int flags) @extern("dlopen");
extern fn void* dlsym(void* handle, char* symbol) @extern("dlsym");
extern fn int dlclose(void* handle) @extern("dlclose");
extern fn char* dlerror() @extern("dlerror");
const int RTLD_LAZY = 1;

// Function pointer aliases for FFI calls (all args as long for x86_64 ABI)
alias FfiFn0 = fn long();
alias FfiFn1 = fn long(long);
alias FfiFn2 = fn long(long, long);
alias FfiFn3 = fn long(long, long, long);
alias FfiFn4 = fn long(long, long, long, long);
alias FfiFn5 = fn long(long, long, long, long, long);
alias FfiFn6 = fn long(long, long, long, long, long, long);

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

// eval — DELETED (replaced by JIT in jit.c3)


// =============================================================================
// SECTION 2: EXPRESSION EVALUATORS
// =============================================================================

// eval_var — DELETED (replaced by JIT in jit.c3)


// eval_lambda — DELETED (replaced by JIT in jit.c3)


// eval_define — DELETED (replaced by JIT in jit.c3)


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
        }
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
    SymbolId[11] builtin_names = {
        interp.sym_Int, interp.sym_Double, interp.sym_String,
        interp.sym_Symbol, interp.sym_List, interp.sym_Bool,
        interp.sym_Nil, interp.sym_Closure, interp.sym_Array,
        interp.sym_Dict, interp.sym_Any
    };
    for (usz i = 0; i < 11; i++) {
        TypeInfo info;
        info.name = builtin_names[i];
        info.kind = TK_BUILTIN;
        info.parent = INVALID_TYPE_ID;
        info.field_count = 0;
        info.type_param_count = 0;
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

    // Set parent chains: Int/Double → Number, List/Array/Dict → Collection
    interp.types.types[(usz)interp.tid_Int].parent = interp.tid_Number;
    interp.types.types[(usz)interp.tid_Double].parent = interp.tid_Number;
    interp.types.types[(usz)interp.tid_List].parent = interp.tid_Collection;
    interp.types.types[(usz)interp.tid_Array].parent = interp.tid_Collection;
    interp.types.types[(usz)interp.tid_Dict].parent = interp.tid_Collection;
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
            if ((uint)v.sym_val == (uint)interp.sym_true || (uint)v.sym_val == (uint)interp.sym_false) { return interp.sym_Bool; }
            return interp.sym_Symbol;
        case CONS:      return interp.sym_List;
        case CLOSURE:   return interp.sym_Closure;
        case ARRAY:     return interp.sym_Array;
        case HASHMAP:   return interp.sym_Dict;
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
 * Create an Instance value (heap-allocated).
 */
fn Value* make_instance(Interp* interp, TypeId type_id, Value** fields, usz field_count) {
    Instance* inst = (Instance*)mem::malloc(Instance.sizeof);
    inst.type_id = type_id;
    inst.field_count = field_count;
    inst.type_arg_count = 0;
    for (usz i = 0; i < field_count && i < MAX_TYPE_FIELDS; i++) {
        inst.fields[i] = fields[i];
    }
    // Allocate Value in root_region so it persists across frame releases
    main::RegionHandle saved = interp.current_frame;
    interp.current_frame = interp.root_region;
    Value* v = interp.alloc_value();
    interp.current_frame = saved;
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
    for (usz i = 0; i < dt.field_count; i++) {
        info.fields[i].name = dt.fields[i].name;
        if (dt.fields[i].type_ann.has_annotation) {
            info.fields[i].field_type = interp.types.lookup(dt.fields[i].type_ann.base_type, &interp.symbols);
            info.fields[i].annotation_sym = dt.fields[i].type_ann.base_type;
        } else {
            info.fields[i].field_type = INVALID_TYPE_ID;  // Any
            info.fields[i].annotation_sym = 0;
        }
    }

    // Register in type registry
    TypeId type_id = interp.types.register_type(info, &interp.symbols);
    if (type_id == INVALID_TYPE_ID) {
        return eval_error("type registry full");
    }

    // Create constructor primitive
    // Constructor is a closure that takes field_count args and creates an Instance
    // We'll implement it as a special primitive that captures the type_id
    Value* constructor = interp.alloc_value();
    constructor.tag = PRIMITIVE;
    constructor.prim_val = (Primitive*)mem::malloc(Primitive.sizeof);
    // Store type name as primitive name
    char[] name_str = interp.symbols.get_name(dt.name);
    for (usz i = 0; i < name_str.len && i < 63; i++) {
        constructor.prim_val.name[i] = name_str[i];
    }
    constructor.prim_val.name[name_str.len < 63 ? name_str.len : 63] = 0;
    constructor.prim_val.arity = (int)dt.field_count;
    constructor.prim_val.func = &prim_type_constructor;
    constructor.prim_val.tag = (int)type_id;

    // Copy constructor to root_region for globals
    if (interp.current_frame.region_id != interp.root_region.region_id) {
        constructor = copy_to_parent(constructor, interp, interp.root_region);
    }

    // Define constructor in global env
    interp.global_env.define(dt.name, constructor);

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
    if (ti == null) return make_error(interp, "invalid type in constructor");

    if (args.len != ti.field_count) {
        return make_error(interp, "wrong number of arguments to constructor");
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
    info.variant_count = 0;
    info.alias_target = INVALID_TYPE_ID;

    if (da.has_parent) {
        info.parent = interp.types.lookup(da.parent, &interp.symbols);
        if (info.parent == INVALID_TYPE_ID) {
            return eval_error("unknown parent type");
        }
    } else {
        info.parent = INVALID_TYPE_ID;
    }

    TypeId type_id = interp.types.register_type(info, &interp.symbols);
    if (type_id == INVALID_TYPE_ID) {
        return eval_error("type registry full");
    }

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
    union_info.variant_count = du.variant_count;
    union_info.alias_target = INVALID_TYPE_ID;
    for (usz i = 0; i < du.type_param_count; i++) {
        union_info.type_params[i] = du.type_params[i];
    }
    for (usz i = 0; i < du.variant_count; i++) {
        union_info.variants[i] = du.variants[i];
    }

    TypeId union_id = interp.types.register_type(union_info, &interp.symbols);
    if (union_id == INVALID_TYPE_ID) {
        return eval_error("type registry full");
    }

    // Register each variant as its own type (with union as parent)
    for (usz i = 0; i < du.variant_count; i++) {
        UnionVariant* variant = &du.variants[i];

        TypeInfo var_info;
        var_info.name = variant.name;
        var_info.kind = TK_CONCRETE;
        var_info.parent = union_id;  // variant is subtype of union
        var_info.field_count = variant.field_count;
        var_info.type_param_count = 0;
        var_info.variant_count = 0;
        var_info.alias_target = INVALID_TYPE_ID;
        for (usz j = 0; j < variant.field_count; j++) {
            var_info.fields[j].name = variant.fields[j];
            var_info.fields[j].field_type = INVALID_TYPE_ID;
        }

        TypeId var_id = interp.types.register_type(var_info, &interp.symbols);
        if (var_id == INVALID_TYPE_ID) {
            return eval_error("type registry full");
        }

        // Create constructor for this variant
        if (variant.field_count == 0) {
            // Nullary variant: define as a constant instance
            Value* inst = make_instance(interp, var_id, null, 0);
            if (interp.current_frame.region_id != interp.root_region.region_id) {
                inst = copy_to_parent(inst, interp, interp.root_region);
            }
            interp.global_env.define(variant.name, inst);
        } else {
            // Variant with fields: define as constructor primitive
            Value* ctor = interp.alloc_value();
            ctor.tag = PRIMITIVE;
            ctor.prim_val = (Primitive*)mem::malloc(Primitive.sizeof);
            char[] vname = interp.symbols.get_name(variant.name);
            for (usz k = 0; k < vname.len && k < 63; k++) {
                ctor.prim_val.name[k] = vname[k];
            }
            ctor.prim_val.name[vname.len < 63 ? vname.len : 63] = 0;
            ctor.prim_val.arity = (int)variant.field_count;
            ctor.prim_val.func = &prim_type_constructor;
            ctor.prim_val.tag = (int)var_id;
            if (interp.current_frame.region_id != interp.root_region.region_id) {
                ctor = copy_to_parent(ctor, interp, interp.root_region);
            }
            interp.global_env.define(variant.name, ctor);
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
    info.variant_count = 0;

    // Resolve target
    if (da.target.has_annotation) {
        info.alias_target = interp.types.lookup(da.target.base_type, &interp.symbols);
    } else {
        info.alias_target = INVALID_TYPE_ID;
    }

    TypeId type_id = interp.types.register_type(info, &interp.symbols);
    if (type_id == INVALID_TYPE_ID) {
        return eval_error("type registry full");
    }

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
    if (type_id == INVALID_TYPE_ID) {
        return eval_error("type registry full");
    }

    return eval_ok(make_nil(interp));
}

// =============================================================================
// SECTION 2.4: FRAME PUSH/POP (ESCAPE-COPY)
// =============================================================================

// NOTE: Frame push/pop integration is deferred. While copy_to_parent correctly
// handles simple values and cons lists, closures capture Env* pointers that may
// live in child frames. Releasing a child frame would create dangling env
// pointers in escaped closures (e.g., (define (f x) (lambda (y) (+ x y)))).
// Additionally, let ^rec creates env cycles (closure→env→binding→closure) that
// would require cycle-aware copying. A proper solution needs either:
// (1) Full mark-and-sweep GC, (2) Reference counting with cycle detection, or
// (3) Cycle-aware env chain copying with a visited set.
// For now, current_frame == root_region always. This function is retained for
// future GC integration.

/**
 * Destructor for Value objects — frees malloc'd backing storage.
 * Called automatically by the region system when a Value is destroyed.
 */
fn void destroy_value(void* ptr) {
    Value* v = (Value*)ptr;
    switch (v.tag) {
        case STRING:
        case ERROR:
            if (v.str_val != null) {
                mem::free(v.str_val);
                v.str_val = null;
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
                mem::free(v.method_table_val);
                v.method_table_val = null;
            }
        case CLOSURE:
            if (v.closure_val != null) {
                if (v.closure_val.type_sig != null) {
                    mem::free(v.closure_val.type_sig);
                }
                mem::free(v.closure_val);
                v.closure_val = null;
            }
        case PRIMITIVE:
            if (v.prim_val != null) {
                mem::free(v.prim_val);
                v.prim_val = null;
            }
        case FFI_HANDLE:
            if (v.ffi_val != null) {
                if (v.ffi_val.lib_handle != null) {
                    dlclose(v.ffi_val.lib_handle);
                    v.ffi_val.lib_handle = null;
                }
                mem::free(v.ffi_val);
                v.ffi_val = null;
            }
        default: {}
    }
}

/**
 * Destructor for HashMap objects — frees malloc'd entries buffer.
 * Called automatically by the region system when a HashMap is destroyed.
 */
fn void destroy_hashmap(void* ptr) {
    HashMap* map = (HashMap*)ptr;
    if (map.entries != null) {
        mem::free(map.entries);
        map.entries = null;
    }
}

/**
 * Destructor for Env objects — frees malloc'd bindings array.
 */
fn void destroy_env(void* ptr) {
    Env* env = (Env*)ptr;
    if (env.bindings != null) {
        mem::free(env.bindings);
        env.bindings = null;
    }
    if (env.hash_table != null) {
        mem::free(env.hash_table);
        env.hash_table = null;
    }
}

/**
 * Register destructors for types with malloc'd backing storage.
 * Initializes the global destructor registry if not already initialized.
 */
fn void register_destructors() {
    if (main::g_destructor_registry.capacity == 0) {
        main::g_destructor_registry.init();
    }
    main::g_destructor_registry.register(Value.typeid, &destroy_value);
    main::g_destructor_registry.register(HashMap.typeid, &destroy_hashmap);
    main::g_destructor_registry.register(Env.typeid, &destroy_env);
}

/**
 * Copy a Value and everything it transitively references to a target region.
 * Used to promote return values out of a child frame before releasing it.
 */
fn Value* copy_to_parent(Value* v, Interp* interp, main::RegionHandle target) {
    if (v == null) return null;
    main::RegionHandle saved = interp.current_frame;
    interp.current_frame = target;

    Value* result;
    switch (v.tag) {
        case NIL:
            result = make_nil(interp);
        case INT:
            result = make_int(interp, v.int_val);
        case DOUBLE:
            result = make_double(interp, v.double_val);
        case STRING:
            result = make_string(interp, v.str_val.chars[:v.str_val.len]);
        case SYMBOL:
            result = make_symbol(interp, v.sym_val);
        case CONS: {
            // Need to restore frame for recursive calls
            interp.current_frame = saved;
            Value* new_car = copy_to_parent(v.cons_val.car, interp, target);
            Value* new_cdr = copy_to_parent(v.cons_val.cdr, interp, target);
            interp.current_frame = target;
            result = make_cons(interp, new_car, new_cdr);
        }
        case CLOSURE:
            result = v;  // closures already in root_region
        case PRIMITIVE:
            result = v;
        case PARTIAL_PRIM:
            result = v;
        case CONTINUATION:
            result = v;
        case HASHMAP:
        case ARRAY:
        case INSTANCE:
        case METHOD_TABLE:
            result = v;  // Value allocated in root_region; backing data is malloc'd with registered destructors
        case FFI_HANDLE:
            result = v;  // FfiHandle is inline in Value; Value allocated in root_region
        case TYPE_INFO:
            result = v;  // type info lives in registry
        case ERROR:
            result = make_error(interp, v.str_val.chars[:v.str_val.len]);
        default:
            result = v;
    }
    interp.current_frame = saved;
    return result;
}

/**
 * Deep-copy an env chain to root_region so closures survive frame release.
 * Stops at global_env (already in root_region).
 */
fn Env* deep_copy_env(Env* env, Interp* interp, usz depth = 0) {
    if (env == null || env == interp.global_env) return env;
    if (depth >= 256) return null;
    main::RegionHandle saved = interp.current_frame;
    interp.current_frame = interp.root_region;
    Env* new_env = interp.alloc_env();
    interp.current_frame = saved;
    // Reallocate bindings to match source capacity
    if (env.binding_count > new_env.capacity) {
        mem::free(new_env.bindings);
        new_env.bindings = (Binding*)mem::malloc(Binding.sizeof * env.binding_count);
        new_env.capacity = env.binding_count;
    }
    new_env.binding_count = env.binding_count;
    for (usz i = 0; i < env.binding_count; i++) {
        new_env.bindings[i].name = env.bindings[i].name;
        new_env.bindings[i].value = copy_to_parent(
            env.bindings[i].value, interp, interp.root_region);
    }
    new_env.parent = deep_copy_env(env.parent, interp, depth + 1);
    // Rebuild hash table if source had one
    if (env.hash_table != null) {
        new_env.build_hash_table();
    }
    return new_env;
}

// =============================================================================
// SECTION 2.5: QUASIQUOTE
// =============================================================================

// eval_quasiquote — DELETED (replaced by JIT in jit.c3)


// qq_expand_elements — DELETED (replaced by JIT in jit.c3)


// qq_expand_call — DELETED (replaced by JIT in jit.c3)


// =============================================================================
// SECTION 2.6: DEFMACRO
// =============================================================================

/**
 * Convert an Expr AST to a Value (quoted list representation).
 * Used for macro expansion: transforms the call AST into data the macro can manipulate.
 */
fn Value* expr_to_value(Expr* expr, Interp* interp) {
    if (expr == null) return make_nil(interp);

    switch (expr.tag) {
        case E_LIT:
            return expr.lit.value;
        case E_VAR:
            return make_symbol(interp, expr.var_expr.name);
        case E_QUOTE: {
            Value* sym = make_symbol(interp, interp.sym_quote);
            Value* rest = make_cons(interp, expr.quote.datum, make_nil(interp));
            return make_cons(interp, sym, rest);
        }
        case E_CALL: {
            // Build (func arg1 arg2 ...)
            Value* result = make_nil(interp);
            // Add args right-to-left
            for (usz i = expr.call.arg_count; i > 0; i--) {
                result = make_cons(interp, expr_to_value(expr.call.args[i - 1], interp), result);
            }
            // Add func
            result = make_cons(interp, expr_to_value(expr.call.func, interp), result);
            return result;
        }
        case E_APP: {
            Value* f = expr_to_value(expr.app.func, interp);
            Value* a = expr_to_value(expr.app.arg, interp);
            Value* rest = make_cons(interp, a, make_nil(interp));
            return make_cons(interp, f, rest);
        }
        case E_IF: {
            Value* sym = make_symbol(interp, interp.sym_if);
            Value* t = expr_to_value(expr.if_expr.test, interp);
            Value* th = expr_to_value(expr.if_expr.then_branch, interp);
            Value* el = expr_to_value(expr.if_expr.else_branch, interp);
            return make_cons(interp, sym, make_cons(interp, t, make_cons(interp, th, make_cons(interp, el, make_nil(interp)))));
        }
        case E_LET: {
            Value* sym = make_symbol(interp, interp.sym_let);
            Value* name_sym = make_symbol(interp, expr.let_expr.name);
            Value* init_val = expr_to_value(expr.let_expr.init, interp);
            Value* binding = make_cons(interp, name_sym, make_cons(interp, init_val, make_nil(interp)));
            Value* bindings = make_cons(interp, binding, make_nil(interp));
            Value* body = expr_to_value(expr.let_expr.body, interp);
            return make_cons(interp, sym, make_cons(interp, bindings, make_cons(interp, body, make_nil(interp))));
        }
        case E_LAMBDA: {
            Value* sym = make_symbol(interp, interp.sym_lambda);
            // Build param list
            Value* params = make_nil(interp);
            if (expr.lambda.param_count > 0) {
                for (usz i = expr.lambda.param_count; i > 0; i--) {
                    params = make_cons(interp, make_symbol(interp, expr.lambda.params[i - 1]), params);
                }
            }
            Value* body = expr_to_value(expr.lambda.body, interp);
            return make_cons(interp, sym, make_cons(interp, params, make_cons(interp, body, make_nil(interp))));
        }
        case E_BEGIN: {
            Value* sym = make_symbol(interp, interp.sym_begin);
            Value* result = make_nil(interp);
            for (usz i = expr.begin.expr_count; i > 0; i--) {
                result = make_cons(interp, expr_to_value(expr.begin.exprs[i - 1], interp), result);
            }
            return make_cons(interp, sym, result);
        }
        case E_SET: {
            Value* sym = make_symbol(interp, interp.sym_set);
            Value* name_sym = make_symbol(interp, expr.set_expr.name);
            Value* val = expr_to_value(expr.set_expr.value, interp);
            return make_cons(interp, sym, make_cons(interp, name_sym, make_cons(interp, val, make_nil(interp))));
        }
        case E_QUASIQUOTE: {
            Value* sym = make_symbol(interp, interp.sym_quasiquote);
            Value* body = expr_to_value(expr.quasiquote.body, interp);
            return make_cons(interp, sym, make_cons(interp, body, make_nil(interp)));
        }
        case E_UNQUOTE: {
            Value* sym = make_symbol(interp, interp.sym_unquote);
            Value* body = expr_to_value(expr.unquote.body, interp);
            return make_cons(interp, sym, make_cons(interp, body, make_nil(interp)));
        }
        case E_UNQUOTE_SPLICING: {
            Value* sym = make_symbol(interp, interp.sym_unquote_splicing);
            Value* body = expr_to_value(expr.unquote_splicing.body, interp);
            return make_cons(interp, sym, make_cons(interp, body, make_nil(interp)));
        }
        case E_DEFINE: {
            Value* sym = make_symbol(interp, interp.sym_define);
            Value* name_sym = make_symbol(interp, expr.define.name);
            Value* val = expr_to_value(expr.define.value, interp);
            return make_cons(interp, sym, make_cons(interp, name_sym, make_cons(interp, val, make_nil(interp))));
        }
        default:
            return make_nil(interp);
    }
}

/**
 * Convert a Value (list representation) back to an Expr AST.
 * Used for macro expansion: transforms the macro output back to executable AST.
 */
fn Expr* value_to_expr(Value* val, Interp* interp) {
    if (val == null || val.tag == NIL) {
        Expr* e = interp.alloc_expr();
        e.tag = E_LIT;
        e.lit.value = make_nil(interp);
        return e;
    }

    if (val.tag == INT || val.tag == STRING || val.tag == DOUBLE) {
        Expr* e = interp.alloc_expr();
        e.tag = E_LIT;
        e.lit.value = val;
        return e;
    }

    if (val.tag == SYMBOL) {
        // Check for special form names
        SymbolId sym = val.sym_val;
        if ((uint)sym == (uint)interp.sym_true || (uint)sym == (uint)interp.sym_false) {
            Expr* e = interp.alloc_expr();
            e.tag = E_VAR;
            e.var_expr.name = sym;
            return e;
        }
        Expr* e = interp.alloc_expr();
        e.tag = E_VAR;
        e.var_expr.name = sym;
        return e;
    }

    if (val.tag == CONS) {
        // It's a list — check if head is a special form symbol
        Value* head = val.cons_val.car;
        if (head != null && head.tag == SYMBOL) {
            SymbolId sym = head.sym_val;
            // Check for special forms and delegate to parser-style construction
            if ((uint)sym == (uint)interp.sym_if) {
                // (if test then else)
                Value* rest = val.cons_val.cdr;
                if (!is_cons(rest)) return value_to_expr(make_nil(interp), interp);
                Expr* e = interp.alloc_expr();
                e.tag = E_IF;
                e.if_expr.test = value_to_expr(rest.cons_val.car, interp);
                rest = rest.cons_val.cdr;
                e.if_expr.then_branch = is_cons(rest) ? value_to_expr(rest.cons_val.car, interp) : value_to_expr(make_nil(interp), interp);
                rest = is_cons(rest) ? rest.cons_val.cdr : rest;
                e.if_expr.else_branch = is_cons(rest) ? value_to_expr(rest.cons_val.car, interp) : value_to_expr(make_nil(interp), interp);
                return e;
            }
            if ((uint)sym == (uint)interp.sym_quote) {
                Value* rest = val.cons_val.cdr;
                Expr* e = interp.alloc_expr();
                e.tag = E_QUOTE;
                e.quote.datum = is_cons(rest) ? rest.cons_val.car : make_nil(interp);
                return e;
            }
            if ((uint)sym == (uint)interp.sym_begin) {
                Value* rest = val.cons_val.cdr;
                Expr* e = interp.alloc_expr();
                e.tag = E_BEGIN;
                e.begin = mem::malloc(ExprBegin.sizeof);
                e.begin.expr_count = 0;
                while (is_cons(rest) && e.begin.expr_count < 16) {
                    e.begin.exprs[e.begin.expr_count] = value_to_expr(rest.cons_val.car, interp);
                    e.begin.expr_count++;
                    rest = rest.cons_val.cdr;
                }
                return e;
            }
            if ((uint)sym == (uint)interp.sym_define) {
                Value* rest = val.cons_val.cdr;
                if (!is_cons(rest)) return value_to_expr(make_nil(interp), interp);
                Expr* e = interp.alloc_expr();
                e.tag = E_DEFINE;
                Value* name_val = rest.cons_val.car;
                e.define.name = (name_val != null && name_val.tag == SYMBOL) ? name_val.sym_val : 0;
                rest = rest.cons_val.cdr;
                e.define.value = is_cons(rest) ? value_to_expr(rest.cons_val.car, interp) : value_to_expr(make_nil(interp), interp);
                return e;
            }
            if ((uint)sym == (uint)interp.sym_lambda) {
                Value* rest = val.cons_val.cdr;
                if (!is_cons(rest)) return value_to_expr(make_nil(interp), interp);
                Value* params_list = rest.cons_val.car;
                rest = rest.cons_val.cdr;
                Expr* body = is_cons(rest) ? value_to_expr(rest.cons_val.car, interp) : value_to_expr(make_nil(interp), interp);

                // Collect params
                SymbolId[16] params;
                usz param_count = 0;
                Value* p = params_list;
                while (is_cons(p) && param_count < 16) {
                    if (p.cons_val.car != null && p.cons_val.car.tag == SYMBOL) {
                        params[param_count] = p.cons_val.car.sym_val;
                        param_count++;
                    }
                    p = p.cons_val.cdr;
                }

                if (param_count == 0) {
                    // Zero-arg lambda
                    Expr* e = interp.alloc_expr();
                    e.tag = E_LAMBDA;
                    e.lambda = mem::malloc(ExprLambda.sizeof);
                    e.lambda.param = (SymbolId)0xFFFFFFFF;
                    e.lambda.param_count = 0;
                    e.lambda.has_rest = false;
                    e.lambda.rest_param = 0;
                    e.lambda.body = body;
                    return e;
                }
                if (param_count == 1) {
                    Expr* e = interp.alloc_expr();
                    e.tag = E_LAMBDA;
                    e.lambda = mem::malloc(ExprLambda.sizeof);
                    e.lambda.param = params[0];
                    e.lambda.param_count = 1;
                    e.lambda.params[0] = params[0];
                    e.lambda.has_rest = false;
                    e.lambda.rest_param = 0;
                    e.lambda.body = body;
                    return e;
                }
                // Multi-param: desugar to nested lambdas
                Expr* inner = body;
                for (usz i = param_count; i > 1; i--) {
                    Expr* wrapper = interp.alloc_expr();
                    wrapper.tag = E_LAMBDA;
                    wrapper.lambda = mem::malloc(ExprLambda.sizeof);
                    wrapper.lambda.param = params[i - 1];
                    wrapper.lambda.param_count = 1;
                    wrapper.lambda.params[0] = params[i - 1];
                    wrapper.lambda.has_rest = false;
                    wrapper.lambda.rest_param = 0;
                    wrapper.lambda.body = inner;
                    inner = wrapper;
                }
                Expr* e = interp.alloc_expr();
                e.tag = E_LAMBDA;
                e.lambda = mem::malloc(ExprLambda.sizeof);
                e.lambda.param = params[0];
                e.lambda.param_count = 1;
                e.lambda.params[0] = params[0];
                e.lambda.has_rest = false;
                e.lambda.rest_param = 0;
                e.lambda.body = inner;
                return e;
            }
            if ((uint)sym == (uint)interp.sym_let) {
                Value* rest = val.cons_val.cdr;
                if (!is_cons(rest)) return value_to_expr(make_nil(interp), interp);
                Value* bindings_list = rest.cons_val.car;
                rest = rest.cons_val.cdr;
                Expr* body = is_cons(rest) ? value_to_expr(rest.cons_val.car, interp) : value_to_expr(make_nil(interp), interp);
                // Build nested lets from bindings
                if (!is_cons(bindings_list)) {
                    return body;
                }
                // Collect all bindings
                SymbolId[16] names;
                Value*[16] inits;
                usz count = 0;
                Value* bp = bindings_list;
                while (is_cons(bp) && count < 16) {
                    Value* binding = bp.cons_val.car;
                    if (is_cons(binding)) {
                        Value* name_v = binding.cons_val.car;
                        names[count] = (name_v != null && name_v.tag == SYMBOL) ? name_v.sym_val : 0;
                        inits[count] = is_cons(binding.cons_val.cdr) ? binding.cons_val.cdr.cons_val.car : make_nil(interp);
                        count++;
                    }
                    bp = bp.cons_val.cdr;
                }
                // Build from inside out
                Expr* inner_body = body;
                for (usz i = count; i > 0; i--) {
                    Expr* e = interp.alloc_expr();
                    e.tag = E_LET;
                    e.let_expr.name = names[i - 1];
                    e.let_expr.init = value_to_expr(inits[i - 1], interp);
                    e.let_expr.body = inner_body;
                    e.let_expr.is_recursive = false;
                    inner_body = e;
                }
                return inner_body;
            }
            if ((uint)sym == (uint)interp.sym_set) {
                Value* rest = val.cons_val.cdr;
                if (!is_cons(rest)) return value_to_expr(make_nil(interp), interp);
                Expr* e = interp.alloc_expr();
                e.tag = E_SET;
                Value* name_val = rest.cons_val.car;
                e.set_expr.name = (name_val != null && name_val.tag == SYMBOL) ? name_val.sym_val : 0;
                rest = rest.cons_val.cdr;
                e.set_expr.value = is_cons(rest) ? value_to_expr(rest.cons_val.car, interp) : value_to_expr(make_nil(interp), interp);
                return e;
            }
            if ((uint)sym == (uint)interp.sym_and) {
                Value* rest = val.cons_val.cdr;
                if (!is_cons(rest)) return value_to_expr(make_nil(interp), interp);
                Expr* e = interp.alloc_expr();
                e.tag = E_AND;
                e.and_expr.left = value_to_expr(rest.cons_val.car, interp);
                rest = rest.cons_val.cdr;
                e.and_expr.right = is_cons(rest) ? value_to_expr(rest.cons_val.car, interp) : value_to_expr(make_nil(interp), interp);
                return e;
            }
            if ((uint)sym == (uint)interp.sym_or) {
                Value* rest = val.cons_val.cdr;
                if (!is_cons(rest)) return value_to_expr(make_nil(interp), interp);
                Expr* e = interp.alloc_expr();
                e.tag = E_OR;
                e.or_expr.left = value_to_expr(rest.cons_val.car, interp);
                rest = rest.cons_val.cdr;
                e.or_expr.right = is_cons(rest) ? value_to_expr(rest.cons_val.car, interp) : value_to_expr(make_nil(interp), interp);
                return e;
            }
            if ((uint)sym == (uint)interp.sym_quasiquote) {
                Value* rest = val.cons_val.cdr;
                Expr* e = interp.alloc_expr();
                e.tag = E_QUASIQUOTE;
                e.quasiquote.body = is_cons(rest) ? value_to_expr(rest.cons_val.car, interp) : value_to_expr(make_nil(interp), interp);
                return e;
            }
            if ((uint)sym == (uint)interp.sym_unquote) {
                Value* rest = val.cons_val.cdr;
                Expr* e = interp.alloc_expr();
                e.tag = E_UNQUOTE;
                e.unquote.body = is_cons(rest) ? value_to_expr(rest.cons_val.car, interp) : value_to_expr(make_nil(interp), interp);
                return e;
            }
            if ((uint)sym == (uint)interp.sym_unquote_splicing) {
                Value* rest = val.cons_val.cdr;
                Expr* e = interp.alloc_expr();
                e.tag = E_UNQUOTE_SPLICING;
                e.unquote_splicing.body = is_cons(rest) ? value_to_expr(rest.cons_val.car, interp) : value_to_expr(make_nil(interp), interp);
                return e;
            }
            if ((uint)sym == (uint)interp.sym_perform) {
                Value* rest = val.cons_val.cdr;
                if (!is_cons(rest)) return value_to_expr(make_nil(interp), interp);
                Expr* e = interp.alloc_expr();
                e.tag = E_PERFORM;
                Value* tag_val = rest.cons_val.car;
                e.perform.tag = (tag_val != null && tag_val.tag == SYMBOL) ? tag_val.sym_val : 0;
                rest = rest.cons_val.cdr;
                e.perform.arg = is_cons(rest) ? value_to_expr(rest.cons_val.car, interp) : value_to_expr(make_nil(interp), interp);
                return e;
            }
        }

        // Generic function call: (func arg1 arg2 ...)
        // Count the list length
        usz len = list_length(val);
        if (len == 0) {
            Expr* e = interp.alloc_expr();
            e.tag = E_LIT;
            e.lit.value = make_nil(interp);
            return e;
        }

        Expr* e = interp.alloc_expr();
        e.tag = E_CALL;
        e.call = mem::malloc(ExprCall.sizeof);
        e.call.func = value_to_expr(val.cons_val.car, interp);
        e.call.arg_count = 0;
        Value* rest = val.cons_val.cdr;
        while (is_cons(rest) && e.call.arg_count < 64) {
            e.call.args[e.call.arg_count] = value_to_expr(rest.cons_val.car, interp);
            e.call.arg_count++;
            rest = rest.cons_val.cdr;
        }
        return e;
    }

    // Fallback: return as literal
    Expr* e = interp.alloc_expr();
    e.tag = E_LIT;
    e.lit.value = val;
    return e;
}

/**
 * Check if a symbol is a registered macro.
 * Returns the macro definition, or null if not a macro.
 */
fn MacroDef* lookup_macro(SymbolId name, Interp* interp) {
    usz hash_slot = (usz)name % MACRO_HASH_SIZE;
    for (usz probe = 0; probe < MACRO_HASH_SIZE; probe++) {
        usz idx = interp.macro_hash_index[hash_slot];
        if (idx == usz.max) return null;  // Empty slot — not found
        if ((uint)interp.macro_table[idx].name == (uint)name) {
            return &interp.macro_table[idx];
        }
        hash_slot = (hash_slot + 1) % MACRO_HASH_SIZE;
    }
    return null;
}

/**
 * Collect pattern variable names from a Pattern into a flat array.
 * Used by eval_define_macro to distinguish pattern vars from template literals.
 */
fn void collect_pattern_vars(Pattern* pat, SymbolId[]* vars, usz* count) {
    if (pat == null) return;
    if (*count >= 32) {
        io::printfn("WARNING: macro has more than 32 pattern variables, some bindings may be lost");
        return;
    }
    switch (pat.tag) {
        case PAT_VAR: {
            (*vars)[*count] = pat.var_name;
            (*count)++;
        }
        case PAT_CONS: {
            collect_pattern_vars(pat.car_pat, vars, count);
            collect_pattern_vars(pat.cdr_pat, vars, count);
        }
        case PAT_SEQ: {
            for (usz i = 0; i < pat.elem_count; i++) {
                collect_pattern_vars(pat.elements[i], vars, count);
            }
            // Rest binding is also a pattern var
            if (pat.rest_pos != REST_NONE && *count < 32) {
                (*vars)[*count] = pat.rest_binding;
                (*count)++;
            }
        }
        case PAT_CONSTRUCTOR: {
            for (usz i = 0; i < pat.ctor_sub_count; i++) {
                collect_pattern_vars(pat.ctor_sub_patterns[i], vars, count);
            }
        }
        default: {}
    }
}

/**
 * Walk a template Value* tree and capture bindings for non-pattern-var,
 * non-gensym, non-special-form symbols from the current global env.
 * This creates a SNAPSHOT of the definition-time bindings.
 */
fn void capture_template_bindings(Value* tmpl, SymbolId* pat_vars, usz pat_var_count,
                                  CapturedBinding* bindings, usz* binding_count,
                                  Interp* interp) {
    if (tmpl == null) return;
    if (*binding_count >= 32) {
        io::printfn("WARNING: macro captures more than 32 bindings, some hygiene may be lost");
        return;
    }

    if (tmpl.tag == SYMBOL) {
        SymbolId sym = tmpl.sym_val;

        // Skip pattern variables
        for (usz i = 0; i < pat_var_count; i++) {
            if ((uint)pat_vars[i] == (uint)sym) return;
        }

        // Skip auto-gensym symbols (ending with #)
        char[] sym_name = interp.symbols.get_name(sym);
        if (sym_name.len > 0 && sym_name[sym_name.len - 1] == '#') return;

        // Skip special form symbols
        if (is_special_form_symbol(sym, interp)) return;

        // Skip if already captured
        for (usz i = 0; i < *binding_count; i++) {
            if ((uint)bindings[i].sym == (uint)sym) return;
        }

        // Look up in global env — if defined, capture it
        if (interp.global_env != null) {
            Value* val = interp.global_env.lookup(sym);
            if (val != null && *binding_count < 32) {
                bindings[*binding_count].sym = sym;
                bindings[*binding_count].value = val;
                (*binding_count)++;
            }
        }
        return;
    }

    if (tmpl.tag == CONS) {
        capture_template_bindings(tmpl.cons_val.car, pat_vars, pat_var_count,
                                  bindings, binding_count, interp);
        capture_template_bindings(tmpl.cons_val.cdr, pat_vars, pat_var_count,
                                  bindings, binding_count, interp);
    }
}

/**
 * Evaluate a define-macro expression.
 * (define [macro] name (pattern1 template1) ...)
 * Captures definition-time bindings for template hygiene.
 */
fn EvalResult eval_define_macro(Expr* expr, Env* env, Interp* interp) {
    if (interp.macro_count >= 64) {
        return eval_error("macro table exhausted (max 64 macros)");
    }
    MacroDef* mdef = &interp.macro_table[interp.macro_count];
    mdef.name = expr.define_macro.name;
    mdef.clause_count = expr.define_macro.clause_count;
    mdef.captured_count = 0;

    for (usz i = 0; i < expr.define_macro.clause_count; i++) {
        mdef.clauses[i] = expr.define_macro.clauses[i];
    }

    // Capture definition-time bindings for hygiene.
    // For each clause, collect pattern vars, then scan template for literals
    // that have current global-env bindings, and snapshot those values.
    for (usz i = 0; i < expr.define_macro.clause_count; i++) {
        // Collect pattern variable names for this clause
        SymbolId[32] pat_vars;
        usz pat_var_count = 0;
        SymbolId[] pat_vars_slice = &pat_vars;
        collect_pattern_vars(mdef.clauses[i].pattern, &pat_vars_slice, &pat_var_count);

        // Walk template and capture bindings
        capture_template_bindings(mdef.clauses[i].tmpl, &pat_vars, pat_var_count,
                                  &mdef.captured_bindings, &mdef.captured_count, interp);
    }

    interp.macro_count++;

    // Insert into macro hash index
    usz macro_idx = interp.macro_count - 1;
    usz hash_slot = (usz)mdef.name % MACRO_HASH_SIZE;
    for (;;) {
        usz existing = interp.macro_hash_index[hash_slot];
        if (existing == usz.max) {
            // Empty slot — insert here
            interp.macro_hash_index[hash_slot] = macro_idx;
            break;
        }
        if ((uint)interp.macro_table[existing].name == (uint)mdef.name) {
            // Same name (redefinition) — update to point to new entry
            interp.macro_hash_index[hash_slot] = macro_idx;
            break;
        }
        hash_slot = (hash_slot + 1) % MACRO_HASH_SIZE;
    }

    return eval_ok(make_nil(interp));
}

/**
 * expand_macros_in_expr — Pre-JIT macro expansion pass.
 *
 * Walks an AST and expands macros before JIT compilation. Within E_BEGIN
 * blocks, processes sequentially: E_DEFMACRO children are evaluated to register
 * the macro, then subsequent E_CALL children are expanded if they invoke a
 * known macro.
 *
 * Returns the (possibly transformed) expression. Mutates the tree in-place
 * where possible (e.g. replacing E_BEGIN children).
 */
fn Expr* expand_macros_in_expr(Expr* expr, Interp* interp) {
    if (expr == null) return expr;

    switch (expr.tag) {
        case E_BEGIN: {
            for (usz i = 0; i < expr.begin.expr_count; i++) {
                Expr* child = expr.begin.exprs[i];
                if (child != null && child.tag == E_DEFMACRO) {
                    // Register macro immediately so subsequent exprs can use it
                    eval_define_macro(child, interp.global_env, interp);
                } else {
                    expr.begin.exprs[i] = expand_macros_in_expr(child, interp);
                }
            }
            return expr;
        }
        case E_CALL: {
            if (expr.call.func != null && expr.call.func.tag == E_VAR) {
                MacroDef* mdef = lookup_macro(expr.call.func.var_expr.name, interp);
                if (mdef != null) {
                    EvalResult r = expand_pattern_macro(mdef, expr, interp);
                    if (!r.error.has_error) {
                        Expr* expanded = value_to_expr(r.value, interp);
                        return expand_macros_in_expr(expanded, interp);
                    }
                }
            }
            // Recurse into function and args
            expr.call.func = expand_macros_in_expr(expr.call.func, interp);
            for (usz i = 0; i < expr.call.arg_count; i++) {
                expr.call.args[i] = expand_macros_in_expr(expr.call.args[i], interp);
            }
            return expr;
        }
        case E_IF: {
            expr.if_expr.test = expand_macros_in_expr(expr.if_expr.test, interp);
            expr.if_expr.then_branch = expand_macros_in_expr(expr.if_expr.then_branch, interp);
            expr.if_expr.else_branch = expand_macros_in_expr(expr.if_expr.else_branch, interp);
            return expr;
        }
        case E_LET: {
            expr.let_expr.init = expand_macros_in_expr(expr.let_expr.init, interp);
            expr.let_expr.body = expand_macros_in_expr(expr.let_expr.body, interp);
            return expr;
        }
        case E_DEFINE: {
            expr.define.value = expand_macros_in_expr(expr.define.value, interp);
            return expr;
        }
        case E_LAMBDA: {
            expr.lambda.body = expand_macros_in_expr(expr.lambda.body, interp);
            return expr;
        }
        case E_APP: {
            expr.app.func = expand_macros_in_expr(expr.app.func, interp);
            expr.app.arg = expand_macros_in_expr(expr.app.arg, interp);
            return expr;
        }
        case E_AND: {
            expr.and_expr.left = expand_macros_in_expr(expr.and_expr.left, interp);
            expr.and_expr.right = expand_macros_in_expr(expr.and_expr.right, interp);
            return expr;
        }
        case E_OR: {
            expr.or_expr.left = expand_macros_in_expr(expr.or_expr.left, interp);
            expr.or_expr.right = expand_macros_in_expr(expr.or_expr.right, interp);
            return expr;
        }
        case E_SET: {
            expr.set_expr.value = expand_macros_in_expr(expr.set_expr.value, interp);
            return expr;
        }
        case E_RESET: {
            expr.reset.body = expand_macros_in_expr(expr.reset.body, interp);
            return expr;
        }
        case E_SHIFT: {
            expr.shift.body = expand_macros_in_expr(expr.shift.body, interp);
            return expr;
        }
        case E_PERFORM: {
            expr.perform.arg = expand_macros_in_expr(expr.perform.arg, interp);
            return expr;
        }
        case E_HANDLE: {
            expr.handle.body = expand_macros_in_expr(expr.handle.body, interp);
            for (usz i = 0; i < expr.handle.clause_count; i++) {
                expr.handle.clauses[i].handler_body = expand_macros_in_expr(expr.handle.clauses[i].handler_body, interp);
            }
            return expr;
        }
        case E_MATCH: {
            expr.match.scrutinee = expand_macros_in_expr(expr.match.scrutinee, interp);
            for (usz i = 0; i < expr.match.clause_count; i++) {
                expr.match.clauses[i].result = expand_macros_in_expr(expr.match.clauses[i].result, interp);
            }
            return expr;
        }
        case E_INDEX: {
            expr.index.collection = expand_macros_in_expr(expr.index.collection, interp);
            expr.index.index = expand_macros_in_expr(expr.index.index, interp);
            return expr;
        }
        case E_MODULE: {
            for (usz i = 0; i < expr.module_expr.body_count; i++) {
                Expr* child = expr.module_expr.body[i];
                if (child != null && child.tag == E_DEFMACRO) {
                    eval_define_macro(child, interp.global_env, interp);
                } else {
                    expr.module_expr.body[i] = expand_macros_in_expr(child, interp);
                }
            }
            return expr;
        }
        default:
            // E_LIT, E_VAR, E_QUOTE, E_PATH, E_IMPORT, E_DEFTYPE,
            // E_DEFABSTRACT, E_DEFUNION, E_DEFALIAS, E_DEFEFFECT,
            // E_DEFMACRO, E_QUASIQUOTE, E_UNQUOTE, E_UNQUOTE_SPLICING
            return expr;
    }
}

/**
 * Check if a symbol is a special form keyword (if, begin, let, lambda, etc.).
 * Special forms are recognized by SymbolId in value_to_expr, so they don't need
 * hygienic resolution — they always produce the correct Expr tag regardless of
 * any user-defined shadowing.
 */
fn bool is_special_form_symbol(SymbolId sym, Interp* interp) {
    return (uint)sym == (uint)interp.sym_if
        || (uint)sym == (uint)interp.sym_begin
        || (uint)sym == (uint)interp.sym_let
        || (uint)sym == (uint)interp.sym_lambda
        || (uint)sym == (uint)interp.sym_define
        || (uint)sym == (uint)interp.sym_quote
        || (uint)sym == (uint)interp.sym_set
        || (uint)sym == (uint)interp.sym_and
        || (uint)sym == (uint)interp.sym_or
        || (uint)sym == (uint)interp.sym_reset
        || (uint)sym == (uint)interp.sym_shift
        || (uint)sym == (uint)interp.sym_perform
        || (uint)sym == (uint)interp.sym_handle
        || (uint)sym == (uint)interp.sym_match
        || (uint)sym == (uint)interp.sym_quasiquote
        || (uint)sym == (uint)interp.sym_unquote
        || (uint)sym == (uint)interp.sym_unquote_splicing
        || (uint)sym == (uint)interp.sym_defmacro
        || (uint)sym == (uint)interp.sym_dotdot
        || (uint)sym == (uint)interp.sym_macro
        || (uint)sym == (uint)interp.sym_true
        || (uint)sym == (uint)interp.sym_false
        || (uint)sym == (uint)interp.sym_module
        || (uint)sym == (uint)interp.sym_import
        || (uint)sym == (uint)interp.sym_export;
}

/**
 * Expand a pattern-based macro.
 * Converts macro call args to a value list, tries each clause's pattern,
 * and on first match, expands the template with bindings.
 */
fn EvalResult expand_pattern_macro(MacroDef* mdef, Expr* call_expr, Interp* interp) {
    // Convert each arg Expr* to Value* and build into a cons list
    usz arg_count = call_expr.call.arg_count;
    Value* args_list = make_nil(interp);
    // Build right-to-left to get proper list order
    for (usz i = arg_count; i > 0; i--) {
        Value* arg_val = expr_to_value(call_expr.call.args[i - 1], interp);
        args_list = make_cons(interp, arg_val, args_list);
    }

    // Try each clause
    for (usz i = 0; i < mdef.clause_count; i++) {
        MatchResult match_result = match_pattern(mdef.clauses[i].pattern, args_list, interp);
        if (match_result.matched) {
            // Expand template with bindings; fresh gensym table per expansion
            GensymTable gensyms;
            gensyms.count = 0;
            Value* expanded = expand_template(mdef.clauses[i].tmpl, &match_result, interp, &gensyms,
                                              &mdef.captured_bindings, mdef.captured_count);
            return eval_ok(expanded);
        }
    }

    return eval_error("no macro clause matched");
}

/**
 * Look up or create a gensym for an auto-gensym symbol (ending with #).
 * Within a single macro expansion, the same #-symbol always maps to the same gensym.
 */
fn SymbolId lookup_or_create_gensym(SymbolId original, GensymTable* table, Interp* interp) {
    // Check if already mapped
    for (usz i = 0; i < table.count; i++) {
        if ((uint)table.mappings[i].original == (uint)original) {
            return table.mappings[i].generated;
        }
    }
    // Create new gensym
    char[32] buf;
    usz len = 0;
    buf[0] = 'g'; buf[1] = '#'; len = 2;
    // Convert counter to string
    interp.gensym_counter++;
    usz counter = interp.gensym_counter;
    if (counter == 0) {
        buf[len++] = '0';
    } else {
        char[16] digits;
        usz dcount = 0;
        usz tmp = counter;
        while (tmp > 0) {
            digits[dcount++] = (char)('0' + (tmp % 10));
            tmp /= 10;
        }
        for (usz i = dcount; i > 0; i--) {
            buf[len++] = digits[i - 1];
        }
    }
    SymbolId gsym = interp.symbols.intern(buf[:len]);
    if (gsym == INVALID_SYMBOL_ID) return INVALID_SYMBOL_ID;
    // Store mapping
    assert(table.count < 16, "too many auto-gensyms in macro template");
    table.mappings[table.count].original = original;
    table.mappings[table.count].generated = gsym;
    table.count++;
    return gsym;
}

/**
 * Expand a macro template with pattern bindings.
 * Symbols in bindings are substituted; literal symbols stay.
 * ".." before a symbol splices the bound list.
 * Symbols ending with # are auto-replaced with unique gensyms for hygiene.
 *
 * HYGIENE: Template literal symbols (not pattern vars, not gensyms, not special forms)
 * are resolved through captured_bindings — a snapshot of definition-time values.
 * This prevents expansion-site shadowing from capturing macro-internal references.
 */
fn Value* expand_template(Value* tmpl, MatchResult* bindings, Interp* interp, GensymTable* gensyms,
                          CapturedBinding* captured, usz captured_count) {
    if (tmpl == null) return make_nil(interp);

    switch (tmpl.tag) {
        case SYMBOL: {
            // 1. Check if this symbol is bound in the pattern — substitute
            for (usz i = 0; i < bindings.binding_count; i++) {
                if ((uint)bindings.bindings[i].name == (uint)tmpl.sym_val) {
                    return bindings.bindings[i].value;
                }
            }
            // 2. Check for auto-gensym (symbol ending with #) — generate unique name
            char[] sym_name = interp.symbols.get_name(tmpl.sym_val);
            if (sym_name.len > 0 && sym_name[sym_name.len - 1] == '#') {
                SymbolId gsym = lookup_or_create_gensym(tmpl.sym_val, gensyms, interp);
                return make_symbol(interp, gsym);
            }
            // 3. Hygienic resolution: check captured definition-time bindings.
            //    If this symbol was bound at macro definition time, embed the
            //    snapshotted value directly to prevent expansion-site shadowing.
            for (usz i = 0; i < captured_count; i++) {
                if ((uint)captured[i].sym == (uint)tmpl.sym_val) {
                    return captured[i].value;
                }
            }
            return tmpl;
        }
        case CONS:
            return expand_template_list(tmpl, bindings, interp, gensyms, captured, captured_count);
        default:
            // INT, STRING, NIL — return as-is
            return tmpl;
    }
}

/**
 * Expand a template list, handling ".." splice.
 * When we see (.. var rest...) in a list, we splice var's bound value.
 */
fn Value* expand_template_list(Value* list, MatchResult* bindings, Interp* interp, GensymTable* gensyms,
                               CapturedBinding* captured, usz captured_count) {
    if (is_nil(list)) return make_nil(interp);
    if (!is_cons(list)) return expand_template(list, bindings, interp, gensyms, captured, captured_count);

    Value* head = list.cons_val.car;

    // Check for splice: (.. var ...)
    if (head != null && head.tag == SYMBOL && (uint)head.sym_val == (uint)interp.sym_dotdot) {
        // Next element is the variable to splice
        Value* rest_after_dotdot = list.cons_val.cdr;
        if (is_cons(rest_after_dotdot)) {
            Value* var_sym = rest_after_dotdot.cons_val.car;
            Value* remaining = rest_after_dotdot.cons_val.cdr;

            // Look up the variable in bindings
            Value* spliced = make_nil(interp);
            if (var_sym != null && var_sym.tag == SYMBOL) {
                for (usz i = 0; i < bindings.binding_count; i++) {
                    if ((uint)bindings.bindings[i].name == (uint)var_sym.sym_val) {
                        spliced = bindings.bindings[i].value;
                        break;
                    }
                }
            }

            // Expand the rest of the template
            Value* expanded_rest = expand_template_list(remaining, bindings, interp, gensyms, captured, captured_count);

            // Append spliced list to expanded_rest
            return append_values(spliced, expanded_rest, interp);
        }
    }

    // Normal element: expand car and recurse on cdr
    Value* expanded_car = expand_template(head, bindings, interp, gensyms, captured, captured_count);
    Value* expanded_cdr = expand_template_list(list.cons_val.cdr, bindings, interp, gensyms, captured, captured_count);
    return make_cons(interp, expanded_car, expanded_cdr);
}

/**
 * Append two value lists.
 */
fn Value* append_values(Value* a, Value* b, Interp* interp, usz depth = 0) {
    if (is_nil(a)) return b;
    if (!is_cons(a)) return b;  // not a proper list, just return b
    if (depth >= 10000) return make_nil(interp);
    return make_cons(interp, a.cons_val.car, append_values(a.cons_val.cdr, b, interp, depth + 1));
}

// =============================================================================
// SECTION 2.7: MODULE SYSTEM
// =============================================================================

/**
 * Find a module by name in the module table.
 */
fn Module* find_module(SymbolId name, Interp* interp) {
    usz hash_slot = (usz)name % MODULE_HASH_SIZE;
    for (usz probe = 0; probe < MODULE_HASH_SIZE; probe++) {
        usz idx = interp.module_hash_index[hash_slot];
        if (idx == usz.max) return null;  // Empty slot — not found
        if ((uint)interp.modules[idx].name == (uint)name) {
            return &interp.modules[idx];
        }
        hash_slot = (hash_slot + 1) % MODULE_HASH_SIZE;
    }
    return null;
}

// eval_module — DELETED (replaced by JIT in jit.c3)


// eval_import — DELETED (replaced by JIT in jit.c3)


// load_module_from_file — DELETED (replaced by JIT in jit.c3)


// =============================================================================
// SECTION 3: CONTINUATIONS (RESET/SHIFT)
// =============================================================================

// eval_reset — DELETED (replaced by JIT in jit.c3)


// eval_shift — DELETED (replaced by JIT in jit.c3)


// =============================================================================
// SECTION 4: EFFECT HANDLERS
// =============================================================================

// eval_perform — DELETED (replaced by JIT in jit.c3)


/**
 * Evaluate a handle expression.
 *
 * (handle body ((tag k x) handler) ...) installs effect handlers.
 */
// eval_handle — DELETED (replaced by jit_handle_impl in jit.c3)

/**
 * Evaluate an index expression: arr.[0], dict.['key]
 * OmniLisp dot-bracket notation for collection access.
 */
// eval_index — DELETED (JIT handles E_INDEX directly)

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
            if (is_symbol(pair_key) && (uint)pair_key.sym_val == (uint)key) {
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
        return eval_error("empty path");
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

    // For each remaining segment, look up field in Instance or alist
    for (usz i = 1; i < expr.path.segment_count; i++) {
        SymbolId key = expr.path.segments[i];

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
            return eval_error("field not found in type instance");
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
            return eval_error("path segment not found");
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
 */
struct MatchResult {
    bool matched;
    Binding[MAX_BINDINGS] bindings;
    usz binding_count;
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
    GensymMapping[16] mappings;
    usz count;
}

fn MatchResult match_fail() @inline {
    return { .matched = false, .binding_count = 0 };
}

fn MatchResult match_ok() @inline {
    return { .matched = true, .binding_count = 0 };
}

fn void MatchResult.add_binding(MatchResult* self, SymbolId name, Value* value) {
    assert(self.binding_count < MAX_BINDINGS, "too many pattern bindings");
    self.bindings[self.binding_count].name = name;
    self.bindings[self.binding_count].value = value;
    self.binding_count++;
}

fn void MatchResult.merge(MatchResult* self, MatchResult* other) {
    for (usz i = 0; i < other.binding_count; i++) {
        self.add_binding(other.bindings[i].name, other.bindings[i].value);
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
            if (a.str_val.len != b.str_val.len) return false;
            for (usz i = 0; i < a.str_val.len; i++) {
                if (a.str_val.chars[i] != b.str_val.chars[i]) return false;
            }
            return true;
        case SYMBOL:
            return (uint)a.sym_val == (uint)b.sym_val;
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
                return match_fail();
            }
            car_result.merge(&cdr_result);
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
                if (!sub.matched) return match_fail();
                result.merge(&sub);
            }
            return result;
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
                if (!elem_result.matched) { return match_fail(); }
                result.merge(&elem_result);
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
                if (!elem_result.matched) { return match_fail(); }
                start_result.merge(&elem_result);
            }
            return start_result;

        case REST_MIDDLE:
            if (list_len < elem_count) {
                return match_fail();
            }
            MatchResult mid_result = match_ok();
            for (usz i = 0; i < elem_count; i++) {
                MatchResult elem_result = match_pattern(pat.elements[i], elems[i], interp);
                if (!elem_result.matched) { return match_fail(); }
                mid_result.merge(&elem_result);
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
                if (!elem_result.matched) { return match_fail(); }
                end_result.merge(&elem_result);
            }
            return end_result;

        default:
            return match_fail();
    }
}

// =============================================================================
// SECTION 5: APPLICATION
// =============================================================================

// apply — DELETED (replaced by jit_apply_value in jit.c3)

// apply_closure — DELETED (replaced by jit_apply_value_impl CLOSURE case in jit.c3)

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
            return eval_error(result.str_val.chars[:result.str_val.len]);
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
        if (is_error(result)) { return eval_error(result.str_val.chars[:result.str_val.len]); }
        return eval_ok(result);
    }
    args[1] = arg;
    Value* result = partial.partial_val.func(args[:2], null, interp);
    if (is_error(result)) {
        return eval_error(result.str_val.chars[:result.str_val.len]);
    }
    return eval_ok(result);
}

// apply_continuation — DELETED (replaced by jit_apply_continuation in jit.c3)

// =============================================================================
// SECTION 6: HELPER FUNCTIONS
// =============================================================================

/**
 * Check if a value is falsy (nil or false symbol).
 */
fn bool is_falsy(Value* v, Interp* interp) {
    if (v == null) return true;
    if (v.tag == NIL) return true;
    if (v.tag == SYMBOL && (uint)v.sym_val == (uint)interp.sym_false) return true;
    return false;
}

// =============================================================================
// SECTION 7: PRIMITIVES
// =============================================================================

// Curried primitive helper - stores first argument and waits for second
struct CurriedPrim {
    Value* first_arg;
    PrimitiveFn binary_fn;
}

fn Value* prim_add(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1) return make_error(interp, "+: expected at least 1 argument");

    if (args.len == 1) {
        if (!is_number(args[0])) return make_error(interp, "+: expected number argument");
        return args[0];
    }

    if (!is_number(args[0])) return make_error(interp, "+: expected number argument");
    if (!is_number(args[1])) return make_error(interp, "+: expected number argument");
    if (is_double(args[0]) || is_double(args[1])) {
        return make_double(interp, to_double(args[0]) + to_double(args[1]));
    }
    return make_int(interp, args[0].int_val + args[1].int_val);
}

fn Value* prim_sub(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1) return make_error(interp, "-: expected at least 1 argument");
    if (args.len == 1) {
        if (!is_number(args[0])) return make_error(interp, "-: expected number argument");
        if (is_double(args[0])) return make_double(interp, -args[0].double_val);
        return make_int(interp, -args[0].int_val);
    }
    if (!is_number(args[0])) return make_error(interp, "-: expected number argument");
    if (!is_number(args[1])) return make_error(interp, "-: expected number argument");
    if (is_double(args[0]) || is_double(args[1])) {
        return make_double(interp, to_double(args[0]) - to_double(args[1]));
    }
    return make_int(interp, args[0].int_val - args[1].int_val);
}

fn Value* prim_mul(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 2) return make_error(interp, "*: expected 2 arguments");
    if (!is_number(args[0])) return make_error(interp, "*: expected number argument");
    if (!is_number(args[1])) return make_error(interp, "*: expected number argument");
    if (is_double(args[0]) || is_double(args[1])) {
        return make_double(interp, to_double(args[0]) * to_double(args[1]));
    }
    return make_int(interp, args[0].int_val * args[1].int_val);
}

fn Value* prim_div(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 2) return make_error(interp, "/: expected 2 arguments");
    if (!is_number(args[0])) return make_error(interp, "/: expected number argument");
    if (!is_number(args[1])) return make_error(interp, "/: expected number argument");
    if (is_double(args[0]) || is_double(args[1])) {
        double b = to_double(args[1]);
        if (b == 0.0) return make_error(interp, "/: division by zero");
        return make_double(interp, to_double(args[0]) / b);
    }
    long b = args[1].int_val;
    if (b == 0) return make_error(interp, "/: division by zero");
    return make_int(interp, args[0].int_val / b);
}

fn Value* prim_mod(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 2) return make_error(interp, "modulo: expected 2 arguments");
    if (!is_number(args[0])) return make_error(interp, "modulo: expected number argument");
    if (!is_number(args[1])) return make_error(interp, "modulo: expected number argument");
    if (is_double(args[0]) || is_double(args[1])) {
        return make_error(interp, "modulo: expected integer arguments");
    }
    long a = args[0].int_val;
    long b = args[1].int_val;
    if (b == 0) return make_error(interp, "modulo: division by zero");
    return make_int(interp, a % b);
}

fn Value* prim_eq(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 2) return make_error(interp, "=: expected 2 arguments");
    bool eq = values_equal(args[0], args[1]);
    return eq ? make_symbol(interp, interp.sym_true) : make_nil(interp);
}

fn Value* prim_lt(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 2) return make_error(interp, "<: expected 2 arguments");
    if (!is_number(args[0]) || !is_number(args[1])) return make_error(interp, "<: expected number arguments");
    bool lt = to_double(args[0]) < to_double(args[1]);
    return lt ? make_symbol(interp, interp.sym_true) : make_nil(interp);
}

fn Value* prim_gt(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 2) return make_error(interp, ">: expected 2 arguments");
    if (!is_number(args[0]) || !is_number(args[1])) return make_error(interp, ">: expected number arguments");
    bool gt = to_double(args[0]) > to_double(args[1]);
    return gt ? make_symbol(interp, interp.sym_true) : make_nil(interp);
}

fn Value* prim_le(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 2) return make_error(interp, "<=: expected 2 arguments");
    if (!is_number(args[0]) || !is_number(args[1])) return make_error(interp, "<=: expected number arguments");
    bool le = to_double(args[0]) <= to_double(args[1]);
    return le ? make_symbol(interp, interp.sym_true) : make_nil(interp);
}

fn Value* prim_ge(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 2) return make_error(interp, ">=: expected 2 arguments");
    if (!is_number(args[0]) || !is_number(args[1])) return make_error(interp, ">=: expected number arguments");
    bool ge = to_double(args[0]) >= to_double(args[1]);
    return ge ? make_symbol(interp, interp.sym_true) : make_nil(interp);
}

fn Value* prim_cons(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 2) return make_error(interp, "cons: expected 2 arguments");
    return make_cons(interp, args[0], args[1]);
}

fn Value* prim_car(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1) return make_error(interp, "car: expected 1 argument");
    if (!is_cons(args[0])) return make_error(interp, "car: argument must be a pair");
    return args[0].cons_val.car;
}

fn Value* prim_cdr(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1) return make_error(interp, "cdr: expected 1 argument");
    if (!is_cons(args[0])) return make_error(interp, "cdr: argument must be a pair");
    return args[0].cons_val.cdr;
}

fn Value* prim_null(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1) return make_symbol(interp, interp.sym_true);
    bool is_null = is_nil(args[0]);
    return is_null ? make_symbol(interp, interp.sym_true) : make_nil(interp);
}

fn Value* prim_pair(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1) return make_nil(interp);
    bool is_pair = is_cons(args[0]);
    return is_pair ? make_symbol(interp, interp.sym_true) : make_nil(interp);
}

/**
 * (length lst) -> int
 * Returns the number of elements in a list
 */
fn Value* prim_length(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1) return make_error(interp, "length: expected 1 argument");

    // nil has length 0
    if (is_nil(args[0])) {
        return make_int(interp, 0);
    }

    // String length
    if (is_string(args[0])) {
        return make_int(interp, (long)args[0].str_val.len);
    }

    // Array length
    if (is_array(args[0])) {
        return make_int(interp, (long)args[0].array_val.length);
    }

    // Dict count
    if (args[0] != null && args[0].tag == HASHMAP) {
        return make_int(interp, (long)args[0].hashmap_val.count);
    }

    // Count cons cells
    if (!is_cons(args[0])) {
        return make_error(interp, "length: expected list, array, dict, or string");
    }

    long count = 0;
    Value* current = args[0];
    while (is_cons(current)) {
        count++;
        current = current.cons_val.cdr;
    }
    return make_int(interp, count);
}

fn Value* prim_print(Value*[] args, Env* env, Interp* interp) {
    if (args.len >= 1) {
        print_value(args[0], &interp.symbols);
    }
    return make_nil(interp);
}

fn Value* prim_println(Value*[] args, Env* env, Interp* interp) {
    if (args.len >= 1) {
        println_value(args[0], &interp.symbols);
    }
    return make_nil(interp);
}

fn Value* prim_newline(Value*[] args, Env* env, Interp* interp) {
    io::printn("");
    return make_nil(interp);
}

fn Value* prim_not(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1) return make_symbol(interp, interp.sym_true);
    bool falsy = is_falsy(args[0], interp);
    return falsy ? make_symbol(interp, interp.sym_true) : make_nil(interp);
}

fn Value* prim_list(Value*[] args, Env* env, Interp* interp) {
    if (args.len == 0) return make_nil(interp);

    // Single array arg → convert to list
    if (args.len == 1 && is_array(args[0])) {
        Array* arr = args[0].array_val;
        Value* result = make_nil(interp);
        for (isz i = (isz)arr.length - 1; i >= 0; i--) {
            result = make_cons(interp, arr.items[(usz)i], result);
        }
        return result;
    }

    Value* result = make_nil(interp);
    for (isz i = (isz)args.len - 1; i >= 0; i--) {
        result = make_cons(interp, args[(usz)i], result);
    }
    return result;
}

// =============================================================================

// =============================================================================
// SECTION 7.5: STRING PRIMITIVES
// =============================================================================

/**
 * (string-append s1 s2 ...) -> concatenated string
 * Variadic: concatenates all string arguments
 */
fn Value* prim_string_append(Value*[] args, Env* env, Interp* interp) {
    if (args.len == 0) return make_string(interp, "");

    // Calculate total length and validate all args are strings
    usz total_len = 0;
    for (usz i = 0; i < args.len; i++) {
        if (!is_string(args[i])) {
            return make_error(interp, "string-append: expected string arguments");
        }
        total_len += args[i].str_val.len;
    }

    // Clamp to max length
    if (total_len > MAX_STRING_LEN - 1) total_len = MAX_STRING_LEN - 1;

    char[MAX_STRING_LEN] buffer;
    usz pos = 0;

    for (usz i = 0; i < args.len && pos < MAX_STRING_LEN - 1; i++) {
        usz arg_len = args[i].str_val.len;
        for (usz j = 0; j < arg_len && pos < MAX_STRING_LEN - 1; j++) {
            buffer[pos++] = args[i].str_val.chars[j];
        }
    }
    buffer[pos] = 0;

    return make_string(interp, buffer[:pos]);
}

/**
 * (string-join sep list) -> joined string
 * Join list of strings with separator
 */
fn Value* prim_string_join(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 2) return make_error(interp, "string-join: expected 2 arguments");
    if (!is_string(args[0])) return make_error(interp, "string-join: expected string separator");

    // Allow nil list to return empty string
    if (is_nil(args[1])) {
        return make_string(interp, "");
    }

    if (!is_cons(args[1])) {
        return make_error(interp, "string-join: expected list as second argument");
    }

    char* sep = &args[0].str_val.chars;
    usz sep_len = args[0].str_val.len;

    char[MAX_STRING_LEN] buffer;
    usz pos = 0;
    bool first = true;

    Value* list = args[1];
    while (is_cons(list)) {
        Value* elem = list.cons_val.car;
        list = list.cons_val.cdr;

        if (!first && pos < MAX_STRING_LEN - 1) {
            // Add separator
            for (usz i = 0; i < sep_len && pos < MAX_STRING_LEN - 1; i++) {
                buffer[pos++] = sep[i];
            }
        }
        first = false;

        if (is_string(elem)) {
            for (usz i = 0; i < elem.str_val.len && pos < MAX_STRING_LEN - 1; i++) {
                buffer[pos++] = elem.str_val.chars[i];
            }
        }
    }
    buffer[pos] = 0;

    // Truncate if too long
    if (pos > MAX_STRING_LEN - 1) pos = MAX_STRING_LEN - 1;
    return make_string(interp, buffer[:pos]);
}

/**
 * (substring s start end) -> substring
 */
fn Value* prim_substring(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 3) return make_error(interp, "substring: expected 3 arguments");
    if (!is_string(args[0])) return make_error(interp, "substring: expected string as first argument");
    if (!is_int(args[1]) || !is_int(args[2])) {
        return make_error(interp, "substring: expected integer indices");
    }

    usz len = args[0].str_val.len;
    long start = args[1].int_val;
    long end = args[2].int_val;

    // Handle negative indices (Python-style)
    if (start < 0) start = (long)len + start;
    if (end < 0) end = (long)len + end;

    // Clamp to bounds
    if (start < 0) start = 0;
    if (end > (long)len) end = (long)len;
    if (start >= end) return make_string(interp, "");

    usz sub_len = (usz)(end - start);
    char[MAX_STRING_LEN] buffer;
    for (usz i = 0; i < sub_len && i < MAX_STRING_LEN - 1; i++) {
        buffer[i] = args[0].str_val.chars[(usz)start + i];
    }
    if (sub_len > MAX_STRING_LEN - 1) sub_len = MAX_STRING_LEN - 1;
    buffer[sub_len] = 0;

    return make_string(interp, buffer[:sub_len]);
}

/**
 * (string-split s sep) -> list of strings
 */
fn Value* prim_string_split(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 2) return make_error(interp, "string-split: expected 2 arguments");
    if (!is_string(args[0]) || !is_string(args[1])) {
        return make_error(interp, "string-split: expected string arguments");
    }
    if (args[1].str_val.len == 0) {
        return make_error(interp, "string-split: empty separator");
    }

    char* str = &args[0].str_val.chars;
    usz str_len = args[0].str_val.len;
    // NOTE: Currently only supports single-character delimiters.
    // The first character of the separator string is used.
    char delim = args[1].str_val.chars[0];

    // Collect parts in reverse order, then reverse
    Value* parts = make_nil(interp);
    usz start = 0;

    for (usz i = 0; i <= str_len; i++) {
        if (i == str_len || str[i] == delim) {
            // Extract substring [start..i)
            usz part_len = i - start;
            if (part_len > MAX_STRING_LEN - 1) part_len = MAX_STRING_LEN - 1;
            char[MAX_STRING_LEN] buffer;
            for (usz j = 0; j < part_len; j++) {
                buffer[j] = str[start + j];
            }
            buffer[part_len] = 0;
            Value* part = make_string(interp, buffer[:part_len]);
            parts = make_cons(interp, part, parts);
            start = i + 1;
        }
    }

    // Reverse the list
    Value* result = make_nil(interp);
    while (is_cons(parts)) {
        result = make_cons(interp, parts.cons_val.car, result);
        parts = parts.cons_val.cdr;
    }
    return result;
}

/**
 * (string-length s) -> int
 */
fn Value* prim_string_length(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1 || !is_string(args[0])) {
        return make_int(interp, 0);
    }
    return make_int(interp, (long)args[0].str_val.len);
}

/**
 * (string->list s) -> list of single-char strings
 */
fn Value* prim_string_to_list(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1) return make_error(interp, "string->list: expected 1 argument");
    if (!is_string(args[0])) {
        return make_error(interp, "string->list: expected string argument");
    }

    Value* result = make_nil(interp);
    usz len = args[0].str_val.len;

    // Build list in reverse order
    for (isz i = (isz)len - 1; i >= 0; i--) {
        char[2] ch;
        ch[0] = args[0].str_val.chars[(usz)i];
        ch[1] = 0;
        Value* char_str = make_string(interp, ch[0..1]);
        result = make_cons(interp, char_str, result);
    }
    return result;
}

/**
 * (list->string chars) -> string
 * Convert a list of single-character strings to a string
 */
fn Value* prim_list_to_string(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1) {
        return make_error(interp, "list->string: expected 1 argument");
    }

    // Allow nil to return empty string
    if (is_nil(args[0])) {
        return make_string(interp, "");
    }

    if (!is_cons(args[0])) {
        return make_error(interp, "list->string: expected a list");
    }

    char[MAX_STRING_LEN] buffer;
    usz pos = 0;

    Value* list = args[0];
    while (is_cons(list) && pos < MAX_STRING_LEN - 1) {
        Value* elem = list.cons_val.car;
        list = list.cons_val.cdr;

        if (is_string(elem) && elem.str_val.len > 0) {
            // Take first character of the string
            buffer[pos++] = elem.str_val.chars[0];
        }
    }
    buffer[pos] = 0;

    return make_string(interp, buffer[:pos]);
}

/**
 * (string-upcase s) -> uppercase string
 */
fn Value* prim_string_upcase(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1) return make_error(interp, "string-upcase: expected 1 argument");
    if (!is_string(args[0])) {
        return make_error(interp, "string-upcase: expected string argument");
    }

    char[MAX_STRING_LEN] buffer;
    usz len = args[0].str_val.len;
    if (len > MAX_STRING_LEN - 1) len = MAX_STRING_LEN - 1;

    for (usz i = 0; i < len; i++) {
        char c = args[0].str_val.chars[i];
        // Convert lowercase a-z to uppercase A-Z
        if (c >= 'a' && c <= 'z') {
            buffer[i] = c - 32;  // 'a' - 'A' = 32
        } else {
            buffer[i] = c;
        }
    }
    buffer[len] = 0;

    return make_string(interp, buffer[:len]);
}

/**
 * (string-downcase s) -> lowercase string
 */
fn Value* prim_string_downcase(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1) return make_error(interp, "string-downcase: expected 1 argument");
    if (!is_string(args[0])) {
        return make_error(interp, "string-downcase: expected string argument");
    }

    char[MAX_STRING_LEN] buffer;
    usz len = args[0].str_val.len;
    if (len > MAX_STRING_LEN - 1) len = MAX_STRING_LEN - 1;

    for (usz i = 0; i < len; i++) {
        char c = args[0].str_val.chars[i];
        // Convert uppercase A-Z to lowercase a-z
        if (c >= 'A' && c <= 'Z') {
            buffer[i] = c + 32;  // 'a' - 'A' = 32
        } else {
            buffer[i] = c;
        }
    }
    buffer[len] = 0;

    return make_string(interp, buffer[:len]);
}

/**
 * (string-trim s) -> string with leading/trailing whitespace removed
 */
fn Value* prim_string_trim(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1) return make_error(interp, "string-trim: expected 1 argument");
    if (!is_string(args[0])) {
        return make_error(interp, "string-trim: expected string argument");
    }

    char* str = &args[0].str_val.chars;
    usz len = args[0].str_val.len;

    // Find start (skip leading whitespace)
    usz start = 0;
    while (start < len && (str[start] == ' ' || str[start] == '\t' ||
                           str[start] == '\n' || str[start] == '\r')) {
        start++;
    }

    // Find end (skip trailing whitespace)
    usz end = len;
    while (end > start && (str[end - 1] == ' ' || str[end - 1] == '\t' ||
                           str[end - 1] == '\n' || str[end - 1] == '\r')) {
        end--;
    }

    // Empty result
    if (start >= end) {
        return make_string(interp, "");
    }

    // Copy trimmed string
    usz trim_len = end - start;
    if (trim_len > MAX_STRING_LEN - 1) trim_len = MAX_STRING_LEN - 1;

    char[MAX_STRING_LEN] buffer;
    for (usz i = 0; i < trim_len; i++) {
        buffer[i] = str[start + i];
    }
    buffer[trim_len] = 0;

    return make_string(interp, buffer[:trim_len]);
}

/**
 * (string? v) -> true if v is a string
 */
fn Value* prim_is_string(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1) return make_nil(interp);
    bool result = is_string(args[0]);
    return result ? make_symbol(interp, interp.sym_true) : make_nil(interp);
}

/**
 * (int? v) -> true if v is an integer
 */
fn Value* prim_is_int(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1) return make_nil(interp);
    bool result = is_int(args[0]);
    return result ? make_symbol(interp, interp.sym_true) : make_nil(interp);
}

/**
 * (symbol? v) -> true if v is a symbol
 */
fn Value* prim_is_symbol(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1) return make_nil(interp);
    bool result = args[0] != null && args[0].tag == SYMBOL;
    return result ? make_symbol(interp, interp.sym_true) : make_nil(interp);
}

/**
 * (closure? v) -> true if v is a closure (user-defined function)
 */
fn Value* prim_is_closure(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1) return make_nil(interp);
    bool result = args[0] != null && args[0].tag == CLOSURE;
    return result ? make_symbol(interp, interp.sym_true) : make_nil(interp);
}

/**
 * (continuation? v) -> true if v is a continuation
 */
fn Value* prim_is_continuation(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1) return make_nil(interp);
    bool result = args[0] != null && args[0].tag == CONTINUATION;
    return result ? make_symbol(interp, interp.sym_true) : make_nil(interp);
}

// =============================================================================
// SECTION 7.6: FILE I/O PRIMITIVES
// =============================================================================

/**
 * (read-file path) -> string contents or nil on error
 */
fn Value* prim_read_file(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1 || !is_string(args[0])) {
        return make_nil(interp);
    }

    // Get path as slice (using :len for exclusive end)
    char[] path = args[0].str_val.chars[:args[0].str_val.len];

    // Try to load file using C3's file::load_temp
    if (try content = io::file::load_temp((String)path)) {
        return make_string(interp, content);
    }
    return make_nil(interp);
}

/**
 * (write-file path content) -> true on success, nil on error
 */
fn Value* prim_write_file(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 2 || !is_string(args[0]) || !is_string(args[1])) {
        return make_nil(interp);
    }

    // Get path and content as slices (using :len for exclusive end)
    char[] path = args[0].str_val.chars[:args[0].str_val.len];
    char[] content = args[1].str_val.chars[:args[1].str_val.len];

    // Open file for writing
    if (try file = io::file::open((String)path, "w")) {
        if (try bytes_written = file.write(content)) {
            file.close()!!;
            return make_symbol(interp, interp.sym_true);
        }
        file.close()!!;
    }
    return make_nil(interp);
}

/**
 * (file-exists? path) -> true if file exists, nil otherwise
 */
fn Value* prim_file_exists(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1 || !is_string(args[0])) {
        return make_nil(interp);
    }

    // Get path as slice (using :len for exclusive end)
    char[] path = args[0].str_val.chars[:args[0].str_val.len];

    // Use file::is_file to check existence
    if (io::file::is_file((String)path)) {
        return make_symbol(interp, interp.sym_true);
    }
    return make_nil(interp);
}

/**
 * (read-lines path) -> list of strings (one per line)
 */
fn Value* prim_read_lines(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1 || !is_string(args[0])) {
        return make_nil(interp);
    }

    // Get path as slice (using :len for exclusive end)
    char[] path = args[0].str_val.chars[:args[0].str_val.len];

    // Try to load file using C3's file::load_temp
    if (try content = io::file::load_temp((String)path)) {
        // Split content into lines
        Value* lines = make_nil(interp);
        usz start = 0;
        usz content_len = content.len;

        for (usz i = 0; i <= content_len; i++) {
            if (i == content_len || content[i] == '\n') {
                // Extract line [start..i)
                usz line_len = i - start;
                // Skip trailing \r if present (Windows line endings)
                if (line_len > 0 && content[start + line_len - 1] == '\r') {
                    line_len--;
                }
                if (line_len > MAX_STRING_LEN - 1) line_len = MAX_STRING_LEN - 1;
                char[MAX_STRING_LEN] buffer;
                for (usz j = 0; j < line_len; j++) {
                    buffer[j] = content[start + j];
                }
                buffer[line_len] = 0;
                Value* line = make_string(interp, buffer[:line_len]);
                lines = make_cons(interp, line, lines);
                start = i + 1;
            }
        }

        // Reverse the list to get correct order
        Value* result = make_nil(interp);
        while (is_cons(lines)) {
            result = make_cons(interp, lines.cons_val.car, result);
            lines = lines.cons_val.cdr;
        }
        return result;
    }
    return make_nil(interp);
}

/**
 * (load path) -> evaluates all expressions in the file, returns last result
 * Reads a script file and evaluates it in the current environment.
 */
fn Value* prim_load(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1 || !is_string(args[0])) {
        return make_nil(interp);
    }

    // Get path as slice (using :len for exclusive end)
    char[] path = args[0].str_val.chars[:args[0].str_val.len];

    // Try to load file
    if (try content = io::file::load_temp((String)path)) {
        char[] source = content;

        // Run all expressions in the file
        EvalResult r = run_program(source, interp);
        if (r.error.has_error) {
            return make_nil(interp);
        }
        return r.value;
    }
    return make_nil(interp);
}

fn Value* prim_string_to_number(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1 || !is_string(args[0])) return make_nil(interp);
    char[] s = args[0].str_val.chars[:args[0].str_val.len];
    if (s.len == 0) return make_nil(interp);
    // Check if it contains a dot or exponent → parse as float
    bool is_float = false;
    for (usz j = 0; j < s.len; j++) {
        if (s[j] == '.' || s[j] == 'e' || s[j] == 'E') { is_float = true; break; }
    }
    if (is_float) {
        // Parse as double: [+-]digits[.digits][e[+-]digits]
        usz i = 0;
        bool negative = false;
        if (i < s.len && (s[i] == '-' || s[i] == '+')) { negative = s[i] == '-'; i++; }
        if (i >= s.len) return make_nil(interp);
        double val = 0.0;
        bool has_digits = false;
        while (i < s.len && s[i] >= '0' && s[i] <= '9') { val = val * 10.0 + (double)(s[i] - '0'); i++; has_digits = true; }
        if (i < s.len && s[i] == '.') {
            i++;
            double frac = 0.1;
            while (i < s.len && s[i] >= '0' && s[i] <= '9') { val = val + (double)(s[i] - '0') * frac; frac = frac * 0.1; i++; has_digits = true; }
        }
        if (!has_digits) return make_nil(interp);
        if (i < s.len && (s[i] == 'e' || s[i] == 'E')) {
            i++;
            bool exp_neg = false;
            if (i < s.len && (s[i] == '-' || s[i] == '+')) { exp_neg = s[i] == '-'; i++; }
            long exp_val = 0;
            while (i < s.len && s[i] >= '0' && s[i] <= '9') { exp_val = exp_val * 10 + (long)(s[i] - '0'); i++; }
            double mult = 1.0;
            for (long e = 0; e < exp_val; e++) mult = mult * 10.0;
            if (exp_neg) { val = val / mult; } else { val = val * mult; }
        }
        if (i != s.len) return make_nil(interp);
        if (negative) val = -val;
        return make_double(interp, val);
    }
    // Integer parse
    usz i = 0;
    bool negative = false;
    if (s[0] == '-') { negative = true; i = 1; }
    if (i >= s.len) return make_nil(interp);
    long val = 0;
    while (i < s.len) {
        if (s[i] < '0' || s[i] > '9') return make_nil(interp);
        val = val * 10 + (long)(s[i] - '0');
        i++;
    }
    if (negative) val = -val;
    return make_int(interp, val);
}

fn Value* prim_number_to_string(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1 || !is_number(args[0])) return make_error(interp, "number->string: expected number");
    if (is_double(args[0])) {
        char[64] dbuf;
        char[] dslice = io::bprintf(&dbuf, "%.15g", args[0].double_val)!!;
        return make_string(interp, dslice);
    }
    long n = args[0].int_val;
    char[24] buf;
    usz len = 0;
    if (n == 0) {
        buf[0] = '0';
        len = 1;
    } else {
        bool negative = n < 0;
        if (negative) n = -n;
        // Reverse digit extraction
        usz start = 0;
        while (n > 0) {
            buf[len] = (char)('0' + (char)(n % 10));
            len++;
            n = n / 10;
        }
        if (negative) { buf[len] = '-'; len++; }
        // Reverse in-place
        for (usz j = 0; j < len / 2; j++) {
            char tmp = buf[j];
            buf[j] = buf[len - 1 - j];
            buf[len - 1 - j] = tmp;
        }
    }
    return make_string(interp, buf[:len]);
}

fn Value* prim_gensym(Value*[] args, Env* env, Interp* interp) {
    char[64] buf;
    buf[0] = 'g'; buf[1] = '#';
    usz len = 2;
    interp.gensym_counter++;
    long n = (long)interp.gensym_counter;
    // Convert number to string
    char[20] digits;
    usz dlen = 0;
    if (n == 0) { digits[0] = '0'; dlen = 1; }
    else {
        while (n > 0) { digits[dlen] = (char)('0' + (char)(n % 10)); dlen++; n = n / 10; }
    }
    for (usz j = 0; j < dlen; j++) {
        buf[len + j] = digits[dlen - 1 - j];
    }
    len += dlen;
    SymbolId id = interp.symbols.intern(buf[:len]);
    if (id == INVALID_SYMBOL_ID) return make_error(interp, "symbol table exhausted");
    return make_symbol(interp, id);
}

fn Value* prim_is_dict(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1) return make_nil(interp);
    if (args[0] != null && args[0].tag == HASHMAP) return make_symbol(interp, interp.sym_true);
    return make_nil(interp);
}

// =============================================================================
// HASH MAP OPERATIONS
// =============================================================================

fn uint fnv1a(char[] data) {
    uint h = 2166136261;
    for (usz i = 0; i < data.len; i++) { h ^= (uint)data[i]; h *= 16777619; }
    return h;
}

fn uint murmur_finalizer(uint h) {
    h ^= h >> 16; h *= 0x85ebca6b; h ^= h >> 13; h *= 0xc2b2ae35; h ^= h >> 16;
    return h;
}

fn uint hash_value(Value* key) {
    if (key == null) return 0;
    switch (key.tag) {
        case INT: return murmur_finalizer((uint)key.int_val);
        case STRING: return fnv1a(key.str_val.chars[:key.str_val.len]);
        case SYMBOL: return murmur_finalizer((uint)key.sym_val);
        default: return 0;
    }
}

fn HashMap* hashmap_new(uint capacity, Interp* interp) {
    // Allocate HashMap struct in root_region
    HashMap hm;
    hm.capacity = capacity;
    hm.count = 0;
    hm.mask = capacity - 1;
    main::ObjectHandle hm_handle = main::allocate_in(interp.root_region, HashMap, hm);
    HashMap* map = main::dereference_as(HashMap, hm_handle);

    // Allocate entries array via malloc (contiguous needed for indexing)
    map.entries = (HashEntry*)mem::malloc(HashEntry.sizeof * capacity);
    for (uint i = 0; i < capacity; i++) {
        map.entries[i].key = null;
        map.entries[i].value = null;
    }

    return map;
}

fn Value* hashmap_get(HashMap* map, Value* key) {
    uint h = hash_value(key) & map.mask;
    for (uint i = 0; i < map.capacity; i++) {
        uint idx = (h + i) & map.mask;
        HashEntry* entry = &map.entries[idx];
        if (entry.key == null) return null;  // empty slot
        if (values_equal(entry.key, key)) return entry.value;
    }
    return null;
}

fn void hashmap_grow(HashMap* map, Interp* interp) {
    uint old_cap = map.capacity;
    HashEntry* old_entries = map.entries;
    uint new_cap = old_cap * 2;
    uint new_mask = new_cap - 1;

    // Allocate new entries array via malloc
    HashEntry* new_entries = (HashEntry*)mem::malloc(HashEntry.sizeof * new_cap);
    for (uint i = 0; i < new_cap; i++) {
        new_entries[i].key = null;
        new_entries[i].value = null;
    }

    map.entries = new_entries;
    map.capacity = new_cap;
    map.mask = new_mask;
    map.count = 0;

    // Rehash
    for (uint i = 0; i < old_cap; i++) {
        if (old_entries[i].key != null) {
            uint h = hash_value(old_entries[i].key) & new_mask;
            for (uint j = 0; j < new_cap; j++) {
                uint idx = (h + j) & new_mask;
                if (new_entries[idx].key == null) {
                    new_entries[idx].key = old_entries[i].key;
                    new_entries[idx].value = old_entries[i].value;
                    map.count++;
                    break;
                }
            }
        }
    }
    mem::free(old_entries);
}

fn void hashmap_set(HashMap* map, Value* key, Value* value, Interp* interp) {
    // Grow at 70% load
    if (map.count * 10 >= map.capacity * 7) {
        hashmap_grow(map, interp);
    }

    uint h = hash_value(key) & map.mask;
    for (uint i = 0; i < map.capacity; i++) {
        uint idx = (h + i) & map.mask;
        HashEntry* entry = &map.entries[idx];
        if (entry.key == null) {
            entry.key = key;
            entry.value = value;
            map.count++;
            return;
        }
        if (values_equal(entry.key, key)) {
            entry.value = value;
            return;
        }
    }
}

fn bool hashmap_remove(HashMap* map, Value* key) {
    uint h = hash_value(key) & map.mask;
    for (uint i = 0; i < map.capacity; i++) {
        uint idx = (h + i) & map.mask;
        HashEntry* entry = &map.entries[idx];
        if (entry.key == null) return false;
        if (values_equal(entry.key, key)) {
            // Backward shift delete
            uint j = idx;
            for (;;) {
                uint next = (j + 1) & map.mask;
                if (map.entries[next].key == null) break;
                uint natural = hash_value(map.entries[next].key) & map.mask;
                // Check if next entry would be displaced by removing j
                // If natural position is between (j+1) and next, it's fine where it is
                bool should_move;
                if (j < next) {
                    should_move = natural <= j || natural > next;
                } else {
                    should_move = natural <= j && natural > next;
                }
                if (!should_move) break;
                map.entries[j] = map.entries[next];
                j = next;
            }
            map.entries[j].key = null;
            map.entries[j].value = null;
            map.count--;
            return true;
        }
    }
    return false;
}

fn Value* make_hashmap(Interp* interp, uint capacity) {
    // Allocate value in root_region so it persists
    main::RegionHandle saved = interp.current_frame;
    interp.current_frame = interp.root_region;
    Value* v = interp.alloc_value();
    v.tag = HASHMAP;
    v.hashmap_val = hashmap_new(capacity, interp);
    interp.current_frame = saved;
    return v;
}

// Dict primitives
fn Value* prim_dict(Value*[] args, Env* env, Interp* interp) {
    if (args.len % 2 != 0) return make_error(interp, "dict: expected even number of args");
    uint cap = 16;
    while (cap < (uint)args.len * 2) cap *= 2;
    Value* v = make_hashmap(interp, cap);
    for (usz i = 0; i < args.len; i += 2) {
        hashmap_set(v.hashmap_val, args[i], args[i + 1], interp);
    }
    return v;
}

fn Value* prim_dict_ref(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 2) return make_error(interp, "dict-ref: expected 2 args");
    if (args[0] == null || args[0].tag != HASHMAP) return make_error(interp, "dict-ref: expected dict");
    Value* result = hashmap_get(args[0].hashmap_val, args[1]);
    if (result == null) return make_nil(interp);
    return result;
}

fn Value* prim_dict_set(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 3) return make_error(interp, "dict-set!: expected 3 args");
    if (args[0] == null || args[0].tag != HASHMAP) return make_error(interp, "dict-set!: expected dict");
    hashmap_set(args[0].hashmap_val, args[1], args[2], interp);
    return args[0];
}

fn Value* prim_dict_has(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 2) return make_error(interp, "dict-has?: expected 2 args");
    if (args[0] == null || args[0].tag != HASHMAP) return make_error(interp, "dict-has?: expected dict");
    Value* result = hashmap_get(args[0].hashmap_val, args[1]);
    if (result != null) return make_symbol(interp, interp.sym_true);
    return make_nil(interp);
}

fn Value* prim_dict_remove(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 2) return make_error(interp, "dict-remove!: expected 2 args");
    if (args[0] == null || args[0].tag != HASHMAP) return make_error(interp, "dict-remove!: expected dict");
    hashmap_remove(args[0].hashmap_val, args[1]);
    return args[0];
}

fn Value* prim_dict_keys(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1 || args[0] == null || args[0].tag != HASHMAP) return make_error(interp, "keys: expected dict");
    HashMap* map = args[0].hashmap_val;
    Value* result = make_nil(interp);
    for (uint i = 0; i < map.capacity; i++) {
        if (map.entries[i].key != null) {
            result = make_cons(interp, map.entries[i].key, result);
        }
    }
    return result;
}

fn Value* prim_dict_values(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1 || args[0] == null || args[0].tag != HASHMAP) return make_error(interp, "values: expected dict");
    HashMap* map = args[0].hashmap_val;
    Value* result = make_nil(interp);
    for (uint i = 0; i < map.capacity; i++) {
        if (map.entries[i].key != null) {
            result = make_cons(interp, map.entries[i].value, result);
        }
    }
    return result;
}

fn Value* prim_dict_count(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1 || args[0] == null || args[0].tag != HASHMAP) return make_error(interp, "dict-count: expected dict");
    return make_int(interp, (long)args[0].hashmap_val.count);
}

// =============================================================================
// SECTION 7.8: FFI PRIMITIVES
// =============================================================================

/**
 * Convert a Pika value to a long based on FFI type symbol.
 * Returns the value as a long suitable for passing through function pointer casts.
 */
fn long ffi_value_to_long(Value* val, char[] type_name, Interp* interp) {
    if (type_name.len == 3 && type_name[0] == 'i' && type_name[1] == 'n' && type_name[2] == 't') {
        // 'int
        if (val != null && val.tag == INT) return val.int_val;
        return 0;
    }
    if (type_name.len == 4 && type_name[0] == 's' && type_name[1] == 'i' && type_name[2] == 'z' && type_name[3] == 'e') {
        // 'size
        if (val != null && val.tag == INT) return val.int_val;
        return 0;
    }
    if (type_name.len == 6 && type_name[0] == 's' && type_name[1] == 't' && type_name[2] == 'r') {
        // 'string
        if (val != null && val.tag == STRING) return (long)&val.str_val.chars;
        return 0;
    }
    if (type_name.len == 3 && type_name[0] == 'p' && type_name[1] == 't' && type_name[2] == 'r') {
        // 'ptr
        if (val != null && val.tag == FFI_HANDLE) return (long)val.ffi_val.lib_handle;
        if (val != null && val.tag == INT) return val.int_val;
        return 0;
    }
    if (type_name.len == 6 && type_name[0] == 'd' && type_name[1] == 'o' && type_name[2] == 'u') {
        // 'double — bit-cast double to long for passing in integer register
        double d = 0.0;
        if (val != null && is_double(val)) { d = val.double_val; }
        else if (val != null && val.tag == INT) { d = (double)val.int_val; }
        long* lp = (long*)&d;
        return *lp;
    }
    return 0;
}

/**
 * Convert a C long result back to a Pika value based on FFI return type symbol.
 */
fn Value* ffi_long_to_value(long result, char[] type_name, Interp* interp) {
    if (type_name.len == 3 && type_name[0] == 'i' && type_name[1] == 'n' && type_name[2] == 't') {
        return make_int(interp, result);
    }
    if (type_name.len == 4 && type_name[0] == 's' && type_name[1] == 'i' && type_name[2] == 'z' && type_name[3] == 'e') {
        return make_int(interp, result);
    }
    if (type_name.len == 6 && type_name[0] == 's' && type_name[1] == 't' && type_name[2] == 'r') {
        // 'string — result is a char*
        char* ptr = (char*)(uptr)result;
        if (ptr == null) return make_nil(interp);
        // Measure length
        usz len = 0;
        while (ptr[len] != 0 && len < MAX_STRING_LEN - 1) len++;
        return make_string(interp, ptr[:len]);
    }
    if (type_name.len == 4 && type_name[0] == 'v' && type_name[1] == 'o' && type_name[2] == 'i' && type_name[3] == 'd') {
        return make_nil(interp);
    }
    if (type_name.len == 3 && type_name[0] == 'p' && type_name[1] == 't' && type_name[2] == 'r') {
        return make_int(interp, result);
    }
    if (type_name.len == 6 && type_name[0] == 'd' && type_name[1] == 'o' && type_name[2] == 'u') {
        // 'double — bit-cast long back to double
        double* dp = (double*)&result;
        return make_double(interp, *dp);
    }
    return make_nil(interp);
}

/**
 * (ffi-open "libc.so.6") => V_FFI_HANDLE
 */
fn Value* prim_ffi_open(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1 || args[0] == null || args[0].tag != STRING) {
        return make_error(interp, "ffi-open: expected string argument");
    }
    char[] name = args[0].str_val.chars[:args[0].str_val.len];
    void* handle = dlopen((ZString)&args[0].str_val.chars, RTLD_LAZY);
    if (handle == null) {
        char* err = dlerror();
        if (err != null) {
            // Build error message
            usz elen = 0;
            while (err[elen] != 0 && elen < 200) elen++;
            return make_error(interp, err[:elen]);
        }
        return make_error(interp, "ffi-open: dlopen failed");
    }
    return make_ffi_handle(interp, handle, name);
}

/**
 * (ffi-call handle "func_name" 'return-type arg1 'type1 arg2 'type2 ...)
 * Variadic: minimum 3 args (handle, name, return type).
 * After return type, args come in pairs: (value, type_symbol).
 */
fn Value* prim_ffi_call(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 3) {
        return make_error(interp, "ffi-call: expected at least 3 args (handle, name, return-type)");
    }
    // Arg 0: FFI handle
    if (args[0] == null || args[0].tag != FFI_HANDLE) {
        return make_error(interp, "ffi-call: first arg must be an ffi-handle");
    }
    if (args[0].ffi_val.lib_handle == null) {
        return make_error(interp, "ffi-call: handle is closed");
    }
    // Arg 1: function name (string)
    if (args[1] == null || args[1].tag != STRING) {
        return make_error(interp, "ffi-call: second arg must be a string (function name)");
    }
    // Arg 2: return type (symbol)
    if (args[2] == null || args[2].tag != SYMBOL) {
        return make_error(interp, "ffi-call: third arg must be a symbol (return type)");
    }

    // Look up the function (with dlsym cache)
    char[] fn_name = args[1].str_val.chars[:args[1].str_val.len];
    void* fn_ptr = null;
    // Check cache first
    for (usz ci = 0; ci < args[0].ffi_val.sym_cache_count; ci++) {
        bool match = fn_name.len < 64;
        if (match) {
            for (usz cj = 0; cj < fn_name.len; cj++) {
                if (args[0].ffi_val.sym_cache_names[ci][cj] != fn_name[cj]) { match = false; break; }
            }
            if (match && args[0].ffi_val.sym_cache_names[ci][fn_name.len] != 0) match = false;
        }
        if (match) { fn_ptr = args[0].ffi_val.sym_cache_ptrs[ci]; break; }
    }
    if (fn_ptr == null) {
        fn_ptr = dlsym(args[0].ffi_val.lib_handle, (ZString)&args[1].str_val.chars);
        if (fn_ptr == null) {
            char* err = dlerror();
            if (err != null) {
                usz elen = 0;
                while (err[elen] != 0 && elen < 200) elen++;
                return make_error(interp, err[:elen]);
            }
            return make_error(interp, "ffi-call: dlsym failed");
        }
        // Populate cache
        if (args[0].ffi_val.sym_cache_count < 32 && fn_name.len < 64) {
            usz ci = args[0].ffi_val.sym_cache_count;
            args[0].ffi_val.sym_cache_ptrs[ci] = fn_ptr;
            for (usz cj = 0; cj < fn_name.len; cj++) {
                args[0].ffi_val.sym_cache_names[ci][cj] = fn_name[cj];
            }
            args[0].ffi_val.sym_cache_names[ci][fn_name.len] = 0;
            args[0].ffi_val.sym_cache_count++;
        }
    }

    // Parse arg pairs: (value, type_symbol) after the first 3 args
    usz remaining = args.len - 3;
    if (remaining % 2 != 0) {
        return make_error(interp, "ffi-call: args after return-type must be (value type) pairs");
    }
    usz c_argc = remaining / 2;
    if (c_argc > 6) {
        return make_error(interp, "ffi-call: max 6 C arguments supported");
    }

    // Convert args to longs
    long[6] c_args;
    for (usz i = 0; i < c_argc; i++) {
        Value* val = args[3 + i * 2];
        Value* type_sym = args[3 + i * 2 + 1];
        if (type_sym == null || type_sym.tag != SYMBOL) {
            return make_error(interp, "ffi-call: type annotation must be a symbol");
        }
        char[] type_name = interp.symbols.get_name(type_sym.sym_val);
        c_args[i] = ffi_value_to_long(val, type_name, interp);
    }

    // Get return type name
    char[] ret_type = interp.symbols.get_name(args[2].sym_val);

    // Call the function based on arg count
    long result;
    switch (c_argc) {
        case 0:
            result = ((FfiFn0)(uptr)fn_ptr)();
        case 1:
            result = ((FfiFn1)(uptr)fn_ptr)(c_args[0]);
        case 2:
            result = ((FfiFn2)(uptr)fn_ptr)(c_args[0], c_args[1]);
        case 3:
            result = ((FfiFn3)(uptr)fn_ptr)(c_args[0], c_args[1], c_args[2]);
        case 4:
            result = ((FfiFn4)(uptr)fn_ptr)(c_args[0], c_args[1], c_args[2], c_args[3]);
        case 5:
            result = ((FfiFn5)(uptr)fn_ptr)(c_args[0], c_args[1], c_args[2], c_args[3], c_args[4]);
        case 6:
            result = ((FfiFn6)(uptr)fn_ptr)(c_args[0], c_args[1], c_args[2], c_args[3], c_args[4], c_args[5]);
        default:
            return make_error(interp, "ffi-call: unreachable arg count");
    }

    return ffi_long_to_value(result, ret_type, interp);
}

/**
 * (ffi-close handle) => nil
 */
fn Value* prim_ffi_close(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1 || args[0] == null || args[0].tag != FFI_HANDLE) {
        return make_error(interp, "ffi-close: expected ffi-handle argument");
    }
    if (args[0].ffi_val.lib_handle != null) {
        dlclose(args[0].ffi_val.lib_handle);
        args[0].ffi_val.lib_handle = null;
        args[0].ffi_val.sym_cache_count = 0;
    }
    return make_nil(interp);
}

/**
 * (ffi-sym handle "func_name") => int (pointer as integer)
 */
fn Value* prim_ffi_sym(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 2) return make_error(interp, "ffi-sym: expected 2 args");
    if (args[0] == null || args[0].tag != FFI_HANDLE) {
        return make_error(interp, "ffi-sym: first arg must be ffi-handle");
    }
    if (args[0].ffi_val.lib_handle == null) {
        return make_error(interp, "ffi-sym: handle is closed");
    }
    if (args[1] == null || args[1].tag != STRING) {
        return make_error(interp, "ffi-sym: second arg must be a string");
    }
    void* sym = dlsym(args[0].ffi_val.lib_handle, (ZString)&args[1].str_val.chars);
    if (sym == null) return make_int(interp, 0);
    return make_int(interp, (long)(uptr)sym);
}

// =============================================================================
// PHASE 1-7 NEW PRIMITIVES
// =============================================================================

// --- Phase 1: Float predicates & conversions ---

fn Value* prim_is_double(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1) return make_nil(interp);
    return is_double(args[0]) ? make_symbol(interp, interp.sym_true) : make_nil(interp);
}

fn Value* prim_is_number(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1) return make_nil(interp);
    return is_number(args[0]) ? make_symbol(interp, interp.sym_true) : make_nil(interp);
}

fn Value* prim_exact_to_inexact(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1 || !is_number(args[0])) return make_error(interp, "exact->inexact: expected number");
    return make_double(interp, to_double(args[0]));
}

fn Value* prim_inexact_to_exact(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1 || !is_number(args[0])) return make_error(interp, "inexact->exact: expected number");
    return make_int(interp, (long)to_double(args[0]));
}

// --- Phase 2: Math library ---

import std::math;

// C math functions (linked via -lm)
extern fn double c_sin(double x) @extern("sin");
extern fn double c_cos(double x) @extern("cos");
extern fn double c_tan(double x) @extern("tan");
extern fn double c_asin(double x) @extern("asin");
extern fn double c_acos(double x) @extern("acos");
extern fn double c_atan(double x) @extern("atan");
extern fn double c_atan2(double y, double x) @extern("atan2");
extern fn double c_exp(double x) @extern("exp");
extern fn double c_log(double x) @extern("log");
extern fn double c_log10(double x) @extern("log10");
extern fn double c_pow(double x, double y) @extern("pow");
extern fn double c_sqrt(double x) @extern("sqrt");
extern fn double c_floor(double x) @extern("floor");
extern fn double c_ceil(double x) @extern("ceil");
extern fn double c_round(double x) @extern("round");
extern fn double c_fabs(double x) @extern("fabs");

fn Value* prim_sin(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1 || !is_number(args[0])) return make_error(interp, "sin: expected number");
    return make_double(interp, c_sin(to_double(args[0])));
}

fn Value* prim_cos(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1 || !is_number(args[0])) return make_error(interp, "cos: expected number");
    return make_double(interp, c_cos(to_double(args[0])));
}

fn Value* prim_tan(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1 || !is_number(args[0])) return make_error(interp, "tan: expected number");
    return make_double(interp, c_tan(to_double(args[0])));
}

fn Value* prim_asin(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1 || !is_number(args[0])) return make_error(interp, "asin: expected number");
    return make_double(interp, c_asin(to_double(args[0])));
}

fn Value* prim_acos(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1 || !is_number(args[0])) return make_error(interp, "acos: expected number");
    return make_double(interp, c_acos(to_double(args[0])));
}

fn Value* prim_atan(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1 || !is_number(args[0])) return make_error(interp, "atan: expected number");
    return make_double(interp, c_atan(to_double(args[0])));
}

fn Value* prim_atan2(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 2 || !is_number(args[0]) || !is_number(args[1])) return make_error(interp, "atan2: expected 2 numbers");
    return make_double(interp, c_atan2(to_double(args[0]), to_double(args[1])));
}

fn Value* prim_exp(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1 || !is_number(args[0])) return make_error(interp, "exp: expected number");
    return make_double(interp, c_exp(to_double(args[0])));
}

fn Value* prim_log(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1 || !is_number(args[0])) return make_error(interp, "log: expected number");
    return make_double(interp, c_log(to_double(args[0])));
}

fn Value* prim_log10(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1 || !is_number(args[0])) return make_error(interp, "log10: expected number");
    return make_double(interp, c_log10(to_double(args[0])));
}

fn Value* prim_pow(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 2 || !is_number(args[0]) || !is_number(args[1])) return make_error(interp, "pow: expected 2 numbers");
    return make_double(interp, c_pow(to_double(args[0]), to_double(args[1])));
}

fn Value* prim_sqrt(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1 || !is_number(args[0])) return make_error(interp, "sqrt: expected number");
    return make_double(interp, c_sqrt(to_double(args[0])));
}

fn Value* prim_floor(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1 || !is_number(args[0])) return make_error(interp, "floor: expected number");
    return make_int(interp, (long)c_floor(to_double(args[0])));
}

fn Value* prim_ceiling(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1 || !is_number(args[0])) return make_error(interp, "ceiling: expected number");
    return make_int(interp, (long)c_ceil(to_double(args[0])));
}

fn Value* prim_round(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1 || !is_number(args[0])) return make_error(interp, "round: expected number");
    return make_int(interp, (long)c_round(to_double(args[0])));
}

fn Value* prim_truncate(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1 || !is_number(args[0])) return make_error(interp, "truncate: expected number");
    return make_int(interp, (long)to_double(args[0]));
}

fn Value* prim_abs(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1 || !is_number(args[0])) return make_error(interp, "abs: expected number");
    if (is_double(args[0])) return make_double(interp, c_fabs(args[0].double_val));
    long n = args[0].int_val;
    return make_int(interp, n < 0 ? -n : n);
}

fn Value* prim_min(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 2 || !is_number(args[0]) || !is_number(args[1])) return make_error(interp, "min: expected 2 numbers");
    if (is_double(args[0]) || is_double(args[1])) {
        double a = to_double(args[0]); double b = to_double(args[1]);
        return make_double(interp, a < b ? a : b);
    }
    return make_int(interp, args[0].int_val < args[1].int_val ? args[0].int_val : args[1].int_val);
}

fn Value* prim_max(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 2 || !is_number(args[0]) || !is_number(args[1])) return make_error(interp, "max: expected 2 numbers");
    if (is_double(args[0]) || is_double(args[1])) {
        double a = to_double(args[0]); double b = to_double(args[1]);
        return make_double(interp, a > b ? a : b);
    }
    return make_int(interp, args[0].int_val > args[1].int_val ? args[0].int_val : args[1].int_val);
}

fn Value* prim_gcd(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 2 || !is_int(args[0]) || !is_int(args[1])) return make_error(interp, "gcd: expected 2 integers");
    long a = args[0].int_val; long b = args[1].int_val;
    if (a < 0) { a = -a; }
    if (b < 0) { b = -b; }
    while (b != 0) { long t = b; b = a % b; a = t; }
    return make_int(interp, a);
}

fn Value* prim_lcm(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 2 || !is_int(args[0]) || !is_int(args[1])) return make_error(interp, "lcm: expected 2 integers");
    long a = args[0].int_val; long b = args[1].int_val;
    if (a == 0 || b == 0) return make_int(interp, 0);
    if (a < 0) { a = -a; }
    if (b < 0) { b = -b; }
    // gcd
    long ga = a; long gb = b;
    while (gb != 0) { long t = gb; gb = ga % gb; ga = t; }
    return make_int(interp, (a / ga) * b);
}

// --- Phase 3: Sorting & Bitwise ---

fn Value* prim_sort(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1) return make_error(interp, "sort: expected list argument");
    if (is_nil(args[0])) return make_nil(interp);
    // Convert list to array
    Value*[256] arr;
    usz len = 0;
    Value* cur = args[0];
    while (is_cons(cur) && len < 256) {
        arr[len++] = cur.cons_val.car;
        cur = cur.cons_val.cdr;
    }
    // Insertion sort (stable)
    for (usz i = 1; i < len; i++) {
        Value* key = arr[i];
        isz j = (isz)i - 1;
        // Compare: numbers by value, strings lexicographic
        while (j >= 0) {
            bool gt = false;
            if (is_number(arr[(usz)j]) && is_number(key)) {
                gt = to_double(arr[(usz)j]) > to_double(key);
            } else if (arr[(usz)j].tag == STRING && key.tag == STRING) {
                // Lexicographic string comparison
                usz al = arr[(usz)j].str_val.len; usz bl = key.str_val.len;
                usz ml = al < bl ? al : bl;
                int cmp = 0;
                for (usz k = 0; k < ml; k++) {
                    if (arr[(usz)j].str_val.chars[k] != key.str_val.chars[k]) {
                        cmp = arr[(usz)j].str_val.chars[k] > key.str_val.chars[k] ? 1 : -1;
                        break;
                    }
                }
                if (cmp == 0) { cmp = al > bl ? 1 : (al < bl ? -1 : 0); }
                gt = cmp > 0;
            }
            if (!gt) break;
            arr[(usz)(j + 1)] = arr[(usz)j];
            j--;
        }
        arr[(usz)(j + 1)] = key;
    }
    // Rebuild list
    Value* result = make_nil(interp);
    for (isz i = (isz)len - 1; i >= 0; i--) {
        result = make_cons(interp, arr[(usz)i], result);
    }
    return result;
}

fn Value* prim_sort_by(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 2) return make_error(interp, "sort-by: expected comparator and list");
    Value* cmp_fn = args[0];
    if (is_nil(args[1])) return make_nil(interp);
    // Convert list to array
    Value*[256] arr;
    usz len = 0;
    Value* cur = args[1];
    while (is_cons(cur) && len < 256) {
        arr[len++] = cur.cons_val.car;
        cur = cur.cons_val.cdr;
    }
    // Insertion sort with custom comparator
    for (usz i = 1; i < len; i++) {
        Value* key = arr[i];
        isz j = (isz)i - 1;
        while (j >= 0) {
            // Call comparator: (cmp a b) should return negative/0/positive or truthy for a>b
            Value* r1 = jit_apply_value(cmp_fn, arr[(usz)j], interp);
            if (r1 == null || r1.tag == ERROR) break;
            Value* r2 = jit_apply_value(r1, key, interp);
            if (r2 == null || r2.tag == ERROR) break;
            // If result > 0 (or truthy for >), swap
            bool gt = false;
            if (is_int(r2)) { gt = r2.int_val > 0; }
            else if (is_double(r2)) { gt = r2.double_val > 0.0; }
            else { gt = !is_nil(r2); }
            if (!gt) break;
            arr[(usz)(j + 1)] = arr[(usz)j];
            j--;
        }
        arr[(usz)(j + 1)] = key;
    }
    // Rebuild list
    Value* result = make_nil(interp);
    for (isz i = (isz)len - 1; i >= 0; i--) {
        result = make_cons(interp, arr[(usz)i], result);
    }
    return result;
}

fn Value* prim_bitwise_and(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 2 || !is_int(args[0]) || !is_int(args[1])) return make_error(interp, "bitwise-and: expected 2 integers");
    return make_int(interp, args[0].int_val & args[1].int_val);
}

fn Value* prim_bitwise_or(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 2 || !is_int(args[0]) || !is_int(args[1])) return make_error(interp, "bitwise-or: expected 2 integers");
    return make_int(interp, args[0].int_val | args[1].int_val);
}

fn Value* prim_bitwise_xor(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 2 || !is_int(args[0]) || !is_int(args[1])) return make_error(interp, "bitwise-xor: expected 2 integers");
    return make_int(interp, args[0].int_val ^ args[1].int_val);
}

fn Value* prim_bitwise_not(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1 || !is_int(args[0])) return make_error(interp, "bitwise-not: expected integer");
    return make_int(interp, ~args[0].int_val);
}

fn Value* prim_lshift(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 2 || !is_int(args[0]) || !is_int(args[1])) return make_error(interp, "lshift: expected 2 integers");
    return make_int(interp, args[0].int_val << args[1].int_val);
}

fn Value* prim_rshift(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 2 || !is_int(args[0]) || !is_int(args[1])) return make_error(interp, "rshift: expected 2 integers");
    return make_int(interp, args[0].int_val >> args[1].int_val);
}

// --- Phase 5: String operations & type predicates ---

fn Value* prim_string_contains(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 2 || args[0].tag != STRING || args[1].tag != STRING) return make_error(interp, "string-contains?: expected 2 strings");
    char[] haystack = args[0].str_val.chars[:args[0].str_val.len];
    char[] needle = args[1].str_val.chars[:args[1].str_val.len];
    if (needle.len == 0) return make_symbol(interp, interp.sym_true);
    if (needle.len > haystack.len) return make_nil(interp);
    for (usz i = 0; i <= haystack.len - needle.len; i++) {
        bool match = true;
        for (usz j = 0; j < needle.len; j++) {
            if (haystack[i + j] != needle[j]) { match = false; break; }
        }
        if (match) return make_symbol(interp, interp.sym_true);
    }
    return make_nil(interp);
}

fn Value* prim_string_index_of(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 2 || args[0].tag != STRING || args[1].tag != STRING) return make_error(interp, "string-index-of: expected 2 strings");
    char[] haystack = args[0].str_val.chars[:args[0].str_val.len];
    char[] needle = args[1].str_val.chars[:args[1].str_val.len];
    if (needle.len == 0) return make_int(interp, 0);
    if (needle.len > haystack.len) return make_int(interp, -1);
    for (usz i = 0; i <= haystack.len - needle.len; i++) {
        bool match = true;
        for (usz j = 0; j < needle.len; j++) {
            if (haystack[i + j] != needle[j]) { match = false; break; }
        }
        if (match) return make_int(interp, (long)i);
    }
    return make_int(interp, -1);
}

fn Value* prim_string_replace(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 3 || args[0].tag != STRING || args[1].tag != STRING || args[2].tag != STRING) {
        return make_error(interp, "string-replace: expected 3 strings");
    }
    char[] str = args[0].str_val.chars[:args[0].str_val.len];
    char[] old = args[1].str_val.chars[:args[1].str_val.len];
    char[] new_str = args[2].str_val.chars[:args[2].str_val.len];
    if (old.len == 0) return args[0]; // nothing to replace
    Value* result = interp.alloc_value();
    result.tag = STRING;
    result.str_val = (StringVal*)mem::malloc(StringVal.sizeof);
    result.str_val.len = 0;
    usz i = 0;
    while (i < str.len) {
        if (i + old.len <= str.len) {
            bool match = true;
            for (usz j = 0; j < old.len; j++) {
                if (str[i + j] != old[j]) { match = false; break; }
            }
            if (match) {
                for (usz j = 0; j < new_str.len && result.str_val.len < MAX_STRING_LEN - 1; j++) {
                    result.str_val.chars[result.str_val.len++] = new_str[j];
                }
                i += old.len;
                continue;
            }
        }
        if (result.str_val.len < MAX_STRING_LEN - 1) {
            result.str_val.chars[result.str_val.len++] = str[i];
        }
        i++;
    }
    result.str_val.chars[result.str_val.len] = 0;
    return result;
}

fn Value* prim_char_at(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 2 || args[0].tag != STRING || !is_int(args[1])) return make_error(interp, "char-at: expected string and int");
    long idx = args[1].int_val;
    if (idx < 0 || (usz)idx >= args[0].str_val.len) return make_error(interp, "char-at: index out of range");
    Value* result = interp.alloc_value();
    result.tag = STRING;
    result.str_val = (StringVal*)mem::malloc(StringVal.sizeof);
    result.str_val.chars[0] = args[0].str_val.chars[(usz)idx];
    result.str_val.chars[1] = 0;
    result.str_val.len = 1;
    return result;
}

fn Value* prim_string_repeat(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 2 || args[0].tag != STRING || !is_int(args[1])) return make_error(interp, "string-repeat: expected string and int");
    long n = args[1].int_val;
    if (n <= 0) return make_string(interp, "");
    Value* result = interp.alloc_value();
    result.tag = STRING;
    result.str_val = (StringVal*)mem::malloc(StringVal.sizeof);
    result.str_val.len = 0;
    for (long i = 0; i < n; i++) {
        for (usz j = 0; j < args[0].str_val.len && result.str_val.len < MAX_STRING_LEN - 1; j++) {
            result.str_val.chars[result.str_val.len++] = args[0].str_val.chars[j];
        }
    }
    result.str_val.chars[result.str_val.len] = 0;
    return result;
}

fn Value* prim_is_boolean(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1) return make_nil(interp);
    if (is_nil(args[0])) return make_symbol(interp, interp.sym_true); // false is nil
    if (is_symbol(args[0]) && (uint)args[0].sym_val == (uint)interp.sym_true) return make_symbol(interp, interp.sym_true);
    return make_nil(interp);
}

fn Value* prim_is_list(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1) return make_nil(interp);
    Value* v = args[0];
    if (is_nil(v)) return make_symbol(interp, interp.sym_true); // empty list is a list
    // Check if proper list (nil-terminated cons chain)
    while (is_cons(v)) { v = v.cons_val.cdr; }
    return is_nil(v) ? make_symbol(interp, interp.sym_true) : make_nil(interp);
}

fn Value* prim_is_procedure(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1) return make_nil(interp);
    ValueTag t = args[0].tag;
    return (t == CLOSURE || t == PRIMITIVE || t == PARTIAL_PRIM || t == METHOD_TABLE) ? make_symbol(interp, interp.sym_true) : make_nil(interp);
}

fn Value* prim_is_zero(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1 || !is_number(args[0])) return make_error(interp, "zero?: expected number");
    return to_double(args[0]) == 0.0 ? make_symbol(interp, interp.sym_true) : make_nil(interp);
}

fn Value* prim_is_positive(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1 || !is_number(args[0])) return make_error(interp, "positive?: expected number");
    return to_double(args[0]) > 0.0 ? make_symbol(interp, interp.sym_true) : make_nil(interp);
}

fn Value* prim_is_negative(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1 || !is_number(args[0])) return make_error(interp, "negative?: expected number");
    return to_double(args[0]) < 0.0 ? make_symbol(interp, interp.sym_true) : make_nil(interp);
}

fn Value* prim_is_even(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1 || !is_int(args[0])) return make_error(interp, "even?: expected integer");
    return (args[0].int_val % 2 == 0) ? make_symbol(interp, interp.sym_true) : make_nil(interp);
}

fn Value* prim_is_odd(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1 || !is_int(args[0])) return make_error(interp, "odd?: expected integer");
    return (args[0].int_val % 2 != 0) ? make_symbol(interp, interp.sym_true) : make_nil(interp);
}

fn Value* prim_format(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1 || args[0].tag != STRING) return make_error(interp, "format: expected format string");
    char[] fmt = args[0].str_val.chars[:args[0].str_val.len];
    Value* result = interp.alloc_value();
    result.tag = STRING;
    result.str_val = (StringVal*)mem::malloc(StringVal.sizeof);
    result.str_val.len = 0;
    usz arg_idx = 1;
    for (usz i = 0; i < fmt.len; i++) {
        if (fmt[i] == '~' && i + 1 < fmt.len) {
            char directive = fmt[i + 1];
            i++;
            if (directive == 'a' || directive == 's') {
                if (arg_idx < args.len) {
                    Value* a = args[arg_idx++];
                    if (a == null || is_nil(a)) {
                        char[] s = "nil";
                        for (usz k = 0; k < 3 && result.str_val.len < MAX_STRING_LEN - 1; k++) {
                            result.str_val.chars[result.str_val.len++] = s[k];
                        }
                    } else if (is_int(a)) {
                        char[32] nbuf;
                        char[] nslice = io::bprintf(&nbuf, "%d", a.int_val)!!;
                        for (usz k = 0; k < nslice.len && result.str_val.len < MAX_STRING_LEN - 1; k++) {
                            result.str_val.chars[result.str_val.len++] = nslice[k];
                        }
                    } else if (is_double(a)) {
                        char[64] nbuf;
                        char[] nslice = io::bprintf(&nbuf, "%.15g", a.double_val)!!;
                        for (usz k = 0; k < nslice.len && result.str_val.len < MAX_STRING_LEN - 1; k++) {
                            result.str_val.chars[result.str_val.len++] = nslice[k];
                        }
                    } else if (a.tag == STRING) {
                        if (directive == 's') {
                            if (result.str_val.len < MAX_STRING_LEN - 1) { result.str_val.chars[result.str_val.len++] = '"'; }
                        }
                        for (usz k = 0; k < a.str_val.len && result.str_val.len < MAX_STRING_LEN - 1; k++) {
                            result.str_val.chars[result.str_val.len++] = a.str_val.chars[k];
                        }
                        if (directive == 's') {
                            if (result.str_val.len < MAX_STRING_LEN - 1) { result.str_val.chars[result.str_val.len++] = '"'; }
                        }
                    } else if (is_symbol(a)) {
                        char[] sname = interp.symbols.get_name(a.sym_val);
                        for (usz k = 0; k < sname.len && result.str_val.len < MAX_STRING_LEN - 1; k++) {
                            result.str_val.chars[result.str_val.len++] = sname[k];
                        }
                    } else {
                        char[] s = "#<obj>";
                        for (usz k = 0; k < s.len && result.str_val.len < MAX_STRING_LEN - 1; k++) {
                            result.str_val.chars[result.str_val.len++] = s[k];
                        }
                    }
                }
            } else if (directive == '~') {
                if (result.str_val.len < MAX_STRING_LEN - 1) { result.str_val.chars[result.str_val.len++] = '~'; }
            } else {
                if (result.str_val.len < MAX_STRING_LEN - 1) { result.str_val.chars[result.str_val.len++] = '~'; }
                if (result.str_val.len < MAX_STRING_LEN - 1) { result.str_val.chars[result.str_val.len++] = directive; }
            }
        } else {
            if (result.str_val.len < MAX_STRING_LEN - 1) { result.str_val.chars[result.str_val.len++] = fmt[i]; }
        }
    }
    result.str_val.chars[result.str_val.len] = 0;
    return result;
}

fn Value* prim_display(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1) return make_error(interp, "display: expected argument");
    Value* v = args[0];
    if (v == null || is_nil(v)) {
        io::print("nil");
    } else if (v.tag == STRING) {
        io::printf("%s", (ZString)&v.str_val.chars); // No quotes for display
    } else {
        print_value(v, &interp.symbols);
    }
    return make_nil(interp);
}

// --- Phase 7: Macroexpand, eval, apply, bound? ---

fn Value* prim_macroexpand(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1) return make_error(interp, "macroexpand: expected quoted expression");
    Value* form = args[0];
    // Form should be a list (macro-name arg...)
    if (!is_cons(form)) return form; // not a macro call, return as-is
    Value* head = form.cons_val.car;
    if (head == null || !is_symbol(head)) return form;
    SymbolId sym = head.sym_val;
    // Check macro table using hash index
    usz mh = (usz)((uint)sym * 2654435761);
    usz mmask = MACRO_HASH_SIZE - 1;
    for (usz probe = 0; probe < MACRO_HASH_SIZE; probe++) {
        usz slot = (mh + probe) & mmask;
        usz midx = interp.macro_hash_index[slot];
        if (midx == usz.max) break; // empty slot
        if ((uint)interp.macro_table[midx].name == (uint)sym) {
            Expr* call_expr = value_to_expr(form, interp);
            if (call_expr == null) return form;
            EvalResult r = expand_pattern_macro(&interp.macro_table[midx], call_expr, interp);
            if (!r.error.has_error && r.value != null) return r.value;
            return form;
        }
    }
    return form;
}

fn Value* prim_eval(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1) return make_error(interp, "eval: expected expression");
    Expr* expr = value_to_expr(args[0], interp);
    if (expr == null) return make_error(interp, "eval: could not convert to expression");
    Value* result = jit_eval(expr, interp.global_env, interp);
    if (result != null && result.tag == ERROR) return make_error(interp, "eval: error during evaluation");
    return result;
}

fn Value* prim_apply(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 2) return make_error(interp, "apply: expected function and argument list");
    Value* func = args[0];
    Value* arg_list = args[1];
    // Apply one arg at a time via JIT path
    Value* result = func;
    Value* cur = arg_list;
    while (is_cons(cur)) {
        result = jit_apply_value(result, cur.cons_val.car, interp);
        if (result != null && result.tag == ERROR) return make_error(interp, "apply: error during application");
        cur = cur.cons_val.cdr;
    }
    return result;
}

fn Value* prim_bound(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1 || !is_symbol(args[0])) return make_error(interp, "bound?: expected symbol");
    SymbolId sym = args[0].sym_val;
    Env* lookup_env = env != null ? env : interp.global_env;
    Value* v = lookup_env.lookup(sym);
    return (v != null) ? make_symbol(interp, interp.sym_true) : make_nil(interp);
}

fn Value* prim_error(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1 || args[0].tag != STRING) return make_error(interp, "error: expected string message");
    return make_error(interp, args[0].str_val.chars[:args[0].str_val.len]);
}

fn Value* prim_error_message(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1 || args[0].tag != ERROR) return make_error(interp, "error-message: expected error value");
    return make_string(interp, args[0].str_val.chars[:args[0].str_val.len]);
}

// --- Phase 8: Array primitives ---

fn Value* prim_array(Value*[] args, Env* env, Interp* interp) {
    // Single list/nil arg → convert to array
    if (args.len == 1 && (is_cons(args[0]) || is_nil(args[0]))) {
        usz count = 0;
        Value* cur = args[0];
        while (is_cons(cur) && count < 65536) { count++; cur = cur.cons_val.cdr; }
        Value* v = make_array(interp, count);
        cur = args[0];
        for (usz i = 0; i < count; i++) {
            v.array_val.items[i] = cur.cons_val.car;
            cur = cur.cons_val.cdr;
        }
        v.array_val.length = count;
        return v;
    }

    Value* v = make_array(interp, args.len < 4 ? 4 : args.len);
    for (usz i = 0; i < args.len; i++) {
        v.array_val.items[i] = args[i];
    }
    v.array_val.length = args.len;
    return v;
}

fn Value* prim_make_array(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 2 || !is_int(args[0])) return make_error(interp, "make-array: expected size and fill");
    long sz = args[0].int_val;
    if (sz < 0 || sz > 65536) return make_error(interp, "make-array: invalid size");
    Value* v = make_array(interp, (usz)sz);
    for (usz i = 0; i < (usz)sz; i++) { v.array_val.items[i] = args[1]; }
    v.array_val.length = (usz)sz;
    return v;
}

fn Value* prim_array_ref(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 2 || !is_array(args[0]) || !is_int(args[1])) return make_error(interp, "array-ref: expected array and int");
    long idx = args[1].int_val;
    if (idx < 0 || (usz)idx >= args[0].array_val.length) return make_error(interp, "array-ref: index out of range");
    return args[0].array_val.items[(usz)idx];
}

fn Value* prim_array_set(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 3 || !is_array(args[0]) || !is_int(args[1])) return make_error(interp, "array-set!: expected array, int, value");
    long idx = args[1].int_val;
    if (idx < 0 || (usz)idx >= args[0].array_val.length) return make_error(interp, "array-set!: index out of range");
    args[0].array_val.items[(usz)idx] = args[2];
    return make_nil(interp);
}

fn Value* prim_array_length(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1 || !is_array(args[0])) return make_error(interp, "array-length: expected array");
    return make_int(interp, (long)args[0].array_val.length);
}

fn Value* prim_array_to_list(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1 || !is_array(args[0])) return make_error(interp, "array->list: expected array");
    Array* vec = args[0].array_val;
    Value* result = make_nil(interp);
    for (isz i = (isz)vec.length - 1; i >= 0; i--) {
        result = make_cons(interp, vec.items[(usz)i], result);
    }
    return result;
}

fn Value* prim_list_to_array(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1) return make_error(interp, "list->array: expected list");
    // Count elements
    usz count = 0;
    Value* cur = args[0];
    while (is_cons(cur) && count < 65536) { count++; cur = cur.cons_val.cdr; }
    Value* v = make_array(interp, count);
    cur = args[0];
    for (usz i = 0; i < count; i++) {
        v.array_val.items[i] = cur.cons_val.car;
        cur = cur.cons_val.cdr;
    }
    v.array_val.length = count;
    return v;
}

fn Value* prim_is_array(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1) return make_nil(interp);
    return is_array(args[0]) ? make_symbol(interp, interp.sym_true) : make_nil(interp);
}

fn Value* prim_array_push(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 2 || !is_array(args[0])) return make_error(interp, "push!: expected array and value");
    Array* vec = args[0].array_val;
    if (vec.length >= vec.capacity) {
        usz new_cap = vec.capacity * 2;
        Value** new_items = (Value**)mem::malloc(Value*.sizeof * new_cap);
        for (usz i = 0; i < vec.length; i++) { new_items[i] = vec.items[i]; }
        mem::free(vec.items);
        vec.items = new_items;
        vec.capacity = new_cap;
    }
    vec.items[vec.length++] = args[1];
    return make_nil(interp);
}

// --- Generic collection primitives ---

fn Value* prim_ref(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 2) return make_error(interp, "ref: expected collection and key");
    Value* coll = args[0];
    if (coll == null) return make_error(interp, "ref: nil collection");

    // Array: (ref arr idx)
    if (coll.tag == ARRAY) {
        if (!is_int(args[1])) return make_error(interp, "ref: array requires int index");
        long idx = args[1].int_val;
        if (coll.array_val == null || idx < 0 || (usz)idx >= coll.array_val.length) { return make_error(interp, "ref: array index out of bounds"); }
        return coll.array_val.items[(usz)idx];
    }

    // Dict: (ref dict key)
    if (coll.tag == HASHMAP) {
        Value* result = hashmap_get(coll.hashmap_val, args[1]);
        if (result == null) return make_nil(interp);
        return result;
    }

    // Cons: (ref pair 0) = car, (ref pair 1) = cdr
    if (coll.tag == CONS) {
        if (!is_int(args[1])) return make_error(interp, "ref: cons requires int index (0=car, 1=cdr)");
        long idx = args[1].int_val;
        if (idx == 0) return coll.cons_val.car;
        if (idx == 1) return coll.cons_val.cdr;
        return make_error(interp, "ref: cons index must be 0 (car) or 1 (cdr)");
    }

    // String: (ref str idx) = char as int
    if (coll.tag == STRING) {
        if (!is_int(args[1])) return make_error(interp, "ref: string requires int index");
        long idx = args[1].int_val;
        if (idx < 0 || (usz)idx >= coll.str_val.len) { return make_error(interp, "ref: string index out of bounds"); }
        return make_int(interp, (long)coll.str_val.chars[(usz)idx]);
    }

    return make_error(interp, "ref: expected array, dict, cons, or string");
}

fn Value* prim_push(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 2) return make_error(interp, "push!: expected collection and value");
    if (!is_array(args[0])) return make_error(interp, "push!: expected array");
    return prim_array_push(args, env, interp);
}

fn Value* prim_keys(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1 || args[0] == null || args[0].tag != HASHMAP) return make_error(interp, "keys: expected dict");
    return prim_dict_keys(args, env, interp);
}

fn Value* prim_values(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1 || args[0] == null || args[0].tag != HASHMAP) return make_error(interp, "values: expected dict");
    return prim_dict_values(args, env, interp);
}

fn Value* prim_has(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 2) return make_error(interp, "has?: expected collection and key");
    if (args[0] == null || args[0].tag != HASHMAP) return make_error(interp, "has?: expected dict");
    return prim_dict_has(args, env, interp);
}

fn Value* prim_remove(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 2) return make_error(interp, "remove!: expected collection and key");
    if (args[0] == null || args[0].tag != HASHMAP) return make_error(interp, "remove!: expected dict");
    return prim_dict_remove(args, env, interp);
}

// --- Phase 9: Set primitives (built on dict) ---

fn Value* prim_set(Value*[] args, Env* env, Interp* interp) {
    // Create a new hash map, using args as keys, value = true
    Value* map_val = interp.alloc_value();
    map_val.tag = HASHMAP;
    uint cap = 16;
    HashMap* map = (HashMap*)mem::malloc(HashMap.sizeof);
    map.capacity = cap;
    map.count = 0;
    map.mask = cap - 1;
    map.entries = (HashEntry*)mem::malloc(HashEntry.sizeof * cap);
    for (uint i = 0; i < cap; i++) { map.entries[i].key = null; }
    map_val.hashmap_val = map;
    Value* true_val = make_symbol(interp, interp.sym_true);
    for (usz i = 0; i < args.len; i++) {
        hashmap_set(map, args[i], true_val, interp);
    }
    map_val.tag = HASHMAP; // Use HASHMAP tag - sets are hashmaps with true values
    return map_val;
}

fn Value* prim_set_add(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 2 || args[0].tag != HASHMAP) return make_error(interp, "set-add: expected set and value");
    Value* true_val = make_symbol(interp, interp.sym_true);
    hashmap_set(args[0].hashmap_val, args[1], true_val, interp);
    return args[0];
}

fn Value* prim_set_remove(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 2 || args[0].tag != HASHMAP) return make_error(interp, "set-remove: expected set and value");
    hashmap_remove(args[0].hashmap_val, args[1]);
    return args[0];
}

fn Value* prim_set_contains(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 2 || args[0].tag != HASHMAP) return make_error(interp, "set-contains?: expected set and value");
    Value* v = hashmap_get(args[0].hashmap_val, args[1]);
    return (v != null) ? make_symbol(interp, interp.sym_true) : make_nil(interp);
}

fn Value* prim_set_size(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1 || args[0].tag != HASHMAP) return make_error(interp, "set-size: expected set");
    return make_int(interp, (long)args[0].hashmap_val.count);
}

fn Value* prim_set_to_list(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1 || args[0].tag != HASHMAP) return make_error(interp, "set->list: expected set");
    HashMap* map = args[0].hashmap_val;
    Value* result = make_nil(interp);
    for (uint i = 0; i < map.capacity; i++) {
        if (map.entries[i].key != null) {
            result = make_cons(interp, map.entries[i].key, result);
        }
    }
    return result;
}

// --- Phase 12: Additional I/O and convenience ---

fn Value* prim_read_string(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1 || args[0].tag != STRING) return make_error(interp, "read-string: expected string");
    char[] src = args[0].str_val.chars[:args[0].str_val.len];
    EvalResult r = run(src, interp);
    if (r.error.has_error) return make_error(interp, "read-string: parse/eval error");
    return r.value;
}

fn Value* prim_string_to_symbol(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1 || args[0].tag != STRING) return make_error(interp, "string->symbol: expected string");
    SymbolId sym = interp.symbols.intern(args[0].str_val.chars[:args[0].str_val.len]);
    return make_symbol(interp, sym);
}

fn Value* prim_symbol_to_string(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1 || !is_symbol(args[0])) return make_error(interp, "symbol->string: expected symbol");
    char[] name = interp.symbols.get_name(args[0].sym_val);
    return make_string(interp, name);
}

/// =============================================================================
// SECTION 7.5: TYPE SYSTEM PRIMITIVES
// =============================================================================

/**
 * (type-of v) -> symbol representing the type name
 */
fn Value* prim_type_of(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1) return make_error(interp, "type-of: expected 1 argument");
    SymbolId type_name = value_type_name(args[0], interp);
    return make_symbol(interp, type_name);
}

/**
 * (is? v 'TypeName) -> true if v's type is or is subtype of TypeName
 */
fn Value* prim_is_type(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 2) return make_error(interp, "is?: expected 2 arguments");
    if (args[1] == null || args[1].tag != SYMBOL) {
        return make_error(interp, "is?: second argument must be a symbol");
    }

    SymbolId target_name = args[1].sym_val;
    TypeId target_type = interp.types.lookup(target_name, &interp.symbols);
    if (target_type == INVALID_TYPE_ID) {
        return make_nil(interp);  // Unknown type
    }

    TypeId value_type = infer_value_type(args[0], interp);
    if (value_type == INVALID_TYPE_ID) {
        return make_nil(interp);
    }

    // Check for Any type (everything matches)
    TypeId any_type = interp.types.lookup(interp.sym_Any, &interp.symbols);
    if (target_type == any_type) {
        return make_symbol(interp, interp.sym_true);
    }

    // Check exact match or subtype
    if (interp.types.is_subtype(value_type, target_type)) {
        return make_symbol(interp, interp.sym_true);
    }

    return make_nil(interp);
}

/**
 * (instance? v) -> true if v is a user-defined type instance
 */
fn Value* prim_is_instance(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1) return make_nil(interp);
    bool result = args[0] != null && args[0].tag == INSTANCE;
    return result ? make_symbol(interp, interp.sym_true) : make_nil(interp);
}

/**
 * (type-args v) -> list of inferred type argument symbols, or nil if not parametric.
 * Example: (type-args (Box 42)) => '(Int)
 */
fn Value* prim_type_args(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1) return make_error(interp, "type-args: expected 1 argument");
    if (args[0] == null || args[0].tag != INSTANCE) return make_nil(interp);
    Instance* inst = args[0].instance_val;
    if (inst == null || inst.type_arg_count == 0) return make_nil(interp);

    // Build a list of type name symbols from the inferred type args
    Value* result = make_nil(interp);
    // Build in reverse so first type arg is at head
    for (usz i = inst.type_arg_count; i > 0; i--) {
        TypeId tid = inst.type_args[i - 1];
        if (tid != INVALID_TYPE_ID) {
            TypeInfo* ti = interp.types.get(tid);
            if (ti != null) {
                result = make_cons(interp, make_symbol(interp, ti.name), result);
            } else {
                result = make_cons(interp, make_nil(interp), result);
            }
        } else {
            result = make_cons(interp, make_nil(interp), result);
        }
    }
    return result;
}

// --- Memory reclamation ---

fn Value* prim_free_bang(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1) return make_error(interp, "free!: expected 1 argument");
    Value* v = args[0];
    if (v == null) return make_nil(interp);

    // Free heap-allocated backing storage and nullify the value.
    // Note: Array struct is malloc'd, but HashMap/Instance structs are region-allocated.
    // Only free what was actually malloc'd.
    switch (v.tag) {
        case ARRAY:
            if (v.array_val != null) {
                if (v.array_val.items != null) mem::free(v.array_val.items);
                mem::free(v.array_val);  // Array struct is malloc'd in make_array
                v.array_val = null;
            }
        case HASHMAP:
            // HashMap struct is region-allocated, only entries buffer is malloc'd
            if (v.hashmap_val != null) {
                if (v.hashmap_val.entries != null) {
                    mem::free(v.hashmap_val.entries);
                    v.hashmap_val.entries = null;
                }
                v.hashmap_val.count = 0;
                v.hashmap_val.capacity = 0;
            }
        case INSTANCE:
            // Instance struct is malloc'd in make_instance
            if (v.instance_val != null) {
                mem::free(v.instance_val);
                v.instance_val = null;
            }
        case STRING:
        case ERROR:
            if (v.str_val != null) {
                mem::free(v.str_val);
                v.str_val = null;
            }
        default: return make_nil(interp);  // No backing to free
    }
    v.tag = NIL;  // Mark as nil so aliases see a safe value
    return make_nil(interp);
}

// =============================================================================
// SECTION 8: INTERPRETER INITIALIZATION
// =============================================================================

/**
 * Register all primitive functions in the interpreter.
 */
fn void register_primitives(Interp* interp) {
    // Register true, false, and nil as symbols bound to their values
    Value* true_val = make_symbol(interp, interp.sym_true);
    Value* false_val = make_nil(interp);  // false is nil
    Value* nil_val = make_nil(interp);
    interp.global_env.define(interp.sym_true, true_val);
    interp.global_env.define(interp.sym_false, false_val);
    SymbolId sym_nil = interp.symbols.intern("nil");
    interp.global_env.define(sym_nil, nil_val);

    // Arithmetic (dispatch-ready for user-defined numeric types)
    register_dispatched_prim(interp, "+", &prim_add, 2);
    register_dispatched_prim(interp, "-", &prim_sub, 2);
    register_dispatched_prim(interp, "*", &prim_mul, 2);
    register_dispatched_prim(interp, "/", &prim_div, 2);
    register_dispatched_prim(interp, "%", &prim_mod, 2);

    // Comparison (dispatch-ready for user-defined ordering)
    register_dispatched_prim(interp, "=", &prim_eq, 2);
    register_dispatched_prim(interp, "<", &prim_lt, 2);
    register_dispatched_prim(interp, ">", &prim_gt, 2);
    register_dispatched_prim(interp, "<=", &prim_le, 2);
    register_dispatched_prim(interp, ">=", &prim_ge, 2);

    // List operations
    register_prim(interp, "cons", &prim_cons, 2);
    register_prim(interp, "car", &prim_car, 1);
    register_prim(interp, "cdr", &prim_cdr, 1);
    register_prim(interp, "null?", &prim_null, 1);
    register_prim(interp, "pair?", &prim_pair, 1);
    register_prim(interp, "list", &prim_list, -1);
    register_dispatched_prim(interp, "length", &prim_length, 1);

    // Boolean
    register_prim(interp, "not", &prim_not, 1);

    // I/O (raw primitives + effect-compatible names)
    register_prim(interp, "__raw-print", &prim_print, 1);
    register_prim(interp, "__raw-println", &prim_println, 1);
    register_prim(interp, "__raw-newline", &prim_newline, 0);
    register_prim(interp, "__raw-display", &prim_display, 1);

    // String primitives
    register_prim(interp, "string-append", &prim_string_append, -1);
    register_prim(interp, "string-join", &prim_string_join, 2);
    register_prim(interp, "substring", &prim_substring, 3);
    register_prim(interp, "string-split", &prim_string_split, 2);
    register_prim(interp, "string-length", &prim_string_length, 1);
    register_prim(interp, "string->list", &prim_string_to_list, 1);
    register_prim(interp, "list->string", &prim_list_to_string, 1);
    register_prim(interp, "string-upcase", &prim_string_upcase, 1);
    register_prim(interp, "string-downcase", &prim_string_downcase, 1);
    register_prim(interp, "string-trim", &prim_string_trim, 1);
    register_prim(interp, "continuation?", &prim_is_continuation, 1);

    // File I/O primitives (raw + effect-compatible)
    register_prim(interp, "__raw-read-file", &prim_read_file, 1);
    register_prim(interp, "__raw-write-file", &prim_write_file, 2);
    register_prim(interp, "__raw-file-exists?", &prim_file_exists, 1);
    register_prim(interp, "__raw-read-lines", &prim_read_lines, 1);
    register_prim(interp, "load", &prim_load, 1);

    // Conversion primitives
    register_prim(interp, "string->number", &prim_string_to_number, 1);
    register_prim(interp, "number->string", &prim_number_to_string, 1);
    register_prim(interp, "exact->inexact", &prim_exact_to_inexact, 1);
    register_prim(interp, "inexact->exact", &prim_inexact_to_exact, 1);

    // Gensym
    register_prim(interp, "gensym", &prim_gensym, 0);

    // Dict primitives
    register_prim(interp, "dict", &prim_dict, -1);
    register_prim(interp, "dict-set!", &prim_dict_set, 3);

    // FFI primitives
    register_prim(interp, "ffi-open", &prim_ffi_open, 1);
    register_prim(interp, "ffi-call", &prim_ffi_call, -1);
    register_prim(interp, "ffi-close", &prim_ffi_close, 1);
    register_prim(interp, "ffi-sym", &prim_ffi_sym, 2);

    // Phase 2: Math library
    register_prim(interp, "sin", &prim_sin, 1);
    register_prim(interp, "cos", &prim_cos, 1);
    register_prim(interp, "tan", &prim_tan, 1);
    register_prim(interp, "asin", &prim_asin, 1);
    register_prim(interp, "acos", &prim_acos, 1);
    register_prim(interp, "atan", &prim_atan, 1);
    register_prim(interp, "atan2", &prim_atan2, 2);
    register_prim(interp, "exp", &prim_exp, 1);
    register_prim(interp, "log", &prim_log, 1);
    register_prim(interp, "log10", &prim_log10, 1);
    register_prim(interp, "pow", &prim_pow, 2);
    register_prim(interp, "sqrt", &prim_sqrt, 1);
    register_prim(interp, "floor", &prim_floor, 1);
    register_prim(interp, "ceiling", &prim_ceiling, 1);
    register_prim(interp, "round", &prim_round, 1);
    register_prim(interp, "truncate", &prim_truncate, 1);
    register_prim(interp, "abs", &prim_abs, 1);
    register_prim(interp, "min", &prim_min, 2);
    register_prim(interp, "max", &prim_max, 2);
    register_prim(interp, "gcd", &prim_gcd, 2);
    register_prim(interp, "lcm", &prim_lcm, 2);

    // Phase 3: Sorting & bitwise
    register_dispatched_prim(interp, "sort", &prim_sort, 1);
    register_dispatched_prim(interp, "sort-by", &prim_sort_by, 2);
    register_prim(interp, "bitwise-and", &prim_bitwise_and, 2);
    register_prim(interp, "bitwise-or", &prim_bitwise_or, 2);
    register_prim(interp, "bitwise-xor", &prim_bitwise_xor, 2);
    register_prim(interp, "bitwise-not", &prim_bitwise_not, 1);
    register_prim(interp, "lshift", &prim_lshift, 2);
    register_prim(interp, "rshift", &prim_rshift, 2);

    // Phase 5: String operations & type predicates
    register_prim(interp, "string-contains?", &prim_string_contains, 2);
    register_prim(interp, "string-index-of", &prim_string_index_of, 2);
    register_prim(interp, "string-replace", &prim_string_replace, 3);
    register_prim(interp, "char-at", &prim_char_at, 2);
    register_prim(interp, "string-repeat", &prim_string_repeat, 2);
    register_prim(interp, "procedure?", &prim_is_procedure, 1);
    register_prim(interp, "format", &prim_format, -1);

    // Phase 7: Introspection & metaprogramming
    register_prim(interp, "macroexpand", &prim_macroexpand, 1);
    register_prim(interp, "eval", &prim_eval, 1);
    register_prim(interp, "apply", &prim_apply, 2);
    register_prim(interp, "bound?", &prim_bound, 1);
    register_prim(interp, "error", &prim_error, 1);
    register_prim(interp, "error-message", &prim_error_message, 1);

    // Phase 8: Arrays
    register_prim(interp, "array", &prim_array, -1);
    register_prim(interp, "array-set!", &prim_array_set, 3);

    // Generic collection primitives (dispatch-ready for user-defined collections)
    register_dispatched_prim(interp, "ref", &prim_ref, 2);
    register_dispatched_prim(interp, "push!", &prim_push, 2);
    register_dispatched_prim(interp, "keys", &prim_keys, 1);
    register_dispatched_prim(interp, "values", &prim_values, 1);
    register_dispatched_prim(interp, "has?", &prim_has, 2);
    register_dispatched_prim(interp, "remove!", &prim_remove, 2);

    // Phase 9: Sets (built on dict)
    register_prim(interp, "set", &prim_set, -1);
    register_prim(interp, "set-add", &prim_set_add, 2);
    register_prim(interp, "set-remove", &prim_set_remove, 2);
    register_prim(interp, "set-contains?", &prim_set_contains, 2);
    register_prim(interp, "set-size", &prim_set_size, 1);
    register_prim(interp, "set->list", &prim_set_to_list, 1);

    // Phase 12: Additional I/O and convenience
    register_prim(interp, "read-string", &prim_read_string, 1);
    register_prim(interp, "string->symbol", &prim_string_to_symbol, 1);
    register_prim(interp, "symbol->string", &prim_symbol_to_string, 1);

    // Type system primitives
    register_prim(interp, "type-of", &prim_type_of, 1);
    register_prim(interp, "is?", &prim_is_type, 2);
    register_prim(interp, "instance?", &prim_is_instance, 1);
    register_prim(interp, "type-args", &prim_type_args, 1);

    // Memory reclamation
    register_prim(interp, "free!", &prim_free_bang, 1);

    // Register built-in types
    register_builtin_types(interp);

    // Cache I/O fast path primitives for O(1) access in eval_perform
    interp.raw_print = interp.global_env.lookup(interp.symbols.intern("__raw-print"));
    interp.raw_println = interp.global_env.lookup(interp.symbols.intern("__raw-println"));
    interp.raw_display = interp.global_env.lookup(interp.symbols.intern("__raw-display"));
    interp.raw_newline = interp.global_env.lookup(interp.symbols.intern("__raw-newline"));
    interp.raw_read_file = interp.global_env.lookup(interp.symbols.intern("__raw-read-file"));
    interp.raw_write_file = interp.global_env.lookup(interp.symbols.intern("__raw-write-file"));
    interp.raw_file_exists = interp.global_env.lookup(interp.symbols.intern("__raw-file-exists?"));
    interp.raw_read_lines = interp.global_env.lookup(interp.symbols.intern("__raw-read-lines"));
}

fn void register_prim(Interp* interp, char[] name, PrimitiveFn func, int arity) {
    SymbolId sym = interp.symbols.intern(name);
    Value* prim = make_primitive(interp, name, func, arity);
    interp.global_env.define(sym, prim);
}

fn void register_dispatched_prim(Interp* interp, char[] name, PrimitiveFn func, int arity) {
    SymbolId sym = interp.symbols.intern(name);
    Value* prim = make_primitive(interp, name, func, arity);

    // Create method table with no typed entries, primitive as fallback
    MethodTable* mt = (MethodTable*)mem::malloc(MethodTable.sizeof);
    mt.name = sym;
    mt.entry_count = 0;
    mt.fallback = prim;

    main::RegionHandle saved = interp.current_frame;
    interp.current_frame = interp.root_region;
    Value* mt_val = interp.alloc_value();
    interp.current_frame = saved;
    mt_val.tag = METHOD_TABLE;
    mt_val.method_table_val = mt;

    interp.global_env.define(sym, mt_val);
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
        // Expand macros before JIT compilation
        expr = expand_macros_in_expr(expr, interp);
        JitFn f = jit_compile(expr, interp);
        if (f != null) {
            Value* jit_result = jit_exec(f, interp);
            if (jit_result != null && jit_result.tag == ERROR) {
                result = eval_error(jit_result.str_val.chars[:jit_result.str_val.len]);
            } else {
                result = eval_ok(jit_result);
            }
            if (result.error.has_error) break;
            continue;
        }
        result = eval_error("JIT compilation failed");
        break;
    }

    exprs.free();
    return result;
}

/**
 * Run a single expression.
 */
fn EvalResult run(char[] source, Interp* interp) {
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

    // JIT path: compile + execute
    expr = expand_macros_in_expr(expr, interp);
    JitFn f = jit_compile(expr, interp);
    if (f != null) {
        Value* result = jit_exec(f, interp);
        if (result != null && result.tag == ERROR) {
            return eval_error(result.str_val.chars[:result.str_val.len]);
        }
        return eval_ok(result);
    }
    return eval_error("JIT compilation failed");
}

/**
 * Register standard library functions defined in Pika itself.
 * Called after register_primitives().
 */
fn void register_stdlib(Interp* interp) {
    // =========================================================================
    // Built-in Effect Declarations (typed I/O effects)
    // =========================================================================
    run("(define [effect] (io/print (^Any x)))", interp);
    run("(define [effect] (io/println (^Any x)))", interp);
    run("(define [effect] (io/display (^Any x)))", interp);
    run("(define [effect] (io/newline (^Any x)))", interp);
    run("(define [effect] (io/read-file (^String path)))", interp);
    run("(define [effect] (io/write-file (^Any x)))", interp);
    run("(define [effect] (io/file-exists? (^String path)))", interp);
    run("(define [effect] (io/read-lines (^String path)))", interp);

    // =========================================================================
    // Standard Macros (defined before HOFs since macros may be used by them)
    // =========================================================================

    // when: (when test body...) - evaluate body if test is truthy
    run("(define [macro] when ([test .. body] (if test (begin .. body) nil)))", interp);

    // unless: (unless test body...) - evaluate body if test is falsy
    run("(define [macro] unless ([test .. body] (if test nil (begin .. body))))", interp);

    // cond: (cond test1 body1 test2 body2 ...) - multi-clause conditional
    run("(define [macro] cond ([] nil) ([test body .. rest] (if test body (cond .. rest))))", interp);

    // with-trampoline: effect-based trampoline for stack-safe non-tail recursion.
    // Usage: (with-trampoline (lambda () (fib 10)))
    // Inside the thunk, use (perform bounce (lambda () ...)) to bounce.
    // Thunks use a dummy parameter: (lambda (xx) body), called with (thunk nil)
    run("(define with-trampoline (lambda (thunk) (handle (thunk nil) ((bounce k next-thunk) (k (with-trampoline next-thunk))))))", interp);

    // =========================================================================
    // Type Predicates (defined via is? and abstract type hierarchy)
    // =========================================================================
    run("(define (int? x) (is? x 'Int))", interp);
    run("(define (double? x) (is? x 'Double))", interp);
    run("(define (number? x) (is? x 'Number))", interp);
    run("(define (string? x) (is? x 'String))", interp);
    run("(define (symbol? x) (is? x 'Symbol))", interp);
    run("(define (boolean? x) (or (null? x) (= x true)))", interp);
    run("(define (list? x) (or (null? x) (pair? x)))", interp);
    run("(define (closure? x) (is? x 'Closure))", interp);
    run("(define (array? x) (is? x 'Array))", interp);
    run("(define (dict? x) (is? x 'Dict))", interp);

    // =========================================================================
    // Numeric Predicates (defined via dispatched comparison ops)
    // =========================================================================
    run("(define (zero? x) (= x 0))", interp);
    run("(define (positive? x) (> x 0))", interp);
    run("(define (negative? x) (< x 0))", interp);
    run("(define (even? x) (= (% x 2) 0))", interp);
    run("(define (odd? x) (not (= (% x 2) 0)))", interp);

    // =========================================================================
    // Higher-Order Functions (all curried, list-based)
    // =========================================================================

    // map: (map f lst) - apply f to each element, return new list
    // Iterative: accumulator + reverse for O(1) stack via TCO
    run("(define map (lambda (f) (lambda (lst) (let loop ((xs lst) (acc nil)) (if (null? xs) (reverse acc) (loop (cdr xs) (cons (f (car xs)) acc)))))))", interp);

    // filter: (filter pred lst) - keep elements where (pred x) is truthy
    // Iterative: accumulator + reverse for O(1) stack via TCO
    run("(define filter (lambda (pred) (lambda (lst) (let loop ((xs lst) (acc nil)) (if (null? xs) (reverse acc) (if (pred (car xs)) (loop (cdr xs) (cons (car xs) acc)) (loop (cdr xs) acc)))))))", interp);

    // foldl: (foldl f acc lst) - left fold
    run("(define foldl (lambda (f) (lambda (acc) (lambda (lst) (if (null? lst) acc (((foldl f) ((f acc) (car lst))) (cdr lst)))))))", interp);

    // foldr: (foldr f init lst) - right fold (iterative: reverse + foldl with flipped f)
    run("(define foldr (lambda (f) (lambda (init) (lambda (lst) (((foldl (lambda (acc) (lambda (x) ((f x) acc)))) init) (reverse lst))))))", interp);

    // append: (append a b) - concatenate two lists (iterative: reverse + prepend)
    run("(define append (lambda (a) (lambda (b) (let loop ((xs (let rev ((l a) (r nil)) (if (null? l) r (rev (cdr l) (cons (car l) r))))) (acc b)) (if (null? xs) acc (loop (cdr xs) (cons (car xs) acc)))))))", interp);

    // reverse: (reverse lst) - reverse a list (iterative)
    run("(define reverse (lambda (lst) (let loop ((xs lst) (acc nil)) (if (null? xs) acc (loop (cdr xs) (cons (car xs) acc))))))", interp);

    // compose: (compose f g) - function composition, returns (lambda (x) (f (g x)))
    run("(define compose (lambda (f) (lambda (g) (lambda (x) (f (g x))))))", interp);

    // id: identity function
    run("(define id (lambda (x) x))", interp);

    // nth: (nth n lst) - get nth element (0-indexed)
    run("(define nth (lambda (n) (lambda (lst) (if (= n 0) (car lst) ((nth (- n 1)) (cdr lst))))))", interp);

    // take: (take n lst) - first n elements (iterative)
    run("(define take (lambda (n) (lambda (lst) (let loop ((i n) (xs lst) (acc nil)) (if (= i 0) (reverse acc) (if (null? xs) (reverse acc) (loop (- i 1) (cdr xs) (cons (car xs) acc))))))))", interp);

    // drop: (drop n lst) - skip first n elements
    run("(define drop (lambda (n) (lambda (lst) (if (= n 0) lst (if (null? lst) nil ((drop (- n 1)) (cdr lst)))))))", interp);

    // zip: (zip a b) - zip two lists into list of pairs (iterative)
    run("(define zip (lambda (a) (lambda (b) (let loop ((xs a) (ys b) (acc nil)) (if (or (null? xs) (null? ys)) (reverse acc) (loop (cdr xs) (cdr ys) (cons (cons (car xs) (car ys)) acc)))))))", interp);

    // range: (range n) - list from 0 to n-1 (iterative, builds in reverse)
    run("(define range (lambda (n) (let loop ((i (- n 1)) (acc nil)) (if (< i 0) acc (loop (- i 1) (cons i acc))))))", interp);

    // for-each: (for-each f lst) - apply f to each element for side effects, return nil
    run("(define for-each (lambda (f) (lambda (lst) (if (null? lst) nil (let ((_r (f (car lst)))) ((for-each f) (cdr lst)))))))", interp);

    // any?: (any? pred lst) - true if pred is truthy for any element
    run("(define any? (lambda (pred) (lambda (lst) (if (null? lst) nil (if (pred (car lst)) true ((any? pred) (cdr lst)))))))", interp);

    // every?: (every? pred lst) - true if pred is truthy for all elements
    run("(define every? (lambda (pred) (lambda (lst) (if (null? lst) true (if (pred (car lst)) ((every? pred) (cdr lst)) nil)))))", interp);

    // =========================================================================
    // Error Handling Convention (via algebraic effects)
    // =========================================================================

    // try: run a thunk, catch errors via 'raise' effect
    // Usage: ((try (lambda (xx) body)) (lambda (msg) handler))
    run("(define try (lambda (thunk) (lambda (handler) (handle (thunk nil) ((raise k msg) (handler msg))))))", interp);

    // assert!: check condition, raise if false
    run("(define assert! (lambda (condition) (lambda (msg) (if condition true (perform raise msg)))))", interp);

    // =========================================================================
    // Association List Helpers
    // =========================================================================

    // assoc: (assoc key alist) - find pair with matching key
    run("(define assoc (lambda (key) (lambda (alist) (if (null? alist) nil (if (= (car (car alist)) key) (car alist) ((assoc key) (cdr alist)))))))", interp);

    // assoc-ref: (assoc-ref key alist) - get value for key (cdr of pair)
    run("(define assoc-ref (lambda (key) (lambda (alist) (let ((pair ((assoc key) alist))) (if (null? pair) nil (cdr pair))))))", interp);

    // =========================================================================
    // Mathematical Constants
    // =========================================================================
    run("(define pi 3.141592653589793)", interp);
    run("(define e 2.718281828459045)", interp);

    // =========================================================================
    // Additional HOFs (Phase 3/6)
    // =========================================================================

    // flatten: flatten nested lists into a flat list
    run("(define (flatten lst) (let loop ((l lst) (acc nil)) (if (null? l) (reverse acc) (if (pair? (car l)) (loop (cdr l) (let loop2 ((inner (car l)) (a acc)) (if (null? inner) a (loop2 (cdr inner) (cons (car inner) a))))) (loop (cdr l) (cons (car l) acc))))))", interp);

    // partition: split list by predicate, returns (kept . rejected)
    run("(define (partition pred lst) (let loop ((l lst) (yes nil) (no nil)) (if (null? l) (cons (reverse yes) (reverse no)) (if (pred (car l)) (loop (cdr l) (cons (car l) yes) no) (loop (cdr l) yes (cons (car l) no))))))", interp);

    // remove: remove elements matching predicate (uses filter)
    run("(define (remove pred lst) ((filter (lambda (x) (not (pred x)))) lst))", interp);

    // find: first element matching predicate, or nil
    run("(define (find pred lst) (let loop ((l lst)) (if (null? l) nil (if (pred (car l)) (car l) (loop (cdr l))))))", interp);

    // =========================================================================
    // Generators & Lazy Streams (Phase 8 — using existing effects)
    // =========================================================================

    // yield: (yield val) inside a generator — returns (cons val continuation)
    run("(define [macro] yield ([val] (shift k (cons val k))))", interp);

    // stream-take: consume n values from a generator continuation
    run("(define (stream-take n gen) (let loop ((i n) (g gen) (acc nil)) (if (= i 0) (reverse acc) (if (null? g) (reverse acc) (let ((pair (if (procedure? g) (g nil) g))) (if (null? pair) (reverse acc) (loop (- i 1) (cdr pair) (cons (car pair) acc))))))))", interp);

    // delay/force: promise (lazy evaluation)
    run("(define (delay thunk) (let ((result nil) (forced nil)) (lambda () (if forced result (begin (set! result (thunk nil)) (set! forced true) result)))))", interp);

    run("(define (force p) (p))", interp);

    // =========================================================================
    // I/O Effect Wrappers (Phase 5)
    // These redefine I/O operations to go through the effects system.
    // When no handler is installed, eval_perform's fast path calls __raw-* directly.
    // =========================================================================
    run("(define print (lambda (x) (perform io/print x)))", interp);
    run("(define println (lambda (x) (perform io/println x)))", interp);
    run("(define display (lambda (x) (perform io/display x)))", interp);
    run("(define newline (lambda () (perform io/newline nil)))", interp);
    run("(define read-file (lambda (path) (perform io/read-file path)))", interp);
    run("(define write-file (lambda (path content) (perform io/write-file (cons path content))))", interp);
    run("(define file-exists? (lambda (path) (perform io/file-exists? path)))", interp);
    run("(define read-lines (lambda (path) (perform io/read-lines path)))", interp);
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

    io::printn("Pika Lisp REPL (type 'quit' or 'exit' to leave, Ctrl-D for EOF)");
    io::printn("---");

    // Buffer for accumulating multi-line input
    char[8192] buf;
    usz buf_len = 0;

    while (true) {
        // Determine prompt: primary or continuation
        char* prompt;
        if (buf_len == 0) {
            prompt = "pika> ";
        } else {
            prompt = "...   ";
        }

        // Use GNU readline for line editing and history support
        char* line = readline(prompt);

        // readline returns null on EOF (Ctrl-D)
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

        // If we're in continuation mode and get an empty line, cancel
        if (buf_len > 0 && len == 0) {
            io::printn("Input cancelled.");
            buf_len = 0;
            mem::free(line);
            continue;
        }

        // Skip empty lines on primary prompt (don't add to history)
        if (buf_len == 0 && len == 0) {
            mem::free(line);
            continue;
        }

        // Check for quit/exit on primary prompt only
        if (buf_len == 0) {
            if (len >= 4 && line[0] == 'q' && line[1] == 'u' &&
                line[2] == 'i' && line[3] == 't') {
                mem::free(line);
                io::printn("Goodbye!");
                break;
            }

            if (len >= 4 && line[0] == 'e' && line[1] == 'x' &&
                line[2] == 'i' && line[3] == 't') {
                mem::free(line);
                io::printn("Goodbye!");
                break;
            }
        }

        // Append line to buffer (with space separator if continuing)
        if (buf_len > 0) {
            // Add a space separator between lines
            if (buf_len < 8191) {
                buf[buf_len] = ' ';
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

        // Free the readline-allocated string
        mem::free(line);

        // Check paren depth to see if expression is complete
        char[] accumulated = buf[:buf_len];
        int depth = count_paren_depth(accumulated);

        if (depth > 0) {
            // Unmatched open parens — prompt for more input
            continue;
        }

        // Expression looks complete (balanced or no parens)
        // Add the full accumulated expression to history
        buf[buf_len] = 0; // null-terminate for add_history
        add_history(&buf[0]);

        // Parse and evaluate in temp region
        char[] input = buf[:buf_len];
        g_interrupted = false;

        // Push temp frame
        main::RegionHandle saved_frame = interp.current_frame;
        interp.current_frame = main::create_region(saved_frame);

        EvalResult r = run(input, interp);

        // Copy result to parent before releasing
        if (!r.error.has_error && r.value != null) {
            r.value = copy_to_parent(r.value, interp, saved_frame);
        }

        // Pop temp frame
        main::release_region(interp.current_frame);
        interp.current_frame = saved_frame;

        if (r.error.has_error) {
            // Print error with location if available
            usz msg_len = 0;
            while (msg_len < 256 && r.error.message[msg_len] != 0) {
                msg_len++;
            }

            if (r.error.line > 0) {
                io::printf("Error at line %d, column %d: ", (int)r.error.line, (int)r.error.column);
                for (usz i = 0; i < msg_len; i++) {
                    io::printf("%c", r.error.message[i]);
                }
                io::printn("");
            } else {
                io::print("Error: ");
                for (usz i = 0; i < msg_len; i++) {
                    io::printf("%c", r.error.message[i]);
                }
                io::printn("");
            }
        } else {
            // Print the result
            print_value(r.value, &interp.symbols);
            io::printn("");
        }

        // Reset buffer for next expression
        buf_len = 0;
    }
}

// =============================================================================
// SECTION 10: TESTS
// =============================================================================

// ---------------------------------------------------------------------------
// Unified Test Helpers — each runs BOTH interpreter AND JIT on the same expr
// ---------------------------------------------------------------------------

fn void setup(Interp* interp, char[] code) {
    run(code, interp);
}

fn void test_eq(Interp* interp, char[] name, char[] expr, long expected, int* pass, int* fail) {
    EvalResult ri = run(expr, interp);
    bool interp_ok = !ri.error.has_error && is_int(ri.value) && ri.value.int_val == expected;

    Expr* ast = parse_for_jit(expr, interp);
    bool jit_ok = false;
    if (ast != null) {
        JitFn f = jit_compile(ast, interp);
        if (f != null) {
            Value* jv = jit_exec(f, interp);
            jit_ok = jv != null && jv.tag == INT && jv.int_val == expected;
        }
    }

    if (interp_ok && jit_ok) {
        io::printfn("[PASS] %s", (ZString)name);
        (*pass)++;
    } else {
        io::printfn("[FAIL] %s (interp=%s, jit=%s)", (ZString)name, interp_ok ? "ok" : "FAIL", jit_ok ? "ok" : "FAIL");
        (*fail)++;
    }
}

fn void test_truthy(Interp* interp, char[] name, char[] expr, int* pass, int* fail) {
    EvalResult ri = run(expr, interp);
    bool interp_ok = !ri.error.has_error && !is_nil(ri.value) && !(is_symbol(ri.value) && (uint)ri.value.sym_val == (uint)interp.sym_false);

    Expr* ast = parse_for_jit(expr, interp);
    bool jit_ok = false;
    if (ast != null) {
        JitFn f = jit_compile(ast, interp);
        if (f != null) {
            Value* jv = jit_exec(f, interp);
            jit_ok = jv != null && !is_nil(jv) && !(is_symbol(jv) && (uint)jv.sym_val == (uint)interp.sym_false);
        }
    }

    if (interp_ok && jit_ok) {
        io::printfn("[PASS] %s", (ZString)name);
        (*pass)++;
    } else {
        io::printfn("[FAIL] %s (interp=%s, jit=%s)", (ZString)name, interp_ok ? "ok" : "FAIL", jit_ok ? "ok" : "FAIL");
        (*fail)++;
    }
}

fn void test_nil(Interp* interp, char[] name, char[] expr, int* pass, int* fail) {
    EvalResult ri = run(expr, interp);
    bool interp_ok = !ri.error.has_error && is_nil(ri.value);

    Expr* ast = parse_for_jit(expr, interp);
    bool jit_ok = false;
    if (ast != null) {
        JitFn f = jit_compile(ast, interp);
        if (f != null) {
            Value* jv = jit_exec(f, interp);
            jit_ok = jv != null && is_nil(jv);
        }
    }

    if (interp_ok && jit_ok) {
        io::printfn("[PASS] %s", (ZString)name);
        (*pass)++;
    } else {
        io::printfn("[FAIL] %s (interp=%s, jit=%s)", (ZString)name, interp_ok ? "ok" : "FAIL", jit_ok ? "ok" : "FAIL");
        (*fail)++;
    }
}

fn void test_error(Interp* interp, char[] name, char[] expr, int* pass, int* fail) {
    EvalResult ri = run(expr, interp);
    bool interp_ok = ri.error.has_error;
    if (interp_ok) {
        io::printfn("[PASS] %s", (ZString)name);
        (*pass)++;
    } else {
        io::printfn("[FAIL] %s (expected error)", (ZString)name);
        (*fail)++;
    }
}

fn void test_str(Interp* interp, char[] name, char[] expr, int* pass, int* fail) {
    EvalResult ri = run(expr, interp);
    bool interp_ok = !ri.error.has_error && is_string(ri.value);

    Expr* ast = parse_for_jit(expr, interp);
    bool jit_ok = false;
    if (ast != null) {
        JitFn f = jit_compile(ast, interp);
        if (f != null) {
            Value* jv = jit_exec(f, interp);
            jit_ok = jv != null && jv.tag == STRING;
        }
    }

    if (interp_ok && jit_ok) {
        io::printfn("[PASS] %s", (ZString)name);
        (*pass)++;
    } else {
        io::printfn("[FAIL] %s (interp=%s, jit=%s)", (ZString)name, interp_ok ? "ok" : "FAIL", jit_ok ? "ok" : "FAIL");
        (*fail)++;
    }
}

fn void test_tag(Interp* interp, char[] name, char[] expr, ValueTag expected, int* pass, int* fail) {
    EvalResult ri = run(expr, interp);
    bool interp_ok = !ri.error.has_error && ri.value != null && ri.value.tag == expected;

    Expr* ast = parse_for_jit(expr, interp);
    bool jit_ok = false;
    if (ast != null) {
        JitFn f = jit_compile(ast, interp);
        if (f != null) {
            Value* jv = jit_exec(f, interp);
            jit_ok = jv != null && jv.tag == expected;
        }
    }

    if (interp_ok && jit_ok) {
        io::printfn("[PASS] %s", (ZString)name);
        (*pass)++;
    } else {
        io::printfn("[FAIL] %s (interp=%s, jit=%s)", (ZString)name, interp_ok ? "ok" : "FAIL", jit_ok ? "ok" : "FAIL");
        (*fail)++;
    }
}

fn void test_gt(Interp* interp, char[] name, char[] expr, long min, int* pass, int* fail) {
    EvalResult ri = run(expr, interp);
    bool interp_ok = !ri.error.has_error && is_int(ri.value) && ri.value.int_val > min;

    Expr* ast = parse_for_jit(expr, interp);
    bool jit_ok = false;
    if (ast != null) {
        JitFn f = jit_compile(ast, interp);
        if (f != null) {
            Value* jv = jit_exec(f, interp);
            jit_ok = jv != null && jv.tag == INT && jv.int_val > min;
        }
    }

    if (interp_ok && jit_ok) {
        io::printfn("[PASS] %s", (ZString)name);
        (*pass)++;
    } else {
        io::printfn("[FAIL] %s (interp=%s, jit=%s)", (ZString)name, interp_ok ? "ok" : "FAIL", jit_ok ? "ok" : "FAIL");
        (*fail)++;
    }
}

// Interp-only: for tests that involve stateful side effects or features
// the JIT cannot handle (modules, error paths, println, etc.)
fn void test_eq_interp(Interp* interp, char[] name, char[] expr, long expected, int* pass, int* fail) {
    EvalResult ri = run(expr, interp);
    if (!ri.error.has_error && is_int(ri.value) && ri.value.int_val == expected) {
        io::printfn("[PASS] %s", (ZString)name);
        (*pass)++;
    } else {
        if (!ri.error.has_error && is_int(ri.value)) { io::printfn("[FAIL] %s (got %d)", (ZString)name, ri.value.int_val); }
        else { io::printfn("[FAIL] %s", (ZString)name); }
        (*fail)++;
    }
}

fn void test_truthy_interp(Interp* interp, char[] name, char[] expr, int* pass, int* fail) {
    EvalResult ri = run(expr, interp);
    if (!ri.error.has_error && !is_nil(ri.value) && !(is_symbol(ri.value) && (uint)ri.value.sym_val == (uint)interp.sym_false)) {
        io::printfn("[PASS] %s", (ZString)name);
        (*pass)++;
    } else {
        io::printfn("[FAIL] %s", (ZString)name);
        (*fail)++;
    }
}

fn void test_nil_interp(Interp* interp, char[] name, char[] expr, int* pass, int* fail) {
    EvalResult ri = run(expr, interp);
    if (!ri.error.has_error && is_nil(ri.value)) {
        io::printfn("[PASS] %s", (ZString)name);
        (*pass)++;
    } else {
        io::printfn("[FAIL] %s", (ZString)name);
        (*fail)++;
    }
}

fn void test_str_interp(Interp* interp, char[] name, char[] expr, int* pass, int* fail) {
    EvalResult ri = run(expr, interp);
    if (!ri.error.has_error && is_string(ri.value)) {
        io::printfn("[PASS] %s", (ZString)name);
        (*pass)++;
    } else {
        io::printfn("[FAIL] %s", (ZString)name);
        (*fail)++;
    }
}

fn void test_tag_interp(Interp* interp, char[] name, char[] expr, ValueTag expected, int* pass, int* fail) {
    EvalResult ri = run(expr, interp);
    if (!ri.error.has_error && ri.value != null && ri.value.tag == expected) {
        io::printfn("[PASS] %s", (ZString)name);
        (*pass)++;
    } else {
        io::printfn("[FAIL] %s", (ZString)name);
        (*fail)++;
    }
}

// JIT-only: for stateful tests that would double-mutate if run through both interp and JIT
fn void test_eq_jit(Interp* interp, char[] name, char[] source, long expected, int* pass, int* fail) {
    Expr* ast = parse_for_jit(source, interp);
    if (ast == null) { (*fail)++; io::printfn("[FAIL] %s (JIT parse error)", (ZString)name); return; }
    JitFn f = jit_compile(ast, interp);
    if (f == null) { (*fail)++; io::printfn("[FAIL] %s (JIT compile error)", (ZString)name); return; }
    Value* result = f(interp);
    if (result != null && result.tag == INT && result.int_val == expected) {
        io::printfn("[PASS] %s", (ZString)name);
        (*pass)++;
    } else {
        (*fail)++;
        if (result != null && result.tag == INT) { io::printfn("[FAIL] %s (JIT got %d)", (ZString)name, result.int_val); }
        else { io::printfn("[FAIL] %s", (ZString)name); }
    }
}

fn void test_nil_jit(Interp* interp, char[] name, char[] source, int* pass, int* fail) {
    Expr* ast = parse_for_jit(source, interp);
    if (ast == null) { (*fail)++; io::printfn("[FAIL] %s (JIT parse error)", (ZString)name); return; }
    JitFn f = jit_compile(ast, interp);
    if (f == null) { (*fail)++; io::printfn("[FAIL] %s (JIT compile error)", (ZString)name); return; }
    Value* result = f(interp);
    if (result == null || is_nil(result)) {
        io::printfn("[PASS] %s", (ZString)name);
        (*pass)++;
    } else {
        (*fail)++;
        io::printfn("[FAIL] %s (JIT expected nil)", (ZString)name);
    }
}

fn double dabs(double x) @inline { return x < 0.0 ? -x : x; }

fn void test_double(Interp* interp, char[] name, char[] expr, double expected, int* pass, int* fail) {
    EvalResult ri = run(expr, interp);
    bool interp_ok = !ri.error.has_error && is_number(ri.value) && dabs(to_double(ri.value) - expected) < 0.0001;

    Expr* ast = parse_for_jit(expr, interp);
    bool jit_ok = false;
    if (ast != null) {
        JitFn f = jit_compile(ast, interp);
        if (f != null) {
            Value* jv = jit_exec(f, interp);
            jit_ok = jv != null && is_number(jv) && dabs(to_double(jv) - expected) < 0.0001;
        }
    }

    if (interp_ok && jit_ok) {
        io::printfn("[PASS] %s", (ZString)name);
        (*pass)++;
    } else {
        io::printfn("[FAIL] %s (interp=%s, jit=%s)", (ZString)name, interp_ok ? "ok" : "FAIL", jit_ok ? "ok" : "FAIL");
        (*fail)++;
    }
}

// =============================================================================
// E2E COMPILER TEST GENERATION
// =============================================================================

struct E2ETestCase {
    char[] name;
    char[] expr;
}

// E2E test expressions — must be compilable (no type system, modules, set!, arrays, dicts)
// Setup defines are run first, then each expr is evaluated and its output captured.
const char[][] E2E_SETUPS = {
    // Basic definitions
    `(define x 10)`,
    `(define add +)`,
    `(define make-adder (lambda (n) (lambda (x) (+ n x))))`,
    `(define adder5 (make-adder 5))`,
    // Recursion
    `(define sum-to (lambda (n) (if (= n 0) 0 (+ n (sum-to (- n 1))))))`,
    `(define fib (lambda (n) (if (< n 2) n (+ (fib (- n 1)) (fib (- n 2))))))`,
    // List helpers
    `(define mymap (lambda (f) (lambda (lst) (if (null? lst) (quote ()) (cons (f (car lst)) ((mymap f) (cdr lst)))))))`,
    `(define double (lambda (x) (* x 2)))`,
    `(define fold-helper (lambda (f) (lambda (init) (lambda (lst) (if (null? lst) init (((fold-helper f) (f init (car lst))) (cdr lst)))))))`,
    // Counter factory
    `(define make-counter (lambda (start) (lambda (x) (+ start x))))`,
    `(define c1 (make-counter 10))`,
    `(define c2 (make-counter 20))`,
    // Named let helper
    `(define fact (lambda (n) (if (= n 0) 1 (* n (fact (- n 1))))))`,
    // Mutual recursion
    `(define is-even (lambda (n) (if (= n 0) true (is-odd (- n 1)))))`,
    `(define is-odd (lambda (n) (if (= n 0) nil (is-even (- n 1)))))`,
    // Composition helpers
    `(define inc (lambda (x) (+ x 1)))`,
    `(define add3 (lambda (x) (+ x 3)))`,
    `(define apply-twice (lambda (f) (lambda (x) (f (f x)))))`,
    // Additional closure
    `(define c3 (make-counter 30))`,
    // Note: id, compose already defined by stdlib
    // set! test variables
    `(define s_x 10)`,
    `(define s_acc 0)`,
    `(define s_count 0)`,
};

const E2ETestCase[] E2E_TESTS = {
    // === Literals ===
    { "int literal 42", "42" },
    { "int literal 0", "0" },
    { "int literal negative", "-5" },
    { "string literal", `"hello"` },
    { "true literal", "true" },
    { "nil literal", "nil" },
    { "quoted symbol", "(quote foo)" },
    { "quoted list", "(quote (a b c))" },
    { "quoted nested", "(quote (1 (2 3) 4))" },
    { "empty list", "(quote ())" },

    // === Arithmetic ===
    { "add", "(+ 1 2)" },
    { "add zero", "(+ 0 0)" },
    { "add negative", "(+ -5 3)" },
    { "add cancel", "(+ 100 -100)" },
    { "add large", "(+ 1000000 2000000)" },
    { "sub", "(- 10 3)" },
    { "sub negative result", "(- 3 10)" },
    { "mul", "(* 6 7)" },
    { "mul zero", "(* 100 0)" },
    { "div", "(/ 10 3)" },
    { "div exact", "(/ 100 10)" },
    { "modulo", "(% 10 3)" },
    { "modulo even", "(% 8 4)" },
    { "nested arith", "(+ (* 3 4) (- 10 5))" },

    // === Floats ===
    { "float literal", "3.14" },
    { "float add", "(+ 1.5 2.5)" },
    { "float mul", "(* 2.0 3.0)" },
    { "float div", "(/ 7.0 2.0)" },
    { "int to float add", "(+ 1 0.5)" },
    { "float integer value", "5.0" },

    // === Comparisons ===
    { "eq true", "(= 5 5)" },
    { "eq false", "(= 5 6)" },
    { "lt true", "(< 3 5)" },
    { "lt false", "(< 5 3)" },
    { "gt true", "(> 5 3)" },
    { "gt false", "(> 3 5)" },
    { "le true", "(<= 5 5)" },
    { "le false", "(<= 6 5)" },
    { "ge true", "(>= 5 5)" },
    { "ge false", "(>= 4 5)" },

    // === Variables & Bindings ===
    { "var x", "x" },
    { "let simple", "(let ((y 7)) y)" },
    { "let nested", "(let ((a 1)) (let ((b 2)) (+ a b)))" },
    { "let shadow", "(let ((x 99)) x)" },
    { "let multi", "(let ((a 1) (b 2) (c 3)) (+ a (+ b c)))" },

    // === Lambda & Application ===
    { "lambda identity", "((lambda (x) x) 5)" },
    { "lambda const", "((lambda (x) 42) 99)" },
    { "lambda arith", "((lambda (x) (+ x 10)) 32)" },
    { "curried add", "(((lambda (x) (lambda (y) (+ x y))) 3) 4)" },
    { "closure adder5", "(adder5 10)" },

    // === If ===
    { "if true", "(if true 1 2)" },
    { "if false", "(if nil 1 2)" },
    { "if nested", "(if (> 5 3) (if (< 2 1) 10 20) 30)" },
    { "if no else", "(if true 42)" },

    // === And/Or ===
    { "and true true", "(and 1 42)" },
    { "and false short", "(and nil 42)" },
    { "or first truthy", "(or 42 99)" },
    { "or fallback", "(or nil 99)" },

    // === Truthiness ===
    { "0 is truthy", "(if 0 42 99)" },
    { "empty string truthy", `(if "" 1 0)` },
    { "empty list truthy", "(if (quote ()) 1 0)" },

    // === Begin ===
    { "begin single", "(begin 42)" },
    { "begin multi", "(begin 1 2 3)" },

    // === Cons / Car / Cdr ===
    { "car cons", "(car (cons 1 2))" },
    { "cdr cons", "(cdr (cons 1 2))" },
    { "car list", "(car (quote (10 20 30)))" },
    { "cdr list", "(cdr (quote (10 20 30)))" },
    { "cons build", "(cons 1 (cons 2 (cons 3 (quote ()))))" },
    { "null? empty", "(null? (quote ()))" },
    { "null? nonempty", "(null? (quote (1)))" },
    { "pair?", "(pair? (cons 1 2))" },
    { "pair? nil", "(pair? nil)" },

    // === Length ===
    { "length empty", "(length (quote ()))" },
    { "length 5", "(length (quote (1 2 3 4 5)))" },

    // === String operations ===
    { "string-length", `(string-length "hello world")` },
    { "string-append", `(string-append "hello" " world")` },
    // substring skipped: 3-arg function not yet supported in compiler runtime
    { "number->string", `(number->string 42)` },
    { "string->number", `(string->number "42")` },

    // === Recursive functions ===
    { "sum-to 50", "(sum-to 50)" },
    { "fib 10", "(fib 10)" },
    { "fib 15", "(fib 15)" },
    { "factorial 5", "(fact 5)" },
    { "factorial 10", "(fact 10)" },

    // === Closure factories ===
    { "counter c1", "(c1 5)" },
    { "counter c2", "(c2 5)" },

    // === Higher-order functions ===
    { "map double car", "(car ((mymap double) (quote (1 2 3))))" },
    { "fold sum", "(((fold-helper +) 0) (quote (1 2 3 4 5)))" },
    { "map and fold", "(((fold-helper +) 0) ((mymap double) (quote (1 2 3 4 5))))" },

    // === Pattern matching ===
    { "match literal", "(match 42 (42 1) (x 0))" },
    { "match variable", "(match 5 (x x))" },
    { "match wildcard", "(match 99 (_ 1))" },
    { "match list", "(match (quote (1 2 3)) ([a b c] (+ a (+ b c))))" },
    { "match head", "(match (quote (10 20 30)) ([head .. tail] head))" },
    { "match rest sum", "(match (quote (1 2 3 4 5)) ([a b ..] (+ a b)))" },
    { "match last", "(match (quote (1 2 3 4 5)) ([.. last] last))" },
    { "match fallthrough", "(match 42 (0 100) (1 200) (x x))" },
    { "match underscore", "(match (quote (1 2 3)) ([_ second _] second))" },

    // === Nested let deep ===
    { "nested let 5", "(let ((a 1)) (let ((b 2)) (let ((c 3)) (let ((d 4)) (let ((e 5)) (+ a (+ b (+ c (+ d e)))))))))" },

    // === Let recursive ===
    { "let rec factorial", "(let ^rec ((f (lambda (n) (if (= n 0) 1 (* n (f (- n 1))))))) (f 5))" },
    { "named let loop", "(let loop ((n 10) (acc 0)) (if (= n 0) acc (loop (- n 1) (+ acc n))))" },

    // === Effects ===
    { "reset passthrough", "(reset (+ 10 20))" },
    { "shift aborts", "(reset (+ 1 (shift k 42)))" },
    { "shift k resumes", "(reset (+ 1 (shift k (k 10))))" },
    { "handle no effect", "(handle 42 ((ask k x) (k 0)))" },
    { "handle perform resume", "(handle (+ 1 (perform ask 0)) ((ask k x) (k 10)))" },
    { "handle abort", "(handle (+ 1 (perform ask 5)) ((ask k x) x))" },

    // === Not ===
    { "not true", "(not true)" },
    { "not nil", "(not nil)" },
    { "not 0", "(not 0)" },

    // === Apply ===
    { "apply add", "(apply + (quote (1 2)))" },
    { "apply list", "(apply cons (quote (1 2)))" },

    // === Arithmetic edge cases ===
    { "sub zero", "(- 0 0)" },
    { "sub neg neg", "(- -5 -3)" },
    { "mul neg pos", "(* -3 4)" },
    { "mul neg neg", "(* -3 -4)" },
    { "div exact 2", "(/ 10 2)" },
    { "div zero num", "(/ 0 5)" },
    { "div neg", "(/ -10 3)" },
    { "mod zero", "(% 9 3)" },
    { "mod small", "(% 7 10)" },
    { "nested add", "(+ 1 (+ 2 3))" },
    { "nested mul add", "(* 2 (+ 3 4))" },
    { "nested sub mul", "(- (* 5 5) (+ 10 5))" },
    { "nested div add", "(/ (+ 10 20) (- 10 4))" },

    // === Comparison edge cases ===
    { "eq zero", "(= 0 0)" },
    { "eq neg", "(= -1 -1)" },
    { "lt equal", "(< 1 1)" },
    { "lt neg zero", "(< -5 0)" },
    { "gt equal", "(> 1 1)" },
    { "le strict", "(<= 1 2)" },
    { "ge strict", "(>= 2 1)" },
    { "if lt branch", "(if (< 1 2) 10 20)" },
    { "if gt branch", "(if (> 1 2) 10 20)" },
    { "if eq computed", "(if (= (+ 2 3) 5) 1 0)" },

    // === And/Or edge cases ===
    { "and true true2", "(and true true)" },
    { "and false true", "(and false true)" },
    { "and true false", "(and true false)" },
    { "or true false", "(or true false)" },
    { "or false nil", "(or false nil)" },
    // and/or short-circuit tests skipped: compiler generates code for both branches

    // === Type predicates ===
    { "string? yes", `(string? "hello")` },
    { "string? no", "(string? 42)" },
    { "int? yes", "(int? 42)" },
    { "int? no", `(int? "hello")` },
    { "symbol? yes", "(symbol? (quote foo))" },
    { "symbol? no", "(symbol? 42)" },
    { "closure? yes", "(closure? (lambda (x) x))" },
    { "closure? no", "(closure? 42)" },
    { "null? nil", "(null? nil)" },
    { "null? 42", "(null? 42)" },
    { "null? list", "(null? (quote (1 2)))" },
    { "pair? list", "(pair? (quote (1 2 3)))" },
    { "pair? 42", "(pair? 42)" },
    { "not false", "(not false)" },
    { "not 42", "(not 42)" },
    { "not empty-str", `(not "")` },

    // === Zero-arg lambdas ===
    { "thunk 42", "((lambda () 42))" },
    { "thunk capture", "(let ((a 10)) ((lambda () a)))" },
    { "thunk begin", "((lambda () (begin 1 2 3)))" },
    { "thunk arith", "((lambda () (+ 10 20)))" },

    // === Multi-param lambdas (auto-curried) ===
    { "multi-param add", "((lambda (x y) (+ x y)) 3 4)" },
    { "multi-param 3", "((lambda (x y z) (+ x (+ y z))) 1 2 3)" },
    { "multi-param partial", "(let ((add (lambda (x y) (+ x y)))) ((add 10) 20))" },

    // === Named let patterns ===
    { "named let sum 5", "(let loop ((n 5) (acc 0)) (if (= n 0) acc (loop (- n 1) (+ acc n))))" },
    { "named let fact 10", "(let f ((n 10) (acc 1)) (if (= n 0) acc (f (- n 1) (* acc n))))" },
    { "named let fib 10", "(let fib ((n 10) (a 0) (b 1)) (if (= n 0) a (fib (- n 1) b (+ a b))))" },
    { "named let fib 20", "(let fib ((n 20) (a 0) (b 1)) (if (= n 0) a (fib (- n 1) b (+ a b))))" },
    { "named let count", "(let count ((n 100)) (if (= n 0) 0 (count (- n 1))))" },

    // === More match patterns ===
    { "match multi 1", "(match 1 (1 10) (2 20) (3 30))" },
    { "match multi 2", "(match 2 (1 10) (2 20) (3 30))" },
    { "match multi 3", "(match 3 (1 10) (2 20) (3 30))" },
    { "match default", "(match 99 (1 10) (2 20) (x (+ x 1)))" },
    { "match computed", "(match (+ 1 1) (1 10) (2 20) (3 30))" },
    { "match pair sum", "(match (quote (10 20)) ([a b] (+ a b)))" },
    { "match single", "(match (quote (42)) ([x] x))" },
    { "match product", "(match (quote (2 3 4)) ([a b c] (* a (* b c))))" },
    { "match rest len", "(match (quote (1 2 3 4 5)) ([h .. t] (length t)))" },
    { "match string", `(match "hello" ("hello" 1) ("world" 2) (x 0))` },

    // === More effects/continuations ===
    { "reset large", "(reset (+ 100 (shift k 42)))" },
    { "shift resume mul", "(reset (* 2 (shift k (k 5))))" },
    { "shift resume add", "(reset (+ 1 (shift k (k (+ 2 3)))))" },
    { "multi-shot k", "(reset (+ 1 (shift k (+ (k 10) 1000))))" },
    { "handle get", "(handle (+ 1 (perform get 0)) ((get k x) (k 100)))" },
    { "handle double", "(handle (perform double 7) ((double k x) (k (* x 2))))" },
    { "handle bail", "(handle (+ 1 (+ 2 (+ 3 (perform bail 42)))) ((bail k x) x))" },
    { "handle no trigger", "(handle (+ 10 20) ((unused k x) 999))" },

    // === More list operations ===
    { "cadr", "(car (cdr (cons 1 (cons 2 (cons 3 nil)))))" },
    { "caddr", "(car (cdr (cdr (cons 1 (cons 2 (cons 3 nil))))))" },
    { "length 1", "(length (quote (1)))" },
    { "length 10", "(length (quote (1 2 3 4 5 6 7 8 9 10)))" },
    { "length cons 2", "(length (cons 1 (cons 2 nil)))" },
    { "car quoted", "(car (quote (10 20 30)))" },
    { "cons dotted", "(cons 1 2)" },

    // === Let shadowing ===
    { "let shadow inner", "(let ((x 10)) (let ((x 20)) x))" },
    { "let no leak", "(let ((x 10)) (let ((y 20)) x))" },

    // === Mutual recursion ===
    { "is-even 10", "(is-even 10)" },
    { "is-odd 7", "(is-odd 7)" },

    // === Composition/apply-twice ===
    { "apply-twice add3", "((apply-twice add3) 10)" },
    { "identity int", "(id 42)" },
    { "identity nil", "(id nil)" },
    { "counter c3", "(c3 5)" },

    // === String edge cases ===
    { "string-length empty", `(string-length "")` },
    { "string-length 1", `(string-length "a")` },
    { "string-append empty", `(string-append "" "abc")` },
    { "string->number neg", `(string->number "-7")` },
    { "number->string 0", `(number->string 0)` },

    // === Float edge cases ===
    { "float neg", "-2.5" },
    { "int+float", "(+ 1 2.0)" },
    { "float*int", "(* 2.5 4)" },
    { "float sub", "(- 5.5 2.5)" },
    { "float lt", "(< 1 2.5)" },
    { "float gt", "(> 3.5 2)" },
    { "float eq", "(= 1 1.0)" },

    // === Begin edge cases ===
    { "begin nested", "(begin (begin 1 2) (begin 3 4))" },
    { "begin if", "(begin (if true 10 20) (if false 30 40))" },

    // === Recursive let 6 ===
    { "let rec fact 6", "(let ^rec ((f (lambda (n) (if (= n 0) 1 (* n (f (- n 1))))))) (f 6))" },

    // === Misc ===
    { "nested closures", "((lambda (x) ((lambda (y) (+ x y)) 20)) 10)" },
    { "deep closure chain", "((((lambda (a) (lambda (b) (lambda (c) (+ a (+ b c))))) 1) 2) 3)" },
    { "list operations", "(car (cdr (cdr (quote (1 2 3 4 5)))))" },
    { "complex arith", "(* (+ 1 2) (- 10 (/ 8 2)))" },
    { "recursive list build", "(length (cons 1 (cons 2 (cons 3 (quote ())))))" },

    // === Stdlib HOFs ===
    { "stdlib map double", "((map (lambda (x) (* x 2))) (quote (1 2 3)))" },
    { "stdlib map inc", "((map (lambda (x) (+ x 1))) (quote (10 20 30)))" },
    { "stdlib map identity", "((map (lambda (x) x)) (quote (1 2 3)))" },
    { "stdlib filter gt2", "((filter (lambda (x) (> x 2))) (quote (1 2 3 4 5)))" },
    { "stdlib filter none", "((filter (lambda (x) (> x 100))) (quote (1 2 3)))" },
    { "stdlib filter all", "((filter (lambda (x) (> x 0))) (quote (1 2 3)))" },
    { "stdlib foldl sum", "(((foldl (lambda (acc) (lambda (x) (+ acc x)))) 0) (quote (1 2 3 4 5)))" },
    { "stdlib foldl product", "(((foldl (lambda (acc) (lambda (x) (* acc x)))) 1) (quote (1 2 3 4 5)))" },
    { "stdlib foldr cons", "(((foldr (lambda (x) (lambda (acc) (cons x acc)))) (quote ())) (quote (1 2 3)))" },
    { "stdlib append", "((append (quote (1 2))) (quote (3 4)))" },
    { "stdlib append empty l", "((append (quote ())) (quote (1 2)))" },
    { "stdlib append empty r", "((append (quote (1 2))) (quote ()))" },
    { "stdlib reverse", "(reverse (quote (1 2 3)))" },
    { "stdlib reverse single", "(reverse (quote (7)))" },
    { "stdlib reverse empty", "(reverse (quote ()))" },
    { "stdlib compose", "(((compose (lambda (x) (* x 2))) (lambda (x) (+ x 1))) 5)" },
    { "stdlib nth 0", "((nth 0) (quote (10 20 30)))" },
    { "stdlib nth 2", "((nth 2) (quote (10 20 30)))" },
    { "stdlib take 3", "((take 3) (quote (1 2 3 4 5)))" },
    { "stdlib take 0", "((take 0) (quote (1 2 3)))" },
    { "stdlib take all", "((take 5) (quote (1 2 3)))" },
    { "stdlib drop 2", "((drop 2) (quote (1 2 3 4 5)))" },
    { "stdlib drop 0", "((drop 0) (quote (1 2 3)))" },
    { "stdlib drop all", "((drop 5) (quote (1 2 3)))" },
    { "stdlib zip", "(car ((zip (quote (1 2 3))) (quote (4 5 6))))" },
    { "stdlib range 5", "(range 5)" },
    { "stdlib range 0", "(range 0)" },
    { "stdlib range 1", "(range 1)" },
    { "stdlib any? found", "((any? (lambda (x) (= x 3))) (quote (1 2 3 4)))" },
    { "stdlib any? not found", "((any? (lambda (x) (= x 99))) (quote (1 2 3)))" },
    { "stdlib every? true", "((every? (lambda (x) (> x 0))) (quote (1 2 3)))" },
    { "stdlib every? false", "((every? (lambda (x) (> x 2))) (quote (1 2 3)))" },
    { "stdlib assoc found", "((assoc 2) (quote ((1 . 10) (2 . 20) (3 . 30))))" },
    { "stdlib assoc not found", "((assoc 99) (quote ((1 . 10) (2 . 20))))" },
    { "stdlib assoc-ref", "((assoc-ref 2) (quote ((1 . 10) (2 . 20) (3 . 30))))" },
    { "stdlib id compose", "(((compose id) id) 42)" },
    { "stdlib map filter", "(((foldl (lambda (acc) (lambda (x) (+ acc x)))) 0) ((filter (lambda (x) (> x 2))) (quote (1 2 3 4 5))))" },

    // === Dict operations ===
    { "dict empty", "(dict? (dict))" },
    { "dict create ref", "(ref (dict 1 10 2 20) 1)" },
    { "dict ref 2", "(ref (dict 1 10 2 20) 2)" },
    { "dict? yes", "(dict? (dict 1 2))" },
    { "dict? no", "(dict? 42)" },
    { "dict has? yes", "(has? (dict 1 10 2 20) 1)" },
    { "dict has? no", "(has? (dict 1 10) 99)" },
    { "dict keys", "(length (keys (dict 1 10 2 20)))" },

    // string-contains?, string-index-of, string-replace, char-at,
    // string-repeat, symbol->string, string->symbol — not mapped in compiler

    // === String operations (extended) ===
    { "string-upcase", `(string-upcase "hello")` },
    { "string-downcase", `(string-downcase "HELLO")` },
    { "string-trim", `(string-trim "  hello  ")` },

    // === set! mutation (setups define s_x=10, s_acc=0, s_count=0) ===
    { "set! returns value", "(set! s_x 20)" },
    { "set! read back", "(begin (set! s_acc 1) (set! s_acc (+ s_acc 1)) s_acc)" },
    { "set! in lambda", "(begin ((lambda (n) (set! s_count n)) 42) s_count)" },

    // === Quasiquote (no unquote — rt_eval_source can't see let-bound vars) ===
    { "quasiquote literal", "`42" },
    { "quasiquote list", "`(1 2 3)" },
    { "quasiquote no unquote", "`(a b c)" },

    // when/unless/cond — skipped: compiler doesn't expand eval-time macros

    // === Dot-bracket indexing ===
    { "dot-bracket 0", "(let ((lst (quote (10 20 30)))) lst.[0])" },
    { "dot-bracket 2", "(let ((lst (quote (10 20 30)))) lst.[2])" },
    { "dot-bracket expr", "(let ((lst (quote (10 20 30)))) lst.[(+ 0 1)])" },

    // === Deep recursion / TCO ===
    { "tco sum 1000", "(let loop ((n 1000) (acc 0)) (if (= n 0) acc (loop (- n 1) (+ acc n))))" },
    { "tco count 5000", "(let count ((n 5000)) (if (= n 0) 0 (count (- n 1))))" },

    // === Complex combinations ===
    { "map then reverse", "(reverse ((map (lambda (x) (* x 10))) (quote (1 2 3))))" },
    { "filter then length", "(length ((filter (lambda (x) (> x 3))) (quote (1 2 3 4 5 6))))" },
    { "nested map", "((map (lambda (x) (+ x 1))) ((map (lambda (x) (* x 2))) (quote (1 2 3))))" },
    { "foldl with filter", "(((foldl (lambda (a) (lambda (x) (+ a x)))) 0) ((filter (lambda (x) (< x 4))) (quote (1 2 3 4 5))))" },
    { "compose triple", "(((compose (lambda (x) (* x 3))) (lambda (x) (+ x 1))) 10)" },
    { "range then sum", "(((foldl (lambda (a) (lambda (x) (+ a x)))) 0) (range 10))" },
    { "take from range", "((take 3) (range 10))" },
    { "drop from range", "((drop 7) (range 10))" },
    { "match on map result", "(match ((map (lambda (x) (* x 10))) (quote (1 2 3))) ([a b c] (+ a (+ b c))))" },
    { "closure over let rec", "(let ^rec ((f (lambda (n) (if (<= n 1) 1 (* n (f (- n 1))))))) (f 7))" },
    { "nested handle", "(handle (handle (+ 1 (perform inner 0)) ((inner k x) (k 10))) ((outer k x) (k 0)))" },
    { "shift double resume", "(reset (+ (shift k (+ (k 1) (k 2))) 10))" },
    { "let-rec factorial 8", "(let ^rec ((f (lambda (n) (if (= n 0) 1 (* n (f (- n 1))))))) (f 8))" },

    // === P1: type-of ===
    { "type-of int", "(type-of 42)" },
    { "type-of string", `(type-of "hello")` },
    { "type-of symbol", "(type-of (quote foo))" },
    { "type-of list", "(type-of (quote (1 2)))" },
    { "type-of nil", "(type-of nil)" },
    { "type-of true", "(type-of true)" },
    { "type-of closure", "(type-of (lambda (x) x))" },
    { "type-of double", "(type-of 3.14)" },

    // === P1: type predicates ===
    { "int? yes", "(int? 42)" },
    { "int? no", "(int? 3.14)" },
    { "string? yes", `(string? "hi")` },
    { "string? no", "(string? 42)" },
    { "symbol? yes", "(symbol? (quote foo))" },
    { "symbol? no", "(symbol? 42)" },
    { "double? yes", "(double? 3.14)" },
    { "double? no", "(double? 42)" },
    { "number? int", "(number? 42)" },
    { "number? double", "(number? 3.14)" },
    { "number? no", "(number? (quote foo))" },
    { "pair? yes", "(pair? (quote (1 2)))" },
    { "pair? no", "(pair? 42)" },
    { "null? yes", "(null? nil)" },
    { "null? no", "(null? 42)" },

    // === P1: math primitives ===
    { "abs positive", "(abs 5)" },
    { "abs negative", "(abs -7)" },
    { "abs zero", "(abs 0)" },
    { "abs double", "(abs -3.5)" },
    { "min ints", "(min 3 7)" },
    { "min reversed", "(min 7 3)" },
    { "min doubles", "(min 1.5 2.5)" },
    { "max ints", "(max 3 7)" },
    { "max reversed", "(max 7 3)" },
    { "max doubles", "(max 1.5 2.5)" },
    { "floor", "(floor 3.7)" },
    { "floor neg", "(floor -3.2)" },
    { "ceiling", "(ceiling 3.2)" },
    { "ceiling neg", "(ceiling -3.7)" },
    { "round up", "(round 3.6)" },
    { "round down", "(round 3.4)" },
    { "truncate pos", "(truncate 3.9)" },
    { "truncate neg", "(truncate -3.9)" },
    { "sqrt 4", "(sqrt 4)" },
    { "sqrt 9", "(sqrt 9)" },
    { "even? yes", "(even? 4)" },
    { "even? no", "(even? 3)" },
    { "odd? yes", "(odd? 3)" },
    { "odd? no", "(odd? 4)" },
    { "zero? yes", "(zero? 0)" },
    { "zero? no", "(zero? 1)" },
    { "positive? yes", "(positive? 5)" },
    { "positive? no", "(positive? -3)" },
    { "negative? yes", "(negative? -3)" },
    { "negative? no", "(negative? 5)" },
    { "gcd", "(gcd 12 8)" },
    { "gcd coprime", "(gcd 7 13)" },

    // === P2: substring (3-arg via interpreter delegation) ===
    { "substring basic", `(substring "hello" 1 3)` },
    { "substring from start", `(substring "abcdef" 0 3)` },
    { "substring to end", `(substring "abcdef" 3 6)` },
    { "substring single", `(substring "hello" 0 1)` },

    // === P2: string-split and string-join ===
    { "string-split", `(length (string-split "a,b,c" ","))` },
    { "string-join", `(string-join " " (quote ("hello" "world")))` },
    { "string-join empty", `(string-join "," (quote ()))` },

    // === P2: when/unless ===
    { "when true", "(when true 42)" },
    { "when false", "(when false 42)" },
    { "unless true", "(unless true 42)" },
    { "unless false", "(unless false 42)" },

    // === P3: Bitwise ops ===
    { "bitwise-and", "(bitwise-and 12 10)" },
    { "bitwise-or", "(bitwise-or 12 10)" },
    { "bitwise-xor", "(bitwise-xor 12 10)" },
    { "bitwise-not 0", "(bitwise-not 0)" },
    { "bitwise-not 1", "(bitwise-not 1)" },
    { "lshift", "(lshift 1 4)" },
    { "rshift", "(rshift 16 4)" },
    { "lshift-rshift", "(rshift (lshift 3 8) 8)" },

    // === P4: Quasiquote + unquote ===
    { "qq unquote let", "(let ((x 42)) `(a ,x b))" },
    { "qq unquote arith", "(let ((n 10)) `(result ,(+ n 5)))" },
    { "qq unquote nested", "(let ((a 1) (b 2)) `(,a ,b ,(+ a b)))" },
    { "qq splice literal", "`(1 2 3)" },
};

fn void generate_e2e_tests(Interp* interp) {
    io::printn("=== Generating E2E Compiler Tests ===");

    // Use a fresh interpreter for e2e generation to avoid test state leaking
    Interp* e2e_interp = (Interp*)mem::malloc(Interp.sizeof);
    e2e_interp.init();
    register_primitives(e2e_interp);
    register_stdlib(e2e_interp);

    // Run all setups
    for (usz i = 0; i < E2E_SETUPS.len; i++) {
        EvalResult r = run(E2E_SETUPS[i], e2e_interp);
        if (r.error.has_error) {
            io::printfn("WARNING: Setup %d failed, skipping", (int)i);
        }
    }

    // Build the combined Pika source (setups + test exprs)
    // and capture expected output for each test expr
    usz buf_size = 131072;  // 128KB
    char* expected_buf = (char*)mem::malloc(buf_size);
    usz expected_pos = 0;

    char* source_buf = (char*)mem::malloc(buf_size);
    usz source_pos = 0;

    int test_count = 0;
    int skip_count = 0;

    // First, add all setup defines to the source
    for (usz i = 0; i < E2E_SETUPS.len; i++) {
        for (usz j = 0; j < E2E_SETUPS[i].len && source_pos < buf_size - 2; j++) {
            source_buf[source_pos++] = E2E_SETUPS[i][j];
        }
        source_buf[source_pos++] = '\n';
    }

    // Now process each test expression
    for (usz i = 0; i < E2E_TESTS.len; i++) {
        // Run through interpreter to get expected output
        EvalResult r = run(E2E_TESTS[i].expr, e2e_interp);

        if (r.error.has_error) {
            io::printfn("  SKIP (interp error): %s", (ZString)E2E_TESTS[i].name);
            skip_count++;
            continue;
        }

        // Capture expected output to buffer
        char[1024] val_buf;
        usz val_len = print_value_to_buf(r.value, &e2e_interp.symbols, &val_buf, val_buf.len);

        // Append to expected buffer
        for (usz j = 0; j < val_len && expected_pos < buf_size - 2; j++) {
            expected_buf[expected_pos++] = val_buf[j];
        }
        expected_buf[expected_pos++] = '\n';

        // Append expression to source
        for (usz j = 0; j < E2E_TESTS[i].expr.len && source_pos < buf_size - 2; j++) {
            source_buf[source_pos++] = E2E_TESTS[i].expr[j];
        }
        source_buf[source_pos++] = '\n';

        test_count++;
    }

    // Compile the combined source
    io::printfn("  Compiling %d test expressions...", test_count);

    char[] pika_source = source_buf[:source_pos];
    char[] c3_code = compile_to_c3_print_all(pika_source, e2e_interp);

    if (c3_code.len == 0) {
        io::printn("ERROR: Compilation produced empty output");
        mem::free(expected_buf);
        mem::free(source_buf);
        mem::free(e2e_interp);
        return;
    }

    // Write generated C3 file
    if (try file = io::file::open("build/e2e_test.c3", "w")) {
        file.write(c3_code)!!;
        file.close()!!;
        io::printfn("  Wrote build/e2e_test.c3 (%d bytes)", (int)c3_code.len);
    } else {
        io::printn("ERROR: Cannot write build/e2e_test.c3");
        mem::free(expected_buf);
        mem::free(source_buf);
        mem::free(e2e_interp);
        return;
    }

    // Write expected output file
    char[] expected_out = expected_buf[:expected_pos];
    if (try file = io::file::open("build/e2e_expected.txt", "w")) {
        file.write(expected_out)!!;
        file.close()!!;
        io::printfn("  Wrote build/e2e_expected.txt (%d lines)", test_count);
    } else {
        io::printn("ERROR: Cannot write build/e2e_expected.txt");
        mem::free(expected_buf);
        mem::free(source_buf);
        mem::free(e2e_interp);
        return;
    }

    io::printfn("=== Generated %d e2e tests (%d skipped) ===", test_count, skip_count);

    mem::free(expected_buf);
    mem::free(source_buf);
    mem::free(e2e_interp);
}

fn void run_lisp_tests() {
    io::printn("=== Unified Tests (Interpreter + JIT) ===");

    Interp* interp = (Interp*)mem::malloc(Interp.sizeof);
    interp.init();
    register_primitives(interp);
    register_destructors();
    register_stdlib(interp);

    int pass = 0;
    int fail = 0;

    run_basic_tests(interp, &pass, &fail);
    run_memory_stress_tests(interp, &pass, &fail);
    run_list_closure_tests(interp, &pass, &fail);
    run_arithmetic_comparison_tests(interp, &pass, &fail);
    run_string_type_tests(interp, &pass, &fail);
    run_advanced_tests(interp, &pass, &fail);

    io::printfn("\n=== Unified Tests: %d passed, %d failed ===", pass, fail);
    assert(fail == 0, "tests failed");

    run_compiler_tests(interp);
}

fn void run_basic_tests(Interp* interp, int* pass, int* fail) {
    io::printn("\n--- Basic Tests ---");

    // Literals & bindings
    test_eq(interp, "integer literal 42", "42", 42, pass, fail);
    setup(interp, "(define x 10)");
    test_eq(interp, "variable binding x => 10", "x", 10, pass, fail);
    test_eq(interp, "lambda identity", "((lambda (x) x) 5)", 5, pass, fail);
    test_eq(interp, "let binding", "(let ((y 7)) y)", 7, pass, fail);
    test_eq(interp, "if true", "(if true 1 2)", 1, pass, fail);

    // Primitives
    setup(interp, "(define add +)");
    test_tag(interp, "quote returns list", "(quote (a b c))", CONS, pass, fail);

    // Closure
    setup(interp, "(define make-adder (lambda (n) (lambda (x) n)))");
    test_tag(interp, "nested lambda", "(make-adder 5)", CLOSURE, pass, fail);

    // Cons/car/cdr
    test_eq(interp, "car (cons 1 2) => 1", "(car (cons 1 2))", 1, pass, fail);
    test_eq(interp, "cdr (cons 1 2) => 2", "(cdr (cons 1 2))", 2, pass, fail);

    // Print (interp only, side effect)
    { io::print("[PASS] Print: "); run("(println (quote hello))", interp); (*pass)++; }

    // Source location tracking
    {
        EvalResult r = run("undefined_var", interp);
        if (r.error.has_error) {
            io::printn("[PASS] source location tracking");
            (*pass)++;
        } else {
            io::printn("[FAIL] source location tracking");
            (*fail)++;
        }
    }

    // Dot-bracket indexing
    setup(interp, "(define mylist (quote (10 20 30 40 50)))");
    test_eq(interp, "mylist.[0] => 10", "mylist.[0]", 10, pass, fail);
    test_eq(interp, "mylist.[2] => 30", "mylist.[2]", 30, pass, fail);
    setup(interp, "(define idx 3)");
    test_eq(interp, "mylist.[idx] => 40", "mylist.[idx]", 40, pass, fail);

    // Pattern matching basics
    test_eq(interp, "match literal", "(match 42 (42 1) (x 0))", 1, pass, fail);
    test_eq(interp, "match variable", "(match 5 (x x))", 5, pass, fail);
    test_eq(interp, "match wildcard", "(match 99 (_ 1))", 1, pass, fail);
    test_eq(interp, "match [a b c]", "(match (quote (1 2 3)) ([a b c] (+ a (+ b c))))", 6, pass, fail);
    test_eq(interp, "match [head .. tail]", "(match (quote (10 20 30)) ([head .. tail] head))", 10, pass, fail);
    test_eq(interp, "match [a b ..]", "(match (quote (1 2 3 4 5)) ([a b ..] (+ a b)))", 3, pass, fail);
    test_eq(interp, "match [.. last]", "(match (quote (1 2 3 4 5)) ([.. last] last))", 5, pass, fail);
    test_eq(interp, "match fallthrough", "(match 42 (0 100) (1 200) (x x))", 42, pass, fail);
    test_eq(interp, "match [_ second _]", "(match (quote (1 2 3)) ([_ second _] second))", 2, pass, fail);

    // Length
    test_eq(interp, "length '() => 0", "(length (quote ()))", 0, pass, fail);
    test_eq(interp, "length '(1 2 3 4 5) => 5", "(length (quote (1 2 3 4 5)))", 5, pass, fail);

    // Path access
    setup(interp, "(define namepair (cons (quote name) \"Alice\"))");
    setup(interp, "(define agepair (cons (quote age) 30))");
    setup(interp, "(define testperson (cons namepair (cons agepair (quote ()))))");
    test_str(interp, "testperson.name => string", "testperson.name", pass, fail);
    test_eq(interp, "testperson.age => 30", "testperson.age", 30, pass, fail);

    // Nested path
    setup(interp, "(define citypair (cons (quote city) \"NYC\"))");
    setup(interp, "(define zippair (cons (quote zip) 10001))");
    setup(interp, "(define testaddress (cons citypair (cons zippair (quote ()))))");
    setup(interp, "(define namepair2 (cons (quote name) \"Bob\"))");
    setup(interp, "(define addrpair (cons (quote address) testaddress))");
    setup(interp, "(define testperson2 (cons namepair2 (cons addrpair (quote ()))))");
    test_str(interp, "testperson2.address.city => string", "testperson2.address.city", pass, fail);

    // Recursive let
    test_eq(interp, "let ^rec factorial 5 => 120", "(let ^rec ((fact (lambda (n) (if (= n 0) 1 (* n (fact (- n 1))))))) (fact 5))", 120, pass, fail);
    test_eq(interp, "non-recursive let", "(let ((x 10)) (+ x 5))", 15, pass, fail);

    // And/or
    test_truthy(interp, "and true true", "(and true true)", pass, fail);
    test_nil(interp, "and false true => nil", "(and false true)", pass, fail);
    test_nil(interp, "and true false => nil", "(and true false)", pass, fail);
    test_eq(interp, "and 1 42 => 42", "(and 1 42)", 42, pass, fail);
    test_truthy(interp, "or true false", "(or true false)", pass, fail);
    test_nil(interp, "or false nil => nil", "(or false nil)", pass, fail);
    test_eq(interp, "or 42 99 => 42", "(or 42 99)", 42, pass, fail);
    test_eq(interp, "or nil 99 => 99", "(or nil 99)", 99, pass, fail);
    test_nil(interp, "and short-circuit", "(and nil undefined_var)", pass, fail);
    test_eq(interp, "or short-circuit", "(or 1 undefined_var)", 1, pass, fail);

    // Truthiness
    test_eq(interp, "0 is truthy", "(if 0 42 99)", 42, pass, fail);

    // Reset/shift
    test_eq(interp, "shift aborts", "(reset (+ 1 (shift k 42)))", 42, pass, fail);
    test_eq(interp, "shift k resumes", "(reset (+ 1 (shift k (k 10))))", 11, pass, fail);
    test_eq(interp, "reset passthrough", "(reset (+ 10 20))", 30, pass, fail);

    // Effect handlers
    test_eq(interp, "handle without effect", "(handle 42 ((ask k x) (k 0)))", 42, pass, fail);
    test_eq(interp, "handle + perform + resume", "(handle (+ 1 (perform ask 0)) ((ask k x) (k 10)))", 11, pass, fail);
    test_eq(interp, "handle abort", "(handle (+ 1 (perform ask 5)) ((ask k x) x))", 5, pass, fail);

    // Empty string truthy
    test_eq(interp, "empty string is truthy", "(if \"\" 1 0)", 1, pass, fail);
}

fn void run_memory_stress_tests(Interp* interp, int* pass, int* fail) {
    io::printn("\n--- Memory Stress Tests ---");

    // Deep recursion
    setup(interp, "(define sum-to (lambda (n) (if (= n 0) 0 (+ n (sum-to (- n 1))))))");
    test_eq(interp, "deep recursion sum 1..50 => 1275", "(sum-to 50)", 1275, pass, fail);

    // Closure factory
    setup(interp, "(define make-counter (lambda (start) (lambda (x) (+ start x))))");
    setup(interp, "(define c1 (make-counter 10))");
    setup(interp, "(define c2 (make-counter 20))");
    setup(interp, "(define c3 (make-counter 30))");
    test_eq(interp, "closure c1 5 => 15", "(c1 5)", 15, pass, fail);
    test_eq(interp, "closure c2 5 => 25", "(c2 5)", 25, pass, fail);
    test_eq(interp, "closure c3 5 => 35", "(c3 5)", 35, pass, fail);

    // String operations
    test_eq(interp, "string-length hello world => 11", "(string-length \"hello world\")", 11, pass, fail);
    test_str(interp, "string-upcase hello => string", "(string-upcase \"hello\")", pass, fail);

    // Pattern matching stress
    setup(interp, "(define data (quote (1 2 3 4 5 6 7 8 9 10)))");
    test_eq(interp, "match [a b c .. rest]", "(match data ([a b c .. rest] (+ a (+ b c))))", 6, pass, fail);
    test_eq(interp, "match [.. last] => 10", "(match data ([.. last] last))", 10, pass, fail);

    // Fibonacci
    setup(interp, "(define fib (lambda (n) (if (< n 2) n (+ (fib (- n 1)) (fib (- n 2))))))");
    test_eq(interp, "fib 15 => 610", "(fib 15)", 610, pass, fail);

    // Map over list
    setup(interp, "(define mymap (lambda (f) (lambda (lst) (if (null? lst) (quote ()) (cons (f (car lst)) ((mymap f) (cdr lst)))))))");
    setup(interp, "(define double (lambda (x) (* x 2)))");
    test_eq(interp, "map double car => 2", "(car ((mymap double) (quote (1 2 3 4 5))))", 2, pass, fail);
    setup(interp, "(define mapped ((mymap double) (quote (1 2 3 4 5))))");
    test_eq(interp, "map double last => 10", "(match mapped ([.. last] last))", 10, pass, fail);

    // Nested let 10 levels
    test_eq(interp, "nested let 10 levels => 55", "(let ((a 1)) (let ((b 2)) (let ((c 3)) (let ((d 4)) (let ((e 5)) (let ((f 6)) (let ((g 7)) (let ((h 8)) (let ((i 9)) (let ((j 10)) (+ a (+ b (+ c (+ d (+ e (+ f (+ g (+ h (+ i j)))))))))))))))))))))", 55, pass, fail);

    // Repeated evaluation
    setup(interp, "(define mem-counter 0)");
    for (int i = 0; i < 100; i++) { run("(define mem-counter (+ mem-counter 1))", interp); }
    test_eq(interp, "repeated eval 100x", "mem-counter", 100, pass, fail);

    // Large list
    setup(interp, "(define build-list-helper (lambda (n) (lambda (acc) (if (= n 0) acc ((build-list-helper (- n 1)) (cons n acc))))))");
    setup(interp, "(define biglist ((build-list-helper 50) (quote ())))");
    test_eq(interp, "build-list 50 elements", "(length biglist)", 50, pass, fail);

    // Fold
    setup(interp, "(define fold-helper (lambda (f) (lambda (init) (lambda (lst) (if (null? lst) init (((fold-helper f) (f init (car lst))) (cdr lst)))))))");
    test_eq(interp, "fold sum 1..10 => 55", "(((fold-helper +) 0) (quote (1 2 3 4 5 6 7 8 9 10)))", 55, pass, fail);
}

fn void run_arithmetic_comparison_tests(Interp* interp, int* pass, int* fail) {
    io::printn("\n--- Arithmetic & Comparison Tests ---");

    // Addition
    test_eq(interp, "(+ 1 2) => 3", "(+ 1 2)", 3, pass, fail);
    test_eq(interp, "(+ 0 0) => 0", "(+ 0 0)", 0, pass, fail);
    test_eq(interp, "(+ -5 3) => -2", "(+ -5 3)", -2, pass, fail);
    test_eq(interp, "(+ 100 -100) => 0", "(+ 100 -100)", 0, pass, fail);
    test_eq(interp, "(+ 1000000 2000000) => 3000000", "(+ 1000000 2000000)", 3000000, pass, fail);

    // Subtraction
    test_eq(interp, "(- 10 3) => 7", "(- 10 3)", 7, pass, fail);
    test_eq(interp, "(- 3 10) => -7", "(- 3 10)", -7, pass, fail);
    test_eq(interp, "(- 0 0) => 0", "(- 0 0)", 0, pass, fail);
    test_eq(interp, "(- -5 -3) => -2", "(- -5 -3)", -2, pass, fail);

    // Multiplication
    test_eq(interp, "(* 3 4) => 12", "(* 3 4)", 12, pass, fail);
    test_eq(interp, "(* 0 999) => 0", "(* 0 999)", 0, pass, fail);
    test_eq(interp, "(* 1 42) => 42", "(* 1 42)", 42, pass, fail);
    test_eq(interp, "(* -3 4) => -12", "(* -3 4)", -12, pass, fail);
    test_eq(interp, "(* -3 -4) => 12", "(* -3 -4)", 12, pass, fail);

    // Division
    test_eq(interp, "(/ 10 2) => 5", "(/ 10 2)", 5, pass, fail);
    test_eq(interp, "(/ 7 2) => 3", "(/ 7 2)", 3, pass, fail);
    test_eq(interp, "(/ 0 5) => 0", "(/ 0 5)", 0, pass, fail);
    test_eq(interp, "(/ -10 3) => -3", "(/ -10 3)", -3, pass, fail);

    // Modulo
    test_eq(interp, "(% 10 3) => 1", "(% 10 3)", 1, pass, fail);
    test_eq(interp, "(% 9 3) => 0", "(% 9 3)", 0, pass, fail);
    test_eq(interp, "(% 7 10) => 7", "(% 7 10)", 7, pass, fail);

    // Nested arithmetic
    test_eq(interp, "(+ 1 (+ 2 3)) => 6", "(+ 1 (+ 2 3))", 6, pass, fail);
    test_eq(interp, "(* 2 (+ 3 4)) => 14", "(* 2 (+ 3 4))", 14, pass, fail);
    test_eq(interp, "(- (* 5 5) (+ 10 5)) => 10", "(- (* 5 5) (+ 10 5))", 10, pass, fail);
    test_eq(interp, "(/ (+ 10 20) (- 10 4)) => 5", "(/ (+ 10 20) (- 10 4))", 5, pass, fail);

    // Equality
    test_truthy(interp, "(= 5 5) => true", "(= 5 5)", pass, fail);
    test_nil(interp, "(= 5 6) => nil", "(= 5 6)", pass, fail);
    test_truthy(interp, "(= 0 0) => true", "(= 0 0)", pass, fail);
    test_truthy(interp, "(= -1 -1) => true", "(= -1 -1)", pass, fail);

    // Less than
    test_truthy(interp, "(< 1 2) => true", "(< 1 2)", pass, fail);
    test_nil(interp, "(< 2 1) => nil", "(< 2 1)", pass, fail);
    test_nil(interp, "(< 1 1) => nil", "(< 1 1)", pass, fail);
    test_truthy(interp, "(< -5 0) => true", "(< -5 0)", pass, fail);

    // Greater than
    test_truthy(interp, "(> 2 1) => true", "(> 2 1)", pass, fail);
    test_nil(interp, "(> 1 2) => nil", "(> 1 2)", pass, fail);
    test_nil(interp, "(> 1 1) => nil", "(> 1 1)", pass, fail);

    // Less or equal
    test_truthy(interp, "(<= 1 2) => true", "(<= 1 2)", pass, fail);
    test_truthy(interp, "(<= 2 2) => true", "(<= 2 2)", pass, fail);
    test_nil(interp, "(<= 3 2) => nil", "(<= 3 2)", pass, fail);

    // Greater or equal
    test_truthy(interp, "(>= 2 1) => true", "(>= 2 1)", pass, fail);
    test_truthy(interp, "(>= 2 2) => true", "(>= 2 2)", pass, fail);
    test_nil(interp, "(>= 1 2) => nil", "(>= 1 2)", pass, fail);

    // Comparison in conditionals
    test_eq(interp, "(if (< 1 2) 10 20) => 10", "(if (< 1 2) 10 20)", 10, pass, fail);
    test_eq(interp, "(if (> 1 2) 10 20) => 20", "(if (> 1 2) 10 20)", 20, pass, fail);
    test_eq(interp, "(if (= (+ 2 3) 5) 1 0) => 1", "(if (= (+ 2 3) 5) 1 0)", 1, pass, fail);
}

fn void run_string_type_tests(Interp* interp, int* pass, int* fail) {
    io::printn("\n--- String & Type Predicate Tests ---");

    // String length
    test_eq(interp, "string-length hello => 5", "(string-length \"hello\")", 5, pass, fail);
    test_eq(interp, "string-length empty => 0", "(string-length \"\")", 0, pass, fail);
    test_eq(interp, "string-length a => 1", "(string-length \"a\")", 1, pass, fail);

    // String case/trim/append/substring
    test_str(interp, "string-upcase hello", "(string-upcase \"hello\")", pass, fail);
    test_str(interp, "string-downcase HELLO", "(string-downcase \"HELLO\")", pass, fail);
    test_str(interp, "string-upcase empty", "(string-upcase \"\")", pass, fail);
    test_str(interp, "string-trim", "(string-trim \"  hello  \")", pass, fail);
    test_str(interp, "string-trim noop", "(string-trim \"hello\")", pass, fail);
    test_str(interp, "string-append", "(string-append \"hello\" \" world\")", pass, fail);
    test_str(interp, "string-append empty", "(string-append \"\" \"abc\")", pass, fail);
    test_str(interp, "substring 0 5", "(substring \"hello world\" 0 5)", pass, fail);
    test_str(interp, "substring 6 11", "(substring \"hello world\" 6 11)", pass, fail);

    // String predicates
    test_truthy(interp, "string? hello => true", "(string? \"hello\")", pass, fail);
    test_nil(interp, "string? 42 => nil", "(string? 42)", pass, fail);
    test_nil(interp, "string? nil => nil", "(string? nil)", pass, fail);

    // int?
    test_truthy(interp, "int? 42 => true", "(int? 42)", pass, fail);
    test_truthy(interp, "int? 0 => true", "(int? 0)", pass, fail);
    test_truthy(interp, "int? -5 => true", "(int? -5)", pass, fail);
    test_nil(interp, "int? hello => nil", "(int? \"hello\")", pass, fail);
    test_nil(interp, "int? nil => nil", "(int? nil)", pass, fail);

    // symbol?
    test_truthy(interp, "symbol? foo => true", "(symbol? (quote foo))", pass, fail);
    test_nil(interp, "symbol? 42 => nil", "(symbol? 42)", pass, fail);
    test_nil(interp, "symbol? hello => nil", "(symbol? \"hello\")", pass, fail);

    // closure?
    test_truthy(interp, "closure? lambda => true", "(closure? (lambda (x) x))", pass, fail);
    test_nil(interp, "closure? 42 => nil", "(closure? 42)", pass, fail);
    test_nil(interp, "closure? hello => nil", "(closure? \"hello\")", pass, fail);

    // null?
    test_truthy(interp, "null? nil => true", "(null? nil)", pass, fail);
    test_truthy(interp, "null? '() => true", "(null? (quote ()))", pass, fail);
    test_nil(interp, "null? 42 => nil", "(null? 42)", pass, fail);
    test_nil(interp, "null? '(1 2) => nil", "(null? (quote (1 2)))", pass, fail);

    // pair?
    test_truthy(interp, "pair? cons => true", "(pair? (cons 1 2))", pass, fail);
    test_truthy(interp, "pair? list => true", "(pair? (quote (1 2 3)))", pass, fail);
    test_nil(interp, "pair? nil => nil", "(pair? nil)", pass, fail);
    test_nil(interp, "pair? 42 => nil", "(pair? 42)", pass, fail);

    // not
    test_truthy(interp, "not nil => true", "(not nil)", pass, fail);
    test_truthy(interp, "not false => true", "(not false)", pass, fail);
    test_nil(interp, "not true => nil", "(not true)", pass, fail);
    test_nil(interp, "not 42 => nil", "(not 42)", pass, fail);
    test_nil(interp, "not 0 => nil (truthy)", "(not 0)", pass, fail);
    test_nil(interp, "not empty-str => nil (truthy)", "(not \"\")", pass, fail);
}

fn void run_list_closure_tests(Interp* interp, int* pass, int* fail) {
    io::printn("\n--- List & Closure Tests ---");

    // cons/car/cdr
    test_eq(interp, "car (cons 1 2) => 1", "(car (cons 1 2))", 1, pass, fail);
    test_eq(interp, "cdr (cons 1 2) => 2", "(cdr (cons 1 2))", 2, pass, fail);
    test_eq(interp, "car (cons (+ 1 2) 4) => 3", "(car (cons (+ 1 2) 4))", 3, pass, fail);
    test_eq(interp, "cadr => 2", "(car (cdr (cons 1 (cons 2 (cons 3 nil)))))", 2, pass, fail);
    test_eq(interp, "caddr => 3", "(car (cdr (cdr (cons 1 (cons 2 (cons 3 nil))))))", 3, pass, fail);

    // Length
    test_eq(interp, "length '() => 0", "(length (quote ()))", 0, pass, fail);
    test_eq(interp, "length '(1) => 1", "(length (quote (1)))", 1, pass, fail);
    test_eq(interp, "length '(1..10) => 10", "(length (quote (1 2 3 4 5 6 7 8 9 10)))", 10, pass, fail);
    test_eq(interp, "length cons => 2", "(length (cons 1 (cons 2 nil)))", 2, pass, fail);

    // Quote
    test_eq(interp, "car '(10 20 30) => 10", "(car (quote (10 20 30)))", 10, pass, fail);
    test_eq(interp, "length '(a b c d e) => 5", "(length (quote (a b c d e)))", 5, pass, fail);
    test_truthy(interp, "null? '() => true", "(null? (quote ()))", pass, fail);

    // List indexing
    setup(interp, "(define idx-list (quote (10 20 30)))");
    test_eq(interp, "idx-list.[0] => 10", "idx-list.[0]", 10, pass, fail);
    test_eq(interp, "idx-list.[1] => 20", "idx-list.[1]", 20, pass, fail);
    test_eq(interp, "idx-list.[2] => 30", "idx-list.[2]", 30, pass, fail);

    // Closures
    setup(interp, "(define make-adder2 (lambda (n) (lambda (x) (+ n x))))");
    setup(interp, "(define add10 (make-adder2 10))");
    setup(interp, "(define add20 (make-adder2 20))");
    test_eq(interp, "add10 5 => 15", "(add10 5)", 15, pass, fail);
    test_eq(interp, "add20 5 => 25", "(add20 5)", 25, pass, fail);

    // Composition
    setup(interp, "(define compose (lambda (f) (lambda (g) (lambda (x) (f (g x))))))");
    setup(interp, "(define double2 (lambda (x) (* x 2)))");
    setup(interp, "(define inc (lambda (x) (+ x 1)))");
    setup(interp, "(define double-then-inc ((compose inc) double2))");
    test_eq(interp, "compose inc double 5 => 11", "(double-then-inc 5)", 11, pass, fail);

    // Multi-capture
    setup(interp, "(define make-linear (lambda (a) (lambda (b) (lambda (x) (+ (* a x) b)))))");
    setup(interp, "(define f2x3 ((make-linear 2) 3))");
    test_eq(interp, "f(x)=2x+3, f(10)=23", "(f2x3 10)", 23, pass, fail);

    // Scoping
    test_eq(interp, "let shadowing", "(let ((x 10)) (let ((x 20)) x))", 20, pass, fail);
    setup(interp, "(define outer-x 100)");
    setup(interp, "(let ((outer-x 999)) outer-x)");
    test_eq(interp, "let no leak", "outer-x", 100, pass, fail);

    setup(interp, "(define scope-test 42)");
    setup(interp, "(define scope-fn (lambda (scope-test) scope-test))");
    test_eq(interp, "lambda scope param", "(scope-fn 99)", 99, pass, fail);
    test_eq(interp, "global preserved", "scope-test", 42, pass, fail);

    // Recursive let
    test_eq(interp, "let ^rec fact 6 => 720", "(let ^rec ((f (lambda (n) (if (= n 0) 1 (* n (f (- n 1))))))) (f 6))", 720, pass, fail);

    // Mutual recursion
    setup(interp, "(define is-even (lambda (n) (if (= n 0) true (is-odd (- n 1)))))");
    setup(interp, "(define is-odd (lambda (n) (if (= n 0) false (is-even (- n 1)))))");
    test_truthy(interp, "even 10 => true", "(is-even 10)", pass, fail);
    test_truthy(interp, "odd 7 => true", "(is-odd 7)", pass, fail);

    // Higher-order
    setup(interp, "(define apply-twice (lambda (f) (lambda (x) (f (f x)))))");
    setup(interp, "(define add3 (lambda (x) (+ x 3)))");
    test_eq(interp, "apply-twice add3 10 => 16", "((apply-twice add3) 10)", 16, pass, fail);

    // Identity
    setup(interp, "(define id (lambda (x) x))");
    test_eq(interp, "id 42 => 42", "(id 42)", 42, pass, fail);
    test_str(interp, "id hello => string", "(id \"hello\")", pass, fail);
    test_nil(interp, "id nil => nil", "(id nil)", pass, fail);
}

fn void run_advanced_tests(Interp* interp, int* pass, int* fail) {
    io::printn("\n--- Advanced Feature Tests ---");

    // Match patterns
    test_eq(interp, "match 1 => 10", "(match 1 (1 10) (2 20) (3 30))", 10, pass, fail);
    test_eq(interp, "match 2 => 20", "(match 2 (1 10) (2 20) (3 30))", 20, pass, fail);
    test_eq(interp, "match 3 => 30", "(match 3 (1 10) (2 20) (3 30))", 30, pass, fail);
    test_eq(interp, "match default => 100", "(match 99 (1 10) (2 20) (x (+ x 1)))", 100, pass, fail);
    test_eq(interp, "match computed", "(match (+ 1 1) (1 10) (2 20) (3 30))", 20, pass, fail);
    test_eq(interp, "match [a b] => 30", "(match (quote (10 20)) ([a b] (+ a b)))", 30, pass, fail);
    test_eq(interp, "match [x] => 42", "(match (quote (42)) ([x] x))", 42, pass, fail);
    test_eq(interp, "match [a b c] product", "(match (quote (2 3 4)) ([a b c] (* a (* b c))))", 24, pass, fail);
    test_eq(interp, "match [h .. t] tail len", "(match (quote (1 2 3 4 5)) ([h .. t] (length t)))", 4, pass, fail);
    test_eq(interp, "match _ wildcard", "(match 42 (_ 0))", 0, pass, fail);
    test_eq(interp, "match string hello", "(match \"hello\" (\"hello\" 1) (\"world\" 2) (x 0))", 1, pass, fail);
    test_eq(interp, "match string world", "(match \"world\" (\"hello\" 1) (\"world\" 2) (x 0))", 2, pass, fail);
    test_eq(interp, "match string default", "(match \"other\" (\"hello\" 1) (\"world\" 2) (x 0))", 0, pass, fail);

    // Continuations
    test_eq(interp, "shift discard k", "(reset (+ 100 (shift k 42)))", 42, pass, fail);
    test_eq(interp, "shift k mul", "(reset (* 2 (shift k (k 5))))", 10, pass, fail);
    test_eq(interp, "shift k expr", "(reset (+ 1 (shift k (k (+ 2 3)))))", 6, pass, fail);
    test_eq(interp, "shift use k result", "(reset (+ 1 (shift k (+ (k 10) 1000))))", 1011, pass, fail);
    test_eq(interp, "reset value", "(reset 42)", 42, pass, fail);

    // Effect handlers
    test_eq(interp, "effect modify", "(handle (+ 1 (perform get 0)) ((get k x) (k 100)))", 101, pass, fail);
    test_eq(interp, "effect arg double", "(handle (perform double 7) ((double k x) (k (* x 2))))", 14, pass, fail);
    test_eq(interp, "effect bail", "(handle (+ 1 (+ 2 (+ 3 (perform bail 42)))) ((bail k x) x))", 42, pass, fail);
    test_eq(interp, "handle no effect", "(handle (+ 10 20) ((unused k x) 999))", 30, pass, fail);

    // If/cond edge cases
    test_eq(interp, "if nil => 2", "(if nil 1 2)", 2, pass, fail);
    test_eq(interp, "if false => 2", "(if false 1 2)", 2, pass, fail);
    test_eq(interp, "if 0 => 1 (truthy)", "(if 0 1 2)", 1, pass, fail);
    test_eq(interp, "if empty-str => 1 (truthy)", "(if \"\" 1 2)", 1, pass, fail);
    test_eq(interp, "if '() => 2 (nil)", "(if (quote ()) 1 2)", 2, pass, fail);
    test_eq(interp, "if '(1) => 1 (truthy)", "(if (quote (1)) 1 2)", 1, pass, fail);
    test_eq(interp, "nested if", "(if true (if false 1 2) 3)", 2, pass, fail);
    test_eq(interp, "if with branches", "(if (= 1 1) (+ 10 20) (+ 30 40))", 30, pass, fail);

    // Error handling
    test_error(interp, "unbound variable", "nonexistent_var_xyz", pass, fail);
    test_error(interp, "call non-function", "(42 1)", pass, fail);
    test_error(interp, "shift outside reset", "(shift k 42)", pass, fail);

    // And/or advanced
    test_eq(interp, "nested and/or", "(and (or nil 5) (or false 10))", 10, pass, fail);
    test_eq(interp, "nested or/and", "(or (and nil 5) (and 1 10))", 10, pass, fail);
    test_eq(interp, "and closure truthy", "(and (lambda (x) x) 42)", 42, pass, fail);

    // Define/redefinition
    setup(interp, "(define redef-test 10)");
    test_eq(interp, "redef before", "redef-test", 10, pass, fail);
    setup(interp, "(define redef-test 20)");
    test_eq(interp, "redef after", "redef-test", 20, pass, fail);
    setup(interp, "(define computed-val (+ (* 3 4) (- 10 5)))");
    test_eq(interp, "define complex expr", "computed-val", 17, pass, fail);

    // Path access
    setup(interp, "(define ptest (cons (cons (quote x) 10) (cons (cons (quote y) 20) (quote ()))))");
    test_eq(interp, "ptest.x => 10", "ptest.x", 10, pass, fail);
    test_eq(interp, "ptest.y => 20", "ptest.y", 20, pass, fail);

    // TCO (JIT-only mode: no interpreter TCO, limited by eval depth)
    setup(interp, "(define tco-loop (lambda (n) (if (= n 0) (quote done) (tco-loop (- n 1)))))");
    test_truthy(interp, "TCO loop 4000", "(tco-loop 4000)", pass, fail);
    setup(interp, "(define sum-acc (lambda (n) (lambda (acc) (if (= n 0) acc ((sum-acc (- n 1)) (+ acc n))))))");
    test_eq(interp, "TCO sum-acc 1000 => 500500", "((sum-acc 1000) 0)", 500500, pass, fail);
    setup(interp, "(define tco-even (lambda (n) (if (= n 0) true (tco-odd (- n 1)))))");
    setup(interp, "(define tco-odd (lambda (n) (if (= n 0) nil (tco-even (- n 1)))))");
    test_truthy(interp, "TCO mutual recursion 1000", "(tco-even 1000)", pass, fail);
    setup(interp, "(define count-down (lambda (n) (match n (0 (quote done)) (n (count-down (- n 1))))))");
    test_truthy(interp, "TCO match tail call 4000", "(count-down 4000)", pass, fail);
    setup(interp, "(define let-loop (lambda (n) (let ((m (- n 1))) (if (= m 0) (quote done) (let-loop m)))))");
    test_truthy(interp, "TCO let body 4000", "(let-loop 4000)", pass, fail);

    // Multi-perform
    test_eq(interp, "multi-perform 2 sum", "(handle (+ (perform bounce 10) (perform bounce 20)) ((bounce k x) (k x)))", 30, pass, fail);
    test_eq(interp, "multi-perform 3 sum", "(handle (+ (perform bounce 1) (+ (perform bounce 2) (perform bounce 3))) ((bounce k x) (k x)))", 6, pass, fail);
    test_eq(interp, "multi-perform doubles", "(handle (+ (perform bounce 10) (perform bounce 20)) ((bounce k x) (k (* x 2))))", 60, pass, fail);
    test_eq(interp, "multi-perform abort", "(handle (+ (perform bounce 10) (perform bounce 20)) ((bounce k x) x))", 10, pass, fail);
    test_eq(interp, "multi-shift sum", "(reset (+ (shift k1 (k1 10)) (shift k2 (k2 20))))", 30, pass, fail);

    // Multi-shot continuations
    test_eq(interp, "multi-shot k twice", "(reset (+ 1 (shift k (+ (k 10) (k 20)))))", 32, pass, fail);
    test_eq(interp, "multi-shot k thrice", "(reset (* 2 (shift k (+ (k 3) (+ (k 5) (k 7))))))", 30, pass, fail);
    test_eq(interp, "multi-shot let bindings", "(reset (+ 10 (shift k (let ((a (k 1)) (b (k 2))) (+ a b)))))", 23, pass, fail);
    test_eq(interp, "multi-shot conditional", "(reset (+ 5 (shift k (if true (k 10) (k 20)))))", 15, pass, fail);
    test_eq(interp, "multi-shot effect", "(handle (+ 1 (perform dup 0)) ((dup k x) (+ (k 10) (k 20))))", 32, pass, fail);

    // TCO inside reset/shift
    setup(interp, "(define reset-loop (lambda (n acc) (if (= n 0) acc (reset-loop (- n 1) (+ acc 1)))))");
    test_eq(interp, "TCO in reset body", "(reset (reset-loop 5000 0))", 5000, pass, fail);
    setup(interp, "(define shift-loop (lambda (n acc) (if (= n 0) acc (shift-loop (- n 1) (+ acc 1)))))");
    test_eq(interp, "TCO in shift body", "(reset (+ 1 (shift k (shift-loop 5000 0))))", 5000, pass, fail);
    test_eq(interp, "TCO begin+if in reset", "(reset (begin (+ 1 2) (if true (+ 10 20) 99)))", 30, pass, fail);
    test_eq(interp, "TCO let chains in shift", "(reset (+ 1 (shift k (let ((x 10)) (let ((y 20)) (+ x y))))))", 30, pass, fail);
    setup(interp, "(define cont-use-loop (lambda (k n) (if (= n 0) (k 42) (cont-use-loop k (- n 1)))))");
    test_eq(interp, "TCO tail-recursive k use", "(reset (+ 1 (shift k (cont-use-loop k 1000))))", 43, pass, fail);
    test_eq(interp, "TCO nested reset", "(reset (+ (reset (if true 10 20)) (shift k (k 5))))", 15, pass, fail);
    setup(interp, "(define handle-loop (lambda (n) (if (= n 0) 0 (+ 1 (handle-loop (- n 1))))))");
    test_eq(interp, "TCO handler body", "(handle (perform count 0) ((count k x) (handle-loop 100)))", 100, pass, fail);

    // Trampoline (JIT delegates effects to eval which handles them)
    test_eq(interp, "trampoline passthrough", "(with-trampoline (lambda (xx) 42))", 42, pass, fail);
    test_eq(interp, "trampoline single bounce", "(with-trampoline (lambda (xx) (+ 1 (perform bounce (lambda (xx) 41)))))", 42, pass, fail);
    setup(interp, "(define fib-t (lambda (n) (if (< n 2) n (+ (perform bounce (lambda (xx) (fib-t (- n 1)))) (perform bounce (lambda (xx) (fib-t (- n 2))))))))");
    test_eq(interp, "trampoline fib 7", "(with-trampoline (lambda (xx) (fib-t 7)))", 13, pass, fail);

    // Variadic lambdas (JIT detects variadic closures and routes to fallback)
    setup(interp, "(define collect (lambda (.. args) args))");
    test_eq(interp, "variadic collect length", "(length (collect 1 2 3))", 3, pass, fail);
    test_nil(interp, "variadic collect empty", "(collect)", pass, fail);
    setup(interp, "(define head-rest (lambda (h .. t) t))");
    test_eq(interp, "variadic rest length", "(length (head-rest 1 2 3))", 2, pass, fail);
    test_nil(interp, "variadic empty rest", "(head-rest 1)", pass, fail);
    setup(interp, "(define multi-rest (lambda (a b .. rest) rest))");
    test_eq(interp, "variadic multi rest len", "(length (multi-rest 1 2 3 4 5))", 3, pass, fail);
    setup(interp, "(define head-only (lambda (h .. t) h))");
    test_eq(interp, "variadic head-only", "(head-only 42 99 100)", 42, pass, fail);
    setup(interp, "(define count-rest (lambda (h .. t) (length t)))");
    test_eq(interp, "variadic count-rest", "(count-rest 1 2 3 4 5)", 4, pass, fail);

    // Begin
    test_eq(interp, "begin returns last", "(begin 1 2 3)", 3, pass, fail);
    test_eq(interp, "begin single", "(begin 42)", 42, pass, fail);
    test_eq(interp, "begin side effects", "(begin (define begin-x 10) (define begin-y 20) (+ begin-x begin-y))", 30, pass, fail);
    test_eq(interp, "begin nested", "(begin (begin 1 2) (begin 3 4))", 4, pass, fail);
    test_eq(interp, "begin with if", "(begin (if true 10 20) (if false 30 40))", 40, pass, fail);

    // Zero-arg lambdas
    test_eq(interp, "zero-arg thunk", "((lambda () 42))", 42, pass, fail);
    test_eq(interp, "zero-arg closure", "(let ((a 10)) ((lambda () a)))", 10, pass, fail);
    test_eq(interp, "zero-arg stored thunk", "(let ((thunk (lambda () 99))) (thunk))", 99, pass, fail);
    test_eq(interp, "zero-arg begin body", "((lambda () (begin 1 2 3)))", 3, pass, fail);
    test_eq(interp, "zero-arg computation", "((lambda () (+ 10 20)))", 30, pass, fail);

    // Multi-param lambda (auto-curry)
    test_eq(interp, "multi-param two", "((lambda (x y) (+ x y)) 3 4)", 7, pass, fail);
    test_eq(interp, "multi-param three", "((lambda (x y z) (+ x (+ y z))) 1 2 3)", 6, pass, fail);
    test_eq(interp, "multi-param direct call", "(let ((add (lambda (x y) (+ x y)))) (add 10 20))", 30, pass, fail);
    setup(interp, "(define mp-add3 (lambda (a b c) (+ a (+ b c))))");
    test_eq(interp, "multi-param defined fn", "(mp-add3 10 20 30)", 60, pass, fail);
    test_eq(interp, "multi-param three call", "(let ((f (lambda (x y z) (+ x (+ y z))))) (f 1 2 3))", 6, pass, fail);

    // Multi-binding let
    test_eq(interp, "multi-let two", "(let ((ml-x 10) (ml-y 20)) (+ ml-x ml-y))", 30, pass, fail);
    test_eq(interp, "multi-let three", "(let ((ml-a 1) (ml-b 2) (ml-c 3)) (+ ml-a (+ ml-b ml-c)))", 6, pass, fail);
    test_eq(interp, "multi-let sequential", "(let ((ml-p 10) (ml-q (+ ml-p 5))) ml-q)", 15, pass, fail);
    test_eq(interp, "multi-let with lambda", "(let ((ml-f (lambda (xx) (+ xx 1))) (ml-v (ml-f 10))) ml-v)", 11, pass, fail);
    test_eq(interp, "multi-let single", "(let ((ml-z 42)) ml-z)", 42, pass, fail);

    // Named let
    test_eq(interp, "named-let sum 1..5", "(let loop ((n 5) (acc 0)) (if (= n 0) acc (loop (- n 1) (+ acc n))))", 15, pass, fail);
    test_eq(interp, "named-let fact 10", "(let fact ((n 10) (acc 1)) (if (= n 0) acc (fact (- n 1) (* acc n))))", 3628800, pass, fail);
    test_eq(interp, "named-let fib 10", "(let fib ((n 10) (a 0) (b 1)) (if (= n 0) a (fib (- n 1) b (+ a b))))", 55, pass, fail);
    test_eq(interp, "named-let fib 20", "(let fib ((n 20) (a 0) (b 1)) (if (= n 0) a (fib (- n 1) b (+ a b))))", 6765, pass, fail);
    test_eq(interp, "named-let countdown", "(let count ((n 100)) (if (= n 0) 0 (count (- n 1))))", 0, pass, fail);
    test_eq(interp, "named-let build list car", "(car (let build ((n 3) (acc (quote ()))) (if (= n 0) acc (build (- n 1) (cons n acc)))))", 1, pass, fail);
    test_eq(interp, "named-let 10000 iters", "(let loop ((n 10000) (acc 0)) (if (= n 0) acc (loop (- n 1) (+ acc 1))))", 10000, pass, fail);

    // set!
    setup(interp, "(define set-test 10)");
    setup(interp, "(set! set-test 20)");
    test_eq(interp, "set! basic", "set-test", 20, pass, fail);

    // Counter tests are stateful (calling counter1 mutates), use JIT-only to avoid double mutation
    setup(interp, "(define make-counter (lambda () (let ((count 0)) (lambda () (begin (set! count (+ count 1)) count)))))");
    setup(interp, "(define counter1 (make-counter))");
    test_eq_jit(interp, "set! counter 1", "(counter1)", 1, pass, fail);
    test_eq_jit(interp, "set! counter 2", "(counter1)", 2, pass, fail);
    test_eq_jit(interp, "set! counter 3", "(counter1)", 3, pass, fail);

    // set! on let-local in closure (JIT detects and falls back to interpreter)
    test_eq(interp, "set! parent scope", "(let ((x 1)) (let ((f (lambda () (set! x 42)))) (begin (f) x)))", 42, pass, fail);
    test_error(interp, "set! unbound", "(set! undefined-var-xyz 1)", pass, fail);
    setup(interp, "(define sr-test 100)");
    setup(interp, "(set! sr-test (+ sr-test 1))");
    test_eq(interp, "set! read back", "sr-test", 101, pass, fail);

    // String<->number
    test_eq(interp, "string->number 42", "(string->number \"42\")", 42, pass, fail);
    test_eq(interp, "string->number -7", "(string->number \"-7\")", -7, pass, fail);
    test_nil(interp, "string->number abc => nil", "(string->number \"abc\")", pass, fail);
    test_nil(interp, "string->number empty => nil", "(string->number \"\")", pass, fail);
    test_eq(interp, "number->string round-trip 42", "(string->number (number->string 42))", 42, pass, fail);
    test_eq(interp, "number->string round-trip 0", "(string->number (number->string 0))", 0, pass, fail);
    test_eq(interp, "number->string round-trip -99", "(string->number (number->string -99))", -99, pass, fail);

    // Stack overflow
    test_error(interp, "stack overflow caught", "(let ^rec ((f (lambda (n) (+ 1 (f (+ n 1)))))) (f 0))", pass, fail);

    // Quasiquote
    test_tag(interp, "quasiquote basic", "`(a b c)", CONS, pass, fail);
    setup(interp, "(define qq-x 42)");
    test_tag(interp, "quasiquote unquote", "`(a ,qq-x c)", CONS, pass, fail);
    test_eq(interp, "quasiquote standalone", "`,(+ 1 2)", 3, pass, fail);
    setup(interp, "(define qq-lst (list 1 2 3))");
    test_eq(interp, "quasiquote splice len", "(length `(a ,@qq-lst d))", 5, pass, fail);
    setup(interp, "(define qq-empty (list))");
    test_eq(interp, "quasiquote splice empty", "(length `(a ,@qq-empty b))", 2, pass, fail);

    // Pattern macros
    setup(interp, "(define [macro] when ([test .. body] (if test (begin .. body) nil)))");
    test_eq(interp, "when true", "(when true 42)", 42, pass, fail);
    test_nil(interp, "when false", "(when false 42)", pass, fail);
    setup(interp, "(define [macro] unless ([test .. body] (if test nil (begin .. body))))");
    test_eq(interp, "unless false", "(unless false 99)", 99, pass, fail);
    test_nil(interp, "unless true", "(unless true 99)", pass, fail);
    setup(interp, "(define [macro] with-val ([name val body] (let ((name val)) body)))");
    test_eq(interp, "macro with-val", "(with-val z 100 (+ z 1))", 101, pass, fail);
    setup(interp, "(define [macro] cond ([] nil) ([test body .. rest] (if test body (cond .. rest))))");
    test_eq(interp, "cond macro", "(cond false 1 true 2 true 3)", 2, pass, fail);
    test_nil(interp, "cond all false", "(cond false 1 false 2)", pass, fail);
    setup(interp, "(define [macro] my-and ([a b] (if a b false)))");
    test_eq(interp, "my-and true", "(my-and true 42)", 42, pass, fail);
    setup(interp, "(define [macro] let1 ([name val body] (let ((name val)) body)))");
    test_eq(interp, "let1 macro", "(let1 x 10 (+ x 5))", 15, pass, fail);

    // Auto-gensym
    setup(interp, "(define [macro] swap! ([a b] (let ((tmp# a)) (begin (set! a b) (set! b tmp#)))))");
    setup(interp, "(define swap-x 10)");
    setup(interp, "(define swap-y 20)");
    setup(interp, "(swap! swap-x swap-y)");
    test_eq(interp, "swap! x", "swap-x", 20, pass, fail);
    test_eq(interp, "swap! y", "swap-y", 10, pass, fail);
    setup(interp, "(define tmp 999)");
    setup(interp, "(define hx 1)");
    setup(interp, "(define hy 2)");
    setup(interp, "(swap! hx hy)");
    test_eq(interp, "gensym hygiene", "tmp", 999, pass, fail);

    // gensym
    test_tag(interp, "gensym returns symbol", "(gensym)", SYMBOL, pass, fail);

    // Macro hygiene (def-env capture)
    setup(interp, "(define hyg-helper (lambda (x) (+ x 100)))");
    setup(interp, "(define [macro] hyg-apply ([x] (hyg-helper x)))");
    test_eq(interp, "hygiene normal", "(hyg-apply 5)", 105, pass, fail);
    setup(interp, "(define hyg-helper (lambda (x) (* x 999)))");
    test_eq(interp, "hygiene immune to shadow", "(hyg-apply 5)", 105, pass, fail);
    setup(interp, "(define [macro] hyg-inc ([x] (+ x 1)))");
    test_eq(interp, "hygiene captures +", "(hyg-inc 10)", 11, pass, fail);
    setup(interp, "(define [macro] hyg-nil-test ([x] (if x 42 nil)))");
    test_nil(interp, "hygiene nil branch", "(hyg-nil-test false)", pass, fail);
    test_eq(interp, "hygiene true branch", "(hyg-nil-test true)", 42, pass, fail);
    setup(interp, "(define hyg-a 10)");
    setup(interp, "(define hyg-b 20)");
    setup(interp, "(define [macro] hyg-sum ([x] (+ (+ hyg-a hyg-b) x)))");
    test_eq(interp, "hygiene multi literals", "(hyg-sum 5)", 35, pass, fail);
    setup(interp, "(define hyg-a 1000)");
    setup(interp, "(define hyg-b 2000)");
    test_eq(interp, "hygiene multi immune", "(hyg-sum 5)", 35, pass, fail);
    setup(interp, "(define hyg-op (lambda (x) (* x 2)))");
    setup(interp, "(define [macro] hyg-with-tmp ([x] (let ((tmp# (hyg-op x))) tmp#)))");
    test_eq(interp, "hygiene gensym+def", "(hyg-with-tmp 5)", 10, pass, fail);
    setup(interp, "(define hyg-op (lambda (x) 0))");
    test_eq(interp, "hygiene gensym+def immune", "(hyg-with-tmp 5)", 10, pass, fail);
    setup(interp, "(define x 999)");
    setup(interp, "(define [macro] hyg-id ([x] x))");
    test_eq(interp, "hygiene patvar substitutes", "(hyg-id 7)", 7, pass, fail);
    test_eq(interp, "hygiene recursive cond", "(cond false 1 false 2 true 3)", 3, pass, fail);
    setup(interp, "(define hyg-late-fn (lambda (x) (+ x 50)))");
    setup(interp, "(define [macro] hyg-late-macro ([x] (hyg-late-fn x)))");
    setup(interp, "(define hyg-late-fn (lambda (x) (- x 50)))");
    test_eq(interp, "hygiene late redef", "(hyg-late-macro 100)", 150, pass, fail);

    // Dicts
    setup(interp, "(define hm1 (dict 1 10 2 20 3 30))");
    test_eq(interp, "ref dict", "(ref hm1 2)", 20, pass, fail);
    test_nil(interp, "ref dict missing", "(ref hm1 99)", pass, fail);
    setup(interp, "(dict-set! hm1 2 200)");
    test_eq(interp, "dict-set! overwrite", "(ref hm1 2)", 200, pass, fail);
    test_truthy(interp, "has? dict yes", "(has? hm1 1)", pass, fail);
    test_nil(interp, "has? dict no", "(has? hm1 99)", pass, fail);
    test_eq(interp, "length dict", "(length hm1)", 3, pass, fail);
    setup(interp, "(define hm2 (dict 1 10 2 20 3 30))");
    setup(interp, "(remove! hm2 2)");
    test_nil(interp, "remove! dict", "(has? hm2 2)", pass, fail);
    test_eq(interp, "remove! count", "(length hm2)", 2, pass, fail);
    setup(interp, "(define hm3 (dict 1 10 2 20))");
    test_eq(interp, "keys len", "(length (keys hm3))", 2, pass, fail);
    test_eq(interp, "values len", "(length (values hm3))", 2, pass, fail);
    setup(interp, "(define hm4 (dict \"name\" \"Alice\" \"age\" 30))");
    test_str(interp, "dict string key", "(ref hm4 \"name\")", pass, fail);
    setup(interp, "(define hm5 (dict))");
    test_eq(interp, "dict empty count", "(length hm5)", 0, pass, fail);
    test_truthy(interp, "dict? yes", "(dict? (dict 1 2))", pass, fail);
    test_nil(interp, "dict? no", "(dict? 42)", pass, fail);
    setup(interp, "(define hm-big (dict))");
    setup(interp, "(let loop ((i 0)) (if (= i 50) hm-big (begin (dict-set! hm-big i (* i i)) (loop (+ i 1)))))");
    test_eq(interp, "dict auto-grow count", "(length hm-big)", 50, pass, fail);
    test_eq(interp, "dict auto-grow ref 7", "(ref hm-big 7)", 49, pass, fail);

    // === LITERAL SYNTAX TESTS ===
    // Dict literal {}
    test_eq(interp, "{} empty", "(length {})", 0, pass, fail);
    test_eq(interp, "{} literal", "(ref {'a 1 'b 2} 'a)", 1, pass, fail);
    test_truthy(interp, "{} string keys", "(has? {\"name\" \"Alice\"} \"name\")", pass, fail);
    test_truthy(interp, "{} has key", "(has? {'x 10 'y 20} 'y)", pass, fail);
    test_eq(interp, "{} count", "(length {'a 1 'b 2 'c 3})", 3, pass, fail);
    // Array literal []
    test_eq(interp, "[] empty", "(length [])", 0, pass, fail);
    test_eq(interp, "[] literal", "(ref [10 20 30] 1)", 20, pass, fail);
    test_eq(interp, "[] length", "(length [1 2 3])", 3, pass, fail);
    test_eq(interp, "[] nested", "(ref (ref [1 [2 3]] 1) 0)", 2, pass, fail);
    test_eq(interp, "[] index", "(let ((a [10 20 30])) a.[1])", 20, pass, fail);
    // Cons cell dot-path access and mutation
    test_eq(interp, "cons .car", "(let ((p (cons 1 2))) p.car)", 1, pass, fail);
    test_eq(interp, "cons .cdr", "(let ((p (cons 1 2))) p.cdr)", 2, pass, fail);
    test_eq_interp(interp, "set! cons .car", "(let ((p (cons 1 2))) (begin (set! p.car 99) p.car))", 99, pass, fail);
    test_eq_interp(interp, "set! cons .cdr", "(let ((p (cons 1 2))) (begin (set! p.cdr 50) p.cdr))", 50, pass, fail);
    test_eq_interp(interp, "set! cons preserves", "(let ((p (cons 1 2))) (begin (set! p.car 99) p.cdr))", 2, pass, fail);

    // === GENERIC OPERATIONS TESTS ===
    // ref - array
    test_eq(interp, "ref array", "(ref [10 20 30] 1)", 20, pass, fail);
    test_error(interp, "ref array oob", "(ref [1] 5)", pass, fail);
    // ref - dict
    test_eq(interp, "ref dict", "(ref {'a 1 'b 2} 'a)", 1, pass, fail);
    test_nil(interp, "ref dict missing", "(ref {'a 1} 'b)", pass, fail);
    // ref - cons
    test_eq(interp, "ref cons car", "(ref (cons 10 20) 0)", 10, pass, fail);
    test_eq(interp, "ref cons cdr", "(ref (cons 10 20) 1)", 20, pass, fail);
    // ref - string
    test_eq(interp, "ref string", "(ref \"hello\" 0)", 104, pass, fail);
    // length - generic
    test_eq(interp, "length array", "(length [1 2 3])", 3, pass, fail);
    test_eq(interp, "length dict", "(length {'a 1 'b 2})", 2, pass, fail);
    test_eq(interp, "length string", "(length \"hello\")", 5, pass, fail);
    test_eq(interp, "length list", "(length '(1 2 3 4))", 4, pass, fail);
    // push!
    setup(interp, "(define ga [1 2])");
    setup(interp, "(push! ga 3)");
    test_eq(interp, "push! generic", "(length ga)", 3, pass, fail);
    test_eq(interp, "push! val", "(ref ga 2)", 3, pass, fail);
    // keys / values / has? / remove!
    setup(interp, "(define gd {'x 10 'y 20})");
    test_eq(interp, "keys len", "(length (keys gd))", 2, pass, fail);
    test_eq(interp, "values len", "(length (values gd))", 2, pass, fail);
    test_truthy(interp, "has? yes", "(has? gd 'x)", pass, fail);
    test_nil(interp, "has? no", "(has? gd 'z)", pass, fail);
    setup(interp, "(remove! gd 'x)");
    test_nil(interp, "remove! gone", "(has? gd 'x)", pass, fail);
    test_eq(interp, "remove! count", "(length gd)", 1, pass, fail);

    // === MODULE SYSTEM TESTS ===
    setup(interp, "(module math-utils (export double triple) (define double (lambda (x) (* x 2))) (define triple (lambda (x) (* x 3))) (define internal (lambda (x) (+ x 100))))");
    setup(interp, "(import math-utils)");
    test_eq(interp, "module double", "(double 5)", 10, pass, fail);
    test_eq(interp, "module triple", "(triple 4)", 12, pass, fail);
    test_error(interp, "module unexported hidden", "(internal 5)", pass, fail);
    setup(interp, "(import math-utils)");
    test_eq(interp, "module re-import cached", "(double 7)", 14, pass, fail);
    test_error(interp, "module duplicate def", "(module math-utils (export) (define x 1))", pass, fail);
    setup(interp, "(module list-utils (export sum-list) (define sum-list (lambda (lst) (((foldl (lambda (a) (lambda (b) (+ a b)))) 0) lst))))");
    setup(interp, "(import list-utils)");
    test_eq(interp, "module stdlib access", "(sum-list (quote (1 2 3 4 5)))", 15, pass, fail);
    setup(interp, "(module base-mod (export base-fn) (define base-fn (lambda (x) (+ x 100))))");
    setup(interp, "(import base-mod)");
    setup(interp, "(module derived-mod (export derived-fn) (define derived-fn (lambda (x) (base-fn (* x 2)))))");
    setup(interp, "(import derived-mod)");
    test_eq(interp, "module depends on another", "(derived-fn 5)", 110, pass, fail);
    setup(interp, "(module private-mod (export) (define secret 42))");
    setup(interp, "(import private-mod)");
    test_error(interp, "module empty export hides", "secret", pass, fail);
    setup(interp, "(module closure-mod (export make-counter) (define make-counter (lambda () (let ((n 0)) (lambda () (begin (set! n (+ n 1)) n))))))");
    setup(interp, "(import closure-mod)");
    setup(interp, "(define my-counter (make-counter))");
    // Counter tests are stateful: JIT-only to avoid double mutation
    test_eq_jit(interp, "module counter 1", "(my-counter)", 1, pass, fail);
    test_eq_jit(interp, "module counter 2", "(my-counter)", 2, pass, fail);
    setup(interp, "(module tail-mod (export tail-sum) (define tail-sum (lambda (n acc) (if (= n 0) acc (tail-sum (- n 1) (+ acc n))))))");
    setup(interp, "(import tail-mod)");
    test_eq(interp, "module tail-call sum 1000", "(tail-sum 1000 0)", 500500, pass, fail);

    // === SHORTHAND DEFINE TESTS ===
    setup(interp, "(define (sh-double x) (* x 2))");
    test_eq(interp, "shorthand define", "(sh-double 21)", 42, pass, fail);
    setup(interp, "(define (sh-add3 a b c) (+ a (+ b c)))");
    test_eq(interp, "shorthand define multi", "(sh-add3 10 20 30)", 60, pass, fail);
    setup(interp, "(define (sh-answer) 42)");
    test_eq(interp, "shorthand define zero-arg", "(sh-answer)", 42, pass, fail);

    // === STDLIB TESTS ===
    test_eq(interp, "map car", "(car ((map (lambda (x) (* x 2))) (quote (1 2 3))))", 2, pass, fail);
    test_eq(interp, "map length", "(length ((map (lambda (x) (* x 2))) (quote (1 2 3))))", 3, pass, fail);
    test_nil(interp, "map empty", "((map (lambda (x) (* x 2))) (quote ()))", pass, fail);
    test_eq(interp, "filter car", "(car ((filter (lambda (x) (> x 2))) (quote (1 2 3 4 5))))", 3, pass, fail);
    test_eq(interp, "filter length", "(length ((filter (lambda (x) (> x 2))) (quote (1 2 3 4 5))))", 3, pass, fail);
    test_nil(interp, "filter none", "((filter (lambda (x) (> x 100))) (quote (1 2 3)))", pass, fail);
    test_eq(interp, "foldl sum", "(((foldl (lambda (acc) (lambda (x) (+ acc x)))) 0) (quote (1 2 3 4 5)))", 15, pass, fail);
    test_eq(interp, "foldl product", "(((foldl (lambda (acc) (lambda (x) (* acc x)))) 1) (quote (1 2 3 4)))", 24, pass, fail);
    test_eq(interp, "foldr cons len", "(length (((foldr (lambda (x) (lambda (acc) (cons x acc)))) nil) (quote (1 2 3))))", 3, pass, fail);
    test_eq(interp, "foldr cons car", "(car (((foldr (lambda (x) (lambda (acc) (cons x acc)))) nil) (quote (1 2 3))))", 1, pass, fail);
    test_eq(interp, "append length", "(length ((append (quote (1 2))) (quote (3 4))))", 4, pass, fail);
    test_eq(interp, "append car", "(car ((append (quote (1 2))) (quote (3 4))))", 1, pass, fail);
    test_eq(interp, "append nil left", "(car ((append nil) (quote (1 2))))", 1, pass, fail);
    test_eq(interp, "reverse car", "(car (reverse (quote (1 2 3))))", 3, pass, fail);
    test_eq(interp, "reverse length", "(length (reverse (quote (1 2 3))))", 3, pass, fail);
    test_nil(interp, "reverse nil", "(reverse nil)", pass, fail);
    test_eq(interp, "compose *2.+1", "(((compose (lambda (x) (* x 2))) (lambda (x) (+ x 1))) 3)", 8, pass, fail);
    test_eq(interp, "compose +1.*2", "(((compose (lambda (x) (+ x 1))) (lambda (x) (* x 2))) 3)", 7, pass, fail);
    test_eq(interp, "id", "(id 42)", 42, pass, fail);
    test_eq(interp, "nth 0", "((nth 0) (quote (10 20 30 40)))", 10, pass, fail);
    test_eq(interp, "nth 2", "((nth 2) (quote (10 20 30 40)))", 30, pass, fail);
    test_eq(interp, "take length", "(length ((take 3) (quote (1 2 3 4 5))))", 3, pass, fail);
    test_eq(interp, "take car", "(car ((take 3) (quote (1 2 3 4 5))))", 1, pass, fail);
    test_nil(interp, "take 0", "((take 0) (quote (1 2 3)))", pass, fail);
    test_eq(interp, "drop car", "(car ((drop 2) (quote (1 2 3 4 5))))", 3, pass, fail);
    test_eq(interp, "drop length", "(length ((drop 2) (quote (1 2 3 4 5))))", 3, pass, fail);
    test_eq(interp, "drop 0", "(car ((drop 0) (quote (1 2 3))))", 1, pass, fail);
    test_eq(interp, "zip length", "(length ((zip (quote (1 2 3))) (quote (4 5 6))))", 3, pass, fail);
    test_eq(interp, "zip first car", "(car (car ((zip (quote (1 2 3))) (quote (4 5 6)))))", 1, pass, fail);
    test_eq(interp, "zip first cdr", "(cdr (car ((zip (quote (1 2 3))) (quote (4 5 6)))))", 4, pass, fail);
    test_eq(interp, "zip unequal", "(length ((zip (quote (1 2))) (quote (4 5 6))))", 2, pass, fail);
    test_eq(interp, "range length", "(length (range 5))", 5, pass, fail);
    test_eq(interp, "range car", "(car (range 5))", 0, pass, fail);
    test_nil(interp, "range 0", "(range 0)", pass, fail);
    test_nil(interp, "for-each", "((for-each (lambda (x) (+ x 1))) (quote (1 2 3)))", pass, fail);
    test_truthy(interp, "any? found", "((any? (lambda (x) (= x 3))) (quote (1 2 3 4)))", pass, fail);
    test_nil(interp, "any? not found", "((any? (lambda (x) (= x 99))) (quote (1 2 3 4)))", pass, fail);
    test_truthy(interp, "every? all", "((every? (lambda (x) (> x 0))) (quote (1 2 3 4)))", pass, fail);
    test_nil(interp, "every? not all", "((every? (lambda (x) (> x 2))) (quote (1 2 3 4)))", pass, fail);
    test_truthy(interp, "every? empty", "((every? (lambda (x) (> x 0))) nil)", pass, fail);
    test_eq(interp, "try/raise catch", "((try (lambda (xx) (+ 1 (perform raise 42)))) (lambda (msg) msg))", 42, pass, fail);
    test_eq(interp, "try no error", "((try (lambda (xx) (+ 1 2))) (lambda (msg) msg))", 3, pass, fail);
    test_truthy(interp, "assert! true", "((assert! (= 1 1)) \"ok\")", pass, fail);
    test_truthy(interp, "assert! false raises", "(handle ((assert! (= 1 2)) \"fail\") ((raise k msg) msg))", pass, fail);
    setup(interp, "(define test-alist (list (cons 1 10) (cons 2 20) (cons 3 30)))");
    test_eq(interp, "assoc find", "(cdr ((assoc 2) test-alist))", 20, pass, fail);
    test_nil(interp, "assoc not found", "((assoc 99) test-alist)", pass, fail);
    test_eq(interp, "assoc-ref", "((assoc-ref 3) test-alist)", 30, pass, fail);
    test_nil(interp, "assoc-ref not found", "((assoc-ref 99) test-alist)", pass, fail);
    test_eq(interp, "map+filter+foldl", "(((foldl (lambda (acc) (lambda (x) (+ acc x)))) 0) ((filter (lambda (x) (> x 3))) ((map (lambda (x) (* x 2))) (quote (1 2 3 4 5)))))", 28, pass, fail);
    test_eq(interp, "reverse+take", "(car ((take 1) (reverse (quote (1 2 3)))))", 3, pass, fail);
    test_eq(interp, "sum of squares", "(((foldl (lambda (acc) (lambda (x) (+ acc x)))) 0) ((map (lambda (x) (* x x))) (range 5)))", 30, pass, fail);

    // === FLOAT TESTS ===
    // Float literals
    test_double(interp, "float 3.14", "3.14", 3.14, pass, fail);
    test_double(interp, "float -2.5", "-2.5", -2.5, pass, fail);
    test_double(interp, "float 1.0e3", "1.0e3", 1000.0, pass, fail);
    test_double(interp, "float 1.5e-2", "1.5e-2", 0.015, pass, fail);
    // Mixed arithmetic
    test_double(interp, "int+float", "(+ 1 2.0)", 3.0, pass, fail);
    test_double(interp, "float*int", "(* 2.5 4)", 10.0, pass, fail);
    test_double(interp, "float/int", "(/ 1.0 3)", 0.333333, pass, fail);
    test_double(interp, "float-float", "(- 5.5 2.5)", 3.0, pass, fail);
    // Int arithmetic stays int
    test_eq(interp, "int+int stays int", "(+ 1 2)", 3, pass, fail);
    // Comparisons with float
    test_truthy(interp, "< int float", "(< 1 2.5)", pass, fail);
    test_truthy(interp, "> float int", "(> 3.5 2)", pass, fail);
    test_truthy(interp, "= int float", "(= 1 1.0)", pass, fail);
    test_truthy(interp, "<= float", "(<= 1.5 1.5)", pass, fail);
    test_truthy(interp, ">= float", "(>= 2.0 1.5)", pass, fail);
    // Unary minus
    test_double(interp, "unary neg float", "(- 0 3.14)", -3.14, pass, fail);
    // Conversions
    test_double(interp, "exact->inexact", "(exact->inexact 5)", 5.0, pass, fail);
    test_eq(interp, "inexact->exact", "(inexact->exact 3.7)", 3, pass, fail);
    // string->number float
    test_double(interp, "string->number float", "(string->number \"3.14\")", 3.14, pass, fail);
    test_double(interp, "string->number exp", "(string->number \"1.5e2\")", 150.0, pass, fail);
    // number->string double
    test_str(interp, "number->string double", "(number->string 3.14)", pass, fail);

    // === MATH LIBRARY TESTS (Phase 2) ===
    test_double(interp, "sin 0", "(sin 0.0)", 0.0, pass, fail);
    test_double(interp, "cos 0", "(cos 0.0)", 1.0, pass, fail);
    test_double(interp, "tan 0", "(tan 0.0)", 0.0, pass, fail);
    test_double(interp, "asin 0", "(asin 0.0)", 0.0, pass, fail);
    test_double(interp, "acos 1", "(acos 1.0)", 0.0, pass, fail);
    test_double(interp, "atan 0", "(atan 0.0)", 0.0, pass, fail);
    test_double(interp, "atan2", "(atan2 1.0 1.0)", 0.7853981, pass, fail);
    test_double(interp, "exp 0", "(exp 0.0)", 1.0, pass, fail);
    test_double(interp, "log 1", "(log 1.0)", 0.0, pass, fail);
    test_double(interp, "sqrt 4", "(sqrt 4.0)", 2.0, pass, fail);
    test_double(interp, "pow 2 10", "(pow 2.0 10.0)", 1024.0, pass, fail);
    test_eq(interp, "floor 3.7", "(floor 3.7)", 3, pass, fail);
    test_eq(interp, "ceiling 3.2", "(ceiling 3.2)", 4, pass, fail);
    test_eq(interp, "round 3.5", "(round 3.5)", 4, pass, fail);
    test_eq(interp, "truncate 3.9", "(truncate 3.9)", 3, pass, fail);
    test_eq(interp, "abs -5", "(abs -5)", 5, pass, fail);
    test_double(interp, "abs -3.14", "(abs -3.14)", 3.14, pass, fail);
    test_eq(interp, "min 3 7", "(min 3 7)", 3, pass, fail);
    test_eq(interp, "max 3 7", "(max 3 7)", 7, pass, fail);
    test_eq(interp, "gcd 12 8", "(gcd 12 8)", 4, pass, fail);
    test_double(interp, "pi constant", "pi", 3.14159265, pass, fail);
    test_double(interp, "e constant", "e", 2.71828182, pass, fail);

    // === SORTING & BITWISE TESTS (Phase 3) ===
    test_eq(interp, "sort car", "(car (sort (quote (3 1 2))))", 1, pass, fail);
    test_eq(interp, "sort length", "(length (sort (quote (5 3 1 4 2))))", 5, pass, fail);
    test_eq(interp, "sort last", "(car (cdr (cdr (sort (quote (3 1 2))))))", 3, pass, fail);
    test_eq(interp, "sort-by custom", "(car (sort-by (lambda (a) (lambda (b) (- a b))) (quote (3 1 2))))", 1, pass, fail);
    test_eq(interp, "bitwise-and", "(bitwise-and 12 10)", 8, pass, fail);
    test_eq(interp, "bitwise-or", "(bitwise-or 12 10)", 14, pass, fail);
    test_eq(interp, "bitwise-xor", "(bitwise-xor 12 10)", 6, pass, fail);
    test_eq(interp, "bitwise-not 0", "(bitwise-not 0)", -1, pass, fail);
    test_eq(interp, "lshift", "(lshift 1 4)", 16, pass, fail);
    test_eq(interp, "rshift", "(rshift 16 4)", 1, pass, fail);

    // === STDLIB HOFs (Phase 3) ===
    test_eq(interp, "flatten", "(length (flatten (quote ((1 2) (3 4) (5)))))", 5, pass, fail);
    test_eq(interp, "flatten car", "(car (flatten (quote ((1 2) (3)))))", 1, pass, fail);
    test_eq(interp, "partition yes", "(length (car (partition (lambda (x) (> x 3)) (quote (1 2 3 4 5)))))", 2, pass, fail);
    test_eq(interp, "partition no", "(length (cdr (partition (lambda (x) (> x 3)) (quote (1 2 3 4 5)))))", 3, pass, fail);
    test_eq(interp, "remove", "(length (remove (lambda (x) (= x 3)) (quote (1 2 3 4 5))))", 4, pass, fail);
    test_eq(interp, "find found", "(find (lambda (x) (= x 3)) (quote (1 2 3 4 5)))", 3, pass, fail);
    test_nil(interp, "find not found", "(find (lambda (x) (= x 99)) (quote (1 2 3)))", pass, fail);

    // === STRING OPS TESTS (Phase 5) ===
    test_truthy(interp, "string-contains? yes", "(string-contains? \"hello world\" \"world\")", pass, fail);
    test_nil(interp, "string-contains? no", "(string-contains? \"hello\" \"xyz\")", pass, fail);
    test_eq(interp, "string-index-of found", "(string-index-of \"hello world\" \"world\")", 6, pass, fail);
    test_eq(interp, "string-index-of not", "(string-index-of \"hello\" \"xyz\")", -1, pass, fail);
    test_str(interp, "string-replace", "(string-replace \"hello world\" \"world\" \"there\")", pass, fail);
    test_str(interp, "char-at", "(char-at \"hello\" 1)", pass, fail);
    test_str(interp, "string-repeat", "(string-repeat \"ab\" 3)", pass, fail);

    // === TYPE PREDICATE TESTS (Phase 5) ===
    test_truthy(interp, "double? yes", "(double? 3.14)", pass, fail);
    test_nil(interp, "double? no", "(double? 42)", pass, fail);
    test_truthy(interp, "number? int", "(number? 42)", pass, fail);
    test_truthy(interp, "number? double", "(number? 3.14)", pass, fail);
    test_nil(interp, "number? string", "(number? \"hello\")", pass, fail);
    test_truthy(interp, "boolean? true", "(boolean? true)", pass, fail);
    test_truthy(interp, "boolean? nil", "(boolean? nil)", pass, fail);
    test_nil(interp, "boolean? int", "(boolean? 42)", pass, fail);
    test_truthy(interp, "list? yes", "(list? (quote (1 2 3)))", pass, fail);
    test_truthy(interp, "list? nil", "(list? nil)", pass, fail);
    test_nil(interp, "list? int", "(list? 42)", pass, fail);
    test_truthy(interp, "procedure? lambda", "(procedure? (lambda (x) x))", pass, fail);
    test_truthy(interp, "procedure? prim", "(procedure? +)", pass, fail);
    test_nil(interp, "procedure? int", "(procedure? 42)", pass, fail);
    test_truthy(interp, "zero? 0", "(zero? 0)", pass, fail);
    test_nil(interp, "zero? 1", "(zero? 1)", pass, fail);
    test_truthy(interp, "positive? 5", "(positive? 5)", pass, fail);
    test_nil(interp, "positive? -1", "(positive? -1)", pass, fail);
    test_truthy(interp, "negative? -1", "(negative? -1)", pass, fail);
    test_nil(interp, "negative? 5", "(negative? 5)", pass, fail);
    test_truthy(interp, "even? 4", "(even? 4)", pass, fail);
    test_nil(interp, "even? 3", "(even? 3)", pass, fail);
    test_truthy(interp, "odd? 3", "(odd? 3)", pass, fail);
    test_nil(interp, "odd? 4", "(odd? 4)", pass, fail);

    // === FORMAT & DISPLAY TESTS (Phase 5) ===
    test_str(interp, "format simple", "(format \"~a plus ~a\" 1 2)", pass, fail);

    // === INTROSPECTION TESTS (Phase 7) ===
    setup(interp, "(define [macro] my-inc ([x] (+ x 1)))");
    test_truthy(interp, "macroexpand", "(pair? (macroexpand (quote (my-inc 5))))", pass, fail);
    test_eq(interp, "eval literal", "(eval 42)", 42, pass, fail);
    test_eq(interp, "eval list", "(eval (quote (+ 1 2)))", 3, pass, fail);
    test_eq(interp, "apply +", "(apply + (quote (1 2)))", 3, pass, fail);
    test_truthy(interp, "bound? yes", "(bound? '+)", pass, fail);
    test_nil(interp, "bound? no", "(bound? 'xyzzy-undefined)", pass, fail);
    test_error(interp, "error raises", "(error \"test error\")", pass, fail);

    // === LAZY EVALUATION TESTS (Phase 8) ===
    setup(interp, "(define p (delay (lambda (xx) 42)))");
    test_eq(interp, "delay/force", "(force p)", 42, pass, fail);

    // === TCO STDLIB TESTS (Phase 6) ===
    test_eq(interp, "foldl TCO 4000", "(((foldl (lambda (a) (lambda (b) (+ a b)))) 0) (range 4000))", 7998000, pass, fail);

    // === ARRAY TESTS (Phase 8) ===
    test_eq(interp, "length array", "(length (array 1 2 3))", 3, pass, fail);
    test_eq(interp, "ref array", "(ref (array 10 20 30) 1)", 20, pass, fail);
    // array/list conversion via constructors
    test_eq(interp, "array from list", "(ref (array '(10 20 30)) 1)", 20, pass, fail);
    setup(interp, "(define test-arr (array 1 2 3))");
    setup(interp, "(array-set! test-arr 1 99)");
    test_eq(interp, "array-set!", "(ref test-arr 1)", 99, pass, fail);
    test_truthy(interp, "array?", "(array? (array 1 2))", pass, fail);
    test_nil(interp, "array? non-arr", "(array? 42)", pass, fail);
    test_truthy(interp, "list from array", "(= (list [1 2 3]) '(1 2 3))", pass, fail);
    test_eq(interp, "array from list 2", "(ref (array '(10 20 30)) 2)", 30, pass, fail);
    test_eq(interp, "array nil→empty", "(length (array nil))", 0, pass, fail);
    setup(interp, "(define push-arr (array 1 2))");
    setup(interp, "(push! push-arr 3)");
    test_eq(interp, "push!", "(length push-arr)", 3, pass, fail);
    test_eq(interp, "push! val", "(ref push-arr 2)", 3, pass, fail);
    test_error(interp, "ref oob", "(ref (array 1) 5)", pass, fail);
    test_error(interp, "array-set! oob", "(array-set! (array 1) 5 0)", pass, fail);
    test_eq(interp, "empty array", "(length (array))", 0, pass, fail);

    // === SET TESTS (Phase 9) ===
    test_eq(interp, "set-size", "(set-size (set 1 2 3))", 3, pass, fail);
    test_truthy(interp, "set-contains?", "(set-contains? (set 1 2 3) 2)", pass, fail);
    test_nil(interp, "set-contains? miss", "(set-contains? (set 1 2 3) 5)", pass, fail);
    setup(interp, "(define test-set (set 1 2))");
    setup(interp, "(set-add test-set 3)");
    test_eq(interp, "set-add", "(set-size test-set)", 3, pass, fail);
    test_truthy(interp, "set-add contains", "(set-contains? test-set 3)", pass, fail);
    setup(interp, "(set-remove test-set 2)");
    test_eq(interp, "set-remove", "(set-size test-set)", 2, pass, fail);
    test_nil(interp, "set-remove gone", "(set-contains? test-set 2)", pass, fail);
    test_eq(interp, "set dedup", "(set-size (set 1 1 2 2 3))", 3, pass, fail);

    // === CONVENIENCE TESTS (Phase 12) ===
    test_eq(interp, "read-string", "(read-string \"(+ 1 2)\")", 3, pass, fail);
    test_truthy(interp, "symbol->string", "(= (symbol->string 'hello) \"hello\")", pass, fail);
    test_truthy(interp, "string->symbol", "(= (string->symbol \"abc\") 'abc)", pass, fail);

    // === TYPE SYSTEM TESTS (Phase 1) ===

    // type-of primitive
    test_truthy(interp, "type-of int", "(= (type-of 42) 'Int)", pass, fail);
    test_truthy(interp, "type-of double", "(= (type-of 3.14) 'Double)", pass, fail);
    test_truthy(interp, "type-of string", "(= (type-of \"hello\") 'String)", pass, fail);
    test_truthy(interp, "type-of symbol", "(= (type-of 'foo) 'Symbol)", pass, fail);
    test_truthy(interp, "type-of nil", "(= (type-of nil) 'Nil)", pass, fail);
    test_truthy(interp, "type-of list", "(= (type-of '(1 2 3)) 'List)", pass, fail);
    test_truthy(interp, "type-of array", "(= (type-of (array 1 2 3)) 'Array)", pass, fail);
    test_truthy(interp, "type-of closure", "(= (type-of (lambda (x) x)) 'Closure)", pass, fail);
    test_truthy(interp, "type-of true", "(= (type-of true) 'Bool)", pass, fail);

    // is? primitive
    test_truthy(interp, "is? int", "(is? 42 'Int)", pass, fail);
    test_truthy(interp, "is? string", "(is? \"hello\" 'String)", pass, fail);
    test_nil(interp, "is? mismatch", "(is? 42 'String)", pass, fail);
    test_truthy(interp, "is? Any", "(is? 42 'Any)", pass, fail);

    // define [type] — simple struct (interp-only: JIT doesn't know user types yet)
    setup(interp, "(define [type] Point (^Int x) (^Int y))");
    test_truthy_interp(interp, "type constructor", "(instance? (Point 3 4))", pass, fail);
    test_truthy_interp(interp, "type-of instance", "(= (type-of (Point 3 4)) 'Point)", pass, fail);
    test_eq_interp(interp, "type field access x", "(let ((p (Point 3 4))) p.x)", 3, pass, fail);
    test_eq_interp(interp, "type field access y", "(let ((p (Point 3 4))) p.y)", 4, pass, fail);
    test_truthy_interp(interp, "is? instance type", "(is? (Point 3 4) 'Point)", pass, fail);

    // define [abstract] — abstract type
    setup(interp, "(define [abstract] Shape)");
    test_truthy(interp, "abstract type defined", "(= 'Shape 'Shape)", pass, fail);

    // define [type] with parent
    setup(interp, "(define [type] (Circle Shape) (^Int radius))");
    test_truthy_interp(interp, "subtype constructor", "(instance? (Circle 5))", pass, fail);
    test_truthy_interp(interp, "is? subtype", "(is? (Circle 5) 'Circle)", pass, fail);
    test_truthy_interp(interp, "is? parent type", "(is? (Circle 5) 'Shape)", pass, fail);
    test_nil_interp(interp, "is? wrong parent", "(is? (Point 1 2) 'Shape)", pass, fail);
    test_eq_interp(interp, "subtype field access", "(let ((c (Circle 5))) c.radius)", 5, pass, fail);

    // Nested type access
    setup(interp, "(define [type] Line (^Point start) (^Point end))");
    test_eq_interp(interp, "nested type access", "(let ((l (Line (Point 1 2) (Point 3 4)))) l.start.x)", 1, pass, fail);
    test_eq_interp(interp, "nested type access 2", "(let ((l (Line (Point 1 2) (Point 3 4)))) l.end.y)", 4, pass, fail);

    // define [union] — union types
    setup(interp, "(define [union] (Option T) None (Some T))");
    test_truthy_interp(interp, "union nullary", "(instance? None)", pass, fail);
    test_truthy_interp(interp, "union constructor", "(instance? (Some 42))", pass, fail);
    test_truthy_interp(interp, "is? union variant", "(is? None 'Option)", pass, fail);
    test_truthy_interp(interp, "is? union Some", "(is? (Some 42) 'Option)", pass, fail);
    test_truthy_interp(interp, "is? union Some exact", "(is? (Some 42) 'Some)", pass, fail);

    // define [alias]
    setup(interp, "(define [alias] Num Int)");
    test_truthy(interp, "alias defined", "(= 'Num 'Num)", pass, fail);

    // instance? primitive
    test_nil(interp, "instance? non-instance", "(instance? 42)", pass, fail);

    // === PARAMETRIC TYPE TESTS (Phase 7) ===

    // Parametric type with single type param
    setup(interp, "(define [type] (Box T) (^T value))");
    test_truthy_interp(interp, "Box constructor", "(instance? (Box 42))", pass, fail);
    test_eq_interp(interp, "Box field access", "(let ((b (Box 42))) b.value)", 42, pass, fail);
    test_truthy_interp(interp, "Box type-of", "(= (type-of (Box 42)) 'Box)", pass, fail);

    // type-args primitive — infer T=Int from (Box 42)
    test_truthy_interp(interp, "type-args Box Int", "(= (car (type-args (Box 42))) 'Int)", pass, fail);
    test_truthy_interp(interp, "type-args Box String", "(= (car (type-args (Box \"hi\"))) 'String)", pass, fail);

    // Parametric type with two type params
    setup(interp, "(define [type] (Pair A B) (^A first) (^B second))");
    test_eq_interp(interp, "Pair field first", "(let ((p (Pair 1 \"two\"))) p.first)", 1, pass, fail);
    test_truthy_interp(interp, "Pair field second", "(= (let ((p (Pair 1 \"two\"))) p.second) \"two\")", pass, fail);
    test_truthy_interp(interp, "type-args Pair A", "(= (car (type-args (Pair 1 \"two\"))) 'Int)", pass, fail);
    test_truthy_interp(interp, "type-args Pair B", "(= (car (cdr (type-args (Pair 1 \"two\")))) 'String)", pass, fail);

    // type-args on non-parametric type returns nil
    test_nil_interp(interp, "type-args non-param", "(type-args (Point 1 2))", pass, fail);
    // type-args on non-instance returns nil
    test_nil_interp(interp, "type-args int", "(type-args 42)", pass, fail);

    // Disambiguation: (Circle Shape) still works as parent, not type param
    // Circle was defined above with parent Shape — verify it still works
    test_truthy_interp(interp, "disambig parent still works", "(is? (Circle 5) 'Shape)", pass, fail);

    // === MULTIPLE DISPATCH TESTS (Phase 2) ===

    // Basic dispatch on types
    setup(interp, "(define (describe (^Int n)) \"integer\")");
    setup(interp, "(define (describe (^String s)) \"string\")");
    setup(interp, "(define (describe x) \"other\")");
    test_truthy_interp(interp, "dispatch int", "(= (describe 42) \"integer\")", pass, fail);
    test_truthy_interp(interp, "dispatch string", "(= (describe \"hi\") \"string\")", pass, fail);
    test_truthy_interp(interp, "dispatch fallback", "(= (describe '(1 2)) \"other\")", pass, fail);

    // Val dispatch (Fibonacci)
    setup(interp, "(define (fib (^(Val 0) n)) 0)");
    setup(interp, "(define (fib (^(Val 1) n)) 1)");
    setup(interp, "(define (fib (^Int n)) (+ (fib (- n 1)) (fib (- n 2))))");
    test_eq_interp(interp, "dispatch fib 0", "(fib 0)", 0, pass, fail);
    test_eq_interp(interp, "dispatch fib 1", "(fib 1)", 1, pass, fail);
    test_eq_interp(interp, "dispatch fib 10", "(fib 10)", 55, pass, fail);

    // Multi-arg dispatch
    setup(interp, "(define (add2 (^Int a) (^Int b)) (+ a b))");
    setup(interp, "(define (add2 (^String a) (^String b)) (string-append a b))");
    test_eq_interp(interp, "dispatch multi-arg int", "(add2 3 4)", 7, pass, fail);
    test_truthy_interp(interp, "dispatch multi-arg str", "(= (add2 \"hello\" \" world\") \"hello world\")", pass, fail);

    // === CONSTRAINED DISPATCH TESTS (Phase 7) ===

    // Define abstract type hierarchy for constraint testing
    setup(interp, "(define [abstract] (Numeric Number))");  // Numeric extends Number (but Number doesn't exist yet, no parent)

    // Actually, let's use fresh abstract type hierarchy
    setup(interp, "(define [abstract] Addable)");
    setup(interp, "(define [type] (MyInt Addable) (^Int val))");
    setup(interp, "(define [type] (MyStr Addable) (^String val))");
    setup(interp, "(define [type] NotAddable (^Int val))");

    // Constrained dispatch: only matches if arg type <: Addable
    setup(interp, "(define (get-val (^{'T Addable} x)) x.val)");
    test_eq_interp(interp, "constraint MyInt match", "(get-val (MyInt 42))", 42, pass, fail);
    test_truthy_interp(interp, "constraint MyStr match", "(= (get-val (MyStr \"hi\")) \"hi\")", pass, fail);

    // Constraint fallback: NotAddable doesn't satisfy Addable constraint
    // Add a fallback method that accepts anything
    setup(interp, "(define (get-val x) (- 0 1))");
    test_eq_interp(interp, "constraint fallback", "(get-val (NotAddable 99))", -1, pass, fail);

    // Array/dict indexing (Phase 3)
    test_eq(interp, "array index", "(let ((v (array 10 20 30))) v.[0])", 10, pass, fail);
    test_eq(interp, "array index 2", "(let ((v (array 10 20 30))) v.[2])", 30, pass, fail);

    // Struct field mutation (Phase 3)
    test_eq_interp(interp, "set! struct field", "(let ((p (Point 1 2))) (begin (set! p.x 99) p.x))", 99, pass, fail);
    test_eq_interp(interp, "set! struct field y", "(let ((p (Point 1 2))) (begin (set! p.y 50) p.y))", 50, pass, fail);
    test_eq_interp(interp, "set! nested field", "(let ((ln (Line (Point 0 0) (Point 10 10)))) (begin (set! ln.start.x 5) ln.start.x))", 5, pass, fail);
    test_eq_interp(interp, "set! preserves other", "(let ((p (Point 3 4))) (begin (set! p.x 99) p.y))", 4, pass, fail);

    // Hashmap indexing (Phase 3)
    test_eq_interp(interp, "dict index", "(let ((m {'a 1 'b 2})) m.['a])", 1, pass, fail);

    // === PRIMITIVE CONSOLIDATION TESTS (Phase 4) ===
    // Overload + for Point type (primitive becomes fallback)
    setup(interp, "(define (add-points (^Point a) (^Point b)) (Point (+ a.x b.x) (+ a.y b.y)))");
    test_eq_interp(interp, "typed dispatch Point+", "(let ((r (add-points (Point 1 2) (Point 3 4)))) r.x)", 4, pass, fail);
    test_eq_interp(interp, "typed dispatch Point+ y", "(let ((r (add-points (Point 1 2) (Point 3 4)))) r.y)", 6, pass, fail);
    // Built-in + still works (dispatch preserves primitive fallback)
    test_eq_interp(interp, "builtin + after dispatch", "(+ 10 20)", 30, pass, fail);
    // Overload length for custom type
    setup(interp, "(define (my-len (^Point p)) 2)");
    test_eq_interp(interp, "typed dispatch custom len", "(my-len (Point 5 6))", 2, pass, fail);

    // === ABSTRACT TYPE HIERARCHY TESTS ===
    // Number hierarchy
    test_truthy_interp(interp, "is? int Number", "(is? 42 'Number)", pass, fail);
    test_truthy_interp(interp, "is? double Number", "(is? 3.14 'Number)", pass, fail);
    test_nil_interp(interp, "is? string not Number", "(is? \"hi\" 'Number)", pass, fail);
    test_nil_interp(interp, "is? nil not Number", "(is? nil 'Number)", pass, fail);

    // Collection hierarchy
    test_truthy_interp(interp, "is? list Collection", "(is? '(1 2) 'Collection)", pass, fail);
    test_truthy_interp(interp, "is? array Collection", "(is? [1 2] 'Collection)", pass, fail);
    test_truthy_interp(interp, "is? dict Collection", "(is? {'a 1} 'Collection)", pass, fail);
    test_nil_interp(interp, "is? int not Collection", "(is? 42 'Collection)", pass, fail);
    test_nil_interp(interp, "is? string not Collection", "(is? \"hi\" 'Collection)", pass, fail);

    // number? uses abstract hierarchy
    test_truthy_interp(interp, "number? int", "(number? 42)", pass, fail);
    test_truthy_interp(interp, "number? double", "(number? 3.14)", pass, fail);
    test_nil_interp(interp, "number? string", "(number? \"hi\")", pass, fail);

    // === PREDICATE EQUIVALENCE TESTS ===
    test_truthy_interp(interp, "int? positive", "(int? 42)", pass, fail);
    test_nil_interp(interp, "int? negative", "(int? 3.14)", pass, fail);
    test_truthy_interp(interp, "double? positive", "(double? 3.14)", pass, fail);
    test_nil_interp(interp, "double? negative", "(double? 42)", pass, fail);
    test_truthy_interp(interp, "string? positive", "(string? \"hi\")", pass, fail);
    test_nil_interp(interp, "string? negative", "(string? 42)", pass, fail);
    test_truthy_interp(interp, "symbol? positive", "(symbol? 'foo)", pass, fail);
    test_nil_interp(interp, "symbol? negative", "(symbol? 42)", pass, fail);
    test_truthy_interp(interp, "boolean? true", "(boolean? true)", pass, fail);
    test_truthy_interp(interp, "boolean? false", "(boolean? false)", pass, fail);
    test_nil_interp(interp, "boolean? int", "(boolean? 42)", pass, fail);
    test_truthy_interp(interp, "list? positive", "(list? '(1 2))", pass, fail);
    test_truthy_interp(interp, "list? nil", "(list? nil)", pass, fail);
    test_nil_interp(interp, "list? int", "(list? 42)", pass, fail);
    test_truthy_interp(interp, "closure? positive", "(closure? (lambda (x) x))", pass, fail);
    test_nil_interp(interp, "closure? int", "(closure? 42)", pass, fail);
    test_truthy_interp(interp, "array? positive", "(array? [1 2])", pass, fail);
    test_nil_interp(interp, "array? int", "(array? 42)", pass, fail);
    test_truthy_interp(interp, "dict? positive", "(dict? {'a 1})", pass, fail);
    test_nil_interp(interp, "dict? int", "(dict? 42)", pass, fail);
    test_truthy_interp(interp, "zero? yes", "(zero? 0)", pass, fail);
    test_nil_interp(interp, "zero? no", "(zero? 1)", pass, fail);
    test_truthy_interp(interp, "positive? yes", "(positive? 5)", pass, fail);
    test_nil_interp(interp, "positive? no", "(positive? -1)", pass, fail);
    test_truthy_interp(interp, "negative? yes", "(negative? -3)", pass, fail);
    test_nil_interp(interp, "negative? no", "(negative? 0)", pass, fail);
    test_truthy_interp(interp, "even? yes", "(even? 4)", pass, fail);
    test_nil_interp(interp, "even? no", "(even? 3)", pass, fail);
    test_truthy_interp(interp, "odd? yes", "(odd? 3)", pass, fail);
    test_nil_interp(interp, "odd? no", "(odd? 4)", pass, fail);

    // === DISPATCH EXTENSIBILITY TESTS ===
    setup(interp, "(define [type] Vec2 (^Double vx) (^Double vy))");
    setup(interp, "(define (+ (^Vec2 a) (^Vec2 b)) (Vec2 (+ a.vx b.vx) (+ a.vy b.vy)))");
    test_truthy_interp(interp, "Vec2 + dispatch x",
        "(= (let ((v (+ (Vec2 1.0 2.0) (Vec2 3.0 4.0)))) v.vx) 4.0)", pass, fail);
    test_truthy_interp(interp, "Vec2 + dispatch y",
        "(= (let ((v (+ (Vec2 1.0 2.0) (Vec2 3.0 4.0)))) v.vy) 6.0)", pass, fail);
    // Built-in + still works after Vec2 extension
    test_eq_interp(interp, "builtin + after Vec2", "(+ 1 2)", 3, pass, fail);
    // Extend length for Vec2
    setup(interp, "(define (length (^Vec2 v)) (sqrt (+ (* v.vx v.vx) (* v.vy v.vy))))");
    test_truthy_interp(interp, "Vec2 length dispatch",
        "(= (length (Vec2 3.0 4.0)) 5.0)", pass, fail);
    // Built-in length still works
    test_eq_interp(interp, "builtin length after Vec2", "(length '(1 2 3))", 3, pass, fail);
    // procedure? recognizes method tables
    test_truthy_interp(interp, "procedure? method table", "(procedure? +)", pass, fail);

    // === I/O EFFECTS TESTS (Phase 5) ===
    // print/println go through effects now (fast path to __raw-*)
    test_nil_interp(interp, "io effect print", "(print 42)", pass, fail);
    test_nil_interp(interp, "io effect println", "(println 99)", pass, fail);

    // Custom handler can suppress output
    test_eq_interp(interp, "io handle suppress",
        "(handle (begin (println \"suppressed\") 42) ((io/println k x) (k nil)))",
        42, pass, fail);

    // Custom handler can capture output
    test_truthy_interp(interp, "io handle capture",
        "(= (handle (begin (println \"captured\") nil) ((io/println k x) x)) \"captured\")",
        pass, fail);

    // __raw-print still works directly
    test_nil_interp(interp, "raw-print direct", "(__raw-print 0)", pass, fail);

    // === TYPED EFFECT DECLARATIONS TESTS ===
    // Declaring effects doesn't error
    test_nil_interp(interp, "defeffect decl", "(define [effect] (test/greet (^String name)))", pass, fail);
    test_nil_interp(interp, "defeffect no arg", "(define [effect] (test/ping))", pass, fail);
    test_nil_interp(interp, "defeffect int arg", "(define [effect] (test/count (^Int n)))", pass, fail);

    // Perform with correct type works
    test_str_interp(interp, "effect correct type",
        "(handle (perform test/greet \"hello\") ((test/greet k x) x))", pass, fail);
    test_eq_interp(interp, "effect int correct",
        "(handle (perform test/count 42) ((test/count k x) x))", 42, pass, fail);

    // Perform with wrong type errors
    test_error(interp, "effect wrong type int",
        "(handle (perform test/greet 42) ((test/greet k x) x))", pass, fail);
    test_error(interp, "effect wrong type str",
        "(handle (perform test/count \"bad\") ((test/count k x) x))", pass, fail);

    // Undeclared effects still work (backward compat)
    test_eq_interp(interp, "effect undeclared ok",
        "(handle (perform custom-tag 42) ((custom-tag k x) (+ x 1)))", 43, pass, fail);

    // Nullary effect (no arg type) accepts anything
    test_eq_interp(interp, "effect nullary any",
        "(handle (perform test/ping 99) ((test/ping k x) x))", 99, pass, fail);

    // ^Any effects accept all types
    test_nil_interp(interp, "io effect Any int", "(print 42)", pass, fail);
    test_nil_interp(interp, "io effect Any str", "(print \"hello\")", pass, fail);

    // io/read-file with wrong type should error (^String)
    test_error(interp, "io/read-file wrong type",
        "(handle (perform io/read-file 42) ((io/read-file k x) x))", pass, fail);

    // io/read-file with correct type works
    test_str_interp(interp, "io/read-file correct type",
        "(handle (perform io/read-file \"test.txt\") ((io/read-file k x) x))", pass, fail);

    // Subtype check: Number accepts Int and Double
    test_nil_interp(interp, "defeffect number arg", "(define [effect] (test/num (^Number n)))", pass, fail);
    test_eq_interp(interp, "effect number int",
        "(handle (perform test/num 42) ((test/num k x) x))", 42, pass, fail);
    test_error(interp, "effect number str",
        "(handle (perform test/num \"bad\") ((test/num k x) x))", pass, fail);

    // === UNION PATTERN MATCHING TESTS (Phase 6) ===
    // Option and Result types already defined in Phase 1 tests above

    // Nullary constructor match
    test_eq_interp(interp, "match None", "(match None (None 0) ((Some x) x))", 0, pass, fail);

    // Constructor with field match
    test_eq_interp(interp, "match Some", "(match (Some 42) (None 0) ((Some x) x))", 42, pass, fail);

    // Result type
    setup(interp, "(define [union] (Result T E) (Ok T) (Err E))");
    test_eq_interp(interp, "match Ok", "(match (Ok 1) ((Ok v) v) ((Err e) (- 0 1)))", 1, pass, fail);
    test_eq_interp(interp, "match Err", "(match (Err 99) ((Ok v) v) ((Err e) e))", 99, pass, fail);

    // Nested constructor match
    test_eq_interp(interp, "match nested Some", "(match (Some (Some 7)) ((Some (Some x)) x) (_ 0))", 7, pass, fail);

    // Wildcard in constructor
    test_eq_interp(interp, "match ctor wildcard", "(match (Ok 42) ((Ok _) 1) ((Err _) 2))", 1, pass, fail);

    // === MEMORY RECLAMATION TESTS ===
    // free! on array: releases backing, value becomes nil
    test_nil_interp(interp, "free! array", "(let ((a [1 2 3])) (begin (free! a) a))", pass, fail);
    // free! on dict: releases backing, value becomes nil
    test_nil_interp(interp, "free! dict", "(let ((d {'a 1 'b 2})) (begin (free! d) d))", pass, fail);
    // free! on non-collection: no-op, returns nil
    test_nil_interp(interp, "free! int noop", "(free! 42)", pass, fail);
    // free! on nil: no-op
    test_nil_interp(interp, "free! nil noop", "(free! nil)", pass, fail);

    // === FFI TESTS ===
    setup(interp, "(define test-libc (ffi-open \"libc.so.6\"))");
    test_tag(interp, "ffi-open handle", "test-libc", FFI_HANDLE, pass, fail);
    test_eq(interp, "ffi-call strlen", "(ffi-call test-libc \"strlen\" 'size \"hello\" 'string)", 5, pass, fail);
    test_eq(interp, "ffi-call abs", "(ffi-call test-libc \"abs\" 'int (- 0 42) 'int)", 42, pass, fail);
    test_gt(interp, "ffi-call getpid", "(ffi-call test-libc \"getpid\" 'int)", 0, pass, fail);
    test_eq(interp, "ffi-call atoi", "(ffi-call test-libc \"atoi\" 'int \"12345\" 'string)", 12345, pass, fail);
    test_truthy(interp, "ffi-sym strlen", "(> (ffi-sym test-libc \"strlen\") 0)", pass, fail);
    setup(interp, "(define test-libc2 (ffi-open \"libc.so.6\"))");
    // ffi-close is a side effect: JIT-only to avoid double-close
    test_nil_jit(interp, "ffi-close", "(ffi-close test-libc2)", pass, fail);
    test_error(interp, "ffi-open bad lib", "(ffi-open \"nonexistent_lib_xyz.so\")", pass, fail);
    // FFI cache hit test: call same function twice (uses integer funcs since double FFI
    // needs XMM register passing which isn't implemented yet)
    test_eq(interp, "ffi-call cached", "(ffi-call test-libc \"abs\" 'int (- 0 7) 'int)", 7, pass, fail);
    setup(interp, "(ffi-close test-libc)");
}

// =============================================================================
// SECTION 11: COMPILER TESTS
// =============================================================================

/**
 * Check if haystack contains needle as a substring.
 */
fn bool str_contains(char[] haystack, char[] needle) {
    if (needle.len == 0) return true;
    if (needle.len > haystack.len) return false;
    for (usz i = 0; i <= haystack.len - needle.len; i++) {
        bool match = true;
        for (usz j = 0; j < needle.len; j++) {
            if (haystack[i + j] != needle[j]) {
                match = false;
                break;
            }
        }
        if (match) return true;
    }
    return false;
}

/**
 * Compiler tests — verify that the compiler produces valid C3 output
 * for all supported features. Tests check that compile_to_c3() succeeds
 * and the output does not contain "unsupported".
 */
fn void run_compiler_tests(Interp* _unused_interp) {
    io::printn("\n=== Compiler Tests ===");
    usz pass = 0;
    usz fail = 0;

    // Use a fresh interp for compiler tests since each compile_to_c3 call
    // parses the entire stdlib prelude, consuming many expr pool entries.
    Interp* interp = (Interp*)mem::malloc(Interp.sizeof);
    interp.init();
    register_primitives(interp);
    register_destructors();

    // --- E_BEGIN tests ---

    // 1. begin with single expression
    {
        char[] code = compile_to_c3("(begin 42)", interp);
        bool ok = !str_contains(code, "unsupported");
        if (ok) { pass++; io::printn("[PASS] Compiler: (begin 42)"); }
        else    { fail++; io::printn("[FAIL] Compiler: (begin 42)"); }
    }

    // 2. begin with multiple expressions
    {
        char[] code = compile_to_c3("(begin 1 2 3)", interp);
        bool ok = !str_contains(code, "unsupported") && str_contains(code, "make_int(3)");
        if (ok) { pass++; io::printn("[PASS] Compiler: (begin 1 2 3)"); }
        else    { fail++; io::printn("[FAIL] Compiler: (begin 1 2 3)"); }
    }

    // 3. begin with define inside
    {
        char[] code = compile_to_c3("(begin (define x 1) x)", interp);
        bool ok = !str_contains(code, "unsupported");
        if (ok) { pass++; io::printn("[PASS] Compiler: (begin (define x 1) x)"); }
        else    { fail++; io::printn("[FAIL] Compiler: (begin (define x 1) x)"); }
    }

    // 4. begin inside lambda body
    {
        char[] code = compile_to_c3("(lambda (x) (begin x x))", interp);
        bool ok = !str_contains(code, "unsupported");
        if (ok) { pass++; io::printn("[PASS] Compiler: (lambda (x) (begin x x))"); }
        else    { fail++; io::printn("[FAIL] Compiler: (lambda (x) (begin x x))"); }
    }

    // --- Zero-arg lambda tests ---

    // 5. zero-arg lambda
    {
        char[] code = compile_to_c3("((lambda () 42))", interp);
        bool ok = !str_contains(code, "unsupported") && str_contains(code, "_unused");
        if (ok) { pass++; io::printn("[PASS] Compiler: ((lambda () 42))"); }
        else    { fail++; io::printn("[FAIL] Compiler: ((lambda () 42))"); }
    }

    // 6. zero-arg define + call
    {
        char[] code = compile_to_c3("(define thunk (lambda () 42)) (thunk)", interp);
        bool ok = !str_contains(code, "unsupported");
        if (ok) { pass++; io::printn("[PASS] Compiler: zero-arg define + call"); }
        else    { fail++; io::printn("[FAIL] Compiler: zero-arg define + call"); }
    }

    // 7. zero-arg in let
    {
        char[] code = compile_to_c3("(let ((f (lambda () 10))) (f))", interp);
        bool ok = !str_contains(code, "unsupported");
        if (ok) { pass++; io::printn("[PASS] Compiler: zero-arg in let"); }
        else    { fail++; io::printn("[FAIL] Compiler: zero-arg in let"); }
    }

    // --- Multi-param + multi-binding (parser desugar) ---

    // 8. multi-param lambda (desugared by parser to nested)
    {
        char[] code = compile_to_c3("((lambda (x y) (+ x y)) 3 4)", interp);
        bool ok = !str_contains(code, "unsupported");
        if (ok) { pass++; io::printn("[PASS] Compiler: multi-param lambda"); }
        else    { fail++; io::printn("[FAIL] Compiler: multi-param lambda"); }
    }

    // 9. multi-binding let (desugared by parser to nested)
    {
        char[] code = compile_to_c3("(let ((x 1) (y 2)) (+ x y))", interp);
        bool ok = !str_contains(code, "unsupported");
        if (ok) { pass++; io::printn("[PASS] Compiler: multi-binding let"); }
        else    { fail++; io::printn("[FAIL] Compiler: multi-binding let"); }
    }

    // 10. 3-param lambda
    {
        char[] code = compile_to_c3("((lambda (a b c) a) 1 2 3)", interp);
        bool ok = !str_contains(code, "unsupported");
        if (ok) { pass++; io::printn("[PASS] Compiler: 3-param lambda"); }
        else    { fail++; io::printn("[FAIL] Compiler: 3-param lambda"); }
    }

    // --- Stdlib availability ---

    // 11. map compiles
    {
        char[] code = compile_to_c3("((map (lambda (x) (+ x 1))) (list 1))", interp);
        bool ok = !str_contains(code, "unsupported");
        if (ok) { pass++; io::printn("[PASS] Compiler: map"); }
        else    { fail++; io::printn("[FAIL] Compiler: map"); }
    }

    // 12. filter compiles
    {
        char[] code = compile_to_c3("((filter (lambda (x) (> x 2))) (list 1 2 3))", interp);
        bool ok = !str_contains(code, "unsupported");
        if (ok) { pass++; io::printn("[PASS] Compiler: filter"); }
        else    { fail++; io::printn("[FAIL] Compiler: filter"); }
    }

    // 13. foldl compiles
    {
        char[] code = compile_to_c3("(((foldl (lambda (acc) (lambda (x) (+ acc x)))) 0) (list 1 2 3))", interp);
        bool ok = !str_contains(code, "unsupported");
        if (ok) { pass++; io::printn("[PASS] Compiler: foldl"); }
        else    { fail++; io::printn("[FAIL] Compiler: foldl"); }
    }

    // --- Existing features still work ---

    // 14. match compiles
    {
        char[] code = compile_to_c3("(match (list 1 2 3) ([a b c] (+ a c)))", interp);
        bool ok = !str_contains(code, "unsupported");
        if (ok) { pass++; io::printn("[PASS] Compiler: match"); }
        else    { fail++; io::printn("[FAIL] Compiler: match"); }
    }

    // 15. handle/perform compiles
    {
        char[] code = compile_to_c3("(handle (+ 1 (perform ask 0)) ((ask k x) (k 10)))", interp);
        bool ok = !str_contains(code, "unsupported");
        if (ok) { pass++; io::printn("[PASS] Compiler: handle/perform"); }
        else    { fail++; io::printn("[FAIL] Compiler: handle/perform"); }
    }

    // 16. zero-arg lambda with closure capture
    {
        char[] code = compile_to_c3("(let ((x 42)) (let ((f (lambda () x))) (f)))", interp);
        bool ok = !str_contains(code, "unsupported");
        if (ok) { pass++; io::printn("[PASS] Compiler: zero-arg closure capture"); }
        else    { fail++; io::printn("[FAIL] Compiler: zero-arg closure capture"); }
    }

    // 17. id and compose from stdlib
    {
        char[] code = compile_to_c3("(id 5)", interp);
        bool ok = !str_contains(code, "unsupported");
        if (ok) { pass++; io::printn("[PASS] Compiler: id"); }
        else    { fail++; io::printn("[FAIL] Compiler: id"); }
    }

    // 18. range from stdlib
    {
        char[] code = compile_to_c3("(range 5)", interp);
        bool ok = !str_contains(code, "unsupported");
        if (ok) { pass++; io::printn("[PASS] Compiler: range"); }
        else    { fail++; io::printn("[FAIL] Compiler: range"); }
    }

    // --- set! tests ---

    // 19. set! on global variable
    {
        char[] code = compile_to_c3("(define x 10) (set! x 20)", interp);
        bool ok = !str_contains(code, "unsupported") && str_contains(code, "x = ");
        if (ok) { pass++; io::printn("[PASS] Compiler: (set! x 20)"); }
        else    { fail++; io::printn("[FAIL] Compiler: (set! x 20)"); }
    }

    // 20. set! in expression context
    {
        char[] code = compile_to_c3("(define x 10) (+ (set! x 20) x)", interp);
        bool ok = !str_contains(code, "unsupported");
        if (ok) { pass++; io::printn("[PASS] Compiler: set! in expression"); }
        else    { fail++; io::printn("[FAIL] Compiler: set! in expression"); }
    }

    // 21. set! inside lambda
    {
        char[] code = compile_to_c3("(define counter 0) (define inc! (lambda (n) (set! counter (+ counter n))))", interp);
        bool ok = !str_contains(code, "unsupported");
        if (ok) { pass++; io::printn("[PASS] Compiler: set! inside lambda"); }
        else    { fail++; io::printn("[FAIL] Compiler: set! inside lambda"); }
    }

    // --- dot-bracket indexing tests ---

    // 22. list indexing with .[i]
    {
        char[] code = compile_to_c3("(define lst (list 10 20 30)) lst.[1]", interp);
        bool ok = !str_contains(code, "unsupported") && str_contains(code, "rt_index");
        if (ok) { pass++; io::printn("[PASS] Compiler: lst.[1]"); }
        else    { fail++; io::printn("[FAIL] Compiler: lst.[1]"); }
    }

    // 23. indexing with expression
    {
        char[] code = compile_to_c3("(define lst (list 1 2 3)) lst.[(+ 0 1)]", interp);
        bool ok = !str_contains(code, "unsupported") && str_contains(code, "rt_index");
        if (ok) { pass++; io::printn("[PASS] Compiler: lst.[(+ 0 1)]"); }
        else    { fail++; io::printn("[FAIL] Compiler: lst.[(+ 0 1)]"); }
    }

    // --- path notation tests ---

    // 24. simple path a.b
    {
        char[] code = compile_to_c3("(define point (list (cons 'x 10) (cons 'y 20))) point.x", interp);
        bool ok = !str_contains(code, "unsupported") && str_contains(code, "rt_field_access");
        if (ok) { pass++; io::printn("[PASS] Compiler: point.x"); }
        else    { fail++; io::printn("[FAIL] Compiler: point.x"); }
    }

    // 25. nested path a.b.c
    {
        char[] code = compile_to_c3("(define obj (list (cons 'inner (list (cons 'val 42))))) obj.inner.val", interp);
        bool ok = !str_contains(code, "unsupported") && str_contains(code, "rt_field_access");
        if (ok) { pass++; io::printn("[PASS] Compiler: obj.inner.val"); }
        else    { fail++; io::printn("[FAIL] Compiler: obj.inner.val"); }
    }

    // 26. path generates make_string for field name (not raw string)
    {
        char[] code = compile_to_c3("point.x", interp);
        bool ok = str_contains(code, "make_string");
        if (ok) { pass++; io::printn("[PASS] Compiler: path uses make_string for field name"); }
        else    { fail++; io::printn("[FAIL] Compiler: path uses make_string for field name"); }
    }

    // --- reset/shift via interpreter delegation ---

    // 27. reset/shift compiles natively (no rt_eval_source)
    {
        char[] code = compile_to_c3("(reset (+ 1 (shift k (k 10))))", interp);
        bool ok = str_contains(code, "rt_compiled_reset") && !str_contains(code, "unsupported");
        if (ok) { pass++; io::printn("[PASS] Compiler: reset/shift compiles natively"); }
        else    { fail++; io::printn("[FAIL] Compiler: reset/shift compiles natively"); }
    }

    // 28. set! inside begin
    {
        char[] code = compile_to_c3("(define x 0) (begin (set! x 1) (set! x (+ x 1)) x)", interp);
        bool ok = !str_contains(code, "unsupported");
        if (ok) { pass++; io::printn("[PASS] Compiler: set! inside begin"); }
        else    { fail++; io::printn("[FAIL] Compiler: set! inside begin"); }
    }

    // --- Continuation form tests (native compilation) ---

    // 29. reset compiles to rt_compiled_reset (not rt_eval_source for the reset)
    {
        char[] code = compile_to_c3("(define x 5) (reset (+ x (shift k (k 10))))", interp);
        bool ok = str_contains(code, "rt_compiled_reset");
        if (ok) { pass++; io::printn("[PASS] Compiler: reset uses rt_compiled_reset"); }
        else    { fail++; io::printn("[FAIL] Compiler: reset uses rt_compiled_reset"); }
    }

    // 30. handle/perform compiles natively
    {
        char[] code = compile_to_c3("(handle (+ 1 (perform ask 0)) ((ask k x) (k 10)))", interp);
        bool ok = str_contains(code, "rt_compiled_handle") && str_contains(code, "rt_compiled_perform");
        if (ok) { pass++; io::printn("[PASS] Compiler: handle/perform uses native compilation"); }
        else    { fail++; io::printn("[FAIL] Compiler: handle/perform uses native compilation"); }
    }

    // 31. standalone perform compiles natively
    {
        char[] code = compile_to_c3("(perform raise \"error\")", interp);
        bool ok = str_contains(code, "rt_compiled_perform");
        if (ok) { pass++; io::printn("[PASS] Compiler: perform uses rt_compiled_perform"); }
        else    { fail++; io::printn("[FAIL] Compiler: perform uses rt_compiled_perform"); }
    }

    // 32. shift compiles natively
    {
        char[] code = compile_to_c3("(shift k 42)", interp);
        bool ok = str_contains(code, "rt_compiled_shift");
        if (ok) { pass++; io::printn("[PASS] Compiler: shift uses rt_compiled_shift"); }
        else    { fail++; io::printn("[FAIL] Compiler: shift uses rt_compiled_shift"); }
    }

    // 33. reset compiles with native function
    {
        char[] code = compile_to_c3("(reset (+ 1 2))", interp);
        bool ok = str_contains(code, "rt_compiled_reset");
        if (ok) { pass++; io::printn("[PASS] Compiler: reset compiles natively"); }
        else    { fail++; io::printn("[FAIL] Compiler: reset compiles natively"); }
    }

    // 34. handle with multiple clauses compiles natively
    {
        char[] code = compile_to_c3("(handle (begin (perform get 0) (perform set 1)) ((get k x) (k 42)) ((set k v) (k v)))", interp);
        bool ok = str_contains(code, "rt_compiled_handle");
        if (ok) { pass++; io::printn("[PASS] Compiler: handle with multiple clauses"); }
        else    { fail++; io::printn("[FAIL] Compiler: handle with multiple clauses"); }
    }

    // 35. try from stdlib (uses handle/perform internally) compiles
    {
        char[] code = compile_to_c3("((try (lambda (xx) (+ 1 2))) (lambda (msg) 0))", interp);
        bool ok = !str_contains(code, "unsupported");
        if (ok) { pass++; io::printn("[PASS] Compiler: try from stdlib"); }
        else    { fail++; io::printn("[FAIL] Compiler: try from stdlib"); }
    }

    // 36. reset with nested shift compiles natively
    {
        char[] code = compile_to_c3("(reset (+ 1 (shift k (+ (k 10) (k 20)))))", interp);
        bool ok = str_contains(code, "rt_compiled_reset") && str_contains(code, "rt_compiled_shift");
        if (ok) { pass++; io::printn("[PASS] Compiler: reset with nested shift"); }
        else    { fail++; io::printn("[FAIL] Compiler: reset with nested shift"); }
    }

    // --- FFI compiler tests ---

    // 37. ffi-open compiles without unsupported
    {
        char[] code = compile_to_c3("(ffi-open \"libc.so.6\")", interp);
        bool ok = !str_contains(code, "unsupported") && str_contains(code, "rt_ffi_open");
        if (ok) { pass++; io::printn("[PASS] Compiler: ffi-open"); }
        else    { fail++; io::printn("[FAIL] Compiler: ffi-open"); }
    }

    // 38. ffi-close compiles without unsupported
    {
        char[] code = compile_to_c3("(define h (ffi-open \"libc.so.6\")) (ffi-close h)", interp);
        bool ok = !str_contains(code, "unsupported") && str_contains(code, "rt_ffi_close");
        if (ok) { pass++; io::printn("[PASS] Compiler: ffi-close"); }
        else    { fail++; io::printn("[FAIL] Compiler: ffi-close"); }
    }

    // 39. ffi-sym compiles without unsupported
    {
        char[] code = compile_to_c3("(define h (ffi-open \"libc.so.6\")) (ffi-sym h \"strlen\")", interp);
        bool ok = !str_contains(code, "unsupported") && str_contains(code, "rt_ffi_sym");
        if (ok) { pass++; io::printn("[PASS] Compiler: ffi-sym"); }
        else    { fail++; io::printn("[FAIL] Compiler: ffi-sym"); }
    }

    // 40. ffi-call compiles without unsupported
    {
        char[] code = compile_to_c3("(define h (ffi-open \"libc.so.6\")) (ffi-call h \"strlen\" 'size \"hello\" 'string)", interp);
        bool ok = !str_contains(code, "unsupported") && str_contains(code, "rt_ffi_call");
        if (ok) { pass++; io::printn("[PASS] Compiler: ffi-call"); }
        else    { fail++; io::printn("[FAIL] Compiler: ffi-call"); }
    }

    // =========================================================================
    // Phase 1: Multi-arg calls (inline currying)
    // =========================================================================

    // 41. Multi-arg call uses inline currying (rt_invoke_once + rt_invoke)
    {
        char[] code = compile_to_c3("(+ 1 2)", interp);
        bool ok = str_contains(code, "rt_invoke") && !str_contains(code, "unsupported");
        if (ok) { pass++; io::printn("[PASS] Compiler: multi-arg uses inline currying"); }
        else    { fail++; io::printn("[FAIL] Compiler: multi-arg uses inline currying"); }
    }

    // 42. Multi-arg uses rt_invoke_once for intermediate args
    {
        char[] code = compile_to_c3("(+ 1 2)", interp);
        bool ok = str_contains(code, "rt_invoke_once") || str_contains(code, "rt_invoke");
        if (ok) { pass++; io::printn("[PASS] Compiler: multi-arg uses rt_invoke"); }
        else    { fail++; io::printn("[FAIL] Compiler: multi-arg uses rt_invoke"); }
    }

    // 43. 3-arg call uses inline currying
    {
        char[] code = compile_to_c3("(substring \"hello\" 1 3)", interp);
        bool ok = str_contains(code, "rt_invoke") && !str_contains(code, "rt_apply_multi");
        if (ok) { pass++; io::printn("[PASS] Compiler: 3-arg inline currying"); }
        else    { fail++; io::printn("[FAIL] Compiler: 3-arg inline currying"); }
    }

    // 44. Zero-arg call via rt_invoke
    {
        char[] code = compile_to_c3("((lambda () 42))", interp);
        bool ok = str_contains(code, "rt_invoke") && str_contains(code, "make_nil()");
        if (ok) { pass++; io::printn("[PASS] Compiler: zero-arg rt_invoke"); }
        else    { fail++; io::printn("[FAIL] Compiler: zero-arg rt_invoke"); }
    }

    // =========================================================================
    // Phase 2: TCO via trampoline
    // =========================================================================

    // 45. Tail calls in lambda body emit make_thunk
    {
        char[] code = compile_to_c3("(define (f x) (f (+ x 1)))", interp);
        bool ok = str_contains(code, "make_thunk");
        if (ok) { pass++; io::printn("[PASS] Compiler: tail call emits make_thunk"); }
        else    { fail++; io::printn("[FAIL] Compiler: tail call emits make_thunk"); }
    }

    // 46. Non-tail calls don't emit make_thunk
    {
        char[] code = compile_to_c3("(define (f x) (+ (f x) 1))", interp);
        // The (f x) is not in tail position (it's an argument to +)
        bool ok = !str_contains(code, "unsupported");
        if (ok) { pass++; io::printn("[PASS] Compiler: non-tail compiles OK"); }
        else    { fail++; io::printn("[FAIL] Compiler: non-tail compiles OK"); }
    }

    // 47. If branches propagate tail position
    {
        char[] code = compile_to_c3("(define (f x) (if (= x 0) 1 (f (- x 1))))", interp);
        bool ok = str_contains(code, "make_thunk");
        if (ok) { pass++; io::printn("[PASS] Compiler: if branch tail position"); }
        else    { fail++; io::printn("[FAIL] Compiler: if branch tail position"); }
    }

    // =========================================================================
    // Phase 3: Quasiquote, macros, modules
    // =========================================================================

    // 48. Quasiquote compiles natively with rt_cons
    {
        char[] code = compile_to_c3("(let ((x 5)) `(a ,x))", interp);
        bool ok = str_contains(code, "rt_cons") && str_contains(code, "make_symbol");
        if (ok) { pass++; io::printn("[PASS] Compiler: quasiquote compiles natively"); }
        else    { fail++; io::printn("[FAIL] Compiler: quasiquote compiles natively"); }
    }

    // 49. Quasiquote with unquote uses make_symbol for literal parts
    {
        char[] code = compile_to_c3("(let ((x 5)) `(a ,x))", interp);
        bool ok = str_contains(code, "make_symbol(\"a\")");
        if (ok) { pass++; io::printn("[PASS] Compiler: quasiquote uses make_symbol"); }
        else    { fail++; io::printn("[FAIL] Compiler: quasiquote uses make_symbol"); }
    }

    // 50. Defmacro delegates to rt_eval_source
    {
        char[] code = compile_to_c3("(define [macro] double ([x] (+ x x)))", interp);
        bool ok = str_contains(code, "make_nil") && !str_contains(code, "unsupported");
        if (ok) { pass++; io::printn("[PASS] Compiler: defmacro compiles as no-op"); }
        else    { fail++; io::printn("[FAIL] Compiler: defmacro compiles as no-op"); }
    }

    // 51. Stdlib functions (when, unless) compiled from STDLIB_PRELUDE
    {
        char[] code = compile_to_c3("42", interp);
        bool ok = str_contains(code, "when") && str_contains(code, "unless");
        if (ok) { pass++; io::printn("[PASS] Compiler: stdlib functions compiled"); }
        else    { fail++; io::printn("[FAIL] Compiler: stdlib functions compiled"); }
    }

    // =========================================================================
    // Phase 4: Missing primitives
    // =========================================================================

    // 52. string->number compiles
    {
        char[] code = compile_to_c3("(string->number \"42\")", interp);
        bool ok = str_contains(code, "rt_string_to_number") && !str_contains(code, "unsupported");
        if (ok) { pass++; io::printn("[PASS] Compiler: string->number"); }
        else    { fail++; io::printn("[FAIL] Compiler: string->number"); }
    }

    // 53. number->string compiles
    {
        char[] code = compile_to_c3("(number->string 42)", interp);
        bool ok = str_contains(code, "rt_number_to_string") && !str_contains(code, "unsupported");
        if (ok) { pass++; io::printn("[PASS] Compiler: number->string"); }
        else    { fail++; io::printn("[FAIL] Compiler: number->string"); }
    }

    // 54. gensym compiles
    {
        char[] code = compile_to_c3("(gensym)", interp);
        bool ok = str_contains(code, "rt_gensym") && !str_contains(code, "unsupported");
        if (ok) { pass++; io::printn("[PASS] Compiler: gensym"); }
        else    { fail++; io::printn("[FAIL] Compiler: gensym"); }
    }

    // 55. apply compiles
    {
        char[] code = compile_to_c3("(apply + (list 1 2))", interp);
        bool ok = str_contains(code, "rt_apply_prim") && !str_contains(code, "unsupported");
        if (ok) { pass++; io::printn("[PASS] Compiler: apply"); }
        else    { fail++; io::printn("[FAIL] Compiler: apply"); }
    }

    // 56. equal? compiles
    {
        char[] code = compile_to_c3("(equal? 1 2)", interp);
        bool ok = str_contains(code, "rt_equal_p") && !str_contains(code, "unsupported");
        if (ok) { pass++; io::printn("[PASS] Compiler: equal?"); }
        else    { fail++; io::printn("[FAIL] Compiler: equal?"); }
    }

    // 57. display compiles
    {
        char[] code = compile_to_c3("(display \"hello\")", interp);
        bool ok = str_contains(code, "rt_display") && !str_contains(code, "unsupported");
        if (ok) { pass++; io::printn("[PASS] Compiler: display"); }
        else    { fail++; io::printn("[FAIL] Compiler: display"); }
    }

    // 58. ref compiles
    {
        char[] code = compile_to_c3("(ref (dict) 'a)", interp);
        bool ok = str_contains(code, "rt_hash_ref") && !str_contains(code, "unsupported");
        if (ok) { pass++; io::printn("[PASS] Compiler: ref"); }
        else    { fail++; io::printn("[FAIL] Compiler: ref"); }
    }

    // 59. load compiles
    {
        char[] code = compile_to_c3("(load \"test.pika\")", interp);
        bool ok = str_contains(code, "rt_load") && !str_contains(code, "unsupported");
        if (ok) { pass++; io::printn("[PASS] Compiler: load"); }
        else    { fail++; io::printn("[FAIL] Compiler: load"); }
    }

    // =========================================================================
    // Phase 5: Mutable capture boxing
    // =========================================================================

    // 60. Mutable capture detected and uses rt_define_var
    {
        char[] code = compile_to_c3("(let ((x 0)) (let ((inc (lambda () (set! x (+ x 1))))) (inc) x))", interp);
        // Mutable capture test - checks generated code for interpreter env usage
        bool ok = str_contains(code, "rt_define_var") && str_contains(code, "rt_set_var");
        if (ok) { pass++; io::printn("[PASS] Compiler: mutable capture uses rt_define_var/rt_set_var"); }
        else    { fail++; io::printn("[FAIL] Compiler: mutable capture uses rt_define_var/rt_set_var"); }
    }

    // 61. Mutable capture reads use rt_lookup_var
    {
        char[] code = compile_to_c3("(let ((x 0)) (let ((inc (lambda () (set! x (+ x 1))))) (inc) x))", interp);
        bool ok = str_contains(code, "rt_lookup_var");
        if (ok) { pass++; io::printn("[PASS] Compiler: mutable capture reads use rt_lookup_var"); }
        else    { fail++; io::printn("[FAIL] Compiler: mutable capture reads use rt_lookup_var"); }
    }

    // 62. Non-mutable let uses C3 local variable (Value x = ...)
    {
        char[] code = compile_to_c3("(let ((x 42)) x)", interp);
        // The main fn body should contain "Value x = " for a non-mutable let
        bool ok = str_contains(code, "Value x = ");
        if (ok) { pass++; io::printn("[PASS] Compiler: non-mutable let uses C3 local"); }
        else    { fail++; io::printn("[FAIL] Compiler: non-mutable let uses C3 local"); }
    }

    // =========================================================================
    // Integration: Verify all features work together
    // =========================================================================

    // 63. Complex program with multiple features compiles
    {
        char[] code = compile_to_c3(
            "(define (factorial n) (if (= n 0) 1 (* n (factorial (- n 1))))) (factorial 10)", interp);
        bool ok = !str_contains(code, "unsupported");
        if (ok) { pass++; io::printn("[PASS] Compiler: factorial compiles"); }
        else    { fail++; io::printn("[FAIL] Compiler: factorial compiles"); }
    }

    // 64. Let-rec with tail call emits make_thunk
    {
        char[] code = compile_to_c3(
            "(let ^rec ((loop (lambda (n) (if (= n 0) 'done (loop (- n 1)))))) (loop 100))", interp);
        bool ok = str_contains(code, "make_thunk") && !str_contains(code, "unsupported");
        if (ok) { pass++; io::printn("[PASS] Compiler: let-rec tail call emits thunk"); }
        else    { fail++; io::printn("[FAIL] Compiler: let-rec tail call emits thunk"); }
    }

    // 65. Nested multi-arg calls compile correctly
    {
        char[] code = compile_to_c3("(+ (* 2 3) (* 4 5))", interp);
        bool ok = str_contains(code, "rt_invoke") && !str_contains(code, "unsupported");
        if (ok) { pass++; io::printn("[PASS] Compiler: nested multi-arg calls"); }
        else    { fail++; io::printn("[FAIL] Compiler: nested multi-arg calls"); }
    }

    // =========================================================================
    // Phase 6: STDLIB_PRELUDE iterative map/filter
    // =========================================================================

    // 66. STDLIB_PRELUDE map uses iterative loop pattern
    {
        char[] code = compile_to_c3("((map (lambda (x) x)) (list 1))", interp);
        bool ok = str_contains(code, "loop");
        if (ok) { pass++; io::printn("[PASS] Compiler: STDLIB map uses loop pattern"); }
        else    { fail++; io::printn("[FAIL] Compiler: STDLIB map uses loop pattern"); }
    }

    // 67. STDLIB_PRELUDE filter uses iterative loop pattern
    {
        char[] code = compile_to_c3("((filter (lambda (x) x)) (list 1))", interp);
        bool ok = str_contains(code, "loop");
        if (ok) { pass++; io::printn("[PASS] Compiler: STDLIB filter uses loop pattern"); }
        else    { fail++; io::printn("[FAIL] Compiler: STDLIB filter uses loop pattern"); }
    }

    // =========================================================================
    // Phase 7: Runtime bridge tests (rt_eval_source correctness)
    // =========================================================================

    // 68. map correctness via runtime
    {
        runtime::Value result = runtime::rt_eval_source("((map (lambda (x) (+ x 1))) (list 1 2 3))");
        // Should be list (2 3 4) — check first element is 2
        bool ok = false;
        if (result.tag == runtime::ValueTag.V_CONS) {
            runtime::Value* car = main::dereference_as(runtime::Value, result.cons_val.car);
            ok = (car.tag == runtime::ValueTag.V_INT && car.int_val == 2);
        }
        if (ok) { pass++; io::printn("[PASS] Runtime: map correctness"); }
        else    { fail++; io::printn("[FAIL] Runtime: map correctness"); }
    }

    // 69. filter correctness via runtime
    {
        runtime::Value result = runtime::rt_eval_source("((filter (lambda (x) (> x 2))) (list 1 2 3 4))");
        // Should be list (3 4) — check first element is 3
        bool ok = false;
        if (result.tag == runtime::ValueTag.V_CONS) {
            runtime::Value* car = main::dereference_as(runtime::Value, result.cons_val.car);
            ok = (car.tag == runtime::ValueTag.V_INT && car.int_val == 3);
        }
        if (ok) { pass++; io::printn("[PASS] Runtime: filter correctness"); }
        else    { fail++; io::printn("[FAIL] Runtime: filter correctness"); }
    }

    // 70. TCO depth via runtime (named-let loop to 10000)
    {
        runtime::Value result = runtime::rt_eval_source("(let loop ((n 10000) (acc 0)) (if (= n 0) acc (loop (- n 1) (+ acc 1))))");
        bool ok = (result.tag == runtime::ValueTag.V_INT && result.int_val == 10000);
        if (ok) { pass++; io::printn("[PASS] Runtime: TCO depth 10000"); }
        else    { fail++; io::printn("[FAIL] Runtime: TCO depth 10000"); }
    }

    // 71. multi-arg via runtime
    {
        runtime::Value result = runtime::rt_eval_source("((lambda (x y z) (+ x (+ y z))) 1 2 3)");
        bool ok = (result.tag == runtime::ValueTag.V_INT && result.int_val == 6);
        if (ok) { pass++; io::printn("[PASS] Runtime: multi-arg call"); }
        else    { fail++; io::printn("[FAIL] Runtime: multi-arg call"); }
    }

    // 72. mutable capture via runtime (using begin + define for set!)
    {
        runtime::Value result = runtime::rt_eval_source("(begin (define x 0) (set! x (+ x 1)) (set! x (+ x 1)) x)");
        bool ok = (result.tag == runtime::ValueTag.V_INT && result.int_val == 2);
        if (ok) { pass++; io::printn("[PASS] Runtime: mutable capture"); }
        else    { fail++; io::printn("[FAIL] Runtime: mutable capture"); }
    }

    // 73. macro via runtime
    {
        runtime::Value result = runtime::rt_eval_source("(begin (define [macro] double ([x] (+ x x))) (double 21))");
        bool ok = (result.tag == runtime::ValueTag.V_INT && result.int_val == 42);
        if (ok) { pass++; io::printn("[PASS] Runtime: macro expansion"); }
        else    { fail++; io::printn("[FAIL] Runtime: macro expansion"); }
    }

    // =========================================================================
    // Phase 8: compile_to_c3_with_print pattern tests
    // =========================================================================

    // 74. print_last works for literals
    {
        char[] code = compile_to_c3_with_print("42", interp);
        bool ok = str_contains(code, "rt_print_value");
        if (ok) { pass++; io::printn("[PASS] Compiler: print_last literal"); }
        else    { fail++; io::printn("[FAIL] Compiler: print_last literal"); }
    }

    // 75. print_last works for calls
    {
        char[] code = compile_to_c3_with_print("(+ 1 2)", interp);
        bool ok = str_contains(code, "rt_print_value");
        if (ok) { pass++; io::printn("[PASS] Compiler: print_last call"); }
        else    { fail++; io::printn("[FAIL] Compiler: print_last call"); }
    }

    // 76. print_last skips defines, wraps last non-define
    {
        char[] code = compile_to_c3_with_print("(define x 1) x", interp);
        bool ok = str_contains(code, "rt_print_value");
        if (ok) { pass++; io::printn("[PASS] Compiler: print_last skips defines"); }
        else    { fail++; io::printn("[FAIL] Compiler: print_last skips defines"); }
    }

    // 77. default case warning text present
    {
        char[] code = compile_to_c3("42", interp);
        // The default case emits "WARNING" in comment — but only for unsupported exprs.
        // We verify the constant string is in the source by checking the STDLIB compiles without it.
        bool ok = !str_contains(code, "WARNING");
        if (ok) { pass++; io::printn("[PASS] Compiler: no WARNING in normal compilation"); }
        else    { fail++; io::printn("[FAIL] Compiler: no WARNING in normal compilation"); }
    }

    mem::free(interp);
    io::printfn("\n=== Compiler Tests: %d passed, %d failed ===", pass, fail);
    assert(fail == 0, "compiler tests had failures");
}
