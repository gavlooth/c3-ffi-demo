#define _POSIX_C_SOURCE 200809L
#include "omni_compile.h"
#include "../util/dstring.h"
#include "../reader/omni_reader.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <dlfcn.h>
#include <unistd.h>
#include <sys/wait.h>

// Forward declarations
static void emit(CompileCtx* ctx, const char* s);
static void emit_fmt(CompileCtx* ctx, const char* fmt, ...);
static void emit_indent(CompileCtx* ctx);
static char* fresh_temp(CompileCtx* ctx);
static char* fresh_label(CompileCtx* ctx);
static int compile_expr_to_var(CompileCtx* ctx, Value* expr, char* result_var);
static int compile_call(CompileCtx* ctx, Value* fn, Value* args, char* result_var);
static int compile_special(CompileCtx* ctx, const char* form, Value* args, char* result_var);

// Emit helpers
static void emit(CompileCtx* ctx, const char* s) {
    size_t len = strlen(s);
    while (ctx->output_len + len + 1 >= ctx->output_cap) {
        ctx->output_cap *= 2;
        ctx->output = realloc(ctx->output, ctx->output_cap);
    }
    memcpy(ctx->output + ctx->output_len, s, len);
    ctx->output_len += len;
    ctx->output[ctx->output_len] = '\0';
}

static void emit_fmt(CompileCtx* ctx, const char* fmt, ...) {
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    emit(ctx, buf);
}

static void emit_indent(CompileCtx* ctx) {
    for (int i = 0; i < ctx->indent; i++) {
        emit(ctx, "    ");
    }
}

static char* fresh_temp(CompileCtx* ctx) {
    char* buf = malloc(32);
    snprintf(buf, 32, "_t%d", ctx->temp_counter++);
    return buf;
}

static char* fresh_label(CompileCtx* ctx) {
    char* buf = malloc(32);
    snprintf(buf, 32, "L%d", ctx->label_counter++);
    return buf;
}

// Create/free context
CompileCtx* compile_ctx_new(void) {
    CompileCtx* ctx = malloc(sizeof(CompileCtx));
    if (!ctx) return NULL;
    ctx->output_cap = 4096;
    ctx->output = malloc(ctx->output_cap);
    ctx->output_len = 0;
    ctx->output[0] = '\0';
    ctx->temp_counter = 0;
    ctx->label_counter = 0;
    ctx->functions = mk_nil();
    ctx->indent = 0;
    return ctx;
}

void compile_ctx_free(CompileCtx* ctx) {
    if (ctx) {
        free(ctx->output);
        free(ctx);
    }
}

const char* compile_get_output(CompileCtx* ctx) {
    return ctx->output;
}

// Compile expression, putting result in result_var
static int compile_expr_to_var(CompileCtx* ctx, Value* expr, char* result_var) {
    if (!expr) {
        emit_indent(ctx);
        emit_fmt(ctx, "Obj* %s = NULL;\n", result_var);
        return 0;
    }

    switch (expr->tag) {
        case T_INT:
            emit_indent(ctx);
            emit_fmt(ctx, "Obj* %s = mk_int(%ld);\n", result_var, expr->i);
            return 0;

        case T_NIL:
            emit_indent(ctx);
            emit_fmt(ctx, "Obj* %s = NULL;\n", result_var);
            return 0;

        case T_SYM: {
            if (strcmp(expr->s, "true") == 0) {
                emit_indent(ctx);
                emit_fmt(ctx, "Obj* %s = TRUE_VAL;\n", result_var);
            } else if (strcmp(expr->s, "false") == 0) {
                emit_indent(ctx);
                emit_fmt(ctx, "Obj* %s = FALSE_VAL;\n", result_var);
            } else if (strcmp(expr->s, "nothing") == 0) {
                emit_indent(ctx);
                emit_fmt(ctx, "Obj* %s = NOTHING_VAL;\n", result_var);
            } else {
                // Variable reference
                emit_indent(ctx);
                emit_fmt(ctx, "Obj* %s = %s;\n", result_var, expr->s);
            }
            return 0;
        }

        case T_CODE:
            // String literal
            emit_indent(ctx);
            emit_fmt(ctx, "Obj* %s = mk_string(\"%s\");\n", result_var, expr->s);
            return 0;

        case T_CELL: {
            if (is_nil(expr)) {
                emit_indent(ctx);
                emit_fmt(ctx, "Obj* %s = NULL;\n", result_var);
                return 0;
            }

            Value* op = car(expr);
            Value* args = cdr(expr);

            // Check for special forms
            if (op && op->tag == T_SYM) {
                const char* name = op->s;

                // Special forms
                if (strcmp(name, "if") == 0 ||
                    strcmp(name, "let") == 0 ||
                    strcmp(name, "do") == 0 ||
                    strcmp(name, "begin") == 0 ||
                    strcmp(name, "lambda") == 0 ||
                    strcmp(name, "fn") == 0 ||
                    strcmp(name, "define") == 0 ||
                    strcmp(name, "quote") == 0 ||
                    strcmp(name, "set!") == 0) {
                    return compile_special(ctx, name, args, result_var);
                }

                // Arithmetic primitives - use Omnilisp runtime functions
                if (strcmp(name, "+") == 0 ||
                    strcmp(name, "-") == 0 ||
                    strcmp(name, "*") == 0 ||
                    strcmp(name, "/") == 0 ||
                    strcmp(name, "%") == 0) {

                    // Two-arg case
                    Value* a = car(args);
                    Value* b = car(cdr(args));

                    char* ta = fresh_temp(ctx);
                    char* tb = fresh_temp(ctx);
                    compile_expr_to_var(ctx, a, ta);
                    compile_expr_to_var(ctx, b, tb);

                    const char* prim_fn = "prim_add";
                    if (strcmp(name, "-") == 0) prim_fn = "prim_sub";
                    else if (strcmp(name, "*") == 0) prim_fn = "prim_mul";
                    else if (strcmp(name, "/") == 0) prim_fn = "prim_div";
                    else if (strcmp(name, "%") == 0) prim_fn = "prim_mod";

                    emit_indent(ctx);
                    emit_fmt(ctx, "Obj* %s = %s(%s, %s);\n",
                             result_var, prim_fn, ta, tb);
                    free(ta);
                    free(tb);
                    return 0;
                }

                // Comparison primitives - use Omnilisp runtime functions
                if (strcmp(name, "<") == 0 ||
                    strcmp(name, ">") == 0 ||
                    strcmp(name, "<=") == 0 ||
                    strcmp(name, ">=") == 0 ||
                    strcmp(name, "=") == 0) {

                    Value* a = car(args);
                    Value* b = car(cdr(args));

                    char* ta = fresh_temp(ctx);
                    char* tb = fresh_temp(ctx);
                    compile_expr_to_var(ctx, a, ta);
                    compile_expr_to_var(ctx, b, tb);

                    const char* prim_fn = "prim_lt";
                    if (strcmp(name, ">") == 0) prim_fn = "prim_gt";
                    else if (strcmp(name, "<=") == 0) prim_fn = "prim_le";
                    else if (strcmp(name, ">=") == 0) prim_fn = "prim_ge";
                    else if (strcmp(name, "=") == 0) prim_fn = "prim_eq";

                    emit_indent(ctx);
                    emit_fmt(ctx, "Obj* %s = %s(%s, %s);\n",
                             result_var, prim_fn, ta, tb);
                    free(ta);
                    free(tb);
                    return 0;
                }
            }

            // General function call
            return compile_call(ctx, op, args, result_var);
        }

        default:
            emit_indent(ctx);
            emit_fmt(ctx, "Obj* %s = NULL; // unknown\n", result_var);
            return 0;
    }
}

// Compile special forms
static int compile_special(CompileCtx* ctx, const char* form, Value* args, char* result_var) {
    if (strcmp(form, "if") == 0) {
        Value* test = car(args);
        Value* then_expr = car(cdr(args));
        Value* else_expr = car(cdr(cdr(args)));

        char* test_var = fresh_temp(ctx);
        char* else_label = fresh_label(ctx);
        char* end_label = fresh_label(ctx);

        compile_expr_to_var(ctx, test, test_var);

        emit_indent(ctx);
        emit_fmt(ctx, "Obj* %s;\n", result_var);
        emit_indent(ctx);
        emit_fmt(ctx, "if (!obj_to_bool(%s)) goto %s;\n", test_var, else_label);

        char* then_var = fresh_temp(ctx);
        compile_expr_to_var(ctx, then_expr, then_var);
        emit_indent(ctx);
        emit_fmt(ctx, "%s = %s;\n", result_var, then_var);
        emit_indent(ctx);
        emit_fmt(ctx, "goto %s;\n", end_label);

        emit_fmt(ctx, "%s:\n", else_label);
        if (else_expr) {
            char* else_var = fresh_temp(ctx);
            compile_expr_to_var(ctx, else_expr, else_var);
            emit_indent(ctx);
            emit_fmt(ctx, "%s = %s;\n", result_var, else_var);
            free(else_var);
        } else {
            emit_indent(ctx);
            emit_fmt(ctx, "%s = NOTHING_VAL;\n", result_var);
        }

        emit_fmt(ctx, "%s:;\n", end_label);

        free(test_var);
        free(then_var);
        free(else_label);
        free(end_label);
        return 0;
    }

    if (strcmp(form, "let") == 0) {
        Value* bindings = car(args);
        Value* body = cdr(args);

        // Handle array wrapper
        if (bindings && bindings->tag == T_CELL && car(bindings) &&
            car(bindings)->tag == T_SYM && strcmp(car(bindings)->s, "array") == 0) {
            bindings = cdr(bindings);
        }

        // Declare result var before block
        emit_indent(ctx);
        emit_fmt(ctx, "Obj* %s;\n", result_var);

        emit_indent(ctx);
        emit(ctx, "{\n");
        ctx->indent++;

        // Process bindings - compile value to temp, then assign to name
        while (!is_nil(bindings)) {
            Value* name = car(bindings);
            bindings = cdr(bindings);
            if (is_nil(bindings)) break;
            Value* val = car(bindings);
            bindings = cdr(bindings);

            char* temp_var = fresh_temp(ctx);
            compile_expr_to_var(ctx, val, temp_var);
            emit_indent(ctx);
            emit_fmt(ctx, "Obj* %s = %s;\n", name->s, temp_var);
            free(temp_var);
        }

        // Compile body
        char* body_result = NULL;
        while (!is_nil(body)) {
            if (body_result) free(body_result);
            body_result = fresh_temp(ctx);
            compile_expr_to_var(ctx, car(body), body_result);
            body = cdr(body);
        }

        // Assign final result
        emit_indent(ctx);
        if (body_result) {
            emit_fmt(ctx, "%s = %s;\n", result_var, body_result);
            free(body_result);
        } else {
            emit_fmt(ctx, "%s = NOTHING_VAL;\n", result_var);
        }

        ctx->indent--;
        emit_indent(ctx);
        emit(ctx, "}\n");

        return 0;
    }

    if (strcmp(form, "do") == 0 || strcmp(form, "begin") == 0) {
        char* last_var = NULL;
        while (!is_nil(args)) {
            if (last_var) free(last_var);
            last_var = fresh_temp(ctx);
            compile_expr_to_var(ctx, car(args), last_var);
            args = cdr(args);
        }
        emit_indent(ctx);
        if (last_var) {
            emit_fmt(ctx, "Obj* %s = %s;\n", result_var, last_var);
            free(last_var);
        } else {
            emit_fmt(ctx, "Obj* %s = NOTHING_VAL;\n", result_var);
        }
        return 0;
    }

    if (strcmp(form, "quote") == 0) {
        Value* quoted = car(args);
        // For simple cases, emit literal construction
        if (quoted->tag == T_SYM) {
            emit_indent(ctx);
            emit_fmt(ctx, "Obj* %s = mk_sym(\"%s\");\n", result_var, quoted->s);
        } else if (quoted->tag == T_INT) {
            emit_indent(ctx);
            emit_fmt(ctx, "Obj* %s = mk_int(%ld);\n", result_var, quoted->i);
        } else {
            emit_indent(ctx);
            emit_fmt(ctx, "Obj* %s = NULL; // complex quote\n", result_var);
        }
        return 0;
    }

    if (strcmp(form, "lambda") == 0 || strcmp(form, "fn") == 0) {
        // For now, lambdas are not fully supported in compiled code
        // We'd need to generate function pointers
        emit_indent(ctx);
        emit_fmt(ctx, "Obj* %s = NULL; // lambda (not compiled)\n", result_var);
        return 0;
    }

    if (strcmp(form, "define") == 0) {
        Value* first = car(args);

        if (first && first->tag == T_CELL) {
            // Function definition
            Value* name = car(first);
            emit_indent(ctx);
            emit_fmt(ctx, "// define function %s (deferred)\n", name->s);
            emit_indent(ctx);
            emit_fmt(ctx, "Obj* %s = NULL;\n", result_var);
        } else {
            // Variable definition
            Value* val = car(cdr(args));
            emit_indent(ctx);
            emit_fmt(ctx, "Obj* %s;\n", first->s);
            compile_expr_to_var(ctx, val, first->s);
            emit_indent(ctx);
            emit_fmt(ctx, "Obj* %s = mk_sym(\"%s\");\n", result_var, first->s);
        }
        return 0;
    }

    if (strcmp(form, "set!") == 0) {
        Value* target = car(args);
        Value* val = car(cdr(args));
        compile_expr_to_var(ctx, val, target->s);
        emit_indent(ctx);
        emit_fmt(ctx, "Obj* %s = NOTHING_VAL;\n", result_var);
        return 0;
    }

    emit_indent(ctx);
    emit_fmt(ctx, "Obj* %s = NULL; // unknown special: %s\n", result_var, form);
    return 0;
}

// Compile function call
static int compile_call(CompileCtx* ctx, Value* fn, Value* args, char* result_var) {
    // Compile function
    char* fn_var = fresh_temp(ctx);
    compile_expr_to_var(ctx, fn, fn_var);

    // Compile arguments into an array
    int argc = 0;
    Value* arg = args;
    while (!is_nil(arg)) {
        argc++;
        arg = cdr(arg);
    }

    char** arg_vars = malloc(sizeof(char*) * argc);
    arg = args;
    for (int i = 0; i < argc; i++) {
        arg_vars[i] = fresh_temp(ctx);
        compile_expr_to_var(ctx, car(arg), arg_vars[i]);
        arg = cdr(arg);
    }

    // Build argument list
    emit_indent(ctx);
    emit_fmt(ctx, "Obj* %s_args = NULL;\n", result_var);
    for (int i = argc - 1; i >= 0; i--) {
        emit_indent(ctx);
        emit_fmt(ctx, "%s_args = mk_pair(%s, %s_args);\n",
                 result_var, arg_vars[i], result_var);
        free(arg_vars[i]);
    }
    free(arg_vars);

    // Call function
    emit_indent(ctx);
    emit_fmt(ctx, "Obj* %s = apply_fn(%s, %s_args);\n",
             result_var, fn_var, result_var);

    free(fn_var);
    return 0;
}

// Compile top-level expression
int compile_expr(CompileCtx* ctx, Value* expr) {
    char* result = fresh_temp(ctx);
    int ret = compile_expr_to_var(ctx, expr, result);
    emit_indent(ctx);
    emit_fmt(ctx, "_result = %s;\n", result);
    free(result);
    return ret;
}

int compile_toplevel(CompileCtx* ctx, Value* form) {
    return compile_expr(ctx, form);
}

int compile_program(CompileCtx* ctx, Value* forms) {
    while (!is_nil(forms)) {
        compile_toplevel(ctx, car(forms));
        forms = cdr(forms);
    }
    return 0;
}

// Generate runtime header using Omnilisp runtime library
static void emit_header(CompileCtx* ctx) {
    emit(ctx, "// Generated by Omnilisp Compiler (using Omnilisp Runtime)\n");
    emit(ctx, "#include <omnilisp.h>\n\n");

    // Helper for printing results
    emit(ctx, "static void print_obj(Obj* o) {\n");
    emit(ctx, "    prim_print(o);\n");
    emit(ctx, "}\n\n");
}

// Generate standalone header (for JIT without libomnilisp linkage)
static void emit_header_standalone(CompileCtx* ctx) {
    emit(ctx, "// Generated by Omnilisp Compiler (standalone mode)\n");
    emit(ctx, "#include <stdio.h>\n");
    emit(ctx, "#include <stdlib.h>\n");
    emit(ctx, "#include <string.h>\n");
    emit(ctx, "#include <stdint.h>\n\n");

    emit(ctx, "// Minimal Omnilisp runtime types\n");
    emit(ctx, "typedef struct Obj Obj;\n");
    emit(ctx, "typedef uint16_t Generation;\n");
    emit(ctx, "struct Obj {\n");
    emit(ctx, "    Generation generation;\n");
    emit(ctx, "    int mark;\n");
    emit(ctx, "    int tag;\n");
    emit(ctx, "    int is_pair;\n");
    emit(ctx, "    int scc_id;\n");
    emit(ctx, "    unsigned int scan_tag;\n");
    emit(ctx, "    union {\n");
    emit(ctx, "        long i;\n");
    emit(ctx, "        double f;\n");
    emit(ctx, "        struct { struct Obj *a, *b; };\n");
    emit(ctx, "        void* ptr;\n");
    emit(ctx, "    };\n");
    emit(ctx, "};\n\n");

    emit(ctx, "#define TAG_INT 1\n");
    emit(ctx, "#define TAG_FLOAT 2\n");
    emit(ctx, "#define TAG_CHAR 3\n");
    emit(ctx, "#define TAG_PAIR 4\n");
    emit(ctx, "#define TAG_SYM 5\n\n");

    emit(ctx, "// Tagged pointer support\n");
    emit(ctx, "#define IMM_TAG_MASK 0x7ULL\n");
    emit(ctx, "#define IMM_TAG_PTR 0x0ULL\n");
    emit(ctx, "#define IMM_TAG_INT 0x1ULL\n");
    emit(ctx, "#define GET_IMM_TAG(p) (((uintptr_t)(p)) & IMM_TAG_MASK)\n");
    emit(ctx, "#define IS_IMMEDIATE_INT(p) (GET_IMM_TAG(p) == IMM_TAG_INT)\n");
    emit(ctx, "#define MAKE_INT_IMM(n) ((Obj*)(((uintptr_t)(n) << 3) | IMM_TAG_INT))\n");
    emit(ctx, "#define INT_IMM_VALUE(p) ((long)((intptr_t)(p) >> 3))\n\n");

    emit(ctx, "static Obj* mk_int(long i) { return MAKE_INT_IMM(i); }\n");
    emit(ctx, "static long obj_to_int(Obj* p) {\n");
    emit(ctx, "    if (IS_IMMEDIATE_INT(p)) return INT_IMM_VALUE(p);\n");
    emit(ctx, "    return p ? p->i : 0;\n");
    emit(ctx, "}\n");
    emit(ctx, "static int is_nil(Obj* x) { return x == NULL; }\n\n");

    emit(ctx, "// Truth values\n");
    emit(ctx, "#define TRUE_VAL mk_int(1)\n");
    emit(ctx, "#define FALSE_VAL mk_int(0)\n");
    emit(ctx, "#define NOTHING_VAL ((Obj*)NULL)\n\n");

    emit(ctx, "static Obj* prim_add(Obj* a, Obj* b) { return mk_int(obj_to_int(a) + obj_to_int(b)); }\n");
    emit(ctx, "static Obj* prim_sub(Obj* a, Obj* b) { return mk_int(obj_to_int(a) - obj_to_int(b)); }\n");
    emit(ctx, "static Obj* prim_mul(Obj* a, Obj* b) { return mk_int(obj_to_int(a) * obj_to_int(b)); }\n");
    emit(ctx, "static Obj* prim_div(Obj* a, Obj* b) { long d = obj_to_int(b); return mk_int(d ? obj_to_int(a) / d : 0); }\n");
    emit(ctx, "static Obj* prim_mod(Obj* a, Obj* b) { long d = obj_to_int(b); return mk_int(d ? obj_to_int(a) % d : 0); }\n");
    emit(ctx, "static Obj* prim_lt(Obj* a, Obj* b) { return mk_int(obj_to_int(a) < obj_to_int(b)); }\n");
    emit(ctx, "static Obj* prim_gt(Obj* a, Obj* b) { return mk_int(obj_to_int(a) > obj_to_int(b)); }\n");
    emit(ctx, "static Obj* prim_le(Obj* a, Obj* b) { return mk_int(obj_to_int(a) <= obj_to_int(b)); }\n");
    emit(ctx, "static Obj* prim_ge(Obj* a, Obj* b) { return mk_int(obj_to_int(a) >= obj_to_int(b)); }\n");
    emit(ctx, "static Obj* prim_eq(Obj* a, Obj* b) { return mk_int(obj_to_int(a) == obj_to_int(b)); }\n\n");

    emit(ctx, "static int obj_to_bool(Obj* p) {\n");
    emit(ctx, "    if (p == NULL) return 0;\n");
    emit(ctx, "    if (IS_IMMEDIATE_INT(p)) return INT_IMM_VALUE(p) != 0;\n");
    emit(ctx, "    return 1;\n");
    emit(ctx, "}\n\n");

    emit(ctx, "static void print_obj(Obj* o) {\n");
    emit(ctx, "    if (!o) { printf(\"()\"); return; }\n");
    emit(ctx, "    if (IS_IMMEDIATE_INT(o)) { printf(\"%ld\", INT_IMM_VALUE(o)); return; }\n");
    emit(ctx, "    if (o->tag == TAG_INT) { printf(\"%ld\", o->i); return; }\n");
    emit(ctx, "    printf(\"<obj>\");\n");
    emit(ctx, "}\n\n");
}

// JIT compile and load
JitFn jit_compile(Value* expr, Env* env) {
    (void)env;

    CompileCtx* ctx = compile_ctx_new();
    if (!ctx) return NULL;

    // Generate code with standalone header (no libomnilisp dependency)
    emit_header_standalone(ctx);

    emit(ctx, "Obj* _jit_fn(void) {\n");
    ctx->indent = 1;
    emit_indent(ctx);
    emit(ctx, "Obj* _result = NULL;\n");

    compile_expr(ctx, expr);

    emit_indent(ctx);
    emit(ctx, "return _result;\n");
    ctx->indent = 0;
    emit(ctx, "}\n");

    // Write to temp file
    char tmp_base[] = "/tmp/omni_jit_XXXXXX";
    char tmp_c[256];
    char tmp_so[256];
    int fd = mkstemp(tmp_base);
    if (fd < 0) {
        compile_ctx_free(ctx);
        return NULL;
    }
    close(fd);

    snprintf(tmp_c, sizeof(tmp_c), "%s.c", tmp_base);
    snprintf(tmp_so, sizeof(tmp_so), "%s.so", tmp_base);

    FILE* f = fopen(tmp_c, "w");
    if (!f) {
        unlink(tmp_base);
        compile_ctx_free(ctx);
        return NULL;
    }
    fwrite(ctx->output, 1, ctx->output_len, f);
    fclose(f);
    unlink(tmp_base); // Remove base file

    // Compile with gcc
    char cmd[512];
    snprintf(cmd, sizeof(cmd),
             "gcc -shared -fPIC -O2 -o %s %s 2>/dev/null",
             tmp_so, tmp_c);
    int ret = system(cmd);

    if (ret != 0) {
        fprintf(stderr, "JIT compilation failed. Generated code:\n%s\n", ctx->output);
        compile_ctx_free(ctx);
        unlink(tmp_c);
        return NULL;
    }

    // Load shared library
    void* handle = dlopen(tmp_so, RTLD_NOW);
    if (!handle) {
        fprintf(stderr, "dlopen failed: %s\n", dlerror());
        compile_ctx_free(ctx);
        unlink(tmp_c);
        unlink(tmp_so);
        return NULL;
    }

    // Get function
    JitFn fn = (JitFn)dlsym(handle, "_jit_fn");
    if (!fn) {
        fprintf(stderr, "dlsym failed: %s\n", dlerror());
        dlclose(handle);
        compile_ctx_free(ctx);
        unlink(tmp_c);
        unlink(tmp_so);
        return NULL;
    }

    // Cleanup temp files
    unlink(tmp_c);
    unlink(tmp_so);
    compile_ctx_free(ctx);

    return fn;
}

// AOT compile to file
int aot_compile_to_file(const char* source_file, const char* output_file) {
    // Read source
    FILE* f = fopen(source_file, "r");
    if (!f) return -1;

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    char* content = malloc(size + 1);
    fread(content, 1, size, f);
    content[size] = '\0';
    fclose(f);

    // Parse
    Value* forms = omni_parse_all(content);
    free(content);
    if (is_error(forms)) return -1;

    // Compile with standalone header (self-contained, no external deps)
    CompileCtx* ctx = compile_ctx_new();
    emit_header_standalone(ctx);

    emit(ctx, "int main(void) {\n");
    ctx->indent = 1;
    emit_indent(ctx);
    emit(ctx, "Obj* _result = NULL;\n");

    while (!is_nil(forms)) {
        compile_expr(ctx, car(forms));
        forms = cdr(forms);
    }

    emit_indent(ctx);
    emit(ctx, "print_obj(_result);\n");
    emit_indent(ctx);
    emit(ctx, "printf(\"\\n\");\n");
    emit_indent(ctx);
    emit(ctx, "return 0;\n");
    ctx->indent = 0;
    emit(ctx, "}\n");

    // Write output
    f = fopen(output_file, "w");
    if (!f) {
        compile_ctx_free(ctx);
        return -1;
    }
    fwrite(ctx->output, 1, ctx->output_len, f);
    fclose(f);

    compile_ctx_free(ctx);
    return 0;
}

// AOT compile to executable
int aot_compile_executable(const char* source_file, const char* output_file) {
    char c_file[256];
    snprintf(c_file, sizeof(c_file), "%s.c", output_file);

    if (aot_compile_to_file(source_file, c_file) != 0) {
        return -1;
    }

    char cmd[512];
    snprintf(cmd, sizeof(cmd), "gcc -O2 -o %s %s", output_file, c_file);
    int ret = system(cmd);

    unlink(c_file);
    return ret;
}
