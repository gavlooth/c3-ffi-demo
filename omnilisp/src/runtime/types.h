#ifndef OMNILISP_TYPES_H
#define OMNILISP_TYPES_H

#define _XOPEN_SOURCE 700
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ucontext.h>
#include <pthread.h>

// -- Core Value Types --

typedef enum {
    T_INT, T_SYM, T_CELL, T_NIL, T_NOTHING, T_PRIM, T_MENV, T_CODE, T_LAMBDA,
    T_ERROR,    // Error value
    T_BOX,      // Mutable reference cell
    T_CONT,     // First-class continuation
    T_CHAN,     // CSP channel
    T_PROCESS,  // Fiber / lightweight coroutine
    T_BOUNCE,   // Trampoline thunk (fn + args)
    T_STRING,   // Immutable string (length + data)
    T_CHAR,     // Unicode character (code point)
    T_FLOAT,    // IEEE 754 double precision float
    T_PORT,     // File I/O port
    T_SYNTAX,   // Hygienic syntax transformer (macro)
    T_FFI_LIB,  // Foreign library handle (dlopen)
    T_FFI_PTR,  // Foreign pointer (opaque handle)
    T_THREAD    // System thread (pthread)
} Tag;

struct Value;

// Function pointer types
typedef struct Value* (*PrimFn)(struct Value* args, struct Value* menv);
typedef struct Value* (*HandlerFn)(struct Value* exp, struct Value* menv);

// Forward declarations for new types
struct Channel;
struct Scheduler;
typedef struct Value* (*ContFn)(struct Value* val);

// Process states
#define PROC_READY   0
#define PROC_RUNNING 1
#define PROC_PARKED  2
#define PROC_DONE    3
#define PROC_YIELDED 4

// Fiber context for true coroutines (stack-switching via ucontext)
#define FIBER_STACK_SIZE (256 * 1024)  // 256KB stack per fiber

typedef struct FiberContext {
    ucontext_t ctx;                    // Execution context
    char* stack;                       // Fiber's own stack
    struct Value* yield_value;         // Value passed to/from yield
    int started;                       // Has fiber been started?
} FiberContext;

// Channel structure for CSP (cooperative, continuation-based)
typedef struct Channel {
    struct Value** buffer;      // Circular buffer for buffered channels
    int capacity;               // 0 for unbuffered
    int head;
    int tail;
    int count;
    int closed;
    struct Value* send_waiters; // List of (process . value) waiting to send
    struct Value* recv_waiters; // List of processes waiting to receive
} Channel;

// Continuation escape structure (for setjmp/longjmp)
#include <setjmp.h>
typedef struct ContEscape {
    jmp_buf env;
    struct Value* result;
    int tag;
    int active;
} ContEscape;

// Prompt stack for delimited continuations
#define MAX_PROMPT_DEPTH 64

// Core Value structure
typedef struct Value {
    Tag tag;
    union {
        long i;                          // T_INT
        char* s;                         // T_SYM, T_CODE, T_ERROR
        struct { struct Value* car; struct Value* cdr; } cell;  // T_CELL
        PrimFn prim;                     // T_PRIM
        struct {                         // T_MENV
            struct Value* env;
            struct Value* parent;
            HandlerFn h_app;
            HandlerFn h_let;
            HandlerFn h_if;
            HandlerFn h_lit;
            HandlerFn h_var;
        } menv;
        struct {                         // T_LAMBDA
            struct Value* params;
            struct Value* body;
            struct Value* env;
            struct Value* defaults;      // Default values for optional params (NULL if none)
        } lam;
        struct Value* box_value;         // T_BOX - mutable reference cell
        struct {                         // T_CONT - continuation
            ContFn fn;
            struct Value* menv;
            int tag;                     // Unique tag for matching
        } cont;
        struct {                         // T_CHAN - channel
            struct Channel* ch;
            int capacity;
        } chan;
        struct {                         // T_PROCESS - fiber/coroutine
            struct Value* thunk;         // Lambda to execute
            struct Value* cont;          // Saved continuation for resuming
            struct Value* menv;          // Saved meta-environment
            struct Value* result;        // Final result when done
            struct Value* park_value;    // Value for park/unpark (yield value)
            int state;
            FiberContext* fiber_ctx;     // ucontext for true coroutines
        } proc;
        struct {                         // T_BOUNCE - trampoline thunk
            struct Value* fn;            // Function to call
            struct Value* args;          // Arguments list
        } bounce;
        struct {                         // T_STRING - immutable string
            char* data;                  // UTF-8 encoded data
            size_t len;                  // Length in bytes
        } str;
        long codepoint;                  // T_CHAR - Unicode code point
        double f;                        // T_FLOAT - IEEE 754 double
        struct {                         // T_PORT - file I/O port
            FILE* fp;
            char* filename;
            int mode;                    // 0=read, 1=write, 2=append
            int closed;
        } port;
        struct {                         // T_SYNTAX - hygienic macro
            char* name;                  // Macro name for error messages
            struct Value* literals;      // #literals list (match literally)
            struct Value* rules;         // List of (pattern template) pairs
            struct Value* def_env;       // Definition-time environment (for hygiene)
        } syntax;
        struct {                         // T_FFI_LIB - foreign library
            void* handle;                // dlopen handle
            char* name;                  // Library name for error messages
        } ffi_lib;
        struct {                         // T_FFI_PTR - foreign pointer
            void* ptr;                   // Raw pointer
            char* type_name;             // Type name for debugging (optional)
            int owned;                   // If 1, we should free when done
        } ffi_ptr;
        struct {                         // T_THREAD - system thread
            pthread_t tid;               // pthread ID
            struct Value* thunk;         // Lambda to execute
            struct Value* result;        // Result when done
            int started;                 // Has thread been started?
            int joined;                  // Has thread been joined?
            int done;                    // Is thread complete?
        } thread;
    };
} Value;

// -- Value Constructors --
Value* alloc_val(Tag tag);
Value* mk_int(long i);
Value* mk_nil(void);
Value* mk_nothing(void);
Value* mk_sym(const char* s);
Value* mk_cell(Value* car, Value* cdr);
Value* mk_prim(PrimFn fn);
Value* mk_code(const char* s);
Value* mk_lambda(Value* params, Value* body, Value* env);
Value* mk_lambda_with_defaults(Value* params, Value* body, Value* env, Value* defaults);
Value* mk_error(const char* msg);
Value* mk_box(Value* initial);
Value* mk_cont(ContFn fn, Value* menv, int tag);
Value* mk_chan(int capacity);
Value* mk_process(Value* thunk);
Value* mk_bounce(Value* fn, Value* args);
Value* mk_string(const char* s, size_t len);
Value* mk_string_cstr(const char* s);  // Convenience for C strings
Value* mk_char(long codepoint);
Value* mk_float(double f);
Value* mk_port(FILE* fp, const char* filename, int mode);
Value* mk_syntax(const char* name, Value* literals, Value* rules, Value* def_env);
Value* mk_ffi_lib(void* handle, const char* name);
Value* mk_ffi_ptr(void* ptr, const char* type_name, int owned);
Value* mk_thread(Value* thunk);

// -- Type Predicates --
int is_box(Value* v);
int is_cont(Value* v);
int is_chan(Value* v);
int is_process(Value* v);
int is_error(Value* v);
int is_bounce(Value* v);
int is_string(Value* v);
int is_char(Value* v);
int is_float(Value* v);
int is_port(Value* v);
int is_syntax(Value* v);
int is_ffi_lib(Value* v);
int is_ffi_ptr(Value* v);
int is_thread(Value* v);

// -- Numeric Helpers --
int is_numeric(Value* v);  // true for T_INT or T_FLOAT
double to_double(Value* v); // convert T_INT or T_FLOAT to double

// -- String Operations --
size_t string_len(Value* s);
const char* string_data(Value* s);
Value* string_append(Value* a, Value* b);
Value* string_ref(Value* s, size_t index);
Value* substring(Value* s, size_t start, size_t end);

// -- Box Operations --
Value* box_get(Value* box);
void box_set(Value* box, Value* val);

// -- Value Helpers --
int is_nil(Value* v);
int is_nothing(Value* v);
int is_code(Value* v);
Value* car(Value* v);
Value* cdr(Value* v);
char* val_to_str(Value* v);
char* list_to_str(Value* v);

// Symbol comparison
int sym_eq(Value* s1, Value* s2);
int sym_eq_str(Value* s1, const char* s2);

// -- Compiler Arena (Phase 12) --
// All compiler allocations use this arena for bulk deallocation
void compiler_arena_init(void);
void compiler_arena_cleanup(void);
void compiler_arena_register_string(char* s);

// -- List Construction --
#define LIST1(a) mk_cell(a, NIL)
#define LIST2(a,b) mk_cell(a, mk_cell(b, NIL))
#define LIST3(a,b,c) mk_cell(a, mk_cell(b, mk_cell(c, NIL)))

#endif // OMNILISP_TYPES_H
