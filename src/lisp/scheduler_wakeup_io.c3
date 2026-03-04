module lisp;

import std::core::mem;
import std::atomic::types;
import std::io;
import std::thread;
import main;

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
        if (head - tail >= WAKEUP_RING_SIZE) {
            g_scheduler.wakeup_drops++;
            return false;
        }

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

fn void scheduler_handle_invalid_wakeup(WakeupEvent ev) {
    if (ev.type == WAKEUP_OFFLOAD_READY && ev.payload != null) {
        scheduler_free_offload_completion(ev.payload);
    }
}

fn void scheduler_handle_wakeup_timer_expired(usz fid) {
    PendingTcpRead* op = &g_scheduler.pending_reads[fid];
    if (!op.active || op.completed) return;
    op.timed_out = true;
    op.completed = true;
    scheduler_complete_pending_tcp_read(fid);
}

fn void scheduler_handle_wakeup_poll_error(usz fid, int status) {
    PendingTcpRead* op = &g_scheduler.pending_reads[fid];
    if (!op.active || op.completed) return;
    op.error_code = status;
    op.completed = true;
    scheduler_complete_pending_tcp_read(fid);
}

fn void scheduler_handle_wakeup_poll_readable(usz fid) {
    PendingTcpRead* op = &g_scheduler.pending_reads[fid];
    if (!op.active || op.completed) return;

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
}

fn void scheduler_handle_wakeup_offload_ready(usz fid, void* payload) {
    PendingOffload* off = &g_scheduler.pending_offloads[fid];
    if (!off.active || off.completed) {
        scheduler_free_offload_completion(payload);
        return;
    }

    off.completion = payload;
    off.completed = true;
    scheduler_complete_pending_offload(fid);
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
            scheduler_handle_invalid_wakeup(ev);
            continue;
        }

        switch (ev.type) {
            case WAKEUP_TIMER_EXPIRED:
                scheduler_handle_wakeup_timer_expired(fid);
            case WAKEUP_POLL_ERROR:
                scheduler_handle_wakeup_poll_error(fid, ev.status);
            case WAKEUP_POLL_READABLE:
                scheduler_handle_wakeup_poll_readable(fid);
            case WAKEUP_OFFLOAD_READY:
                scheduler_handle_wakeup_offload_ready(fid, ev.payload);
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
