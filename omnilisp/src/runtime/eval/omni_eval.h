#ifndef OMNI_EVAL_H
#define OMNI_EVAL_H

#include "../types.h"

// Environment structure
typedef struct Env {
    Value* bindings;        // Association list ((name . value) ...)
    struct Env* parent;
} Env;

// Create/extend environments
Env* env_new(Env* parent);
void env_free(Env* env);
Env* env_extend(Env* env, Value* names, Value* values);
Value* env_lookup(Env* env, Value* name);
void env_define(Env* env, Value* name, Value* value);
void env_set(Env* env, Value* name, Value* value);

// Core evaluation
Value* omni_eval(Value* expr, Env* env);
Value* omni_apply(Value* fn, Value* args, Env* env);

// Initialize runtime with builtins
Env* omni_env_init(void);

// REPL evaluation
Value* omni_eval_string(const char* input);
Value* omni_eval_file(const char* filename);

// Special forms registry
typedef Value* (*SpecialFormFn)(Value* args, Env* env);
void register_special_form(const char* name, SpecialFormFn fn);

// Primitive function registry
typedef Value* (*PrimitiveFn)(Value* args);
void register_primitive(const char* name, int arity, PrimitiveFn fn);

#endif // OMNI_EVAL_H
