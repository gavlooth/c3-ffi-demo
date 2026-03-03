module lisp;

import std::core::mem;
import std::atomic::types;
import std::io;
import std::thread;
import main;

// ============================================================
// Fiber Scheduler — coroutines + effect handler
//
// A fiber is a coroutine managed by the scheduler.
// spawn creates a fiber. The scheduler resumes fibers round-robin.
// Fibers run until they yield, complete, or signal an I/O effect.
// ============================================================

const usz MAX_FIBERS = 256;
const usz NO_FIBER = MAX_FIBERS;
const usz MAX_THREAD_TASKS = 256;
const usz NO_TASK = MAX_THREAD_TASKS;

enum FiberState : char {
    FIBER_READY,
    FIBER_RUNNING,
    FIBER_BLOCKED,
    FIBER_DONE
}

struct FiberEntry {
    Value* coroutine;    // COROUTINE value
    Value* result;       // Final result (set on completion)
    Value* pending_result;   // Completion result while waiting on children
    usz parent_id;
    usz live_children;
    usz await_target;
    bool awaiting_fiber;
    bool waiting_children;
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

struct PendingOffload {
    bool active;
    bool completed;
    void* completion; // OffloadCompletion*
}

struct SharedBlob {
    types::Atomic{long} refcount;
    char* bytes;
    usz len;
}

enum WakeupEventType : char {
    WAKEUP_POLL_READABLE,
    WAKEUP_POLL_ERROR,
    WAKEUP_TIMER_EXPIRED,
    WAKEUP_OFFLOAD_READY
}

struct WakeupEvent {
    usz fiber_id;
    WakeupEventType type;
    int status;
    void* payload;
}

const usz WAKEUP_RING_SIZE = 256;
const usz WAKEUP_RING_MASK = WAKEUP_RING_SIZE - 1;

enum OffloadOp : char {
    OFFLOAD_SLEEP_MS,
    OFFLOAD_GZIP,
    OFFLOAD_DEFLATE
}

enum OffloadResultKind : char {
    OFFLOAD_RES_NIL,
    OFFLOAD_RES_INT,
    OFFLOAD_RES_BYTES,
    OFFLOAD_RES_ERROR
}

struct OffloadWork {
    usz fiber_id;
    usz task_id;
    OffloadOp op;
    long int_arg;
    SharedBlob* blob;
}

struct OffloadCompletion {
    OffloadResultKind kind;
    long int_result;
    SharedBlob* blob;
    char* error_msg;
    usz error_len;
}

const usz OFFLOAD_QUEUE_SIZE = 256;
const usz OFFLOAD_QUEUE_MASK = OFFLOAD_QUEUE_SIZE - 1;

struct OffloadQueue {
    OffloadWork[OFFLOAD_QUEUE_SIZE] ring;
    usz head;
    usz tail;
    usz count;
    thread::Mutex mu;
    thread::ConditionVariable cv;
    bool running;
    bool initialized;
    thread::Thread worker;
}

struct ThreadTaskEntry {
    bool active;
    bool completed;
    OffloadCompletion* completion;
}

struct Scheduler {
    FiberEntry[MAX_FIBERS] fibers;
    PendingTcpRead[MAX_FIBERS] pending_reads;
    PendingOffload[MAX_FIBERS] pending_offloads;
    WakeupEvent[WAKEUP_RING_SIZE] wakeup_ring;
    types::Atomic{bool}[WAKEUP_RING_SIZE] wakeup_ready;
    types::Atomic{usz} wakeup_head;    // producers reserve slots
    types::Atomic{usz} wakeup_tail;    // consumer drains slots
    usz fiber_count;
    usz current_fiber_id;
    bool running;
    bool uv_loop_ready;
    void* uv_loop;
    void* async_handle;                // uv_async_t*
    OffloadQueue offload_queue;
    ThreadTaskEntry[MAX_THREAD_TASKS] thread_tasks;
    thread::Mutex thread_task_mu;
    bool thread_task_mu_ready;
}

// Global scheduler (single-threaded)
Scheduler g_scheduler;
bool g_scheduler_initialized = false;

fn void scheduler_async_noop_cb(void* handle) {
    // Intentionally empty; uv_async_send only wakes the loop.
    (void)handle;
}

fn void scheduler_reset_wakeup_queue() {
    g_scheduler.wakeup_head.store(0);
    g_scheduler.wakeup_tail.store(0);
    for (usz i = 0; i < WAKEUP_RING_SIZE; i++) {
        g_scheduler.wakeup_ready[i].store(false);
    }
}

fn bool scheduler_str_eq(char[] a, char[] b) @inline {
    if (a.len != b.len) return false;
    for (usz i = 0; i < a.len; i++) if (a[i] != b[i]) return false;
    return true;
}

fn SharedBlob* shared_blob_new_owned(char* owned_bytes, usz len) {
    if (owned_bytes == null) return null;
    SharedBlob* blob = (SharedBlob*)mem::malloc(SharedBlob.sizeof);
    if (blob == null) {
        mem::free(owned_bytes);
        return null;
    }
    blob.refcount.store(1);
    blob.bytes = owned_bytes;
    blob.len = len;
    return blob;
}

fn SharedBlob* shared_blob_new_copy(char[] bytes) {
    usz cap = bytes.len + 1;
    if (cap < 32) cap = 32;
    char* copy = (char*)mem::malloc(cap);
    if (copy == null) return null;
    for (usz i = 0; i < bytes.len; i++) copy[i] = bytes[i];
    copy[bytes.len] = 0;
    return shared_blob_new_owned(copy, bytes.len);
}

fn void shared_blob_release(SharedBlob* blob) @inline {
    if (blob == null) return;
    long old = blob.refcount.add(-1);
    if (old <= 1) {
        if (blob.bytes != null) {
            mem::free(blob.bytes);
            blob.bytes = null;
        }
        mem::free(blob);
    }
}

fn void scheduler_free_offload_completion(void* raw_completion) {
    if (raw_completion == null) return;
    OffloadCompletion* completion = (OffloadCompletion*)raw_completion;
    if (completion.blob != null) {
        shared_blob_release(completion.blob);
        completion.blob = null;
    }
    if (completion.error_msg != null) {
        mem::free(completion.error_msg);
        completion.error_msg = null;
    }
    mem::free(completion);
}

fn void scheduler_set_offload_error(OffloadCompletion* completion, char[] message) {
    completion.kind = OFFLOAD_RES_ERROR;
    completion.error_len = message.len;
    completion.error_msg = (char*)mem::malloc(message.len);
    if (completion.error_msg == null) {
        completion.error_len = 0;
        return;
    }
    for (usz i = 0; i < message.len; i++) completion.error_msg[i] = message[i];
}

fn Value* scheduler_build_offload_work(Value* job, OffloadWork* work, Interp* interp) {
    if (!is_cons(job)) return raise_error(interp, "offload: expected (op args...)");
    Value* op_val = job.cons_val.car;
    if (!is_symbol(op_val)) return raise_error(interp, "offload: op must be a symbol");
    char[] op_name = interp.symbols.get_name(op_val.sym_val);
    Value* arg_list = job.cons_val.cdr;

    work.fiber_id = NO_FIBER;
    work.task_id = NO_TASK;
    work.op = OFFLOAD_SLEEP_MS;
    work.int_arg = 0;
    work.blob = null;

    if (scheduler_str_eq(op_name, "sleep-ms")) {
        if (!is_cons(arg_list) || !is_int(arg_list.cons_val.car)) {
            return raise_error(interp, "offload sleep-ms: expected integer milliseconds");
        }
        work.op = OFFLOAD_SLEEP_MS;
        work.int_arg = arg_list.cons_val.car.int_val;
        return null;
    }

    if (scheduler_str_eq(op_name, "gzip") || scheduler_str_eq(op_name, "deflate")) {
        if (!is_cons(arg_list) || !is_string(arg_list.cons_val.car)) {
            return raise_error(interp, "offload compress: expected string payload");
        }
        Value* src = arg_list.cons_val.car;
        work.op = scheduler_str_eq(op_name, "gzip") ? OFFLOAD_GZIP : OFFLOAD_DEFLATE;
        work.blob = shared_blob_new_copy(src.str_chars[:src.str_len]);
        if (work.blob == null) return raise_error(interp, "offload: out of memory");
        return null;
    }

    return raise_error(interp, "offload: unsupported op");
}

fn usz scheduler_alloc_thread_task() {
    if (!g_scheduler.thread_task_mu_ready) return NO_TASK;
    g_scheduler.thread_task_mu.lock();
    defer g_scheduler.thread_task_mu.unlock();

    for (usz i = 0; i < MAX_THREAD_TASKS; i++) {
        if (g_scheduler.thread_tasks[i].active) continue;
        g_scheduler.thread_tasks[i] = {};
        g_scheduler.thread_tasks[i].active = true;
        return i;
    }
    return NO_TASK;
}

fn void scheduler_complete_thread_task(usz task_id, OffloadCompletion* completion) {
    if (completion == null) return;
    if (task_id >= MAX_THREAD_TASKS || !g_scheduler.thread_task_mu_ready) {
        scheduler_free_offload_completion(completion);
        return;
    }

    g_scheduler.thread_task_mu.lock();
    if (!g_scheduler.thread_tasks[task_id].active || g_scheduler.thread_tasks[task_id].completed) {
        g_scheduler.thread_task_mu.unlock();
        scheduler_free_offload_completion(completion);
        return;
    }
    g_scheduler.thread_tasks[task_id].completion = completion;
    g_scheduler.thread_tasks[task_id].completed = true;
    g_scheduler.thread_task_mu.unlock();
}

fn OffloadCompletion* scheduler_take_thread_task_completion(usz task_id) {
    if (task_id >= MAX_THREAD_TASKS || !g_scheduler.thread_task_mu_ready) return null;

    g_scheduler.thread_task_mu.lock();
    defer g_scheduler.thread_task_mu.unlock();
    if (!g_scheduler.thread_tasks[task_id].active || !g_scheduler.thread_tasks[task_id].completed) {
        return null;
    }

    OffloadCompletion* completion = g_scheduler.thread_tasks[task_id].completion;
    g_scheduler.thread_tasks[task_id] = {};
    return completion;
}

fn void scheduler_drop_thread_task(usz task_id) {
    if (task_id >= MAX_THREAD_TASKS || !g_scheduler.thread_task_mu_ready) return;
    g_scheduler.thread_task_mu.lock();
    OffloadCompletion* completion = null;
    if (g_scheduler.thread_tasks[task_id].active) {
        if (g_scheduler.thread_tasks[task_id].completed) {
            completion = g_scheduler.thread_tasks[task_id].completion;
        }
        g_scheduler.thread_tasks[task_id] = {};
    }
    g_scheduler.thread_task_mu.unlock();
    if (completion != null) scheduler_free_offload_completion(completion);
}

fn bool scheduler_offload_compress(bool gzip_mode, SharedBlob* input, OffloadCompletion* completion) {
    if (input == null || input.bytes == null) {
        scheduler_set_offload_error(completion, "offload: missing input");
        return false;
    }

    DeflateCompressor* compressor = libdeflate_alloc_compressor(6);
    if (compressor == null) {
        scheduler_set_offload_error(completion, "offload: compressor allocation failed");
        return false;
    }
    defer libdeflate_free_compressor(compressor);

    usz input_len = input.len;
    usz bound = gzip_mode
        ? libdeflate_gzip_compress_bound(compressor, input_len)
        : libdeflate_deflate_compress_bound(compressor, input_len);
    char* out = (char*)mem::malloc(bound + 1);
    if (out == null) {
        scheduler_set_offload_error(completion, "offload: out of memory");
        return false;
    }

    usz written = gzip_mode
        ? libdeflate_gzip_compress(compressor, input.bytes, input_len, out, bound)
        : libdeflate_deflate_compress(compressor, input.bytes, input_len, out, bound);
    if (written == 0) {
        mem::free(out);
        scheduler_set_offload_error(completion, "offload: compression failed");
        return false;
    }
    out[written] = 0;

    SharedBlob* result = shared_blob_new_owned(out, written);
    if (result == null) {
        scheduler_set_offload_error(completion, "offload: result allocation failed");
        return false;
    }

    completion.kind = OFFLOAD_RES_BYTES;
    completion.blob = result;
    return true;
}

fn OffloadCompletion* scheduler_execute_offload(OffloadWork work) {
    OffloadCompletion* completion = (OffloadCompletion*)mem::malloc(OffloadCompletion.sizeof);
    if (completion == null) {
        shared_blob_release(work.blob);
        return null;
    }
    *completion = {};

    switch (work.op) {
        case OFFLOAD_SLEEP_MS:
            if (work.int_arg > 0) c_usleep((uint)(work.int_arg * 1000));
            completion.kind = OFFLOAD_RES_NIL;
        case OFFLOAD_GZIP:
            scheduler_offload_compress(true, work.blob, completion);
        case OFFLOAD_DEFLATE:
            scheduler_offload_compress(false, work.blob, completion);
    }

    shared_blob_release(work.blob);
    return completion;
}

fn int scheduler_offload_worker_main(void* arg) {
    (void)arg;
    while (true) {
        OffloadWork work = {};

        g_scheduler.offload_queue.mu.lock();
        while (g_scheduler.offload_queue.count == 0 && g_scheduler.offload_queue.running) {
            g_scheduler.offload_queue.cv.wait(&g_scheduler.offload_queue.mu);
        }
        if (!g_scheduler.offload_queue.running) {
            g_scheduler.offload_queue.mu.unlock();
            return 0;
        }

        usz slot = g_scheduler.offload_queue.tail & OFFLOAD_QUEUE_MASK;
        work = g_scheduler.offload_queue.ring[slot];
        g_scheduler.offload_queue.tail++;
        g_scheduler.offload_queue.count--;
        g_scheduler.offload_queue.mu.unlock();

        OffloadCompletion* completion = scheduler_execute_offload(work);
        if (completion == null) {
            continue;
        }

        if (work.task_id != NO_TASK) {
            scheduler_complete_thread_task(work.task_id, completion);
            continue;
        }

        while (!wakeup_enqueue(WAKEUP_OFFLOAD_READY, work.fiber_id, 0, completion)) {
            c_usleep(1000);
        }
    }
}

fn void scheduler_init_offload_queue() {
    OffloadQueue* q = &g_scheduler.offload_queue;
    if (q.initialized) return;

    *q = {};
    if (catch err = q.mu.init()) return;
    if (catch err = q.cv.init()) {
        q.mu.destroy();
        return;
    }
    q.running = true;

    if (catch err = q.worker.create(&scheduler_offload_worker_main, null)) {
        q.cv.destroy();
        q.mu.destroy();
        *q = {};
        return;
    }

    q.initialized = true;
}

fn bool scheduler_enqueue_offload(OffloadWork work) {
    OffloadQueue* q = &g_scheduler.offload_queue;
    if (!q.initialized) return false;

    q.mu.lock();
    defer q.mu.unlock();

    if (q.count >= OFFLOAD_QUEUE_SIZE) return false;
    usz slot = q.head & OFFLOAD_QUEUE_MASK;
    q.ring[slot] = work;
    q.head++;
    q.count++;
    q.cv.signal();
    return true;
}

fn void scheduler_init() {
    if (g_scheduler_initialized) return;

    g_scheduler = {};
    g_scheduler.fiber_count = 0;
    g_scheduler.current_fiber_id = NO_FIBER;
    g_scheduler.running = false;
    if (catch err = g_scheduler.thread_task_mu.init()) {
        g_scheduler.thread_task_mu_ready = false;
    } else {
        g_scheduler.thread_task_mu_ready = true;
    }
    scheduler_reset_wakeup_queue();
    scheduler_init_offload_queue();

    usz loop_size = uv_loop_size();
    if (loop_size > 0) {
        void* loop_mem = mem::malloc(loop_size);
        if (loop_mem != null && uv_loop_init(loop_mem) == 0) {
            g_scheduler.uv_loop_ready = true;
            g_scheduler.uv_loop = loop_mem;

            usz async_size = uv_handle_size(UV_HANDLE_ASYNC);
            if (async_size > 0) {
                void* async_mem = mem::malloc(async_size);
                if (async_mem != null) {
                    if (uv_async_init(g_scheduler.uv_loop, async_mem, &scheduler_async_noop_cb) == 0) {
                        g_scheduler.async_handle = async_mem;
                    } else {
                        mem::free(async_mem);
                    }
                }
            }
        } else if (loop_mem != null) {
            mem::free(loop_mem);
        }
    }

    g_scheduler_initialized = true;
}

fn usz scheduler_add_fiber(Value* coroutine, usz parent_id, Interp* interp) {
    scheduler_init();
    if (g_scheduler.fiber_count >= MAX_FIBERS) return MAX_FIBERS;

    coroutine = promote_to_root(coroutine, interp);
    usz id = g_scheduler.fiber_count;
    FiberEntry* f = &g_scheduler.fibers[id];
    f.coroutine = coroutine;
    f.result = null;
    f.pending_result = null;
    f.parent_id = parent_id;
    f.live_children = 0;
    f.await_target = NO_FIBER;
    f.awaiting_fiber = false;
    f.waiting_children = false;
    f.state = FIBER_READY;
    g_scheduler.pending_reads[id] = {};
    g_scheduler.pending_offloads[id] = {};
    g_scheduler.fiber_count++;
    return id;
}

fn bool scheduler_is_running_fiber_context() @inline {
    if (!g_scheduler.running) return false;
    if (g_scheduler.current_fiber_id >= g_scheduler.fiber_count) return false;
    FiberEntry* f = &g_scheduler.fibers[g_scheduler.current_fiber_id];
    if (f.state != FIBER_RUNNING) return false;
    if (f.coroutine == null || f.coroutine.tag != COROUTINE || f.coroutine.coroutine_val == null) return false;
    return main::g_current_stack_ctx == f.coroutine.coroutine_val;
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

fn void scheduler_close_pending_offload(usz fiber_id) {
    if (fiber_id >= g_scheduler.fiber_count) return;
    PendingOffload* off = &g_scheduler.pending_offloads[fiber_id];
    if (!off.active) return;

    if (off.completion != null) {
        scheduler_free_offload_completion(off.completion);
        off.completion = null;
    }
    g_scheduler.pending_offloads[fiber_id] = {};
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

fn void scheduler_complete_pending_offload(usz fiber_id) {
    if (fiber_id >= g_scheduler.fiber_count) return;
    PendingOffload* off = &g_scheduler.pending_offloads[fiber_id];
    if (!off.active) return;

    FiberEntry* f = &g_scheduler.fibers[fiber_id];
    if (f.state == FIBER_BLOCKED) f.state = FIBER_READY;
}

fn bool wakeup_enqueue(WakeupEventType type, usz fiber_id, int status, void* payload) {
    while (true) {
        usz tail = g_scheduler.wakeup_tail.load();
        usz head = g_scheduler.wakeup_head.load();
        if (head - tail >= WAKEUP_RING_SIZE) return false;

        usz prev = mem::compare_exchange(&g_scheduler.wakeup_head.data, head, head + 1);
        if (prev != head) continue;

        usz slot = head & WAKEUP_RING_MASK;
        WakeupEvent ev = {};
        ev.fiber_id = fiber_id;
        ev.type = type;
        ev.status = status;
        ev.payload = payload;
        g_scheduler.wakeup_ring[slot] = ev;
        g_scheduler.wakeup_ready[slot].store(true);

        if (g_scheduler.async_handle != null) (void)uv_async_send(g_scheduler.async_handle);
        return true;
    }
}

fn void scheduler_uv_timer_cb(void* handle) {
    isz idx = scheduler_find_pending_by_handle(handle);
    if (idx < 0) return;

    PendingTcpRead* op = &g_scheduler.pending_reads[(usz)idx];
    if (!op.active || op.completed) return;

    (void)wakeup_enqueue(WAKEUP_TIMER_EXPIRED, (usz)idx, 0, null);
}

fn void scheduler_uv_poll_cb(void* handle, int status, int events) {
    isz idx = scheduler_find_pending_by_handle(handle);
    if (idx < 0) return;

    PendingTcpRead* op = &g_scheduler.pending_reads[(usz)idx];
    if (!op.active || op.completed) return;

    if (status < 0) {
        (void)wakeup_enqueue(WAKEUP_POLL_ERROR, (usz)idx, status, null);
    } else if ((events & UV_READABLE) != 0) {
        (void)wakeup_enqueue(WAKEUP_POLL_READABLE, (usz)idx, 0, null);
    }
}

fn void drain_wakeups() {
    usz tail = g_scheduler.wakeup_tail.load();
    while (true) {
        usz head = g_scheduler.wakeup_head.load();
        if (tail == head) break;

        usz slot = tail & WAKEUP_RING_MASK;
        if (!g_scheduler.wakeup_ready[slot].load()) break;

        WakeupEvent ev = g_scheduler.wakeup_ring[slot];
        g_scheduler.wakeup_ready[slot].store(false);
        tail++;

        usz fid = ev.fiber_id;
        if (fid >= g_scheduler.fiber_count) {
            if (ev.type == WAKEUP_OFFLOAD_READY && ev.payload != null) {
                scheduler_free_offload_completion(ev.payload);
            }
            continue;
        }

        switch (ev.type) {
            case WAKEUP_TIMER_EXPIRED:
                PendingTcpRead* op = &g_scheduler.pending_reads[fid];
                if (!op.active || op.completed) continue;
                op.timed_out = true;
                op.completed = true;
                scheduler_complete_pending_tcp_read(fid);
            case WAKEUP_POLL_ERROR:
                PendingTcpRead* op = &g_scheduler.pending_reads[fid];
                if (!op.active || op.completed) continue;
                op.error_code = ev.status;
                op.completed = true;
                scheduler_complete_pending_tcp_read(fid);
            case WAKEUP_POLL_READABLE:
                PendingTcpRead* op = &g_scheduler.pending_reads[fid];
                if (!op.active || op.completed) continue;
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
                if (op.completed) scheduler_complete_pending_tcp_read(fid);
            case WAKEUP_OFFLOAD_READY:
                PendingOffload* off = &g_scheduler.pending_offloads[fid];
                if (!off.active || off.completed) {
                    scheduler_free_offload_completion(ev.payload);
                    continue;
                }
                off.completion = ev.payload;
                off.completed = true;
                scheduler_complete_pending_offload(fid);
        }
    }
    g_scheduler.wakeup_tail.store(tail);
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

fn Value* scheduler_value_from_offload_completion(OffloadCompletion* completion, Interp* interp) {
    if (completion == null) return raise_error(interp, "offload: missing completion");

    Value* out;
    switch (completion.kind) {
        case OFFLOAD_RES_NIL:
            out = make_nil(interp);
        case OFFLOAD_RES_INT:
            out = make_int(interp, completion.int_result);
        case OFFLOAD_RES_BYTES:
            if (completion.blob == null || completion.blob.bytes == null) {
                out = make_string(interp, "");
            } else {
                // Transfer byte ownership into a Value on the scheduler thread.
                char* owned = completion.blob.bytes;
                usz len = completion.blob.len;
                completion.blob.bytes = null;
                completion.blob.len = 0;
                shared_blob_release(completion.blob);
                completion.blob = null;
                out = make_string_owned(interp, owned, len);
            }
        case OFFLOAD_RES_ERROR:
            if (completion.error_msg != null && completion.error_len > 0) {
                out = raise_error(interp, completion.error_msg[:completion.error_len]);
            } else {
                out = raise_error(interp, "offload: worker failed");
            }
    }
    return out;
}

fn Value* scheduler_consume_pending_offload(usz fiber_id, Interp* interp) {
    PendingOffload* off = &g_scheduler.pending_offloads[fiber_id];
    if (!off.active || !off.completed || off.completion == null) {
        return raise_error(interp, "offload: resumed before completion");
    }

    OffloadCompletion* completion = (OffloadCompletion*)off.completion;
    Value* out = scheduler_value_from_offload_completion(completion, interp);

    scheduler_free_offload_completion(off.completion);
    g_scheduler.pending_offloads[fiber_id] = {};
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
    f.pending_result = null;
    f.awaiting_fiber = false;
    f.await_target = NO_FIBER;
    f.waiting_children = false;
    scheduler_close_pending_tcp_read(fiber_id);
    scheduler_close_pending_offload(fiber_id);

    if (f.parent_id < g_scheduler.fiber_count) {
        FiberEntry* parent = &g_scheduler.fibers[f.parent_id];
        if (parent.live_children > 0) parent.live_children--;

        if (parent.waiting_children && parent.live_children == 0) {
            Value* final_result = parent.pending_result != null ? parent.pending_result : make_nil(interp);
            parent.pending_result = null;
            parent.waiting_children = false;
            scheduler_complete_fiber(f.parent_id, final_result, interp);
            return;
        }

        if (parent.awaiting_fiber && parent.await_target == fiber_id && parent.state == FIBER_BLOCKED) {
            parent.awaiting_fiber = false;
            parent.await_target = NO_FIBER;
            parent.state = FIBER_READY;
        }
    }
}

fn void scheduler_complete_fiber(usz fiber_id, Value* result, Interp* interp) {
    FiberEntry* f = &g_scheduler.fibers[fiber_id];
    if (f.live_children > 0) {
        f.pending_result = promote_to_root(result, interp);
        f.waiting_children = true;
        f.awaiting_fiber = false;
        f.await_target = NO_FIBER;
        f.state = FIBER_BLOCKED;
        return;
    }
    scheduler_mark_done(fiber_id, result, interp);
}

fn void scheduler_resume_fiber(usz fiber_id, Interp* interp) {
    FiberEntry* f = &g_scheduler.fibers[fiber_id];
    if (f.state != FIBER_READY) return;

    f.state = FIBER_RUNNING;
    g_scheduler.current_fiber_id = fiber_id;

    Value*[1] resume_args;
    resume_args[0] = f.coroutine;
    Value* result = prim_resume(resume_args[..], null, interp);

    g_scheduler.current_fiber_id = NO_FIBER;

    if (result != null && result.tag == ERROR) {
        scheduler_complete_fiber(fiber_id, result, interp);
        return;
    }

    if (f.coroutine == null || f.coroutine.coroutine_val == null) {
        scheduler_complete_fiber(fiber_id, result, interp);
        return;
    }

    StackCtx* ctx = f.coroutine.coroutine_val;
    if (ctx.status == main::StackCtxStatus.CTX_COMPLETED || ctx.status == main::StackCtxStatus.CTX_DEAD) {
        scheduler_complete_fiber(fiber_id, result, interp);
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
    if (!scheduler_is_running_fiber_context()) return null;

    usz fiber_id = g_scheduler.current_fiber_id;
    FiberEntry* f = &g_scheduler.fibers[fiber_id];

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
// (__raw-offload (op arg...)) -> value
//
// Worker-thread offload bridge:
// - In fiber context: enqueue job, block current fiber, resume on completion.
// - Outside fiber context: execute synchronously as fallback.
// ============================================================

fn Value* prim_offload(Value*[] args, Env* env, Interp* interp) {
    (void)env;
    scheduler_init();

    if (args.len < 1) return raise_error(interp, "offload: expected job list");
    OffloadWork work = {};
    Value* parse_err = scheduler_build_offload_work(args[0], &work, interp);
    if (parse_err != null) return parse_err;

    if (!scheduler_is_running_fiber_context()) {
        OffloadCompletion* completion = scheduler_execute_offload(work);
        if (completion == null) return raise_error(interp, "offload: worker completion alloc failed");
        Value* out = scheduler_value_from_offload_completion(completion, interp);
        scheduler_free_offload_completion(completion);
        return out;
    }

    usz fiber_id = g_scheduler.current_fiber_id;
    PendingOffload* pending = &g_scheduler.pending_offloads[fiber_id];
    if (pending.active) {
        shared_blob_release(work.blob);
        return raise_error(interp, "offload: pending work already exists for this fiber");
    }

    work.fiber_id = fiber_id;
    work.task_id = NO_TASK;
    if (!scheduler_enqueue_offload(work)) {
        shared_blob_release(work.blob);
        return raise_error(interp, "offload: queue is full");
    }

    pending.active = true;
    pending.completed = false;
    pending.completion = null;
    g_scheduler.fibers[fiber_id].state = FIBER_BLOCKED;

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

fn Value* prim_thread_join(Value*[] args, Env* env, Interp* interp) {
    (void)env;
    scheduler_init();
    if (!g_scheduler.thread_task_mu_ready) return raise_error(interp, "thread-join: task mutex unavailable");
    if (args.len < 1 || !is_int(args[0])) {
        return raise_error(interp, "thread-join: expected task-id integer");
    }
    if (scheduler_is_running_fiber_context()) {
        return raise_error(interp, "thread-join: not allowed inside a running fiber");
    }

    long raw_id = args[0].int_val;
    if (raw_id < 0) return raise_error(interp, "thread-join: invalid task id");
    usz task_id = (usz)raw_id;
    if (task_id >= MAX_THREAD_TASKS) return raise_error(interp, "thread-join: invalid task id");

    usz spins = 0;
    while (true) {
        OffloadCompletion* completion = scheduler_take_thread_task_completion(task_id);
        if (completion != null) {
            Value* out = scheduler_value_from_offload_completion(completion, interp);
            scheduler_free_offload_completion(completion);
            return out;
        }

        if (g_scheduler.thread_task_mu_ready) {
            bool exists = false;
            g_scheduler.thread_task_mu.lock();
            exists = g_scheduler.thread_tasks[task_id].active;
            g_scheduler.thread_task_mu.unlock();
            if (!exists) return raise_error(interp, "thread-join: task already joined or invalid");
        }

        c_usleep(1000);
        spins++;
        if (spins > 60000000) return raise_error(interp, "thread-join: timeout");
    }
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
            Value* done_result = g_scheduler.fibers[target].result;
            return done_result != null ? done_result : make_nil(interp);
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

        Value* done_result = g_scheduler.fibers[target].result;
        return done_result != null ? done_result : make_nil(interp);
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
        g_scheduler.current_fiber_id = NO_FIBER;
    }

    usz max_rounds = 100000;
    usz round = 0;

    while (g_scheduler.fibers[target].state != FIBER_DONE && round < max_rounds) {
        drain_wakeups();
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
            drain_wakeups();
            round++;
            continue;
        }

        break;
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
        drain_wakeups();
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
            drain_wakeups();
            round++;
            continue;
        }

        break;
    }

    drain_wakeups();
    scheduler_reset_if_idle();
}
