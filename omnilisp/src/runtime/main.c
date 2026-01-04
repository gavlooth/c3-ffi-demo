#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "include/omnilisp.h"
#include "reader/omni_reader.h"
#include "eval/omni_eval.h"
#include "compiler/omni_compile.h"

static void print_usage(const char* prog) {
    printf("Omnilisp - A Self-Hosting Lisp\n\n");
    printf("Usage:\n");
    printf("  %s                      Start REPL\n", prog);
    printf("  %s <expr>               Evaluate expression\n", prog);
    printf("  %s <file.lisp>          Run file\n", prog);
    printf("  %s --jit <expr>         JIT compile and run expression\n", prog);
    printf("  %s --compile <src> <out>  AOT compile to C\n", prog);
    printf("  %s --build <src> <out>    AOT compile to executable\n", prog);
    printf("  %s --help               Show this help\n", prog);
}

int main(int argc, char** argv) {
    // Initialize runtime
    Env* env = omni_env_init();

    if (argc > 1) {
        // Help
        if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0) {
            print_usage(argv[0]);
            return 0;
        }

        // JIT compilation
        if (strcmp(argv[1], "--jit") == 0) {
            if (argc < 3) {
                fprintf(stderr, "Error: --jit requires an expression\n");
                return 1;
            }
            Value* expr = omni_parse(argv[2]);
            if (is_error(expr)) {
                printf("Parse error: %s\n", expr->s);
                return 1;
            }

            JitFn fn = jit_compile(expr, env);
            if (!fn) {
                fprintf(stderr, "JIT compilation failed\n");
                return 1;
            }

            // JIT returns an Obj* using tagged pointers
            // Immediate integers have tag 0x1 in low 3 bits
            #define IMM_TAG_MASK 0x7ULL
            #define IMM_TAG_INT 0x1ULL
            #define GET_IMM_TAG(p) (((uintptr_t)(p)) & IMM_TAG_MASK)
            #define IS_IMMEDIATE_INT(p) (GET_IMM_TAG(p) == IMM_TAG_INT)
            #define INT_IMM_VALUE(p) ((long)((intptr_t)(p) >> 3))

            typedef struct JitObj {
                uint16_t generation;
                int mark;
                int tag;
                int is_pair;
                int scc_id;
                unsigned int scan_tag;
                union {
                    long i;
                    double f;
                    struct { struct JitObj *a, *b; };
                    void* ptr;
                };
            } JitObj;

            JitObj* result = (JitObj*)fn();
            if (!result) {
                printf("()\n");
            } else if (IS_IMMEDIATE_INT(result)) {
                printf("%ld\n", INT_IMM_VALUE(result));
            } else if (result->tag == 1) { // TAG_INT (boxed)
                printf("%ld\n", result->i);
            } else if (result->tag == 5) { // TAG_SYM
                printf("%s\n", (char*)result->ptr);
            } else {
                printf("(compiled result)\n");
            }
            return 0;
        }

        // AOT compile to C
        if (strcmp(argv[1], "--compile") == 0) {
            if (argc < 4) {
                fprintf(stderr, "Error: --compile requires source and output files\n");
                return 1;
            }
            int ret = aot_compile_to_file(argv[2], argv[3]);
            if (ret != 0) {
                fprintf(stderr, "Compilation failed\n");
                return 1;
            }
            printf("Compiled to %s\n", argv[3]);
            return 0;
        }

        // AOT compile to executable
        if (strcmp(argv[1], "--build") == 0) {
            if (argc < 4) {
                fprintf(stderr, "Error: --build requires source and output files\n");
                return 1;
            }
            int ret = aot_compile_executable(argv[2], argv[3]);
            if (ret != 0) {
                fprintf(stderr, "Build failed\n");
                return 1;
            }
            printf("Built executable: %s\n", argv[3]);
            return 0;
        }

        // Check if it's a file or expression
        if (argv[1][0] == '(' || argv[1][0] == '[' || argv[1][0] == '{' ||
            argv[1][0] == '\'' || argv[1][0] == '"') {
            // Parse and evaluate expression
            Value* expr = omni_parse(argv[1]);
            if (is_error(expr)) {
                printf("Parse error: %s\n", expr->s);
                return 1;
            }
            Value* result = omni_eval(expr, env);
            if (is_error(result)) {
                printf("Error: %s\n", result->s);
                return 1;
            }
            char* s = val_to_str(result);
            printf("%s\n", s);
            free(s);
        } else {
            // Try to evaluate as file
            Value* result = omni_eval_file(argv[1]);
            if (is_error(result)) {
                printf("Error: %s\n", result->s);
                return 1;
            }
            char* s = val_to_str(result);
            printf("%s\n", s);
            free(s);
        }
        return 0;
    }

    // REPL mode
    char buffer[4096];
    printf("Omnilisp REPL (type 'quit' to exit)\n");
    printf("> ");
    fflush(stdout);

    while (fgets(buffer, sizeof(buffer), stdin)) {
        // Remove newline
        size_t len = strlen(buffer);
        if (len > 0 && buffer[len-1] == '\n') buffer[len-1] = '\0';

        // Skip empty lines
        if (strlen(buffer) == 0) {
            printf("> ");
            fflush(stdout);
            continue;
        }

        // Exit commands
        if (strcmp(buffer, "quit") == 0 || strcmp(buffer, "exit") == 0) {
            break;
        }

        // Parse
        Value* expr = omni_parse(buffer);
        if (is_error(expr)) {
            printf("Parse error: %s\n", expr->s);
            printf("> ");
            fflush(stdout);
            continue;
        }

        // Evaluate
        Value* result = omni_eval(expr, env);
        if (is_error(result)) {
            printf("Error: %s\n", result->s);
        } else {
            char* s = val_to_str(result);
            printf("=> %s\n", s);
            free(s);
        }
        printf("> ");
        fflush(stdout);
    }

    printf("\nGoodbye!\n");
    return 0;
}
