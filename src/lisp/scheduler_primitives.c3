module lisp;

import std::core::mem;
import std::atomic::types;
import std::io;
import std::thread;
import main;

// ============================================================
// Scheduler public primitives and run loops
// ============================================================
fn Value* scheduler_run_offload_sync(OffloadWork work, Interp* interp) {
    OffloadCompletion* completion = scheduler_execute_offload(work);
    if (completion == null) return raise_error(interp, "offload: worker completion alloc failed");
    Value* out = scheduler_value_from_offload_completion(completion, interp);
    scheduler_free_offload_completion(completion);
    return out;
}

fn Value* scheduler_begin_fiber_offload(OffloadWork* work, usz fiber_id, Interp* interp) {
    PendingOffload* pending = &g_scheduler.pending_offloads[fiber_id];
    if (pending.active) {
        shared_blob_release(work.blob);
        return raise_error(interp, "offload: pending work already exists for this fiber");
    }

    work.fiber_id = fiber_id;
    work.task_id = NO_TASK;
    if (!scheduler_enqueue_offload(*work)) {
        shared_blob_release(work.blob);
        return raise_error(interp, "offload: queue is full");
    }

    pending.active = true;
    pending.completed = false;
    pending.completion = null;
    g_scheduler.fibers[fiber_id].state = FIBER_BLOCKED;
    return null;
}

fn Value* prim_offload(Value*[] args, Env* env, Interp* interp) {
    (void)env;
    scheduler_init();

    if (args.len < 1) return raise_error(interp, "offload: expected job list");
    OffloadWork work = {};
    Value* parse_err = scheduler_build_offload_work(args[0], &work, interp);
    if (parse_err != null) return parse_err;

    if (!scheduler_is_running_fiber_context()) {
        return scheduler_run_offload_sync(work, interp);
    }

    usz fiber_id = g_scheduler.current_fiber_id;
    Value* begin_err = scheduler_begin_fiber_offload(&work, fiber_id, interp);
    if (begin_err != null) return begin_err;

    Value*[1] yargs;
    yargs[0] = make_nil(interp);
    Value* y = prim_yield(yargs[..], null, interp);
    if (y != null && y.tag == ERROR) return y;

    return scheduler_consume_pending_offload(fiber_id, interp);
}

// ============================================================
// (__raw-thread-spawn (op arg...)) -> task-id
// (__raw-thread-join task-id) -> value
//
// Minimal threaded task API over sendable offload jobs.
// ============================================================

fn Value* prim_thread_spawn(Value*[] args, Env* env, Interp* interp) {
    (void)env;
    scheduler_init();
    if (!g_scheduler.thread_task_mu_ready) return raise_error(interp, "thread-spawn: task mutex unavailable");
    if (args.len < 1) return raise_error(interp, "thread-spawn: expected job list");

    OffloadWork work = {};
    Value* parse_err = scheduler_build_offload_work(args[0], &work, interp);
    if (parse_err != null) return parse_err;

    usz task_id = scheduler_alloc_thread_task();
    if (task_id == NO_TASK) {
        shared_blob_release(work.blob);
        return raise_error(interp, "thread-spawn: task table full");
    }

    work.fiber_id = NO_FIBER;
    work.task_id = task_id;
    if (!scheduler_enqueue_offload(work)) {
        shared_blob_release(work.blob);
        scheduler_drop_thread_task(task_id);
        return raise_error(interp, "thread-spawn: offload queue is full");
    }

    return make_int(interp, (long)task_id);
}

fn Value* scheduler_thread_join_impl(long raw_id, long timeout_ms, Interp* interp) {
    if (raw_id < 0) return raise_error(interp, "thread-join: invalid task id");
    usz task_id = (usz)raw_id;
    if (task_id >= MAX_THREAD_TASKS) return raise_error(interp, "thread-join: invalid task id");

    long remaining = timeout_ms;
    while (true) {
        OffloadCompletion* completion = scheduler_take_thread_task_completion(task_id);
        if (completion != null) {
            Value* out = scheduler_value_from_offload_completion(completion, interp);
            scheduler_free_offload_completion(completion);
            return out;
        }

        if (!scheduler_thread_task_exists(task_id)) {
            return raise_error(interp, "thread-join: task already joined or invalid");
        }

        if (remaining == 0) {
            return raise_error(interp, "thread-join: timeout");
        }

        c_usleep(1000);
        if (remaining > 0) remaining--;
    }
}

fn Value* scheduler_validate_thread_join_context(char[] op_name, Interp* interp) {
    if (!g_scheduler.thread_task_mu_ready) {
        char[96] ebuf;
        char[] msg = io::bprintf(&ebuf, "%s: task mutex unavailable", (ZString)op_name)!!;
        return raise_error(interp, msg);
    }
    if (scheduler_is_running_fiber_context()) {
        char[96] ebuf;
        char[] msg = io::bprintf(&ebuf, "%s: not allowed inside a running fiber", (ZString)op_name)!!;
        return raise_error(interp, msg);
    }
    return null;
}

fn Value* scheduler_parse_thread_join_timeout_pair(Value* pair, long* task_id_out, long* timeout_ms_out, Interp* interp) {
    Value* task_id = pair.cons_val.car;
    Value* timeout_val = pair.cons_val.cdr;
    if (!is_int(task_id) || !is_int(timeout_val)) {
        return raise_error(interp, "thread-join-timeout: expected integer task-id and timeout-ms");
    }
    long timeout_ms = timeout_val.int_val;
    if (timeout_ms < 0) {
        return raise_error(interp, "thread-join-timeout: timeout-ms must be >= 0");
    }
    *task_id_out = task_id.int_val;
    *timeout_ms_out = timeout_ms;
    return null;
}

fn Value* scheduler_parse_nonnegative_int_arg(
    Value*[] args,
    char[] expected_msg,
    char[] invalid_msg,
    long* raw_id_out,
    Interp* interp
) {
    if (args.len < 1 || !is_int(args[0])) {
        return raise_error(interp, expected_msg);
    }
    long raw_id = args[0].int_val;
    if (raw_id < 0) return raise_error(interp, invalid_msg);
    *raw_id_out = raw_id;
    return null;
}

fn Value* scheduler_true_value(Interp* interp) @inline {
    return interp.global_env.lookup(interp.sym_true);
}

fn Value* prim_thread_join(Value*[] args, Env* env, Interp* interp) {
    (void)env;
    scheduler_init();
    Value* context_err = scheduler_validate_thread_join_context("thread-join", interp);
    if (context_err != null) return context_err;
    if (args.len < 1 || !is_int(args[0])) {
        return raise_error(interp, "thread-join: expected task-id integer");
    }

    return scheduler_thread_join_impl(args[0].int_val, -1, interp);
}

fn Value* prim_thread_join_timeout(Value*[] args, Env* env, Interp* interp) {
    (void)env;
    scheduler_init();
    Value* context_err = scheduler_validate_thread_join_context("thread-join-timeout", interp);
    if (context_err != null) return context_err;
    if (args.len < 1 || !is_cons(args[0])) {
        return raise_error(interp, "thread-join-timeout: expected (task-id . timeout-ms)");
    }

    long task_id = 0;
    long timeout_ms = 0;
    Value* parse_err = scheduler_parse_thread_join_timeout_pair(args[0], &task_id, &timeout_ms, interp);
    if (parse_err != null) return parse_err;
    return scheduler_thread_join_impl(task_id, timeout_ms, interp);
}

fn Value* prim_thread_cancel(Value*[] args, Env* env, Interp* interp) {
    (void)env;
    scheduler_init();
    if (!g_scheduler.thread_task_mu_ready) return raise_error(interp, "thread-cancel: task mutex unavailable");
    long raw_id = 0;
    Value* parse_err = scheduler_parse_nonnegative_int_arg(
        args,
        "thread-cancel: expected task-id integer",
        "thread-cancel: invalid task id",
        &raw_id,
        interp
    );
    if (parse_err != null) return parse_err;
    usz task_id = (usz)raw_id;
    if (task_id >= MAX_THREAD_TASKS) return raise_error(interp, "thread-cancel: invalid task id");

    bool already_done = false;
    if (!scheduler_cancel_thread_task(task_id, &already_done)) {
        return raise_error(interp, "thread-cancel: task already joined or invalid");
    }
    if (already_done) return make_nil(interp);
    return scheduler_true_value(interp);
}

// ============================================================
// (fiber-cancel fiber-id) → true/nil
//
// Cancels a BLOCKED or READY fiber. Cleans up pending I/O,
// marks fiber DONE with error result, notifies parent.
// Cannot cancel RUNNING or already-DONE fibers.
// ============================================================

fn bool scheduler_cancel_fiber(usz fiber_id, Interp* interp) {
    if (fiber_id >= g_scheduler.fiber_count) return false;
    FiberEntry* f = &g_scheduler.fibers[fiber_id];

    // Can only cancel READY or BLOCKED fibers
    if (f.state != FIBER_READY && f.state != FIBER_BLOCKED) return false;

    // Cancel all children first (recursive)
    for (usz i = 0; i < g_scheduler.fiber_count; i++) {
        if (g_scheduler.fibers[i].parent_id == fiber_id &&
            g_scheduler.fibers[i].state != FIBER_DONE) {
            scheduler_cancel_fiber(i, interp);
        }
    }

    // Complete with cancellation error
    Value* cancel_err = raise_error(interp, "fiber cancelled");
    scheduler_mark_done(fiber_id, cancel_err, interp);
    return true;
}

fn Value* prim_fiber_cancel(Value*[] args, Env* env, Interp* interp) {
    (void)env;
    scheduler_init();
    long raw_id = 0;
    Value* parse_err = scheduler_parse_nonnegative_int_arg(
        args,
        "fiber-cancel: expected fiber-id integer",
        "fiber-cancel: invalid fiber id",
        &raw_id,
        interp
    );
    if (parse_err != null) return parse_err;
    usz fiber_id = (usz)raw_id;
    if (fiber_id >= g_scheduler.fiber_count) {
        return raise_error(interp, "fiber-cancel: invalid fiber id");
    }

    // Cannot cancel self
    if (scheduler_is_running_fiber_context() && g_scheduler.current_fiber_id == fiber_id) {
        return raise_error(interp, "fiber-cancel: fiber cannot cancel itself");
    }

    if (!scheduler_cancel_fiber(fiber_id, interp)) {
        return make_nil(interp);  // already done or running
    }
    return scheduler_true_value(interp);
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

    usz parent_id = NO_FIBER;
    if (scheduler_is_running_fiber_context()) {
        parent_id = g_scheduler.current_fiber_id;
    }

    usz id = scheduler_add_fiber(co, parent_id, interp);
    if (id >= MAX_FIBERS) return raise_error(interp, "spawn: scheduler full");

    if (parent_id < g_scheduler.fiber_count) {
        g_scheduler.fibers[parent_id].live_children++;
    }

    return make_int(interp, (long)id);
}

fn Value* scheduler_result_or_nil(usz fiber_id, Interp* interp) @inline {
    Value* result = g_scheduler.fibers[fiber_id].result;
    return result != null ? result : make_nil(interp);
}

fn Value* scheduler_await_in_fiber_context(usz target, Interp* interp) {
    usz waiter_id = g_scheduler.current_fiber_id;
    FiberEntry* waiter = &g_scheduler.fibers[waiter_id];

    if (target == waiter_id) return raise_error(interp, "await: fiber cannot await itself");
    if (g_scheduler.fibers[target].parent_id != waiter_id) {
        return raise_error(interp, "await: fiber can only await direct children");
    }
    if (waiter.waiting_children) {
        return raise_error(interp, "await: parent is already waiting for child completion");
    }

    if (g_scheduler.fibers[target].state == FIBER_DONE) {
        return scheduler_result_or_nil(target, interp);
    }

    waiter.awaiting_fiber = true;
    waiter.await_target = target;
    waiter.state = FIBER_BLOCKED;

    Value*[1] yargs;
    yargs[0] = make_nil(interp);
    Value* y = prim_yield(yargs[..], null, interp);
    if (y != null && y.tag == ERROR) return y;

    if (g_scheduler.fibers[target].state != FIBER_DONE) {
        return raise_error(interp, "await: resumed before child completed");
    }

    return scheduler_result_or_nil(target, interp);
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

    if (scheduler_is_running_fiber_context()) {
        return scheduler_await_in_fiber_context(target, interp);
    }

    if (g_scheduler.fibers[target].state == FIBER_DONE) {
        return scheduler_result_or_nil(target, interp);
    }

    scheduler_run_until(target, interp);
    return scheduler_result_or_nil(target, interp);
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

fn bool scheduler_resume_ready_round(Interp* interp, usz target, bool stop_on_target, bool* target_done) {
    bool ran_ready = false;
    usz fc = g_scheduler.fiber_count;
    usz off = g_scheduler.resume_offset % (fc > 0 ? fc : 1);

    for (usz j = 0; j < fc; j++) {
        usz i = (off + j) % fc;
        if (g_scheduler.fibers[i].state != FIBER_READY) continue;
        ran_ready = true;
        scheduler_resume_fiber(i, interp);
        if (stop_on_target && g_scheduler.fibers[target].state == FIBER_DONE) {
            if (target_done != null) *target_done = true;
            break;
        }
    }
    g_scheduler.resume_offset = off + 1;
    return ran_ready;
}

fn bool scheduler_wait_blocked_once() {
    if (!scheduler_has_blocked_fibers()) return false;
    scheduler_wait_for_io_once();
    drain_wakeups();
    return true;
}

fn bool scheduler_advance_round(
    Interp* interp,
    usz target,
    bool stop_on_target,
    bool* target_done_out = null
) {
    drain_wakeups();

    bool target_done = false;
    bool ran_ready = scheduler_resume_ready_round(interp, target, stop_on_target, &target_done);
    if (target_done_out != null) {
        *target_done_out = target_done;
    }
    if (target_done) return true;
    if (ran_ready) return true;
    if (scheduler_wait_blocked_once()) return true;
    return false;
}

fn void scheduler_run_until(usz target, Interp* interp) {
    g_scheduler.running = true;
    defer {
        g_scheduler.running = false;
        g_scheduler.current_fiber_id = NO_FIBER;
    }

    usz max_rounds = 100000;
    usz round = 0;

    while (g_scheduler.fibers[target].state != FIBER_DONE && round < max_rounds) {
        bool target_done = false;
        bool progressed = scheduler_advance_round(interp, target, true, &target_done);

        if (target_done || g_scheduler.fibers[target].state == FIBER_DONE) break;
        if (!progressed) break;
        round++;
    }

    if (g_scheduler.uv_loop_ready && g_scheduler.uv_loop != null) {
        (void)uv_run(g_scheduler.uv_loop, UV_RUN_NOWAIT);
    }
    drain_wakeups();
}

fn void scheduler_reset_if_idle() {
    if (!scheduler_all_done()) return;

    if (g_scheduler.uv_loop_ready && g_scheduler.uv_loop != null) {
        (void)uv_run(g_scheduler.uv_loop, UV_RUN_NOWAIT);
    }

    g_scheduler.fiber_count = 0;
    g_scheduler.current_fiber_id = NO_FIBER;
    scheduler_reset_wakeup_queue();
    for (usz i = 0; i < MAX_FIBERS; i++) {
        if (g_scheduler.pending_offloads[i].completion != null) {
            scheduler_free_offload_completion(g_scheduler.pending_offloads[i].completion);
        }
        g_scheduler.fibers[i] = {};
        g_scheduler.pending_reads[i] = {};
        g_scheduler.pending_offloads[i] = {};
    }
}

fn void scheduler_run_all(Interp* interp) {
    g_scheduler.running = true;
    defer {
        g_scheduler.running = false;
        g_scheduler.current_fiber_id = NO_FIBER;
    }

    usz max_rounds = 100000;
    usz round = 0;

    while (round < max_rounds) {
        bool progressed = scheduler_advance_round(interp, 0, false);

        if (scheduler_all_done()) break;
        if (!progressed) break;
        round++;
    }

    drain_wakeups();
    scheduler_reset_if_idle();
}
