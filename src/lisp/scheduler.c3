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

enum FiberState : char {
    FIBER_READY,
    FIBER_RUNNING,
    FIBER_BLOCKED,
    FIBER_DONE
}

struct FiberEntry {
    Value* coroutine;    // COROUTINE value
    Value* result;       // Final result (set on completion)
    FiberState state;
}

struct PendingTcpRead {
    bool active;
    bool completed;
    bool consumed;
    bool timed_out;
    int fd;
    usz max_bytes;
    char* buffer;
    long nread;
    int error_code;
    void* poll_handle;
    void* timer_handle;
}

struct Scheduler {
    FiberEntry[MAX_FIBERS] fibers;
    PendingTcpRead[MAX_FIBERS] pending_reads;
    usz fiber_count;
    usz current_fiber_id;
    bool running;
    bool uv_loop_ready;
    void* uv_loop;
}

// Global scheduler (single-threaded)
Scheduler g_scheduler;
bool g_scheduler_initialized = false;

fn void scheduler_init() {
    if (g_scheduler_initialized) return;

    g_scheduler = {};
    g_scheduler.fiber_count = 0;
    g_scheduler.current_fiber_id = MAX_FIBERS;
    g_scheduler.running = false;

    usz loop_size = uv_loop_size();
    if (loop_size > 0) {
        void* loop_mem = mem::malloc(loop_size);
        if (loop_mem != null && uv_loop_init(loop_mem) == 0) {
            g_scheduler.uv_loop_ready = true;
            g_scheduler.uv_loop = loop_mem;
        } else if (loop_mem != null) {
            mem::free(loop_mem);
        }
    }

    g_scheduler_initialized = true;
}

fn usz scheduler_add_fiber(Value* coroutine, Interp* interp) {
    scheduler_init();
    if (g_scheduler.fiber_count >= MAX_FIBERS) return MAX_FIBERS;

    coroutine = promote_to_root(coroutine, interp);
    usz id = g_scheduler.fiber_count;
    g_scheduler.fibers[id].coroutine = coroutine;
    g_scheduler.fibers[id].result = null;
    g_scheduler.fibers[id].state = FIBER_READY;
    g_scheduler.pending_reads[id] = {};
    g_scheduler.fiber_count++;
    return id;
}

fn isz scheduler_find_pending_by_handle(void* handle) {
    for (usz i = 0; i < g_scheduler.fiber_count; i++) {
        PendingTcpRead* op = &g_scheduler.pending_reads[i];
        if (!op.active) continue;
        if (op.poll_handle == handle || op.timer_handle == handle) return (isz)i;
    }
    return -1;
}

fn void scheduler_free_handle_cb(void* handle) {
    if (handle != null) mem::free(handle);
}

fn void scheduler_close_pending_tcp_read(usz fiber_id) {
    if (fiber_id >= g_scheduler.fiber_count) return;

    PendingTcpRead* op = &g_scheduler.pending_reads[fiber_id];
    if (!op.active) return;

    if (op.poll_handle != null) {
        (void)uv_poll_stop(op.poll_handle);
        uv_close(op.poll_handle, &scheduler_free_handle_cb);
        op.poll_handle = null;
    }
    if (op.timer_handle != null) {
        (void)uv_timer_stop(op.timer_handle);
        uv_close(op.timer_handle, &scheduler_free_handle_cb);
        op.timer_handle = null;
    }

    if (op.buffer != null) {
        mem::free(op.buffer);
        op.buffer = null;
    }

    g_scheduler.pending_reads[fiber_id] = {};
}

fn void scheduler_uv_close_cb(void* handle) {
    isz idx = scheduler_find_pending_by_handle(handle);
    if (idx < 0) {
        if (handle != null) mem::free(handle);
        return;
    }

    PendingTcpRead* op = &g_scheduler.pending_reads[(usz)idx];
    if (handle != null) mem::free(handle);
    if (op.poll_handle == handle) op.poll_handle = null;
    if (op.timer_handle == handle) op.timer_handle = null;
}

fn void scheduler_complete_pending_tcp_read(usz fiber_id) {
    PendingTcpRead* op = &g_scheduler.pending_reads[fiber_id];
    if (!op.active) return;

    if (op.poll_handle != null) {
        (void)uv_poll_stop(op.poll_handle);
        uv_close(op.poll_handle, &scheduler_uv_close_cb);
    }
    if (op.timer_handle != null) {
        (void)uv_timer_stop(op.timer_handle);
        uv_close(op.timer_handle, &scheduler_uv_close_cb);
    }

    FiberEntry* f = &g_scheduler.fibers[fiber_id];
    if (f.state == FIBER_BLOCKED) f.state = FIBER_READY;
}

fn void scheduler_uv_timer_cb(void* handle) {
    isz idx = scheduler_find_pending_by_handle(handle);
    if (idx < 0) return;

    PendingTcpRead* op = &g_scheduler.pending_reads[(usz)idx];
    if (!op.active || op.completed) return;

    op.timed_out = true;
    op.completed = true;
    scheduler_complete_pending_tcp_read((usz)idx);
}

fn void scheduler_uv_poll_cb(void* handle, int status, int events) {
    isz idx = scheduler_find_pending_by_handle(handle);
    if (idx < 0) return;

    PendingTcpRead* op = &g_scheduler.pending_reads[(usz)idx];
    if (!op.active || op.completed) return;

    if (status < 0) {
        op.error_code = status;
        op.completed = true;
    } else if ((events & UV_READABLE) != 0) {
        long n = c_recv(op.fd, op.buffer, op.max_bytes, MSG_DONTWAIT);
        if (n >= 0) {
            op.nread = n;
            op.completed = true;
        } else {
            int err = 0;
            int* err_ptr = c_errno_location();
            if (err_ptr != null) err = *err_ptr;
            if (err != ERRNO_EAGAIN && err != ERRNO_EWOULDBLOCK) {
                op.error_code = err;
                op.completed = true;
            }
        }
    }

    if (!op.completed) return;

    scheduler_complete_pending_tcp_read((usz)idx);
}

fn Value* scheduler_consume_pending_tcp_read(usz fiber_id, Interp* interp) {
    PendingTcpRead* op = &g_scheduler.pending_reads[fiber_id];
    if (!op.active || !op.completed) {
        return raise_error(interp, "tcp-read: resumed before async read completed");
    }

    Value* out;
    if (op.timed_out) {
        out = raise_error(interp, "tcp-read: timeout");
    } else if (op.error_code != 0) {
        out = raise_error(interp, "tcp-read: async read failed");
    } else if (op.nread <= 0) {
        out = make_string(interp, "");
    } else {
        out = make_string(interp, op.buffer[:(usz)op.nread]);
    }

    op.consumed = true;
    if (op.buffer != null) mem::free(op.buffer);
    g_scheduler.pending_reads[fiber_id] = {};
    if (g_scheduler.uv_loop_ready && g_scheduler.uv_loop != null) {
        (void)uv_run(g_scheduler.uv_loop, UV_RUN_NOWAIT);
    }

    return out;
}

fn bool scheduler_has_blocked_fibers() @inline {
    for (usz i = 0; i < g_scheduler.fiber_count; i++) {
        if (g_scheduler.fibers[i].state == FIBER_BLOCKED) return true;
    }
    return false;
}

fn bool scheduler_all_done() @inline {
    for (usz i = 0; i < g_scheduler.fiber_count; i++) {
        if (g_scheduler.fibers[i].state != FIBER_DONE) return false;
    }
    return true;
}

fn void scheduler_mark_done(usz fiber_id, Value* result, Interp* interp) {
    FiberEntry* f = &g_scheduler.fibers[fiber_id];
    f.state = FIBER_DONE;
    f.result = promote_to_root(result, interp);
    scheduler_close_pending_tcp_read(fiber_id);
}

fn void scheduler_resume_fiber(usz fiber_id, Interp* interp) {
    FiberEntry* f = &g_scheduler.fibers[fiber_id];
    if (f.state != FIBER_READY) return;

    f.state = FIBER_RUNNING;
    g_scheduler.current_fiber_id = fiber_id;

    Value*[1] resume_args;
    resume_args[0] = f.coroutine;
    Value* result = prim_resume(resume_args[..], null, interp);

    g_scheduler.current_fiber_id = MAX_FIBERS;

    if (result != null && result.tag == ERROR) {
        scheduler_mark_done(fiber_id, result, interp);
        return;
    }

    if (f.coroutine == null || f.coroutine.coroutine_val == null) {
        scheduler_mark_done(fiber_id, result, interp);
        return;
    }

    StackCtx* ctx = f.coroutine.coroutine_val;
    if (ctx.status == main::StackCtxStatus.CTX_COMPLETED || ctx.status == main::StackCtxStatus.CTX_DEAD) {
        scheduler_mark_done(fiber_id, result, interp);
        return;
    }

    // Default yield path: runnable again unless an async op marked it BLOCKED.
    if (f.state == FIBER_RUNNING) f.state = FIBER_READY;
}

fn void scheduler_wait_for_io_once() {
    if (g_scheduler.uv_loop_ready && g_scheduler.uv_loop != null) {
        (void)uv_run(g_scheduler.uv_loop, UV_RUN_ONCE);
    } else {
        c_usleep(1000);  // fallback if libuv loop failed to initialize
    }
}

// Async bridge used by tcp-read fast-path:
// returns non-null when it handled the call (including errors);
// null means "fall back to blocking path".
fn Value* scheduler_try_async_tcp_read(int fd, usz max_bytes, long timeout_ms, Interp* interp) {
    scheduler_init();
    if (!g_scheduler.running) return null;
    if (g_scheduler.current_fiber_id >= g_scheduler.fiber_count) return null;

    usz fiber_id = g_scheduler.current_fiber_id;
    FiberEntry* f = &g_scheduler.fibers[fiber_id];
    if (f.state != FIBER_RUNNING) return null;
    if (f.coroutine == null || f.coroutine.tag != COROUTINE || f.coroutine.coroutine_val == null) return null;
    if (main::g_current_stack_ctx != f.coroutine.coroutine_val) return null;

    PendingTcpRead* op = &g_scheduler.pending_reads[fiber_id];
    if (op.active) return raise_error(interp, "tcp-read: pending async read already exists for this fiber");

    // Fast non-blocking attempt before registering a watcher.
    char* probe_buf = (char*)mem::malloc(max_bytes);
    if (probe_buf == null) return raise_error(interp, "tcp-read: out of memory");

    long probe_n = c_recv(fd, probe_buf, max_bytes, MSG_DONTWAIT);
    if (probe_n >= 0) {
        Value* out = probe_n == 0 ? make_string(interp, "") : make_string(interp, probe_buf[:(usz)probe_n]);
        mem::free(probe_buf);
        return out;
    }

    int probe_err = 0;
    int* err_ptr = c_errno_location();
    if (err_ptr != null) probe_err = *err_ptr;
    mem::free(probe_buf);

    if (probe_err != ERRNO_EAGAIN && probe_err != ERRNO_EWOULDBLOCK) {
        return raise_error(interp, "tcp-read: recv failed");
    }

    if (!g_scheduler.uv_loop_ready || g_scheduler.uv_loop == null) return null;

    usz handle_size = uv_handle_size(UV_HANDLE_POLL);
    if (handle_size == 0) return null;

    void* poll_handle = mem::malloc(handle_size);
    if (poll_handle == null) return raise_error(interp, "tcp-read: out of memory");

    char* read_buf = (char*)mem::malloc(max_bytes);
    if (read_buf == null) {
        mem::free(poll_handle);
        return raise_error(interp, "tcp-read: out of memory");
    }

    if (uv_poll_init(g_scheduler.uv_loop, poll_handle, fd) != 0) {
        mem::free(read_buf);
        mem::free(poll_handle);
        return raise_error(interp, "tcp-read: uv_poll_init failed");
    }

    void* timer_handle = null;
    if (timeout_ms >= 0) {
        usz timer_size = uv_handle_size(UV_HANDLE_TIMER);
        if (timer_size == 0) {
            uv_close(poll_handle, &scheduler_free_handle_cb);
            (void)uv_run(g_scheduler.uv_loop, UV_RUN_NOWAIT);
            mem::free(read_buf);
            return raise_error(interp, "tcp-read: uv timer handle unavailable");
        }

        timer_handle = mem::malloc(timer_size);
        if (timer_handle == null) {
            uv_close(poll_handle, &scheduler_free_handle_cb);
            (void)uv_run(g_scheduler.uv_loop, UV_RUN_NOWAIT);
            mem::free(read_buf);
            return raise_error(interp, "tcp-read: out of memory");
        }

        if (uv_timer_init(g_scheduler.uv_loop, timer_handle) != 0) {
            mem::free(timer_handle);
            uv_close(poll_handle, &scheduler_free_handle_cb);
            (void)uv_run(g_scheduler.uv_loop, UV_RUN_NOWAIT);
            mem::free(read_buf);
            return raise_error(interp, "tcp-read: uv_timer_init failed");
        }
    }

    if (uv_poll_start(poll_handle, UV_READABLE, &scheduler_uv_poll_cb) != 0) {
        uv_close(poll_handle, &scheduler_free_handle_cb);
        if (timer_handle != null) {
            uv_close(timer_handle, &scheduler_free_handle_cb);
        }
        if (g_scheduler.uv_loop_ready && g_scheduler.uv_loop != null) {
            (void)uv_run(g_scheduler.uv_loop, UV_RUN_NOWAIT);
        }
        mem::free(read_buf);
        return raise_error(interp, "tcp-read: uv_poll_start failed");
    }

    if (timer_handle != null && uv_timer_start(timer_handle, &scheduler_uv_timer_cb, (ulong)timeout_ms, 0) != 0) {
        (void)uv_poll_stop(poll_handle);
        uv_close(poll_handle, &scheduler_free_handle_cb);
        uv_close(timer_handle, &scheduler_free_handle_cb);
        if (g_scheduler.uv_loop_ready && g_scheduler.uv_loop != null) {
            (void)uv_run(g_scheduler.uv_loop, UV_RUN_NOWAIT);
        }
        mem::free(read_buf);
        return raise_error(interp, "tcp-read: uv_timer_start failed");
    }

    op.active = true;
    op.completed = false;
    op.consumed = false;
    op.timed_out = false;
    op.fd = fd;
    op.max_bytes = max_bytes;
    op.buffer = read_buf;
    op.nread = -1;
    op.error_code = 0;
    op.poll_handle = poll_handle;
    op.timer_handle = timer_handle;

    f.state = FIBER_BLOCKED;

    // Suspend current fiber until libuv callback marks it ready.
    Value*[1] yargs;
    yargs[0] = make_nil(interp);
    Value* y = prim_yield(yargs[..], null, interp);
    if (y != null && y.tag == ERROR) return y;

    return scheduler_consume_pending_tcp_read(fiber_id, interp);
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

    usz id = scheduler_add_fiber(co, interp);
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

    if (g_scheduler.fibers[target].state == FIBER_DONE) {
        Value* r = g_scheduler.fibers[target].result;
        return r != null ? r : make_nil(interp);
    }

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
    defer {
        g_scheduler.running = false;
        g_scheduler.current_fiber_id = MAX_FIBERS;
    }

    usz max_rounds = 100000;
    usz round = 0;

    while (g_scheduler.fibers[target].state != FIBER_DONE && round < max_rounds) {
        bool ran_ready = false;

        for (usz i = 0; i < g_scheduler.fiber_count; i++) {
            if (g_scheduler.fibers[i].state != FIBER_READY) continue;
            ran_ready = true;
            scheduler_resume_fiber(i, interp);
            if (g_scheduler.fibers[target].state == FIBER_DONE) break;
        }

        if (g_scheduler.fibers[target].state == FIBER_DONE) break;

        if (ran_ready) {
            round++;
            continue;
        }

        if (scheduler_has_blocked_fibers()) {
            scheduler_wait_for_io_once();
            round++;
            continue;
        }

        break;
    }

    if (g_scheduler.uv_loop_ready && g_scheduler.uv_loop != null) {
        (void)uv_run(g_scheduler.uv_loop, UV_RUN_NOWAIT);
    }
}

fn void scheduler_reset_if_idle() {
    if (!scheduler_all_done()) return;

    if (g_scheduler.uv_loop_ready && g_scheduler.uv_loop != null) {
        (void)uv_run(g_scheduler.uv_loop, UV_RUN_NOWAIT);
    }

    g_scheduler.fiber_count = 0;
    g_scheduler.current_fiber_id = MAX_FIBERS;
    for (usz i = 0; i < MAX_FIBERS; i++) {
        g_scheduler.fibers[i] = {};
        g_scheduler.pending_reads[i] = {};
    }
}

fn void scheduler_run_all(Interp* interp) {
    g_scheduler.running = true;
    defer {
        g_scheduler.running = false;
        g_scheduler.current_fiber_id = MAX_FIBERS;
    }

    usz max_rounds = 100000;
    usz round = 0;

    while (round < max_rounds) {
        bool ran_ready = false;

        for (usz i = 0; i < g_scheduler.fiber_count; i++) {
            if (g_scheduler.fibers[i].state != FIBER_READY) continue;
            ran_ready = true;
            scheduler_resume_fiber(i, interp);
        }

        if (scheduler_all_done()) break;

        if (ran_ready) {
            round++;
            continue;
        }

        if (scheduler_has_blocked_fibers()) {
            scheduler_wait_for_io_once();
            round++;
            continue;
        }

        break;
    }

    scheduler_reset_if_idle();
}
