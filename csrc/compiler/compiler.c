/*
 * OmniLisp Compiler Implementation
 */

#include "compiler.h"
#include "../macro/macro.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/wait.h>
#include <unistd.h>

#define OMNILISP_VERSION "0.1.0"

/* ============== Initialization ============== */

static bool g_initialized = false;

void omni_compiler_init(void) {
    if (g_initialized) return;
    omni_ast_arena_init();
    omni_grammar_init();
    g_initialized = true;
}

void omni_compiler_cleanup(void) {
    if (!g_initialized) return;
    omni_grammar_cleanup();
    omni_ast_arena_cleanup();
    g_initialized = false;
}

const char* omni_compiler_version(void) {
    return OMNILISP_VERSION;
}

/* ============== Compiler Management ============== */

static CompilerOptions default_options(void) {
    CompilerOptions opts = {
        .output_file = NULL,
        .emit_c_only = false,
        .verbose = false,
        .runtime_path = NULL,
        .use_embedded_runtime = true,
        .opt_level = 1,
        .enable_reuse = false,
        .enable_dps = false,
        .emit_debug_info = false,
        .enable_asan = false,
        .enable_tsan = false,
        .cc = "gcc",
        .cflags = NULL,
    };
    return opts;
}

Compiler* omni_compiler_new(void) {
    CompilerOptions opts = default_options();
    return omni_compiler_new_with_options(&opts);
}

Compiler* omni_compiler_new_with_options(const CompilerOptions* options) {
    omni_compiler_init();

    Compiler* c = malloc(sizeof(Compiler));
    if (!c) return NULL;
    memset(c, 0, sizeof(Compiler));

    if (options) {
        c->options = *options;
    } else {
        c->options = default_options();
    }

    return c;
}

void omni_compiler_free(Compiler* compiler) {
    if (!compiler) return;

    if (compiler->analysis) {
        omni_analysis_free(compiler->analysis);
    }
    if (compiler->codegen) {
        omni_codegen_free(compiler->codegen);
    }

    for (size_t i = 0; i < compiler->error_count; i++) {
        free(compiler->errors[i]);
    }
    free(compiler->errors);

    free(compiler);
}

void omni_compiler_set_runtime(Compiler* compiler, const char* path) {
    if (compiler) {
        compiler->options.runtime_path = path;
        compiler->options.use_embedded_runtime = (path == NULL);
    }
}

/* ============== Error Handling ============== */

static void add_error(Compiler* c, const char* fmt, ...) {
    if (c->error_count >= c->error_capacity) {
        c->error_capacity = c->error_capacity ? c->error_capacity * 2 : 8;
        c->errors = realloc(c->errors, c->error_capacity * sizeof(char*));
    }

    char buf[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    c->errors[c->error_count++] = strdup(buf);
}

bool omni_compiler_has_errors(Compiler* compiler) {
    return compiler && compiler->error_count > 0;
}

size_t omni_compiler_error_count(Compiler* compiler) {
    return compiler ? compiler->error_count : 0;
}

const char* omni_compiler_get_error(Compiler* compiler, size_t index) {
    if (!compiler || index >= compiler->error_count) return NULL;
    return compiler->errors[index];
}

void omni_compiler_clear_errors(Compiler* compiler) {
    if (!compiler) return;
    for (size_t i = 0; i < compiler->error_count; i++) {
        free(compiler->errors[i]);
    }
    compiler->error_count = 0;
}

/* ============== Compilation ============== */

char* omni_compiler_compile_to_c(Compiler* compiler, const char* source) {
    if (!compiler || !source) return NULL;

    omni_compiler_clear_errors(compiler);

    /* Parse */
    OmniParser* parser = omni_parser_new(source);
    size_t expr_count;
    OmniValue** exprs = omni_parser_parse_all(parser, &expr_count);

    if (omni_parser_get_errors(parser)) {
        OmniParseError* err = omni_parser_get_errors(parser);
        while (err) {
            add_error(compiler, "Parse error at line %d, col %d: %s",
                      err->line, err->column, err->message);
            err = err->next;
        }
        omni_parser_free(parser);
        return NULL;
    }
    omni_parser_free(parser);

    if (expr_count == 0) {
        add_error(compiler, "No expressions to compile");
        return NULL;
    }

    /* Macro Expansion */
    MacroExpander* macro_exp = omni_macro_expander_new();
    exprs = omni_macro_expand_program(macro_exp, exprs, &expr_count);

    if (omni_macro_expander_has_errors(macro_exp)) {
        for (size_t i = 0; i < omni_macro_expander_error_count(macro_exp); i++) {
            add_error(compiler, "Macro error: %s",
                      omni_macro_expander_get_error(macro_exp, i));
        }
        omni_macro_expander_free(macro_exp);
        free(exprs);
        return NULL;
    }
    omni_macro_expander_free(macro_exp);

    /* Check if all expressions were macros (no code left) */
    if (expr_count == 0) {
        add_error(compiler, "No expressions to compile after macro expansion");
        free(exprs);
        return NULL;
    }

    /* Generate code */
    CodeGenContext* codegen = omni_codegen_new_buffer();
    if (compiler->options.runtime_path) {
        omni_codegen_set_runtime(codegen, compiler->options.runtime_path);
    }

    /* Set shared mode for module compilation */
    if (compiler->options.shared_mode) {
        omni_codegen_set_shared_mode(codegen, compiler->options.module_name);
    }

    omni_codegen_program(codegen, exprs, expr_count);

    char* output = omni_codegen_get_output(codegen);
    omni_codegen_free(codegen);

    free(exprs);

    return output;
}

static char* create_temp_file(const char* suffix) {
    char* path = malloc(256);
    snprintf(path, 256, "/tmp/omnilisp_XXXXXX%s", suffix);
    int fd = mkstemps(path, strlen(suffix));
    if (fd < 0) {
        free(path);
        return NULL;
    }
    close(fd);
    return path;
}

bool omni_compiler_compile_to_binary(Compiler* compiler, const char* source, const char* output) {
    if (!compiler || !source || !output) return false;

    /* Generate C code */
    char* c_code = omni_compiler_compile_to_c(compiler, source);
    if (!c_code) return false;

    /* Write to temp file */
    char* c_file = create_temp_file(".c");
    if (!c_file) {
        add_error(compiler, "Failed to create temp file: %s", strerror(errno));
        free(c_code);
        return false;
    }

    FILE* f = fopen(c_file, "w");
    if (!f) {
        add_error(compiler, "Failed to write temp file: %s", strerror(errno));
        free(c_file);
        free(c_code);
        return false;
    }
    fputs(c_code, f);
    fclose(f);
    free(c_code);

    /* Build gcc command */
    char cmd[2048];
    const char* cc = compiler->options.cc ? compiler->options.cc : "gcc";

    /* Flags for shared library vs executable */
    const char* shared_flags = compiler->options.shared_mode ? "-shared -fPIC " : "";

    if (compiler->options.runtime_path) {
        /* Use shared library (libomni.so) and set rpath for runtime lookup.
         * This ensures main program and modules share the same runtime state. */
        snprintf(cmd, sizeof(cmd),
                 "%s -std=c99 -pthread -lm -ldl -O%d %s%s%s%s-I%s/include -o %s %s -L%s -Wl,-rpath,%s -lomni",
                 cc,
                 compiler->options.opt_level,
                 shared_flags,
                 compiler->options.emit_debug_info ? "-g " : "",
                 compiler->options.enable_asan ? "-fsanitize=address " : "",
                 compiler->options.enable_tsan ? "-fsanitize=thread " : "",
                 compiler->options.runtime_path,
                 output,
                 c_file,
                 compiler->options.runtime_path,
                 compiler->options.runtime_path);
    } else {
        snprintf(cmd, sizeof(cmd),
                 "%s -std=c99 -pthread -lm -ldl -O%d %s%s%s%s-o %s %s",
                 cc,
                 compiler->options.opt_level,
                 shared_flags,
                 compiler->options.emit_debug_info ? "-g " : "",
                 compiler->options.enable_asan ? "-fsanitize=address " : "",
                 compiler->options.enable_tsan ? "-fsanitize=thread " : "",
                 output,
                 c_file);
    }

    if (compiler->options.verbose) {
        fprintf(stderr, "Compiling: %s\n", cmd);
    }

    int status = system(cmd);
    unlink(c_file);
    free(c_file);

    if (status != 0) {
        add_error(compiler, "C compilation failed with status %d", status);
        return false;
    }

    return true;
}

char* omni_compiler_compile_file_to_c(Compiler* compiler, const char* filename) {
    if (!compiler || !filename) return NULL;

    FILE* f = fopen(filename, "r");
    if (!f) {
        add_error(compiler, "Cannot open file: %s", filename);
        return NULL;
    }

    /* Read file contents */
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    char* source = malloc(size + 1);
    size_t read = fread(source, 1, size, f);
    source[read] = '\0';
    fclose(f);

    char* result = omni_compiler_compile_to_c(compiler, source);
    free(source);
    return result;
}

bool omni_compiler_compile_file_to_binary(Compiler* compiler, const char* filename, const char* output) {
    if (!compiler || !filename || !output) return false;

    FILE* f = fopen(filename, "r");
    if (!f) {
        add_error(compiler, "Cannot open file: %s", filename);
        return false;
    }

    /* Read file contents */
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    char* source = malloc(size + 1);
    size_t read = fread(source, 1, size, f);
    source[read] = '\0';
    fclose(f);

    bool result = omni_compiler_compile_to_binary(compiler, source, output);
    free(source);
    return result;
}

int omni_compiler_run(Compiler* compiler, const char* source) {
    if (!compiler || !source) return -1;

    /* Compile to temp binary */
    char* bin_file = create_temp_file("");
    if (!bin_file) {
        add_error(compiler, "Failed to create temp file");
        return -1;
    }

    if (!omni_compiler_compile_to_binary(compiler, source, bin_file)) {
        unlink(bin_file);
        free(bin_file);
        return -1;
    }

    /* Execute */
    pid_t pid = fork();
    if (pid == 0) {
        /* Child process */
        execl(bin_file, bin_file, NULL);
        _exit(127);  /* exec failed */
    } else if (pid < 0) {
        add_error(compiler, "Failed to fork: %s", strerror(errno));
        unlink(bin_file);
        free(bin_file);
        return -1;
    }

    /* Parent process - wait for child */
    int status;
    waitpid(pid, &status, 0);

    unlink(bin_file);
    free(bin_file);

    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    return -1;
}

/* ============== Desugaring for REPL ,expand ============== */

/* Forward declarations for desugaring */
static OmniValue* desugar_expr(OmniValue* expr);
static OmniValue* desugar_list(OmniValue* list);

/* Helper: create a list from variadic OmniValue* args */
static OmniValue* make_list(int count, ...) {
    va_list args;
    va_start(args, count);

    OmniValue* result = NULL;
    OmniValue* tail = NULL;

    for (int i = 0; i < count; i++) {
        OmniValue* val = va_arg(args, OmniValue*);
        OmniValue* cell = omni_new_cell(val, NULL);
        if (!result) {
            result = cell;
            tail = cell;
        } else {
            tail->cell.cdr = cell;
            tail = cell;
        }
    }

    va_end(args);
    return result ? result : omni_nil;
}

/* Get second element: (cadr x) */
static OmniValue* cadr(OmniValue* x) {
    return omni_car(omni_cdr(x));
}

/* Get third element: (caddr x) */
static OmniValue* caddr(OmniValue* x) {
    return omni_car(omni_cdr(omni_cdr(x)));
}

/* Get fourth element: (cadddr x) */
static OmniValue* cadddr(OmniValue* x) {
    return omni_car(omni_cdr(omni_cdr(omni_cdr(x))));
}

/* Desugar (if cond then else) => (match cond true then false else) */
static OmniValue* desugar_if(OmniValue* expr) {
    OmniValue* cond_expr = desugar_expr(cadr(expr));
    OmniValue* then_expr = desugar_expr(caddr(expr));
    OmniValue* else_expr = desugar_expr(cadddr(expr));

    return make_list(6,
        omni_new_sym("match"),
        cond_expr,
        omni_new_sym("true"), then_expr,
        omni_new_sym("false"), else_expr);
}

/* Desugar (and) => true, (and a) => a, (and a b ...) => (if a (and b ...) false) */
static OmniValue* desugar_and(OmniValue* expr) {
    OmniValue* args = omni_cdr(expr);

    /* (and) => true */
    if (omni_is_nil(args)) {
        return omni_new_sym("true");
    }

    /* (and a) => a */
    if (omni_is_nil(omni_cdr(args))) {
        return desugar_expr(omni_car(args));
    }

    /* (and a b ...) => (if a (and b ...) false) */
    OmniValue* rest_and = omni_new_cell(omni_new_sym("and"), omni_cdr(args));

    return make_list(4,
        omni_new_sym("if"),
        desugar_expr(omni_car(args)),
        desugar_and(rest_and),
        omni_new_sym("false"));
}

/* Desugar (or) => false, (or a) => a, (or a b ...) => (let [_t a] (if _t _t (or b ...))) */
static OmniValue* desugar_or(OmniValue* expr) {
    OmniValue* args = omni_cdr(expr);

    /* (or) => false */
    if (omni_is_nil(args)) {
        return omni_new_sym("false");
    }

    /* (or a) => a */
    if (omni_is_nil(omni_cdr(args))) {
        return desugar_expr(omni_car(args));
    }

    /* (or a b ...) => (let [_or_tmp a] (if _or_tmp _or_tmp (or b ...))) */
    OmniValue* tmp = omni_new_sym("_or_tmp");
    OmniValue* rest_or = omni_new_cell(omni_new_sym("or"), omni_cdr(args));

    /* Build binding vector [_or_tmp <desugared-a>] */
    OmniValue* bindings = omni_new_array(2);
    omni_array_push(bindings, tmp);
    omni_array_push(bindings, desugar_expr(omni_car(args)));

    return make_list(3,
        omni_new_sym("let"),
        bindings,
        make_list(4, omni_new_sym("if"), tmp, tmp, desugar_or(rest_or)));
}

/* Desugar (cond (test1 expr1) (test2 expr2) ... (else exprN))
 * => (if test1 expr1 (if test2 expr2 ... exprN)) */
static OmniValue* desugar_cond(OmniValue* expr) {
    OmniValue* clauses = omni_cdr(expr);

    if (omni_is_nil(clauses)) {
        return omni_new_sym("nil");
    }

    OmniValue* clause = omni_car(clauses);
    OmniValue* test = omni_car(clause);
    OmniValue* body = cadr(clause);

    /* Check for else clause */
    if (omni_is_sym(test) && omni_sym_eq_str(test, "else")) {
        return desugar_expr(body);
    }

    /* Build (if test body (cond rest...)) */
    OmniValue* rest_cond = omni_new_cell(omni_new_sym("cond"), omni_cdr(clauses));

    return make_list(4,
        omni_new_sym("if"),
        desugar_expr(test),
        desugar_expr(body),
        desugar_cond(rest_cond));
}

/* Recursively desugar an expression */
static OmniValue* desugar_expr(OmniValue* expr) {
    if (!expr || omni_is_nil(expr)) return expr;
    if (!omni_is_cell(expr)) return expr;

    OmniValue* head = omni_car(expr);
    if (!omni_is_sym(head)) {
        return desugar_list(expr);
    }

    const char* name = head->str_val;

    if (strcmp(name, "if") == 0)   return desugar_if(expr);
    if (strcmp(name, "and") == 0)  return desugar_and(expr);
    if (strcmp(name, "or") == 0)   return desugar_or(expr);
    if (strcmp(name, "cond") == 0) return desugar_cond(expr);

    /* For other forms, recursively desugar subexpressions */
    return desugar_list(expr);
}

/* Desugar each element in a list */
static OmniValue* desugar_list(OmniValue* list) {
    if (!list || omni_is_nil(list)) return list;

    return omni_new_cell(
        desugar_expr(omni_car(list)),
        desugar_list(omni_cdr(list)));
}

/* Public API: Desugar source code for REPL inspection */
char* omni_compiler_desugar(const char* source) {
    if (!source) return NULL;

    omni_compiler_init();

    OmniValue* parsed = omni_parse_string(source);
    if (!parsed) return NULL;

    OmniValue* desugared = desugar_expr(parsed);
    return omni_value_to_string(desugared);
}
