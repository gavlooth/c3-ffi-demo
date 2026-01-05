#define _POSIX_C_SOURCE 200809L
#include "types.h"
#include "util/dstring.h"
#include <string.h>

// -- Compiler Arena (Phase 12) --
// Global arena for all compiler-phase allocations

typedef struct ArenaBlock {
    char* memory;
    size_t size;
    size_t used;
    struct ArenaBlock* next;
} ArenaBlock;

typedef struct StringNode {
    char* s;
    struct StringNode* next;
} StringNode;

static ArenaBlock* compiler_arena_blocks = NULL;
static ArenaBlock* compiler_arena_current = NULL;
static StringNode* compiler_strings = NULL;
static size_t compiler_arena_block_size = 65536;  // 64KB blocks
static int compiler_arena_string_oom = 0;

static void* compiler_arena_alloc(size_t size);

void compiler_arena_init(void) {
    compiler_arena_blocks = NULL;
    compiler_arena_current = NULL;
    compiler_strings = NULL;
    compiler_arena_string_oom = 0;
    // Allocate initial block so compiler allocations use the arena
    (void)compiler_arena_alloc(0);
}

static void* compiler_arena_alloc(size_t size) {
    // Align to 8 bytes
    size = (size + 7) & ~(size_t)7;

    if (!compiler_arena_current || compiler_arena_current->used + size > compiler_arena_current->size) {
        // Need new block
        size_t bs = compiler_arena_block_size;
        if (size > bs) bs = size;

        ArenaBlock* b = malloc(sizeof(ArenaBlock));
        if (!b) return NULL;
        b->memory = malloc(bs);
        if (!b->memory) {
            free(b);
            return NULL;
        }
        b->size = bs;
        b->used = 0;
        b->next = compiler_arena_blocks;
        compiler_arena_blocks = b;
        compiler_arena_current = b;
    }

    void* ptr = compiler_arena_current->memory + compiler_arena_current->used;
    compiler_arena_current->used += size;
    return ptr;
}

void compiler_arena_register_string(char* s) {
    if (!s || compiler_arena_string_oom) return;
    StringNode* node = compiler_arena_alloc(sizeof(StringNode));
    if (!node) {
        compiler_arena_string_oom = 1;
        fprintf(stderr, "Warning: OOM tracking compiler string; potential leak\n");
        return;
    }
    node->s = s;
    node->next = compiler_strings;
    compiler_strings = node;
}

void compiler_arena_cleanup(void) {
    // Free all strings
    StringNode* sn = compiler_strings;
    while (sn) {
        StringNode* next = sn->next;
        free(sn->s);
        sn = next;
    }
    compiler_strings = NULL;

    // Free all arena blocks
    ArenaBlock* b = compiler_arena_blocks;
    while (b) {
        ArenaBlock* next = b->next;
        free(b->memory);
        free(b);
        b = next;
    }
    compiler_arena_blocks = NULL;
    compiler_arena_current = NULL;
}

// -- Value Constructors --

Value* alloc_val(Tag tag) {
    Value* v;
    if (compiler_arena_current) {
        v = compiler_arena_alloc(sizeof(Value));
    } else {
        v = malloc(sizeof(Value));
    }
    if (!v) return NULL;
    v->tag = tag;
    return v;
}

Value* mk_int(long i) {
    Value* v = alloc_val(T_INT);
    if (!v) return NULL;
    v->i = i;
    return v;
}

Value* mk_nil(void) {
    static Value nil_singleton = { .tag = T_NIL };
    return &nil_singleton;
}

Value* mk_nothing(void) {
    static Value nothing_singleton = { .tag = T_NOTHING };
    return &nothing_singleton;
}

Value* mk_sym(const char* s) {
    if (!s) s = "";
    Value* v = alloc_val(T_SYM);
    if (!v) return NULL;
    v->s = strdup(s);
    if (!v->s) {
        // Don't free v if using arena (arena will bulk free)
        if (!compiler_arena_current) free(v);
        return NULL;
    }
    if (compiler_arena_current) {
        compiler_arena_register_string(v->s);
    }
    return v;
}

Value* mk_cell(Value* car, Value* cdr) {
    Value* v = alloc_val(T_CELL);
    if (!v) return NULL;
    v->cell.car = car;
    v->cell.cdr = cdr;
    return v;
}

Value* mk_prim(PrimFn fn) {
    Value* v = alloc_val(T_PRIM);
    if (!v) return NULL;
    v->prim = fn;
    return v;
}

Value* mk_code(const char* s) {
    if (!s) s = "";
    Value* v = alloc_val(T_CODE);
    if (!v) return NULL;
    v->s = strdup(s);
    if (!v->s) {
        // Don't free v if using arena (arena will bulk free)
        if (!compiler_arena_current) free(v);
        return NULL;
    }
    if (compiler_arena_current) {
        compiler_arena_register_string(v->s);
    }
    return v;
}

Value* mk_lambda(Value* params, Value* body, Value* env) {
    Value* v = alloc_val(T_LAMBDA);
    if (!v) return NULL;
    v->lam.params = params;
    v->lam.body = body;
    v->lam.env = env;
    v->lam.defaults = NULL;  // No defaults
    return v;
}

// Create lambda with default parameter values
// defaults is a list of default expressions, one per param (or NULL for required params)
Value* mk_lambda_with_defaults(Value* params, Value* body, Value* env, Value* defaults) {
    Value* v = alloc_val(T_LAMBDA);
    if (!v) return NULL;
    v->lam.params = params;
    v->lam.body = body;
    v->lam.env = env;
    v->lam.defaults = defaults;
    return v;
}

Value* mk_error(const char* msg) {
    if (!msg) msg = "unknown error";
    Value* v = alloc_val(T_ERROR);
    if (!v) return NULL;
    v->s = strdup(msg);
    if (!v->s) {
        if (!compiler_arena_current) free(v);
        return NULL;
    }
    if (compiler_arena_current) {
        compiler_arena_register_string(v->s);
    }
    return v;
}

Value* mk_box(Value* initial) {
    Value* v = alloc_val(T_BOX);
    if (!v) return NULL;
    v->box_value = initial;
    return v;
}

Value* mk_cont(ContFn fn, Value* menv, int tag) {
    Value* v = alloc_val(T_CONT);
    if (!v) return NULL;
    v->cont.fn = fn;
    v->cont.menv = menv;
    v->cont.tag = tag;
    return v;
}

Value* mk_chan(int capacity) {
    Value* v = alloc_val(T_CHAN);
    if (!v) return NULL;

    // Allocate the Channel structure
    Channel* ch = malloc(sizeof(Channel));
    if (!ch) {
        if (!compiler_arena_current) free(v);
        return NULL;
    }

    ch->capacity = capacity;
    ch->head = 0;
    ch->tail = 0;
    ch->count = 0;
    ch->closed = 0;
    ch->send_waiters = NULL;
    ch->recv_waiters = NULL;

    // Allocate buffer for buffered channels
    if (capacity > 0) {
        ch->buffer = malloc(sizeof(Value*) * capacity);
        if (!ch->buffer) {
            free(ch);
            if (!compiler_arena_current) free(v);
            return NULL;
        }
        for (int i = 0; i < capacity; i++) {
            ch->buffer[i] = NULL;
        }
    } else {
        ch->buffer = NULL;
    }

    v->chan.ch = ch;
    v->chan.capacity = capacity;
    return v;
}

Value* mk_process(Value* thunk) {
    Value* v = alloc_val(T_PROCESS);
    if (!v) return NULL;
    v->proc.thunk = thunk;
    v->proc.cont = NULL;
    v->proc.menv = NULL;
    v->proc.result = NULL;
    v->proc.park_value = NULL;
    v->proc.state = PROC_READY;

    // Allocate fiber context for ucontext-based coroutines
    FiberContext* ctx = malloc(sizeof(FiberContext));
    if (!ctx) {
        if (!compiler_arena_current) free(v);
        return NULL;
    }
    ctx->stack = malloc(FIBER_STACK_SIZE);
    if (!ctx->stack) {
        free(ctx);
        if (!compiler_arena_current) free(v);
        return NULL;
    }
    ctx->yield_value = NULL;
    ctx->started = 0;
    v->proc.fiber_ctx = ctx;

    return v;
}

Value* mk_bounce(Value* fn, Value* args) {
    Value* v = alloc_val(T_BOUNCE);
    if (!v) return NULL;
    v->bounce.fn = fn;
    v->bounce.args = args;
    return v;
}

Value* mk_string(const char* s, size_t len) {
    Value* v = alloc_val(T_STRING);
    if (!v) return NULL;
    v->str.data = malloc(len + 1);
    if (!v->str.data) {
        if (!compiler_arena_current) free(v);
        return NULL;
    }
    memcpy(v->str.data, s, len);
    v->str.data[len] = '\0';
    v->str.len = len;
    if (compiler_arena_current) {
        compiler_arena_register_string(v->str.data);
    }
    return v;
}

Value* mk_string_cstr(const char* s) {
    if (!s) return mk_string("", 0);
    return mk_string(s, strlen(s));
}

Value* mk_char(long codepoint) {
    Value* v = alloc_val(T_CHAR);
    if (!v) return NULL;
    v->codepoint = codepoint;
    return v;
}

Value* mk_float(double f) {
    Value* v = alloc_val(T_FLOAT);
    if (!v) return NULL;
    v->f = f;
    return v;
}

Value* mk_port(FILE* fp, const char* filename, int mode) {
    Value* v = alloc_val(T_PORT);
    if (!v) return NULL;
    v->port.fp = fp;
    v->port.filename = strdup(filename);
    v->port.mode = mode;
    v->port.closed = 0;
    return v;
}

Value* mk_syntax(const char* name, Value* literals, Value* rules, Value* def_env) {
    Value* v = alloc_val(T_SYNTAX);
    if (!v) return NULL;
    v->syntax.name = name ? strdup(name) : NULL;
    v->syntax.literals = literals;
    v->syntax.rules = rules;
    v->syntax.def_env = def_env;
    if (v->syntax.name && compiler_arena_current) {
        compiler_arena_register_string(v->syntax.name);
    }
    return v;
}

Value* mk_ffi_lib(void* handle, const char* name) {
    Value* v = alloc_val(T_FFI_LIB);
    if (!v) return NULL;
    v->ffi_lib.handle = handle;
    v->ffi_lib.name = name ? strdup(name) : NULL;
    if (v->ffi_lib.name && compiler_arena_current) {
        compiler_arena_register_string(v->ffi_lib.name);
    }
    return v;
}

Value* mk_ffi_ptr(void* ptr, const char* type_name, int owned) {
    Value* v = alloc_val(T_FFI_PTR);
    if (!v) return NULL;
    v->ffi_ptr.ptr = ptr;
    v->ffi_ptr.type_name = type_name ? strdup(type_name) : NULL;
    v->ffi_ptr.owned = owned;
    if (v->ffi_ptr.type_name && compiler_arena_current) {
        compiler_arena_register_string(v->ffi_ptr.type_name);
    }
    return v;
}

Value* mk_thread(Value* thunk) {
    Value* v = alloc_val(T_THREAD);
    if (!v) return NULL;
    v->thread.thunk = thunk;
    v->thread.result = NULL;
    v->thread.started = 0;
    v->thread.joined = 0;
    v->thread.done = 0;
    return v;
}

// -- Type Predicates --

int is_box(Value* v) {
    return v != NULL && v->tag == T_BOX;
}

int is_cont(Value* v) {
    return v != NULL && v->tag == T_CONT;
}

int is_chan(Value* v) {
    return v != NULL && v->tag == T_CHAN;
}

int is_process(Value* v) {
    return v != NULL && v->tag == T_PROCESS;
}

int is_error(Value* v) {
    return v != NULL && v->tag == T_ERROR;
}

int is_bounce(Value* v) {
    return v != NULL && v->tag == T_BOUNCE;
}

int is_string(Value* v) {
    return v != NULL && v->tag == T_STRING;
}

int is_char(Value* v) {
    return v != NULL && v->tag == T_CHAR;
}

int is_float(Value* v) {
    return v != NULL && v->tag == T_FLOAT;
}

int is_port(Value* v) {
    return v != NULL && v->tag == T_PORT;
}

int is_syntax(Value* v) {
    return v != NULL && v->tag == T_SYNTAX;
}

int is_ffi_lib(Value* v) {
    return v != NULL && v->tag == T_FFI_LIB;
}

int is_ffi_ptr(Value* v) {
    return v != NULL && v->tag == T_FFI_PTR;
}

int is_thread(Value* v) {
    return v != NULL && v->tag == T_THREAD;
}

// -- Numeric Helpers --

int is_numeric(Value* v) {
    return v != NULL && (v->tag == T_INT || v->tag == T_FLOAT);
}

double to_double(Value* v) {
    if (!v) return 0.0;
    if (v->tag == T_FLOAT) return v->f;
    if (v->tag == T_INT) return (double)v->i;
    return 0.0;
}

// -- String Operations --

size_t string_len(Value* s) {
    if (!s || s->tag != T_STRING) return 0;
    return s->str.len;
}

const char* string_data(Value* s) {
    if (!s || s->tag != T_STRING) return "";
    return s->str.data;
}

Value* string_append(Value* a, Value* b) {
    if (!is_string(a) || !is_string(b)) return NULL;
    size_t new_len = a->str.len + b->str.len;
    char* new_data = malloc(new_len + 1);
    if (!new_data) return NULL;
    memcpy(new_data, a->str.data, a->str.len);
    memcpy(new_data + a->str.len, b->str.data, b->str.len);
    new_data[new_len] = '\0';
    Value* result = mk_string(new_data, new_len);
    free(new_data);
    return result;
}

Value* string_ref(Value* s, size_t index) {
    if (!is_string(s)) return NULL;
    if (index >= s->str.len) return NULL;
    // For now, return the byte at index as a char
    // TODO: proper UTF-8 handling
    return mk_char((unsigned char)s->str.data[index]);
}

Value* substring(Value* s, size_t start, size_t end) {
    if (!is_string(s)) return NULL;
    if (start > end || end > s->str.len) return NULL;
    return mk_string(s->str.data + start, end - start);
}

// -- Box Operations --

Value* box_get(Value* box) {
    if (!box || box->tag != T_BOX) return NULL;
    return box->box_value;
}

void box_set(Value* box, Value* val) {
    if (box && box->tag == T_BOX) {
        box->box_value = val;
    }
}

// -- Value Helpers --

int is_nil(Value* v) {
    return v == NULL || v->tag == T_NIL;
}

int is_nothing(Value* v) {
    return v != NULL && v->tag == T_NOTHING;
}

int is_code(Value* v) {
    return v && v->tag == T_CODE;
}

Value* car(Value* v) {
    return (v && v->tag == T_CELL) ? v->cell.car : NULL;
}

Value* cdr(Value* v) {
    return (v && v->tag == T_CELL) ? v->cell.cdr : NULL;
}

int sym_eq(Value* s1, Value* s2) {
    if (!s1 || !s2) return 0;
    if (s1->tag != T_SYM || s2->tag != T_SYM) return 0;
    if (!s1->s || !s2->s) return 0;
    return strcmp(s1->s, s2->s) == 0;
}

int sym_eq_str(Value* s1, const char* s2) {
    if (!s1 || s1->tag != T_SYM) return 0;
    if (!s1->s || !s2) return 0;
    return strcmp(s1->s, s2) == 0;
}

char* list_to_str(Value* v) {
    DString* ds = ds_new();
    if (!ds) return NULL;
    ds_append_char(ds, '(');
    while (v && !is_nil(v)) {
        char* s = val_to_str(car(v));
        if (s) {
            ds_append(ds, s);
            free(s);
        }
        v = cdr(v);
        if (v && !is_nil(v)) ds_append_char(ds, ' ');
    }
    ds_append_char(ds, ')');
    return ds_take(ds);
}

char* val_to_str(Value* v) {
    if (!v) return strdup("NULL");
    DString* ds;
    switch (v->tag) {
        case T_INT:
            ds = ds_new();
            if (!ds) return NULL;
            ds_append_int(ds, v->i);
            return ds_take(ds);
        case T_SYM:
            return v->s ? strdup(v->s) : NULL;
        case T_CODE:
            return v->s ? strdup(v->s) : NULL;
        case T_CELL:
            return list_to_str(v);
        case T_NIL:
            return strdup("()");
        case T_NOTHING:
            return strdup("nothing");
        case T_PRIM:
            return strdup("#<prim>");
        case T_LAMBDA:
            return strdup("#<lambda>");
        case T_MENV:
            return strdup("#<menv>");
        case T_ERROR:
            ds = ds_new();
            if (!ds) return NULL;
            ds_append(ds, "#<error: ");
            if (v->s) ds_append(ds, v->s);
            ds_append(ds, ">");
            return ds_take(ds);
        case T_BOX:
            ds = ds_new();
            if (!ds) return NULL;
            ds_append(ds, "#<box ");
            if (v->box_value) {
                char* inner = val_to_str(v->box_value);
                if (inner) {
                    ds_append(ds, inner);
                    free(inner);
                }
            } else {
                ds_append(ds, "nothing");
            }
            ds_append(ds, ">");
            return ds_take(ds);
        case T_CONT:
            return strdup("#<continuation>");
        case T_CHAN:
            ds = ds_new();
            if (!ds) return NULL;
            ds_printf(ds, "#<channel cap=%d>", v->chan.capacity);
            return ds_take(ds);
        case T_PROCESS: {
            const char* state_names[] = {"ready", "running", "parked", "done"};
            const char* state = (v->proc.state >= 0 && v->proc.state <= 3)
                                ? state_names[v->proc.state] : "unknown";
            ds = ds_new();
            if (!ds) return NULL;
            ds_printf(ds, "#<process %s>", state);
            return ds_take(ds);
        }
        case T_BOUNCE:
            return strdup("#<bounce>");
        case T_STRING:
            ds = ds_new();
            if (!ds) return NULL;
            ds_append_char(ds, '"');
            if (v->str.data) {
                // Escape special characters
                for (size_t i = 0; i < v->str.len; i++) {
                    char c = v->str.data[i];
                    switch (c) {
                        case '\n': ds_append(ds, "\\n"); break;
                        case '\t': ds_append(ds, "\\t"); break;
                        case '\r': ds_append(ds, "\\r"); break;
                        case '"':  ds_append(ds, "\\\""); break;
                        case '\\': ds_append(ds, "\\\\"); break;
                        default:   ds_append_char(ds, c); break;
                    }
                }
            }
            ds_append_char(ds, '"');
            return ds_take(ds);
        case T_CHAR:
            ds = ds_new();
            if (!ds) return NULL;
            // Print as #\c or #\newline etc
            ds_append(ds, "#\\");
            if (v->codepoint == '\n') ds_append(ds, "newline");
            else if (v->codepoint == '\t') ds_append(ds, "tab");
            else if (v->codepoint == '\r') ds_append(ds, "return");
            else if (v->codepoint == ' ') ds_append(ds, "space");
            else if (v->codepoint >= 32 && v->codepoint < 127)
                ds_append_char(ds, (char)v->codepoint);
            else
                ds_printf(ds, "x%lx", v->codepoint);
            return ds_take(ds);
        case T_FLOAT:
            ds = ds_new();
            if (!ds) return NULL;
            // Print with enough precision, remove trailing zeros
            ds_printf(ds, "%.15g", v->f);
            return ds_take(ds);
        case T_PORT:
            ds = ds_new();
            if (!ds) return NULL;
            ds_printf(ds, "#<port:%s%s>",
                      v->port.filename ? v->port.filename : "?",
                      v->port.closed ? " (closed)" : "");
            return ds_take(ds);
        case T_SYNTAX:
            ds = ds_new();
            if (!ds) return NULL;
            ds_printf(ds, "#<syntax:%s>",
                      v->syntax.name ? v->syntax.name : "?");
            return ds_take(ds);
        case T_FFI_LIB:
            ds = ds_new();
            if (!ds) return NULL;
            ds_printf(ds, "#<ffi-lib:%s>",
                      v->ffi_lib.name ? v->ffi_lib.name : "?");
            return ds_take(ds);
        case T_FFI_PTR:
            ds = ds_new();
            if (!ds) return NULL;
            ds_printf(ds, "#<ffi-ptr:%p%s>",
                      v->ffi_ptr.ptr,
                      v->ffi_ptr.type_name ? v->ffi_ptr.type_name : "");
            return ds_take(ds);
        case T_THREAD: {
            const char* state = v->thread.done ? "done" :
                               (v->thread.started ? "running" : "pending");
            ds = ds_new();
            if (!ds) return NULL;
            ds_printf(ds, "#<thread %s>", state);
            return ds_take(ds);
        }
        default:
            return strdup("?");
    }
}
