module lisp;

import std::core::mem;
import std::io;
import main;

// ============================================================
// Fiber Scheduler — coroutines + effect handler
//
// A fiber is a coroutine managed by the scheduler.
// spawn creates a fiber. The scheduler resumes fibers round-robin.
// Fibers run until they yield, complete, or signal an I/O effect.
// ============================================================

const usz MAX_FIBERS = 256;

struct FiberEntry {
    Value* coroutine;    // COROUTINE value
    Value* result;       // Final result (set on completion)
    bool   completed;
    bool   active;
}

struct Scheduler {
    FiberEntry[MAX_FIBERS] fibers;
    usz fiber_count;
    bool running;
}

// Global scheduler (single-threaded)
Scheduler g_scheduler;
bool g_scheduler_initialized = false;

fn void scheduler_init() {
    if (g_scheduler_initialized) return;
    g_scheduler.fiber_count = 0;
    g_scheduler.running = false;
    g_scheduler_initialized = true;
}

fn usz scheduler_add_fiber(Value* coroutine) {
    scheduler_init();
    if (g_scheduler.fiber_count >= MAX_FIBERS) return MAX_FIBERS;  // full
    usz id = g_scheduler.fiber_count;
    g_scheduler.fibers[id].coroutine = coroutine;
    g_scheduler.fibers[id].result = null;
    g_scheduler.fibers[id].completed = false;
    g_scheduler.fibers[id].active = true;
    g_scheduler.fiber_count++;
    return id;
}

// ============================================================
// (spawn thunk) → fiber-id (integer)
//
// Creates a coroutine from thunk, adds to scheduler.
// ============================================================

fn Value* prim_spawn(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1) return raise_error(interp, "spawn: expected (spawn thunk)");

    Value* thunk = args[0];
    if (thunk == null || thunk.tag != CLOSURE) {
        return raise_error(interp, "spawn: argument must be a closure");
    }

    // Create coroutine from thunk
    Value*[1] co_args;
    co_args[0] = thunk;
    Value* co = prim_coroutine(co_args[..], env, interp);
    if (co == null || co.tag == ERROR) return co;

    usz id = scheduler_add_fiber(co);
    if (id >= MAX_FIBERS) return raise_error(interp, "spawn: scheduler full");

    return make_int(interp, (long)id);
}

// ============================================================
// (await fiber-id) → result value
//
// Runs the scheduler until the specified fiber completes.
// Returns the fiber's final result.
// ============================================================

fn Value* prim_await(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1 || !is_int(args[0])) {
        return raise_error(interp, "await: expected (await fiber-id)");
    }

    usz target = (usz)args[0].int_val;
    if (target >= g_scheduler.fiber_count) {
        return raise_error(interp, "await: invalid fiber id");
    }

    // Already completed?
    if (g_scheduler.fibers[target].completed) {
        Value* r = g_scheduler.fibers[target].result;
        return r != null ? r : make_nil(interp);
    }

    // Run scheduler until target completes
    scheduler_run_until(target, interp);

    Value* r = g_scheduler.fibers[target].result;
    return r != null ? r : make_nil(interp);
}

// ============================================================
// (run-fibers) → nil
//
// Runs all spawned fibers to completion. Round-robin scheduling.
// ============================================================

fn Value* prim_run_fibers(Value*[] args, Env* env, Interp* interp) {
    scheduler_run_all(interp);
    return make_nil(interp);
}

// ============================================================
// Scheduler core — round-robin resume loop
// ============================================================

fn void scheduler_run_until(usz target, Interp* interp) {
    g_scheduler.running = true;
    defer g_scheduler.running = false;

    usz max_rounds = 100000;  // safety limit
    usz round = 0;

    while (!g_scheduler.fibers[target].completed && round < max_rounds) {
        bool any_active = false;

        for (usz i = 0; i < g_scheduler.fiber_count; i++) {
            FiberEntry* f = &g_scheduler.fibers[i];
            if (!f.active || f.completed) continue;

            any_active = true;

            // Resume the coroutine
            Value*[1] resume_args;
            resume_args[0] = f.coroutine;
            Value* result = prim_resume(resume_args[..], null, interp);

            // Check if coroutine completed or errored
            if (result != null && result.tag == ERROR) {
                f.completed = true;
                f.result = result;
                continue;
            }

            // Check coroutine status via StackCtx
            if (f.coroutine.coroutine_val != null) {
                StackCtx* ctx = f.coroutine.coroutine_val;
                if (ctx.status == main::StackCtxStatus.CTX_COMPLETED || ctx.status == main::StackCtxStatus.CTX_DEAD) {
                    f.completed = true;
                    f.result = result;
                }
            }
        }

        if (!any_active) break;
        round++;
    }
}

fn void scheduler_run_all(Interp* interp) {
    g_scheduler.running = true;
    defer g_scheduler.running = false;

    usz max_rounds = 100000;
    usz round = 0;

    while (round < max_rounds) {
        bool any_active = false;

        for (usz i = 0; i < g_scheduler.fiber_count; i++) {
            FiberEntry* f = &g_scheduler.fibers[i];
            if (!f.active || f.completed) continue;

            any_active = true;

            Value*[1] resume_args;
            resume_args[0] = f.coroutine;
            Value* result = prim_resume(resume_args[..], null, interp);

            if (result != null && result.tag == ERROR) {
                f.completed = true;
                f.result = result;
                continue;
            }

            if (f.coroutine.coroutine_val != null) {
                StackCtx* ctx = f.coroutine.coroutine_val;
                if (ctx.status == main::StackCtxStatus.CTX_COMPLETED || ctx.status == main::StackCtxStatus.CTX_DEAD) {
                    f.completed = true;
                    f.result = result;
                }
            }
        }

        if (!any_active) break;
        round++;
    }

    // Reset scheduler for next batch
    g_scheduler.fiber_count = 0;
}
