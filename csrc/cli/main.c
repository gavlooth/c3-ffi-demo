/*
 * OmniLisp CLI - Command Line Interface
 *
 * Pure C replacement for main.go
 * Provides: compile, run, REPL modes
 *
 * Issue 27 P2: Enhanced REPL features:
 *   - ,time expr - Time expression evaluation
 *   - ,expand expr - Macro expand without evaluation
 *   - ,trace on/off - Toggle execution tracing
 *   - (doc symbol) - Show documentation
 *   - (source symbol) - Show source code
 *   - readline support (optional) for history/completion
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <unistd.h>
#include <getopt.h>
#include <time.h>

/* Optional readline support for history and tab completion */
#ifdef USE_READLINE
#include <readline/readline.h>
#include <readline/history.h>

/* Symbol table for tab completion */
static const char* g_completion_symbols[] = {
    "define", "lambda", "fn", "let", "let*", "if", "cond", "match",
    "quote", "quasiquote", "unquote", "do", "begin",
    "+", "-", "*", "/", "mod", "=", "<", ">", "<=", ">=",
    "cons", "car", "cdr", "list", "append", "reverse", "length",
    "map", "filter", "reduce", "fold", "for-each",
    "null?", "pair?", "list?", "number?", "string?", "symbol?",
    "display", "print", "newline", "read", "write",
    "doc", "source", "inspect", "type-of", "address-of",
    "random", "random-int", "sort", "group-by", "zip",
    "json-parse", "json-stringify", "read-file", "write-file",
    "string-length", "string-split", "string-join", "string-replace",
    "string-trim", "string-upcase", "string-downcase",
    "string-starts-with", "string-ends-with",
    "abs", "min", "max", "sqrt", "pow", "sin", "cos", "tan",
    "atom", "deref", "reset!", "swap!", "channel", "send", "recv",
    NULL
};

static char* symbol_generator(const char* text, int state) {
    static int list_index;
    static size_t len;
    if (!state) { list_index = 0; len = strlen(text); }

    while (g_completion_symbols[list_index]) {
        const char* name = g_completion_symbols[list_index++];
        if (strncmp(name, text, len) == 0)
            return strdup(name);
    }
    return NULL;
}

static char** omni_completion(const char* text, int start, int end) {
    (void)start; (void)end;
    rl_attempted_completion_over = 1;
    return rl_completion_matches(text, symbol_generator);
}
#endif

#include "../compiler/compiler.h"
#include "../parser/parser.h"
#include "../ast/ast.h"

/* ============== REPL State ============== */

static bool g_repl_trace = false;  /* Execution tracing */

/* ============== Options ============== */

typedef struct {
    bool compile_mode;        /* -c: emit C code only */
    bool verbose;             /* -v: verbose output */
    bool shared_mode;         /* --shared: compile as shared library module */
    const char* output_file;  /* -o: output file */
    const char* eval_expr;    /* -e: evaluate expression */
    const char* runtime_path; /* --runtime: runtime path */
    const char* module_name;  /* --module-name: module name for shared library */
    const char* input_file;   /* Input file */
} CliOptions;

static void print_usage(const char* prog) {
    fprintf(stderr, "OmniLisp - Native Compiler with ASAP Memory Management\n\n");
    fprintf(stderr, "Usage: %s [options] [file.omni]\n\n", prog);
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  -c             Compile to C code instead of binary\n");
    fprintf(stderr, "  -o <file>      Output file (default: stdout for -c, a.out for binary)\n");
    fprintf(stderr, "  -e <expr>      Evaluate expression from command line\n");
    fprintf(stderr, "  -v             Verbose output\n");
    fprintf(stderr, "  --runtime <path>     Path to runtime library\n");
    fprintf(stderr, "  --shared             Compile as shared library module (for require/import)\n");
    fprintf(stderr, "  --module-name <name> Module name for shared library (default: from filename)\n");
    fprintf(stderr, "  -h, --help     Show this help\n");
    fprintf(stderr, "  --version      Show version\n");
    fprintf(stderr, "\nExamples:\n");
    fprintf(stderr, "  %s -e '(+ 1 2)'              # Compile and run expression\n", prog);
    fprintf(stderr, "  %s -c -e '(+ 1 2)'           # Emit C code to stdout\n", prog);
    fprintf(stderr, "  %s program.omni              # Compile and run file\n", prog);
    fprintf(stderr, "  %s -c program.omni -o out.c  # Compile file to C\n", prog);
    fprintf(stderr, "  %s -o prog program.omni      # Compile to binary 'prog'\n", prog);
    fprintf(stderr, "  %s --shared -o mymod.so module.omni  # Compile module to .so\n", prog);
}

static void print_version(void) {
    printf("OmniLisp Compiler version %s\n", omni_compiler_version());
    printf("Built with ASAP (As Static As Possible) memory management\n");
    printf("Target: C99 + POSIX\n");
}

/* ============== REPL Utilities ============== */

/* Get current time in nanoseconds */
static uint64_t repl_get_time_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

/* Format nanoseconds for display */
static void repl_format_time(uint64_t ns, char* buf, size_t buf_size) {
    if (ns < 1000) {
        snprintf(buf, buf_size, "%lu ns", (unsigned long)ns);
    } else if (ns < 1000000) {
        snprintf(buf, buf_size, "%.2f µs", ns / 1000.0);
    } else if (ns < 1000000000) {
        snprintf(buf, buf_size, "%.2f ms", ns / 1000000.0);
    } else {
        snprintf(buf, buf_size, "%.3f s", ns / 1000000000.0);
    }
}

/* Read line - uses readline if available, otherwise fgets */
static char* repl_read_line(const char* prompt) {
#ifdef USE_READLINE
    char* line = readline(prompt);
    if (line && *line) {
        add_history(line);
    }
    return line;
#else
    static char line[4096];
    printf("%s", prompt);
    fflush(stdout);
    if (!fgets(line, sizeof(line), stdin)) {
        return NULL;
    }
    /* Remove trailing newline */
    size_t len = strlen(line);
    while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r')) {
        line[--len] = '\0';
    }
    return strdup(line);
#endif
}

/* ============== REPL ============== */

static void run_repl(Compiler* compiler) {
    printf("OmniLisp Native REPL - ASAP Memory Management\n");
    printf("Type 'help' for commands, 'quit' to exit\n\n");

    char** definitions = NULL;
    size_t def_count = 0;
    size_t def_capacity = 0;
    bool show_code = false;

#ifdef USE_READLINE
    /* Initialize readline history and completion */
    using_history();
    rl_attempted_completion_function = omni_completion;
#endif

    while (1) {
        const char* prompt;
        if (g_repl_trace) {
            prompt = show_code ? "omni(tc)> " : "omni(t)> ";
        } else {
            prompt = show_code ? "omni(c)> " : "omni> ";
        }

        char* line = repl_read_line(prompt);
        if (!line) {
            printf("\n");
            break;
        }

        size_t len = strlen(line);

        /* Skip empty lines */
        if (len == 0) {
            free(line);
            continue;
        }

        /* Handle commands */
        if (strcmp(line, "quit") == 0 || strcmp(line, "exit") == 0) {
            printf("Goodbye!\n");
            free(line);
            break;
        }
        if (strcmp(line, "help") == 0) {
            printf("Commands:\n");
            printf("  quit          - exit the REPL\n");
            printf("  code          - toggle C code display\n");
            printf("  defs          - show current definitions\n");
            printf("  clear         - clear all definitions\n");
            printf("  help          - show this help\n");
            printf("\nMeta Commands (prefix with ,):\n");
            printf("  ,time <expr>  - time expression evaluation\n");
            printf("  ,expand <expr>- show desugared form (if->match, and/or->if)\n");
            printf("  ,trace on/off - toggle execution tracing\n");
            printf("  ,env          - show environment info\n");
            printf("\nLanguage:\n");
            printf("  (define name value)     - define a variable\n");
            printf("  (define (f x) body)     - define a function\n");
            printf("  (lambda (x) body)       - anonymous function\n");
            printf("  (let [x val] body)      - local binding\n");
            printf("  (if cond then else)     - conditional\n");
            printf("\nIntrospection:\n");
            printf("  (doc symbol)            - show documentation\n");
            printf("  (source symbol)         - show source code\n");
            printf("  (inspect obj)           - inspect object details\n");
            printf("  (type-of obj)           - get object type\n");
            printf("\nPrimitives:\n");
            printf("  Arithmetic: + - * / %%\n");
            printf("  Comparison: < > <= >= =\n");
            printf("  Lists: cons car cdr null?\n");
            printf("  I/O: display print newline\n");
            free(line);
            continue;
        }
        if (strcmp(line, "code") == 0) {
            show_code = !show_code;
            printf("C code display %s\n", show_code ? "ON" : "OFF");
            free(line);
            continue;
        }
        if (strcmp(line, "clear") == 0) {
            for (size_t i = 0; i < def_count; i++) {
                free(definitions[i]);
            }
            def_count = 0;
            printf("Definitions cleared\n");
            free(line);
            continue;
        }
        if (strcmp(line, "defs") == 0) {
            if (def_count == 0) {
                printf("No definitions\n");
            } else {
                printf("Current definitions:\n");
                for (size_t i = 0; i < def_count; i++) {
                    printf("  %s\n", definitions[i]);
                }
            }
            free(line);
            continue;
        }

        /* ===== Meta Commands (prefix with ,) ===== */

        /* ,trace on/off - toggle execution tracing */
        if (strncmp(line, ",trace", 6) == 0) {
            const char* arg = line + 6;
            while (*arg == ' ') arg++;
            if (strcmp(arg, "on") == 0) {
                g_repl_trace = true;
                printf("Execution tracing ON\n");
            } else if (strcmp(arg, "off") == 0) {
                g_repl_trace = false;
                printf("Execution tracing OFF\n");
            } else {
                printf("Trace is %s (use ,trace on/off)\n", g_repl_trace ? "ON" : "OFF");
            }
            free(line);
            continue;
        }

        /* ,time <expr> - time expression evaluation */
        if (strncmp(line, ",time ", 6) == 0) {
            const char* expr_str = line + 6;
            while (*expr_str == ' ') expr_str++;

            if (!*expr_str) {
                printf("Usage: ,time <expression>\n");
                free(line);
                continue;
            }

// REVIEWED:NAIVE
            /* Build full program with definitions + expression */
            size_t total_len = 0;
            for (size_t i = 0; i < def_count; i++) {
                total_len += strlen(definitions[i]) + 1;
            }
            total_len += strlen(expr_str) + 1;

            char* full_input = malloc(total_len);
            char* p = full_input;
            for (size_t i = 0; i < def_count; i++) {
                size_t dlen = strlen(definitions[i]);
                memcpy(p, definitions[i], dlen);
                p += dlen;
                *p++ = '\n';
            }
            strcpy(p, expr_str);

            /* Time the execution */
            uint64_t start = repl_get_time_ns();
            int result = omni_compiler_run(compiler, full_input);
            uint64_t elapsed = repl_get_time_ns() - start;

            char time_buf[64];
            repl_format_time(elapsed, time_buf, sizeof(time_buf));

            if (omni_compiler_has_errors(compiler)) {
                for (size_t i = 0; i < omni_compiler_error_count(compiler); i++) {
                    fprintf(stderr, "Error: %s\n", omni_compiler_get_error(compiler, i));
                }
            } else {
                printf("Time: %s\n", time_buf);
            }

            free(full_input);
            free(line);
            (void)result;
            continue;
        }

        /* ,expand <expr> - show desugared form */
        if (strncmp(line, ",expand ", 8) == 0) {
            const char* expr_str = line + 8;
            while (*expr_str == ' ') expr_str++;

            if (!*expr_str) {
                printf("Usage: ,expand <expression>\n");
                free(line);
                continue;
            }

            char* desugared = omni_compiler_desugar(expr_str);
            if (desugared) {
                printf("=> %s\n", desugared);
                free(desugared);
            } else {
                printf("Parse error\n");
            }

            free(line);
            continue;
        }

        /* ,env - show environment info */
        if (strcmp(line, ",env") == 0) {
            printf("Environment:\n");
            printf("  Definitions: %zu\n", def_count);
            printf("  Tracing: %s\n", g_repl_trace ? "ON" : "OFF");
            printf("  Show code: %s\n", show_code ? "ON" : "OFF");
            printf("  Compiler version: %s\n", omni_compiler_version());
            free(line);
            continue;
        }

        /* Skip bare words that aren't meta commands */
        if (line[0] != '(' && line[0] != '\'' && line[0] != '[' && line[0] != ',') {
            printf("Unknown command: %s (use 'help' for commands)\n", line);
            free(line);
            continue;
        }

        /* Handle unknown meta commands */
        if (line[0] == ',') {
            printf("Unknown meta command: %s\n", line);
            printf("Available: ,time, ,expand, ,trace, ,env\n");
            free(line);
            continue;
        }

        /* Parse to check if it's a definition */
        OmniValue* expr = omni_parse_string(line);
        if (!expr) {
            printf("Parse error\n");
            free(line);
            continue;
        }

        bool is_define = omni_is_cell(expr) && omni_is_sym(omni_car(expr)) &&
                         strcmp(omni_car(expr)->str_val, "define") == 0;

        if (is_define) {
            /* Store definition */
            if (def_count >= def_capacity) {
                def_capacity = def_capacity ? def_capacity * 2 : 8;
                definitions = realloc(definitions, def_capacity * sizeof(char*));
            }
            definitions[def_count++] = strdup(line);
            printf("Defined\n");
            free(line);
            continue;
        }

        /* Build full program with definitions */
        size_t total_len = 0;
        for (size_t i = 0; i < def_count; i++) {
            total_len += strlen(definitions[i]) + 1;
        }
        total_len += len + 1;

        char* full_input = malloc(total_len);
        char* ptr = full_input;
        for (size_t i = 0; i < def_count; i++) {
            size_t dlen = strlen(definitions[i]);
            memcpy(ptr, definitions[i], dlen);
            ptr += dlen;
            *ptr++ = '\n';
        }
        memcpy(ptr, line, len + 1);

        /* Compile and run (with optional timing if tracing is enabled) */
        uint64_t start_time = 0;
        if (g_repl_trace) {
            start_time = repl_get_time_ns();
        }

        if (show_code) {
            char* code = omni_compiler_compile_to_c(compiler, full_input);
            if (code) {
                printf("--- C code ---\n%s--- end ---\n", code);
                free(code);
            }
        }

        int result = omni_compiler_run(compiler, full_input);
        if (omni_compiler_has_errors(compiler)) {
            for (size_t i = 0; i < omni_compiler_error_count(compiler); i++) {
                fprintf(stderr, "Error: %s\n", omni_compiler_get_error(compiler, i));
            }
        } else if (g_repl_trace) {
            uint64_t elapsed = repl_get_time_ns() - start_time;
            char time_buf[64];
            repl_format_time(elapsed, time_buf, sizeof(time_buf));
            printf("[trace] Execution time: %s\n", time_buf);
        }

        free(full_input);
        free(line);
        (void)result;
    }

    /* Cleanup */
    for (size_t i = 0; i < def_count; i++) {
        free(definitions[i]);
    }
    free(definitions);
}

/* ============== Main ============== */

int main(int argc, char** argv) {
    CliOptions opts = {0};

    static struct option long_options[] = {
        {"help", no_argument, 0, 'h'},
        {"version", no_argument, 0, 'V'},
        {"runtime", required_argument, 0, 'r'},
        {"shared", no_argument, 0, 'S'},
        {"module-name", required_argument, 0, 'M'},
        {0, 0, 0, 0}
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "cho:e:vr:", long_options, NULL)) != -1) {
        switch (opt) {
        case 'c':
            opts.compile_mode = true;
            break;
        case 'o':
            opts.output_file = optarg;
            break;
        case 'e':
            opts.eval_expr = optarg;
            break;
        case 'v':
            opts.verbose = true;
            break;
        case 'r':
            opts.runtime_path = optarg;
            break;
        case 'S':
            opts.shared_mode = true;
            break;
        case 'M':
            opts.module_name = optarg;
            break;
        case 'h':
            print_usage(argv[0]);
            return 0;
        case 'V':
            print_version();
            return 0;
        default:
            print_usage(argv[0]);
            return 1;
        }
    }

    if (optind < argc) {
        opts.input_file = argv[optind];
    }

    /* Auto-detect runtime path */
    if (!opts.runtime_path) {
        /* Check relative to executable */
        char* exe_dir = realpath(argv[0], NULL);
        if (exe_dir) {
            char* slash = strrchr(exe_dir, '/');
            if (slash) *slash = '\0';

            char runtime_check[1024];
            snprintf(runtime_check, sizeof(runtime_check), "%s/../runtime/libomni.a", exe_dir);
            if (access(runtime_check, F_OK) == 0) {
                snprintf(runtime_check, sizeof(runtime_check), "%s/../runtime", exe_dir);
                opts.runtime_path = strdup(runtime_check);
            }
            free(exe_dir);
        }

        /* Check current directory */
        if (!opts.runtime_path && access("runtime/libomni.a", F_OK) == 0) {
            opts.runtime_path = "runtime";
        }
    }

    /* Derive module name from filename if not provided */
    char* derived_module_name = NULL;
    if (opts.shared_mode && !opts.module_name && opts.input_file) {
        /* Extract basename without extension */
        const char* base = strrchr(opts.input_file, '/');
        base = base ? base + 1 : opts.input_file;
        derived_module_name = strdup(base);
        char* dot = strrchr(derived_module_name, '.');
        if (dot) *dot = '\0';
        /* Replace non-identifier chars with underscore */
        for (char* p = derived_module_name; *p; p++) {
            if (!(*p >= 'a' && *p <= 'z') && !(*p >= 'A' && *p <= 'Z') &&
                !(*p >= '0' && *p <= '9') && *p != '_') {
                *p = '_';
            }
        }
        opts.module_name = derived_module_name;
    }

    /* Create compiler */
    CompilerOptions comp_opts = {
        .output_file = opts.output_file,
        .emit_c_only = opts.compile_mode,
        .verbose = opts.verbose,
        .shared_mode = opts.shared_mode,
        .module_name = opts.module_name,
        .runtime_path = opts.runtime_path,
        .use_embedded_runtime = (opts.runtime_path == NULL),
        .opt_level = 2,
        .cc = "gcc",
    };

    Compiler* compiler = omni_compiler_new_with_options(&comp_opts);

    /* Get input */
    char* input = NULL;

    if (opts.eval_expr) {
        input = strdup(opts.eval_expr);
    } else if (opts.input_file) {
        FILE* f = fopen(opts.input_file, "r");
        if (!f) {
            fprintf(stderr, "Error: cannot open file: %s\n", opts.input_file);
            omni_compiler_free(compiler);
            return 1;
        }
        fseek(f, 0, SEEK_END);
        long size = ftell(f);
        fseek(f, 0, SEEK_SET);
        input = malloc(size + 1);
        size_t read = fread(input, 1, size, f);
        input[read] = '\0';
        fclose(f);
    } else {
        /* Check if stdin is a terminal */
        if (isatty(STDIN_FILENO)) {
            /* Interactive REPL mode */
            run_repl(compiler);
            omni_compiler_free(compiler);
            return 0;
        }

        /* Read from stdin */
        size_t capacity = 4096;
        size_t len = 0;
        input = malloc(capacity);
        int c;
        while ((c = getchar()) != EOF) {
            if (len + 1 >= capacity) {
                capacity *= 2;
                input = realloc(input, capacity);
            }
            input[len++] = c;
        }
        input[len] = '\0';
    }

    /* Skip empty input */
    bool empty = true;
    for (const char* p = input; *p; p++) {
        if (*p != ' ' && *p != '\t' && *p != '\n' && *p != '\r') {
            empty = false;
            break;
        }
    }

    if (empty) {
        /* Empty input - go to REPL */
        free(input);
        run_repl(compiler);
        omni_compiler_free(compiler);
        return 0;
    }

    int exit_code = 0;

    if (opts.compile_mode) {
        /* Emit C code */
        char* code = omni_compiler_compile_to_c(compiler, input);
        if (code) {
            if (opts.output_file) {
                FILE* f = fopen(opts.output_file, "w");
                if (f) {
                    fputs(code, f);
                    fclose(f);
                    if (opts.verbose) {
                        fprintf(stderr, "C code written to %s\n", opts.output_file);
                    }
                } else {
                    fprintf(stderr, "Error: cannot write to %s\n", opts.output_file);
                    exit_code = 1;
                }
            } else {
                printf("%s", code);
            }
            free(code);
        } else {
            for (size_t i = 0; i < omni_compiler_error_count(compiler); i++) {
                fprintf(stderr, "Error: %s\n", omni_compiler_get_error(compiler, i));
            }
            exit_code = 1;
        }
    } else if (opts.output_file) {
        /* Compile to binary */
        if (!omni_compiler_compile_to_binary(compiler, input, opts.output_file)) {
            for (size_t i = 0; i < omni_compiler_error_count(compiler); i++) {
                fprintf(stderr, "Error: %s\n", omni_compiler_get_error(compiler, i));
            }
            exit_code = 1;
        } else if (opts.verbose) {
            fprintf(stderr, "Binary written to %s\n", opts.output_file);
        }
    } else {
        /* Compile and run */
        exit_code = omni_compiler_run(compiler, input);
        if (omni_compiler_has_errors(compiler)) {
            for (size_t i = 0; i < omni_compiler_error_count(compiler); i++) {
                fprintf(stderr, "Error: %s\n", omni_compiler_get_error(compiler, i));
            }
            exit_code = 1;
        }
    }

    free(input);
    omni_compiler_free(compiler);
    omni_compiler_cleanup();

    return exit_code;
}
