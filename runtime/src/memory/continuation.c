/*
 * Fiber-Based Concurrency Implementation
 *
 * This implements:
 * - CEK-style continuation frames
 * - Delimited continuations (prompt/control)
 * - Fiber scheduler (lightweight cooperative tasks)
 * - Fiber-based channels
 * - Generators/iterators
 * - Promises (async/await)
 */

#include "continuation.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* Forward declaration - defined in runtime.c */
extern void inc_ref(Obj* x);
extern void dec_ref(Obj* x);
extern Obj* call_closure(Obj* clos, Obj** args, int arg_count);

/* ========== Thread-Local State ========== */

static _Thread_local Scheduler* tl_scheduler = NULL;
static _Thread_local Obj* tl_current_env = NULL;
static _Thread_local Frame* tl_current_frame = NULL;
static _Thread_local uint32_t tl_next_prompt_tag = PROMPT_TAG_USER_BASE;

/* ========== Frame Pool ========== */

#define FRAME_POOL_INITIAL_SIZE 64

static Frame* frame_pool_alloc(Scheduler* sched) {
    if (sched->frame_freelist) {
        Frame* f = sched->frame_freelist;
        sched->frame_freelist = f->prev;
        sched->frame_pool_size--;
        return f;
    }
    return malloc(sizeof(Frame));
}

static void frame_pool_free(Scheduler* sched, Frame* f) {
    if (sched->frame_pool_size < FRAME_POOL_INITIAL_SIZE * 2) {
        f->prev = sched->frame_freelist;
        sched->frame_freelist = f;
        sched->frame_pool_size++;
    } else {
        free(f);
    }
}

/* ========== Frame Management ========== */

Frame* frame_alloc(FrameTag tag) {
    Scheduler* sched = scheduler_current();
    Frame* f = sched ? frame_pool_alloc(sched) : malloc(sizeof(Frame));
    if (!f) return NULL;

    memset(f, 0, sizeof(Frame));
    f->tag = tag;
    f->refcount = 1;
    return f;
}

void frame_free(Frame* f) {
    if (!f) return;

    /* Release referenced objects */
    if (f->env) dec_ref(f->env);

    switch (f->tag) {
        case FRAME_APP_FN:
        case FRAME_APP_ARGS:
        case FRAME_APP_DONE:
            if (f->app.fn) dec_ref(f->app.fn);
            if (f->app.args_done) dec_ref(f->app.args_done);
            if (f->app.args_todo) dec_ref(f->app.args_todo);
            break;

        case FRAME_IF_TEST:
            if (f->if_test.then_branch) dec_ref(f->if_test.then_branch);
            if (f->if_test.else_branch) dec_ref(f->if_test.else_branch);
            break;

        case FRAME_LET_BIND:
        case FRAME_LET_BODY:
            if (f->let.var) dec_ref(f->let.var);
            if (f->let.body) dec_ref(f->let.body);
            if (f->let.bindings_todo) dec_ref(f->let.bindings_todo);
            break;

        case FRAME_SEQ:
            if (f->seq.remaining) dec_ref(f->seq.remaining);
            break;

        case FRAME_PROMPT:
            if (f->prompt.result) dec_ref(f->prompt.result);
            if (f->prompt.handler) dec_ref(f->prompt.handler);
            break;

        default:
            break;
    }

    Scheduler* sched = scheduler_current();
    if (sched) {
        frame_pool_free(sched, f);
    } else {
        free(f);
    }
}

Frame* frame_clone(Frame* f) {
    if (!f) return NULL;

    Frame* clone = frame_alloc(f->tag);
    if (!clone) return NULL;

    /* Shallow copy */
    memcpy(clone, f, sizeof(Frame));
    clone->refcount = 1;
    clone->prev = NULL;

    /* Increment refs on contained objects */
    if (clone->env) inc_ref(clone->env);

    switch (clone->tag) {
        case FRAME_APP_FN:
        case FRAME_APP_ARGS:
        case FRAME_APP_DONE:
            if (clone->app.fn) inc_ref(clone->app.fn);
            if (clone->app.args_done) inc_ref(clone->app.args_done);
            if (clone->app.args_todo) inc_ref(clone->app.args_todo);
            break;

        case FRAME_IF_TEST:
            if (clone->if_test.then_branch) inc_ref(clone->if_test.then_branch);
            if (clone->if_test.else_branch) inc_ref(clone->if_test.else_branch);
            break;

        case FRAME_LET_BIND:
        case FRAME_LET_BODY:
            if (clone->let.var) inc_ref(clone->let.var);
            if (clone->let.body) inc_ref(clone->let.body);
            if (clone->let.bindings_todo) inc_ref(clone->let.bindings_todo);
            break;

        case FRAME_SEQ:
            if (clone->seq.remaining) inc_ref(clone->seq.remaining);
            break;

        case FRAME_PROMPT:
            if (clone->prompt.result) inc_ref(clone->prompt.result);
            if (clone->prompt.handler) inc_ref(clone->prompt.handler);
            clone->prompt.cloned = true;
            break;

        default:
            break;
    }

    return clone;
}

void frame_inc_ref(Frame* f) {
    if (f) f->refcount++;
}

void frame_dec_ref(Frame* f) {
    if (!f) return;
    if (--f->refcount == 0) {
        /* Recursively release chain */
        Frame* prev = f->prev;
        frame_free(f);
        if (prev) frame_dec_ref(prev);
    }
}

/* ========== Continuation Management ========== */

static Continuation* continuation_alloc(void) {
    Continuation* k = malloc(sizeof(Continuation));
    if (!k) return NULL;
    memset(k, 0, sizeof(Continuation));
    k->refcount = 1;
    return k;
}

Continuation* cont_capture(uint32_t tag) {
    Continuation* k = continuation_alloc();
    if (!k) return NULL;

    k->prompt_tag = tag;
    k->one_shot = false;
    k->invoked = false;

    /* Walk up frame chain, cloning frames until we hit matching prompt */
    Frame* captured_head = NULL;
    Frame* captured_tail = NULL;
    Frame* current = tl_current_frame;

    while (current) {
        if (current->tag == FRAME_PROMPT &&
            current->prompt.tag == tag) {
            /* Found the delimiter */
            k->prompt_env = current->env;
            if (k->prompt_env) inc_ref(k->prompt_env);
            break;
        }

        /* Clone this frame */
        Frame* clone = frame_clone(current);
        if (!clone) {
            /* Cleanup on failure */
            frame_dec_ref(captured_head);
            free(k);
            return NULL;
        }

        /* Add to captured chain */
        if (!captured_head) {
            captured_head = clone;
            captured_tail = clone;
        } else {
            captured_tail->prev = clone;
            captured_tail = clone;
        }

        current = current->prev;
    }

    if (!current) {
        /* No matching prompt found */
        frame_dec_ref(captured_head);
        free(k);
        return NULL;
    }

    k->frames = captured_head;
    return k;
}

Obj* cont_invoke(Continuation* k, Obj* value) {
    if (!k) return NULL;

    if (k->one_shot && k->invoked) {
        /* One-shot continuation already used */
        return NULL;  /* Should return error */
    }
    k->invoked = true;

    /* Clone frames if multi-shot (refcount > 1) */
    Frame* frames_to_install;
    if (k->refcount > 1 && !k->one_shot) {
        /* Need to clone the frame chain */
        Frame* cloned_head = NULL;
        Frame* cloned_tail = NULL;
        Frame* src = k->frames;

        while (src) {
            Frame* clone = frame_clone(src);
            if (!clone) {
                frame_dec_ref(cloned_head);
                return NULL;
            }
            if (!cloned_head) {
                cloned_head = clone;
                cloned_tail = clone;
            } else {
                cloned_tail->prev = clone;
                cloned_tail = clone;
            }
            src = src->prev;
        }
        frames_to_install = cloned_head;
    } else {
        /* Can use frames directly */
        frames_to_install = k->frames;
        frame_inc_ref(frames_to_install);
    }

    /* Find the bottom of installed frames and link to current prompt */
    Frame* bottom = frames_to_install;
    while (bottom && bottom->prev) {
        bottom = bottom->prev;
    }

    /* Find matching prompt in current stack */
    Frame* prompt_frame = tl_current_frame;
    while (prompt_frame) {
        if (prompt_frame->tag == FRAME_PROMPT &&
            prompt_frame->prompt.tag == k->prompt_tag) {
            break;
        }
        prompt_frame = prompt_frame->prev;
    }

    if (prompt_frame && bottom) {
        bottom->prev = prompt_frame;
        frame_inc_ref(prompt_frame);
    }

    /* Install new continuation */
    tl_current_frame = frames_to_install;

    /* Return value will be the result of the computation */
    return value;
}

bool cont_is_valid(Continuation* k) {
    if (!k) return false;
    if (k->one_shot && k->invoked) return false;
    return k->frames != NULL;
}

void cont_release(Continuation* k) {
    if (!k) return;
    if (--k->refcount == 0) {
        frame_dec_ref(k->frames);
        if (k->prompt_env) dec_ref(k->prompt_env);
        free(k);
    }
}

/* ========== Prompt Operations ========== */

Obj* cont_prompt(uint32_t tag, Obj* body, Obj* env, Obj* handler) {
    /* Push prompt frame */
    Frame* f = frame_alloc(FRAME_PROMPT);
    if (!f) return NULL;

    f->prompt.tag = tag;
    f->prompt.result = NULL;
    f->prompt.handler = handler;
    if (handler) inc_ref(handler);
    f->env = env;
    if (env) inc_ref(env);
    f->prev = tl_current_frame;
    if (tl_current_frame) frame_inc_ref(tl_current_frame);

    tl_current_frame = f;

    /* Set up escape point */
    if (setjmp(f->prompt.escape) != 0) {
        /* Returned via control */
        Obj* result = f->prompt.result;
        f->prompt.result = NULL;

        /* Pop prompt frame */
        Frame* old = tl_current_frame;
        tl_current_frame = old->prev;
        if (tl_current_frame) frame_inc_ref(tl_current_frame);
        frame_dec_ref(old);

        return result;
    }

    /* Evaluate body (caller handles this) */
    return body;
}

uint32_t prompt_tag_generate(void) {
    return tl_next_prompt_tag++;
}

/* ========== Scheduler ========== */

void scheduler_init(void) {
    if (tl_scheduler) return;

    tl_scheduler = malloc(sizeof(Scheduler));
    if (!tl_scheduler) return;

    memset(tl_scheduler, 0, sizeof(Scheduler));
    tl_scheduler->next_fiber_id = 1;

    /* Pre-allocate frame pool */
    for (int i = 0; i < FRAME_POOL_INITIAL_SIZE; i++) {
        Frame* f = malloc(sizeof(Frame));
        if (f) {
            f->prev = tl_scheduler->frame_freelist;
            tl_scheduler->frame_freelist = f;
            tl_scheduler->frame_pool_size++;
        }
    }
}

void scheduler_shutdown(void) {
    if (!tl_scheduler) return;

    /* Free remaining tasks */
    Fiber* t = tl_scheduler->ready_head;
    while (t) {
        Fiber* next = t->next;
        frame_dec_ref(t->cont);
        if (t->value) dec_ref(t->value);
        if (t->result) dec_ref(t->result);
        free(t);
        t = next;
    }

    /* Free frame pool */
    Frame* f = tl_scheduler->frame_freelist;
    while (f) {
        Frame* next = f->prev;
        free(f);
        f = next;
    }

    free(tl_scheduler);
    tl_scheduler = NULL;
}

Scheduler* scheduler_current(void) {
    return tl_scheduler;
}

static void scheduler_enqueue(Fiber* task) {
    Scheduler* sched = tl_scheduler;
    if (!sched) return;

    task->state = FIBER_READY;
    task->next = NULL;
    task->prev = sched->ready_tail;

    if (sched->ready_tail) {
        sched->ready_tail->next = task;
    } else {
        sched->ready_head = task;
    }
    sched->ready_tail = task;
}

static Fiber* scheduler_dequeue(void) {
    Scheduler* sched = tl_scheduler;
    if (!sched || !sched->ready_head) return NULL;

    Fiber* task = sched->ready_head;
    sched->ready_head = task->next;
    if (sched->ready_head) {
        sched->ready_head->prev = NULL;
    } else {
        sched->ready_tail = NULL;
    }

    task->next = NULL;
    task->prev = NULL;
    task->state = FIBER_RUNNING;
    return task;
}

static void scheduler_remove(Fiber* task) {
    Scheduler* sched = tl_scheduler;
    if (!sched) return;

    if (task->prev) {
        task->prev->next = task->next;
    } else {
        sched->ready_head = task->next;
    }

    if (task->next) {
        task->next->prev = task->prev;
    } else {
        sched->ready_tail = task->prev;
    }

    task->next = NULL;
    task->prev = NULL;
}

void scheduler_run(void) {
    Scheduler* sched = tl_scheduler;
    if (!sched) return;

    while (sched->ready_head) {
        if (!scheduler_step()) break;
    }
}

bool scheduler_step(void) {
    Scheduler* sched = tl_scheduler;
    if (!sched) return false;

    Fiber* task = scheduler_dequeue();
    if (!task) return false;

    sched->current = task;
    sched->context_switches++;

    /* Restore task's continuation state */
    Frame* saved_frame = tl_current_frame;
    Obj* saved_env = tl_current_env;

    tl_current_frame = task->cont;
    tl_current_env = task->env;

    /* Resume task with value */
    Obj* result = task->value;

    /* TODO: Actually execute the task
     * This would integrate with the evaluator:
     * result = eval_step(task->expr, task->env, task->cont, task->value);
     */

    /* For now, mark task as done */
    task->state = FIBER_DONE;
    task->result = result;

    /* Restore previous state */
    tl_current_frame = saved_frame;
    tl_current_env = saved_env;
    sched->current = NULL;

    /* Complete the task */
    if (task->completion) {
        promise_resolve(task->completion, result);
    }

    sched->fibers_completed++;
    sched->fiber_count--;

    /* Free task */
    frame_dec_ref(task->cont);
    free(task);

    return true;
}

bool scheduler_is_idle(void) {
    Scheduler* sched = tl_scheduler;
    return !sched || !sched->ready_head;
}

/* ========== Green Threads ========== */

Fiber* fiber_spawn(Obj* thunk) {
    Scheduler* sched = tl_scheduler;
    if (!sched) {
        scheduler_init();
        sched = tl_scheduler;
    }
    if (!sched || !thunk) return NULL;

    Fiber* task = malloc(sizeof(Fiber));
    if (!task) return NULL;

    memset(task, 0, sizeof(Fiber));
    task->id = sched->next_fiber_id++;
    task->state = FIBER_READY;
    task->expr = thunk;
    if (thunk) inc_ref(thunk);

    sched->fibers_created++;
    sched->fiber_count++;

    scheduler_enqueue(task);
    return task;
}

Promise* fiber_spawn_async(Obj* thunk) {
    Promise* p = promise_create();
    if (!p) return NULL;

    Fiber* task = fiber_spawn(thunk);
    if (!task) {
        promise_release(p);
        return NULL;
    }

    task->completion = p;
    p->refcount++;  /* Task holds reference */
    return p;
}

void fiber_yield(void) {
    Scheduler* sched = tl_scheduler;
    if (!sched || !sched->current) return;

    Fiber* current = sched->current;

    /* Save current state */
    current->cont = tl_current_frame;
    if (tl_current_frame) frame_inc_ref(tl_current_frame);
    current->env = tl_current_env;
    if (tl_current_env) inc_ref(tl_current_env);

    /* Re-enqueue at end */
    scheduler_enqueue(current);
    sched->current = NULL;

    /* Context switch happens in scheduler_step return */
}

Fiber* fiber_current(void) {
    Scheduler* sched = tl_scheduler;
    return sched ? sched->current : NULL;
}

void fiber_park(void* reason, Obj* value) {
    Scheduler* sched = tl_scheduler;
    if (!sched || !sched->current) return;

    Fiber* current = sched->current;

    /* Save state */
    current->cont = tl_current_frame;
    if (tl_current_frame) frame_inc_ref(tl_current_frame);
    current->env = tl_current_env;
    if (tl_current_env) inc_ref(tl_current_env);

    current->state = FIBER_PARKED;
    current->park_reason = reason;
    current->park_value = value;
    if (value) inc_ref(value);

    sched->current = NULL;
}

void fiber_unpark(Fiber* t, Obj* value) {
    if (!t || t->state != FIBER_PARKED) return;

    t->value = value;
    if (value) inc_ref(value);
    t->park_reason = NULL;
    if (t->park_value) {
        dec_ref(t->park_value);
        t->park_value = NULL;
    }

    scheduler_enqueue(t);
}

/* ========== Green Channels ========== */

FiberChannel* fiber_channel_create(int capacity) {
    FiberChannel* ch = malloc(sizeof(FiberChannel));
    if (!ch) return NULL;

    memset(ch, 0, sizeof(FiberChannel));
    ch->capacity = capacity;
    ch->refcount = 1;

    if (capacity > 0) {
        ch->buffer = calloc(capacity, sizeof(Obj*));
        if (!ch->buffer) {
            free(ch);
            return NULL;
        }
    }

    return ch;
}

void fiber_channel_send(FiberChannel* ch, Obj* value) {
    if (!ch || ch->closed) return;

    /* Check for waiting receiver */
    if (ch->recv_waiters) {
        Fiber* receiver = ch->recv_waiters;
        ch->recv_waiters = receiver->next;
        if (ch->recv_waiters) {
            ch->recv_waiters->prev = NULL;
        }
        receiver->next = NULL;

        /* Hand off value directly */
        fiber_unpark(receiver, value);
        return;
    }

    /* Try to buffer */
    if (ch->capacity > 0 && ch->count < ch->capacity) {
        ch->buffer[ch->tail] = value;
        if (value) inc_ref(value);
        ch->tail = (ch->tail + 1) % ch->capacity;
        ch->count++;
        return;
    }

    /* Must wait - park sender */
    Fiber* current = fiber_current();
    if (!current) return;  /* Not in scheduler context */

    /* Add to send waiters */
    current->next = ch->send_waiters;
    if (ch->send_waiters) {
        ch->send_waiters->prev = current;
    }
    ch->send_waiters = current;

    fiber_park(ch, value);
}

Obj* fiber_channel_recv(FiberChannel* ch) {
    if (!ch) return NULL;

    /* Check buffer first */
    if (ch->count > 0) {
        Obj* value = ch->buffer[ch->head];
        ch->buffer[ch->head] = NULL;
        ch->head = (ch->head + 1) % ch->capacity;
        ch->count--;

        /* Wake a waiting sender if any */
        if (ch->send_waiters) {
            Fiber* sender = ch->send_waiters;
            ch->send_waiters = sender->next;
            if (ch->send_waiters) {
                ch->send_waiters->prev = NULL;
            }
            sender->next = NULL;

            /* Buffer their value */
            Obj* send_val = sender->park_value;
            sender->park_value = NULL;

            ch->buffer[ch->tail] = send_val;
            ch->tail = (ch->tail + 1) % ch->capacity;
            ch->count++;

            fiber_unpark(sender, NULL);
        }

        return value;
    }

    /* Check for waiting sender (unbuffered or after buffer empty) */
    if (ch->send_waiters) {
        Fiber* sender = ch->send_waiters;
        ch->send_waiters = sender->next;
        if (ch->send_waiters) {
            ch->send_waiters->prev = NULL;
        }
        sender->next = NULL;

        Obj* value = sender->park_value;
        sender->park_value = NULL;

        fiber_unpark(sender, NULL);
        return value;
    }

    /* Channel closed and empty */
    if (ch->closed) {
        return NULL;
    }

    /* Must wait - park receiver */
    Fiber* current = fiber_current();
    if (!current) return NULL;

    current->next = ch->recv_waiters;
    if (ch->recv_waiters) {
        ch->recv_waiters->prev = current;
    }
    ch->recv_waiters = current;

    fiber_park(ch, NULL);
    return NULL;  /* Value will be set when unparked */
}

bool fiber_channel_try_send(FiberChannel* ch, Obj* value) {
    if (!ch || ch->closed) return false;

    /* Receiver waiting? */
    if (ch->recv_waiters) {
        Fiber* receiver = ch->recv_waiters;
        ch->recv_waiters = receiver->next;
        if (ch->recv_waiters) ch->recv_waiters->prev = NULL;
        receiver->next = NULL;
        fiber_unpark(receiver, value);
        return true;
    }

    /* Buffer space? */
    if (ch->capacity > 0 && ch->count < ch->capacity) {
        ch->buffer[ch->tail] = value;
        if (value) inc_ref(value);
        ch->tail = (ch->tail + 1) % ch->capacity;
        ch->count++;
        return true;
    }

    return false;
}

Obj* fiber_channel_try_recv(FiberChannel* ch, bool* ok) {
    *ok = false;
    if (!ch) return NULL;

    /* Buffer has data? */
    if (ch->count > 0) {
        Obj* value = ch->buffer[ch->head];
        ch->buffer[ch->head] = NULL;
        ch->head = (ch->head + 1) % ch->capacity;
        ch->count--;
        *ok = true;

        /* Wake sender if waiting */
        if (ch->send_waiters) {
            Fiber* sender = ch->send_waiters;
            ch->send_waiters = sender->next;
            if (ch->send_waiters) ch->send_waiters->prev = NULL;
            sender->next = NULL;

            Obj* send_val = sender->park_value;
            sender->park_value = NULL;

            ch->buffer[ch->tail] = send_val;
            ch->tail = (ch->tail + 1) % ch->capacity;
            ch->count++;

            fiber_unpark(sender, NULL);
        }

        return value;
    }

    /* Sender waiting? */
    if (ch->send_waiters) {
        Fiber* sender = ch->send_waiters;
        ch->send_waiters = sender->next;
        if (ch->send_waiters) ch->send_waiters->prev = NULL;
        sender->next = NULL;

        Obj* value = sender->park_value;
        sender->park_value = NULL;
        fiber_unpark(sender, NULL);
        *ok = true;
        return value;
    }

    return NULL;
}

void fiber_channel_close(FiberChannel* ch) {
    if (!ch || ch->closed) return;
    ch->closed = true;

    /* Wake all waiting receivers with NULL */
    Fiber* t = ch->recv_waiters;
    while (t) {
        Fiber* next = t->next;
        fiber_unpark(t, NULL);
        t = next;
    }
    ch->recv_waiters = NULL;

    /* Wake all waiting senders (they'll see closed) */
    t = ch->send_waiters;
    while (t) {
        Fiber* next = t->next;
        if (t->park_value) {
            dec_ref(t->park_value);
            t->park_value = NULL;
        }
        fiber_unpark(t, NULL);
        t = next;
    }
    ch->send_waiters = NULL;
}

bool fiber_channel_is_closed(FiberChannel* ch) {
    return ch ? ch->closed : true;
}

void fiber_channel_release(FiberChannel* ch) {
    if (!ch) return;
    if (--ch->refcount > 0) return;

    fiber_channel_close(ch);

    /* Free buffered values */
    while (ch->count > 0) {
        Obj* val = ch->buffer[ch->head];
        if (val) dec_ref(val);
        ch->head = (ch->head + 1) % ch->capacity;
        ch->count--;
    }

    free(ch->buffer);
    free(ch);
}

/* ========== Generators (Iterators) ========== */

Generator* generator_create(Obj* producer) {
    Generator* g = malloc(sizeof(Generator));
    if (!g) return NULL;

    memset(g, 0, sizeof(Generator));
    g->state = GEN_CREATED;
    g->producer = producer;
    if (producer) inc_ref(producer);
    g->refcount = 1;

    return g;
}

Obj* generator_next(Generator* g) {
    if (!g) return NULL;

    switch (g->state) {
        case GEN_DONE:
            return NULL;

        case GEN_CREATED: {
            /* First call - start the producer */
            g->state = GEN_RUNNING;

            /* Create a prompt for yields */
            uint32_t tag = PROMPT_TAG_GENERATOR;

            /* Push yield frame so generator_yield knows where we are */
            Frame* f = frame_alloc(FRAME_YIELD);
            if (!f) return NULL;
            f->yield.gen = g;
            f->prev = tl_current_frame;
            if (tl_current_frame) frame_inc_ref(tl_current_frame);
            tl_current_frame = f;

            /* Call producer - it will call yield internally */
            Obj* nil = NULL;
            Obj* result = call_closure(g->producer, &nil, 0);

            /* Producer finished without more yields */
            g->state = GEN_DONE;
            g->current = result;

            /* Pop yield frame */
            Frame* old = tl_current_frame;
            tl_current_frame = old->prev;
            frame_dec_ref(old);

            return result;
        }

        case GEN_SUSPENDED: {
            /* Resume from yield */
            g->state = GEN_RUNNING;

            if (g->cont) {
                Obj* result = cont_invoke(g->cont, NULL);
                cont_release(g->cont);
                g->cont = NULL;

                /* Check if done or yielded again */
                if (g->state == GEN_SUSPENDED) {
                    return g->current;
                } else {
                    g->state = GEN_DONE;
                    g->current = result;
                    return result;
                }
            }
            return NULL;
        }

        case GEN_RUNNING:
            /* Recursive call - error */
            return NULL;
    }

    return NULL;
}

bool generator_is_done(Generator* g) {
    return !g || g->state == GEN_DONE;
}

void generator_yield(Obj* value) {
    /* Find the yield frame */
    Frame* f = tl_current_frame;
    while (f && f->tag != FRAME_YIELD) {
        f = f->prev;
    }
    if (!f) return;  /* Not in generator context */

    Generator* g = f->yield.gen;
    if (!g || g->state != GEN_RUNNING) return;

    /* Save current value */
    g->current = value;
    if (value) inc_ref(value);

    /* Capture continuation for resume */
    g->cont = cont_capture(PROMPT_TAG_GENERATOR);
    g->state = GEN_SUSPENDED;

    /* Return to generator_next via the prompt's setjmp */
    Frame* prompt = f;
    while (prompt && !(prompt->tag == FRAME_PROMPT &&
                       prompt->prompt.tag == PROMPT_TAG_GENERATOR)) {
        prompt = prompt->prev;
    }

    if (prompt) {
        if (prompt->prompt.cloned) {
            fprintf(stderr, "FATAL: Attempted to yield to cloned prompt! (jmp_buf invalid)\n");
            abort();
        }
        prompt->prompt.result = value;
        longjmp(prompt->prompt.escape, 1);
    }
}

void generator_close(Generator* g) {
    if (!g) return;
    g->state = GEN_DONE;
    if (g->cont) {
        cont_release(g->cont);
        g->cont = NULL;
    }
}

void generator_release(Generator* g) {
    if (!g) return;
    if (--g->refcount > 0) return;

    if (g->producer) dec_ref(g->producer);
    if (g->current) dec_ref(g->current);
    if (g->cont) cont_release(g->cont);
    free(g);
}

/* ========== Promises (Async/Await) ========== */

Promise* promise_create(void) {
    Promise* p = malloc(sizeof(Promise));
    if (!p) return NULL;

    memset(p, 0, sizeof(Promise));
    p->state = PROMISE_PENDING;
    p->refcount = 1;

    return p;
}

void promise_resolve(Promise* p, Obj* value) {
    if (!p || p->state != PROMISE_PENDING) return;

    p->state = PROMISE_FULFILLED;
    p->value = value;
    if (value) inc_ref(value);

    /* Wake all waiting tasks */
    Fiber* t = p->waiters;
    while (t) {
        Fiber* next = t->next;
        fiber_unpark(t, value);
        t = next;
    }
    p->waiters = NULL;

    /* Call callbacks */
    /* TODO: Execute on_fulfill callbacks */
}

void promise_reject(Promise* p, Obj* error) {
    if (!p || p->state != PROMISE_PENDING) return;

    p->state = PROMISE_REJECTED;
    p->error = error;
    if (error) inc_ref(error);

    /* Wake all waiting tasks with error */
    Fiber* t = p->waiters;
    while (t) {
        Fiber* next = t->next;
        fiber_unpark(t, error);  /* TODO: Distinguish error from value */
        t = next;
    }
    p->waiters = NULL;

    /* Call reject callbacks */
    /* TODO: Execute on_reject callbacks */
}

Obj* promise_await(Promise* p) {
    if (!p) return NULL;

    /* Already settled? */
    if (p->state == PROMISE_FULFILLED) {
        return p->value;
    }
    if (p->state == PROMISE_REJECTED) {
        return p->error;  /* TODO: Proper error handling */
    }

    /* Must wait */
    Fiber* current = fiber_current();
    if (!current) return NULL;

    current->next = p->waiters;
    if (p->waiters) p->waiters->prev = current;
    p->waiters = current;

    fiber_park(p, NULL);
    return NULL;  /* Value set when unparked */
}

void promise_then(Promise* p, Obj* on_fulfill, Obj* on_reject) {
    if (!p) return;

    /* Already settled? Call immediately */
    if (p->state == PROMISE_FULFILLED && on_fulfill) {
        Obj* args[1] = { p->value };
        call_closure(on_fulfill, args, 1);
        return;
    }
    if (p->state == PROMISE_REJECTED && on_reject) {
        Obj* args[1] = { p->error };
        call_closure(on_reject, args, 1);
        return;
    }

    /* Still pending - save callbacks */
    /* TODO: Store callbacks in list */
}

bool promise_is_settled(Promise* p) {
    return p && p->state != PROMISE_PENDING;
}

PromiseState promise_state(Promise* p) {
    return p ? p->state : PROMISE_REJECTED;
}

void promise_release(Promise* p) {
    if (!p) return;
    if (--p->refcount > 0) return;

    if (p->value) dec_ref(p->value);
    if (p->error) dec_ref(p->error);
    if (p->on_fulfill) dec_ref(p->on_fulfill);
    if (p->on_reject) dec_ref(p->on_reject);

    /* Cancel waiting tasks */
    Fiber* t = p->waiters;
    while (t) {
        Fiber* next = t->next;
        t->state = FIBER_DONE;  /* TODO: Better cancellation */
        t = next;
    }

    free(p);
}

/* ========== Select (Multi-Wait) ========== */

int fiber_select(SelectCase* cases, int count) {
    if (!cases || count <= 0) return -1;

    /* First pass: check for immediately ready case */
    for (int i = 0; i < count; i++) {
        SelectCase* c = &cases[i];

        if (c->op == SELECT_DEFAULT) {
            continue;  /* Check default last */
        }

        if (c->op == SELECT_RECV) {
            bool ok;
            Obj* val = fiber_channel_try_recv(c->channel, &ok);
            if (ok) {
                if (c->result) *c->result = val;
                return i;
            }
        } else if (c->op == SELECT_SEND) {
            if (fiber_channel_try_send(c->channel, c->value)) {
                return i;
            }
        }
    }

    /* Check for default case */
    for (int i = 0; i < count; i++) {
        if (cases[i].op == SELECT_DEFAULT) {
            return i;
        }
    }

    /* Must block - register on all channels */
    Fiber* current = fiber_current();
    if (!current) return -1;

    /* TODO: Register on all channels and wait */
    /* This is complex - need to track which channel woke us */

    return -1;
}

/* ========== Timer ========== */

Promise* timer_after(uint64_t ms) {
    Promise* p = promise_create();
    if (!p) return NULL;

    /* TODO: Register with event loop */
    /* For now, immediately resolve (placeholder) */
    promise_resolve(p, NULL);

    return p;
}

Obj* await_timeout(Promise* p, uint64_t ms) {
    if (!p) return NULL;

    if (promise_is_settled(p)) {
        return p->value;
    }

    /* TODO: Race between promise and timer */
    return promise_await(p);
}

/* ========== Thread-Local Accessors ========== */

Obj* cont_get_env(void) {
    return tl_current_env;
}

void cont_set_env(Obj* env) {
    if (tl_current_env) dec_ref(tl_current_env);
    tl_current_env = env;
    if (env) inc_ref(env);
}

Frame* cont_get_current(void) {
    return tl_current_frame;
}

void cont_set_current(Frame* f) {
    if (tl_current_frame) frame_dec_ref(tl_current_frame);
    tl_current_frame = f;
    if (f) frame_inc_ref(f);
}

/* ========== Thread Pool Implementation ========== */

#include <unistd.h>
#include <sched.h>

/* Global thread pool */
static ThreadPool* g_pool = NULL;

/* Thread-local worker reference */
static _Thread_local WorkerThread* tl_worker = NULL;

/* ========== Work-Stealing Deque ========== */

void deque_init(WorkStealDeque* d) {
    d->buffer = calloc(DEQUE_INITIAL_SIZE, sizeof(Fiber*));
    d->top = 0;
    d->bottom = 0;
    d->capacity = DEQUE_INITIAL_SIZE;
    pthread_mutex_init(&d->grow_lock, NULL);
}

void deque_destroy(WorkStealDeque* d) {
    pthread_mutex_destroy(&d->grow_lock);
    free(d->buffer);
    d->buffer = NULL;
    d->capacity = 0;
}

static void deque_grow(WorkStealDeque* d) {
    pthread_mutex_lock(&d->grow_lock);

    size_t new_capacity = d->capacity * 2;
    Fiber** new_buffer = calloc(new_capacity, sizeof(Fiber*));

    /* Copy existing elements */
    int64_t size = d->bottom - d->top;
    for (int64_t i = 0; i < size; i++) {
        new_buffer[i] = d->buffer[(d->top + i) % d->capacity];
    }

    free(d->buffer);
    d->buffer = new_buffer;
    d->top = 0;
    d->bottom = size;
    d->capacity = new_capacity;

    pthread_mutex_unlock(&d->grow_lock);
}

void deque_push(WorkStealDeque* d, Fiber* f) {
    int64_t bottom = d->bottom;
    int64_t top = d->top;
    int64_t size = bottom - top;

    /* Grow if needed */
    if (size >= (int64_t)(d->capacity - 1)) {
        deque_grow(d);
        bottom = d->bottom;
    }

    d->buffer[bottom % d->capacity] = f;
    __sync_synchronize();  /* Memory fence */
    d->bottom = bottom + 1;
}

Fiber* deque_pop(WorkStealDeque* d) {
    int64_t bottom = d->bottom - 1;
    d->bottom = bottom;
    __sync_synchronize();  /* Memory fence */

    int64_t top = d->top;
    int64_t size = bottom - top;

    if (size < 0) {
        /* Empty */
        d->bottom = top;
        return NULL;
    }

    Fiber* f = d->buffer[bottom % d->capacity];

    if (size > 0) {
        /* More than one element, safe to return */
        return f;
    }

    /* Last element, race with stealer */
    if (!__sync_bool_compare_and_swap(&d->top, top, top + 1)) {
        /* Stealer won */
        f = NULL;
    }
    d->bottom = top + 1;
    return f;
}

Fiber* deque_steal(WorkStealDeque* d) {
    int64_t top = d->top;
    __sync_synchronize();  /* Memory fence */
    int64_t bottom = d->bottom;

    int64_t size = bottom - top;
    if (size <= 0) {
        return NULL;
    }

    Fiber* f = d->buffer[top % d->capacity];

    if (!__sync_bool_compare_and_swap(&d->top, top, top + 1)) {
        /* Another thief got it */
        return NULL;
    }

    return f;
}

bool deque_is_empty(WorkStealDeque* d) {
    int64_t top = d->top;
    int64_t bottom = d->bottom;
    return bottom <= top;
}

/* ========== Worker Thread ========== */

static Fiber* worker_find_work(WorkerThread* w) {
    ThreadPool* pool = w->pool;

    /* First try local queue */
    Fiber* f = deque_pop(&w->local_queue);
    if (f) return f;

    /* Try global queue */
    pthread_mutex_lock(&pool->global_lock);
    if (pool->global_head) {
        f = pool->global_head;
        pool->global_head = f->next;
        if (!pool->global_head) {
            pool->global_tail = NULL;
        }
        f->next = NULL;
        f->prev = NULL;
    }
    pthread_mutex_unlock(&pool->global_lock);
    if (f) return f;

    /* Try to steal from other workers */
    w->state = WORKER_STEALING;
    uint32_t start = (w->id + 1) % pool->num_workers;
    for (uint32_t i = 0; i < pool->num_workers - 1; i++) {
        uint32_t victim_id = (start + i) % pool->num_workers;
        if (victim_id == w->id) continue;

        WorkerThread* victim = &pool->workers[victim_id];
        w->steal_attempts++;

        f = deque_steal(&victim->local_queue);
        if (f) {
            w->tasks_stolen++;
            __sync_fetch_and_add(&pool->total_steals, 1);
            return f;
        }
    }

    return NULL;
}

static void worker_execute_fiber(WorkerThread* w, Fiber* f) {
    w->state = WORKER_RUNNING;

    /* Set up scheduler context */
    Scheduler* sched = w->scheduler;
    sched->current = f;
    f->state = FIBER_RUNNING;
    sched->context_switches++;

    /* Save current state */
    Frame* saved_frame = tl_current_frame;
    Obj* saved_env = tl_current_env;

    tl_current_frame = f->cont;
    tl_current_env = f->env;

    /* Execute fiber (placeholder - real impl would call evaluator) */
    Obj* result = f->value;

    /* For now mark as done */
    f->state = FIBER_DONE;
    f->result = result;

    /* Restore state */
    tl_current_frame = saved_frame;
    tl_current_env = saved_env;
    sched->current = NULL;

    /* Complete fiber */
    if (f->completion) {
        promise_resolve(f->completion, result);
    }

    sched->fibers_completed++;
    sched->fiber_count--;
    w->tasks_executed++;
    __sync_fetch_and_add(&w->pool->total_tasks_completed, 1);

    /* Free fiber */
    frame_dec_ref(f->cont);
    free(f);
}

static void* worker_thread_main(void* arg) {
    WorkerThread* w = (WorkerThread*)arg;
    tl_worker = w;

    /* Initialize thread-local scheduler */
    scheduler_init();
    w->scheduler = tl_scheduler;

    while (!w->pool->shutdown_requested) {
        Fiber* f = worker_find_work(w);

        if (f) {
            worker_execute_fiber(w, f);
        } else {
            /* No work available, wait */
            w->state = WORKER_IDLE;
            pthread_mutex_lock(&w->pool->work_mutex);
            if (!w->pool->shutdown_requested && deque_is_empty(&w->local_queue)) {
                /* Wait with timeout to check for shutdown periodically */
                struct timespec ts;
                clock_gettime(CLOCK_REALTIME, &ts);
                ts.tv_nsec += 10000000;  /* 10ms timeout */
                if (ts.tv_nsec >= 1000000000) {
                    ts.tv_sec++;
                    ts.tv_nsec -= 1000000000;
                }
                pthread_cond_timedwait(&w->pool->work_available, &w->pool->work_mutex, &ts);
            }
            pthread_mutex_unlock(&w->pool->work_mutex);
        }
    }

    w->state = WORKER_SHUTDOWN;
    scheduler_shutdown();
    return NULL;
}

/* ========== Thread Pool API ========== */

void fiber_pool_init(int num_workers) {
    if (g_pool) return;  /* Already initialized */

    /* Auto-detect CPU count */
    if (num_workers <= 0) {
        num_workers = (int)sysconf(_SC_NPROCESSORS_ONLN);
        if (num_workers <= 0) num_workers = 4;
    }

    g_pool = malloc(sizeof(ThreadPool));
    memset(g_pool, 0, sizeof(ThreadPool));

    g_pool->num_workers = num_workers;
    g_pool->workers = calloc(num_workers, sizeof(WorkerThread));
    g_pool->running = true;
    g_pool->shutdown_requested = false;

    pthread_mutex_init(&g_pool->global_lock, NULL);
    pthread_mutex_init(&g_pool->work_mutex, NULL);
    pthread_cond_init(&g_pool->work_available, NULL);

    /* Initialize main thread as worker 0 */
    WorkerThread* main_worker = &g_pool->workers[0];
    main_worker->id = 0;
    main_worker->state = WORKER_IDLE;
    main_worker->pool = g_pool;
    deque_init(&main_worker->local_queue);

    /* Initialize scheduler for main thread */
    scheduler_init();
    main_worker->scheduler = tl_scheduler;
    tl_worker = main_worker;

    /* Start worker threads (1 to num_workers-1) */
    for (int i = 1; i < num_workers; i++) {
        WorkerThread* w = &g_pool->workers[i];
        w->id = i;
        w->state = WORKER_IDLE;
        w->pool = g_pool;
        deque_init(&w->local_queue);

        pthread_create(&w->thread, NULL, worker_thread_main, w);
    }
}

void fiber_pool_shutdown(void) {
    if (!g_pool) return;

    g_pool->shutdown_requested = true;
    g_pool->running = false;

    /* Wake up all waiting workers */
    pthread_mutex_lock(&g_pool->work_mutex);
    pthread_cond_broadcast(&g_pool->work_available);
    pthread_mutex_unlock(&g_pool->work_mutex);

    /* Wait for worker threads to finish */
    for (uint32_t i = 1; i < g_pool->num_workers; i++) {
        pthread_join(g_pool->workers[i].thread, NULL);
        deque_destroy(&g_pool->workers[i].local_queue);
    }

    /* Cleanup main worker */
    deque_destroy(&g_pool->workers[0].local_queue);

    /* Cleanup global resources */
    pthread_mutex_destroy(&g_pool->global_lock);
    pthread_mutex_destroy(&g_pool->work_mutex);
    pthread_cond_destroy(&g_pool->work_available);

    free(g_pool->workers);
    free(g_pool);
    g_pool = NULL;
    tl_worker = NULL;
}

ThreadPool* fiber_pool_current(void) {
    return g_pool;
}

int fiber_pool_size(void) {
    return g_pool ? (int)g_pool->num_workers : 0;
}

WorkerThread* fiber_pool_current_worker(void) {
    return tl_worker;
}

Fiber* fiber_pool_spawn(Obj* thunk) {
    if (!g_pool || !thunk) return NULL;

    /* Create fiber */
    Fiber* f = malloc(sizeof(Fiber));
    if (!f) return NULL;

    memset(f, 0, sizeof(Fiber));

    WorkerThread* w = tl_worker;
    Scheduler* sched = w ? w->scheduler : tl_scheduler;
    if (!sched) {
        scheduler_init();
        sched = tl_scheduler;
    }

    f->id = sched->next_fiber_id++;
    f->state = FIBER_READY;
    f->expr = thunk;
    if (thunk) inc_ref(thunk);

    sched->fibers_created++;
    sched->fiber_count++;
    __sync_fetch_and_add(&g_pool->total_tasks_spawned, 1);

    /* Push to local queue if we have a worker, else global queue */
    if (w) {
        deque_push(&w->local_queue, f);
    } else {
        pthread_mutex_lock(&g_pool->global_lock);
        f->next = NULL;
        f->prev = g_pool->global_tail;
        if (g_pool->global_tail) {
            g_pool->global_tail->next = f;
        } else {
            g_pool->global_head = f;
        }
        g_pool->global_tail = f;
        pthread_mutex_unlock(&g_pool->global_lock);
    }

    /* Signal workers that work is available */
    pthread_mutex_lock(&g_pool->work_mutex);
    pthread_cond_signal(&g_pool->work_available);
    pthread_mutex_unlock(&g_pool->work_mutex);

    return f;
}
