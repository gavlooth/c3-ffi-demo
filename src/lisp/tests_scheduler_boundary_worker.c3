module lisp;

import std::core::mem;
import std::io;

fn void run_scheduler_offload_worker_retry_full_wakeup_boundary_tests(Interp* interp, int* pass, int* fail) {
    // Invariant: offload worker retries WAKEUP_OFFLOAD_READY enqueue while ring is full,
    // then succeeds after drain without mutating interpreter boundary/runtime fields.
    SchedulerBoundarySnapshot saved = scheduler_capture_runtime_boundary_snapshot(interp);
    bool ok = true;
    usz failed_step = 0;
    char[] failed_phase = "";

    for (usz i = 0; i < 8; i++) {
        void* saved_async = null;
        scheduler_test_prepare_blocked_pending_offload(&saved_async);

        usz pushed_fill = 0;
        for (usz j = 0; j < WAKEUP_RING_SIZE; j++) {
            if (wakeup_enqueue(WAKEUP_POLL_ERROR, MAX_FIBERS + i * WAKEUP_RING_SIZE + j + 1, -1, null)) {
                pushed_fill++;
            }
        }
        if (pushed_fill != WAKEUP_RING_SIZE) {
            ok = false;
            failed_step = i;
            failed_phase = "prefill";
            scheduler_test_cleanup_no_async(saved_async);
            break;
        }

        usz drops_before = g_scheduler.wakeup_drops.load();
        OffloadWork work = {};
        work.fiber_id = 0;
        work.task_id = NO_TASK;
        work.op = OFFLOAD_SLEEP_MS;
        work.int_arg = 0;
        work.blob = null;
        bool queued = scheduler_enqueue_offload(work);
        if (!queued) {
            ok = false;
            failed_step = i;
            failed_phase = "enqueue-offload";
            scheduler_test_cleanup_single_fiber_no_async(saved_async);
            break;
        }

        bool observed_retry = false;
        for (usz spin = 0; spin < 40; spin++) {
            c_usleep(1000);
            if (g_scheduler.wakeup_drops.load() > drops_before) {
                observed_retry = true;
                break;
            }
        }

        // Release the full ring and wait until worker completion is delivered
        // to the active pending-offload slot, then consume it to avoid leaks.
        drain_wakeups();
        bool completion_ready = false;
        for (usz spin = 0; spin < 160; spin++) {
            c_usleep(1000);
            drain_wakeups();
            if (g_scheduler.pending_offloads[0].active &&
                g_scheduler.pending_offloads[0].completed &&
                g_scheduler.pending_offloads[0].completion != null &&
                g_scheduler.fibers[0].state == FIBER_READY) {
                completion_ready = true;
                break;
            }
        }

        bool consume_ok = false;
        if (completion_ready) {
            Value* consumed = scheduler_consume_pending_offload(0, interp);
            consume_ok = consumed != null && consumed.tag != ERROR;
        }

        // Defensive final drain in case a producer raced after consume.
        drain_wakeups();
        c_usleep(1000);
        drain_wakeups();

        bool state_ok = scheduler_runtime_boundary_matches_snapshot(interp, &saved);
        bool queue_empty = g_scheduler.wakeup_head.load() == g_scheduler.wakeup_tail.load();
        bool slot_cleared = !g_scheduler.pending_offloads[0].active &&
                            !g_scheduler.pending_offloads[0].completed &&
                            g_scheduler.pending_offloads[0].completion == null;
        bool cycle_ok = observed_retry && completion_ready && consume_ok && queue_empty && slot_cleared && state_ok;
        scheduler_test_cleanup_single_fiber_no_async(saved_async);
        if (!cycle_ok) {
            ok = false;
            failed_step = i;
            if (!observed_retry) {
                failed_phase = "retry";
            } else if (!completion_ready || !consume_ok || !slot_cleared) {
                failed_phase = "completion";
            } else if (!queue_empty) {
                failed_phase = "queue";
            } else {
                failed_phase = "boundary";
            }
            break;
        }
    }

    if (ok) {
        emit_pass_literal("[PASS] scheduler offload worker retry on full wakeup boundary restore");
        (*pass)++;
    } else {
        io::printfn(
            "[FAIL] scheduler offload worker retry on full wakeup boundary restore at step %d (%s)",
            (int)failed_step,
            (ZString)failed_phase
        );
        (*fail)++;
    }
}

fn void run_scheduler_wakeup_offload_ready_barrier_boundary_tests(Interp* interp, int* pass, int* fail) {
    // Invariant: drain_wakeups must respect wakeup_ready barrier for
    // WAKEUP_OFFLOAD_READY payload events; once unblocked, completion delivery
    // and consume path must preserve boundary/runtime state.
    SchedulerBoundarySnapshot saved = scheduler_capture_runtime_boundary_snapshot(interp);

    void* saved_async = null;
    scheduler_test_prepare_blocked_pending_offload(&saved_async);

    OffloadCompletion* completion = (OffloadCompletion*)mem::malloc(OffloadCompletion.sizeof);
    if (completion == null) {
        scheduler_test_cleanup_single_fiber_no_async(saved_async);
        io::printn("[FAIL] scheduler wakeup offload-ready barrier boundary restore (alloc)");
        (*fail)++;
        return;
    }
    *completion = {};
    completion.kind = OFFLOAD_RES_NIL;

    WakeupEvent ev0 = {};
    ev0.fiber_id = 0;
    ev0.type = WAKEUP_OFFLOAD_READY;
    ev0.status = 0;
    ev0.payload = completion;
    WakeupEvent ev1 = {};
    ev1.fiber_id = MAX_FIBERS + 222;
    ev1.type = WAKEUP_POLL_ERROR;
    ev1.status = -13;
    ev1.payload = null;

    g_scheduler.wakeup_ring[0] = ev0;
    g_scheduler.wakeup_ring[1] = ev1;
    g_scheduler.wakeup_ready[0].store(false);
    g_scheduler.wakeup_ready[1].store(true);
    g_scheduler.wakeup_tail.store(0);
    g_scheduler.wakeup_head.store(2);

    drain_wakeups();
    usz tail_after_block = g_scheduler.wakeup_tail.load();
    bool blocked_ok = tail_after_block == 0 &&
                      g_scheduler.pending_offloads[0].active &&
                      !g_scheduler.pending_offloads[0].completed &&
                      g_scheduler.pending_offloads[0].completion == null &&
                      g_scheduler.fibers[0].state == FIBER_BLOCKED;
    bool state_ok_first = scheduler_runtime_boundary_matches_snapshot(interp, &saved);

    g_scheduler.wakeup_ready[0].store(true);
    drain_wakeups();
    usz head_after_release = g_scheduler.wakeup_head.load();
    usz tail_after_release = g_scheduler.wakeup_tail.load();
    bool delivered_ok = head_after_release == tail_after_release &&
                        g_scheduler.pending_offloads[0].active &&
                        g_scheduler.pending_offloads[0].completed &&
                        g_scheduler.pending_offloads[0].completion == completion &&
                        g_scheduler.fibers[0].state == FIBER_READY;
    bool state_ok_second = scheduler_runtime_boundary_matches_snapshot(interp, &saved);

    Value* consumed = scheduler_consume_pending_offload(0, interp);
    bool consume_ok = consumed != null &&
                      consumed.tag == NIL &&
                      !g_scheduler.pending_offloads[0].active &&
                      !g_scheduler.pending_offloads[0].completed &&
                      g_scheduler.pending_offloads[0].completion == null;
    bool state_ok_third = scheduler_runtime_boundary_matches_snapshot(interp, &saved);

    bool ok = blocked_ok && delivered_ok && consume_ok &&
              state_ok_first && state_ok_second && state_ok_third;

    scheduler_test_cleanup_single_fiber_no_async(saved_async);

    if (ok) {
        emit_pass_literal("[PASS] scheduler wakeup offload-ready barrier boundary restore");
        (*pass)++;
    } else {
        io::printn("[FAIL] scheduler wakeup offload-ready barrier boundary restore");
        (*fail)++;
    }
}

fn void run_scheduler_thread_task_worker_cancel_interleave_boundary_tests(Interp* interp, int* pass, int* fail) {
    // Invariant: real worker enqueue + immediate task cancel converges to one
    // cancel completion and preserves interpreter boundary/runtime state.
    SchedulerBoundarySnapshot saved = scheduler_capture_runtime_boundary_snapshot(interp);

    bool ok = true;
    usz failed_step = 0;
    char[] failed_phase = "";

    for (usz i = 0; i < 16; i++) {
        usz task_id = scheduler_alloc_thread_task();
        if (task_id == NO_TASK) {
            ok = false;
            failed_step = i;
            failed_phase = "alloc";
            break;
        }

        OffloadWork work = {};
        work.fiber_id = NO_FIBER;
        work.task_id = task_id;
        work.op = OFFLOAD_SLEEP_MS;
        work.int_arg = 10;
        work.blob = null;
        bool enqueued = scheduler_enqueue_offload(work);
        if (!enqueued) {
            scheduler_drop_thread_task(task_id);
            ok = false;
            failed_step = i;
            failed_phase = "enqueue";
            break;
        }

        bool already_done = false;
        bool cancel_ok = scheduler_cancel_thread_task(task_id, &already_done);
        bool cancel_state_ok = cancel_ok && !already_done;
        if (!cancel_state_ok) {
            scheduler_drop_thread_task(task_id);
            ok = false;
            failed_step = i;
            failed_phase = "cancel";
            break;
        }

        OffloadCompletion* completion = null;
        for (usz spin = 0; spin < 240; spin++) {
            completion = scheduler_take_thread_task_completion(task_id);
            if (completion != null) break;
            c_usleep(1000);
        }

        bool completion_ok = completion != null &&
                             completion.kind == OFFLOAD_RES_ERROR &&
                             !scheduler_thread_task_exists(task_id);
        if (completion != null) {
            scheduler_free_offload_completion(completion);
        } else {
            scheduler_drop_thread_task(task_id);
        }

        bool state_ok = scheduler_runtime_boundary_matches_snapshot(interp, &saved);
        bool cycle_ok = completion_ok && state_ok;
        if (!cycle_ok) {
            ok = false;
            failed_step = i;
            if (!completion_ok) {
                failed_phase = "completion";
            } else {
                failed_phase = "boundary";
            }
            break;
        }
    }

    if (ok) {
        emit_pass_literal("[PASS] scheduler worker cancel interleave boundary restore");
        (*pass)++;
    } else {
        io::printfn(
            "[FAIL] scheduler worker cancel interleave boundary restore at step %d (%s)",
            (int)failed_step,
            (ZString)failed_phase
        );
        (*fail)++;
    }
}

fn void run_scheduler_thread_join_timeout_then_join_boundary_tests(Interp* interp, int* pass, int* fail) {
    // Invariant: timeout join does not consume task completion; a later join can
    // complete successfully and clear task entry, with boundary/runtime state preserved.
    SchedulerBoundarySnapshot saved = scheduler_capture_runtime_boundary_snapshot(interp);

    bool ok = true;
    usz failed_step = 0;
    char[] failed_phase = "";

    for (usz i = 0; i < 12; i++) {
        usz task_id = scheduler_alloc_thread_task();
        if (task_id == NO_TASK) {
            ok = false;
            failed_step = i;
            failed_phase = "alloc";
            break;
        }

        OffloadWork work = {};
        work.fiber_id = NO_FIBER;
        work.task_id = task_id;
        work.op = OFFLOAD_SLEEP_MS;
        work.int_arg = 20;
        work.blob = null;
        bool enqueued = scheduler_enqueue_offload(work);
        if (!enqueued) {
            scheduler_drop_thread_task(task_id);
            ok = false;
            failed_step = i;
            failed_phase = "enqueue";
            break;
        }

        Value* timeout_v = scheduler_thread_join_impl((long)task_id, 0, interp);
        bool timeout_ok = timeout_v != null && timeout_v.tag == ERROR;
        bool exists_after_timeout = scheduler_thread_task_exists(task_id);

        Value* join_v = scheduler_thread_join_impl((long)task_id, 250, interp);
        bool join_ok = join_v != null && join_v.tag != ERROR;
        bool gone_after_join = !scheduler_thread_task_exists(task_id);

        bool state_ok = scheduler_runtime_boundary_matches_snapshot(interp, &saved);
        bool cycle_ok = timeout_ok &&
                        exists_after_timeout &&
                        join_ok &&
                        gone_after_join &&
                        state_ok;
        if (!cycle_ok) {
            scheduler_drop_thread_task(task_id);
            ok = false;
            failed_step = i;
            if (!timeout_ok || !exists_after_timeout) {
                failed_phase = "timeout";
            } else if (!join_ok || !gone_after_join) {
                failed_phase = "join";
            } else {
                failed_phase = "boundary";
            }
            break;
        }
    }

    if (ok) {
        emit_pass_literal("[PASS] scheduler timeout-then-join boundary restore");
        (*pass)++;
    } else {
        io::printfn(
            "[FAIL] scheduler timeout-then-join boundary restore at step %d (%s)",
            (int)failed_step,
            (ZString)failed_phase
        );
        (*fail)++;
    }
}
