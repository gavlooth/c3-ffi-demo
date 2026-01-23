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
#include "region_core.h"
#include "transmigrate.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* Forward declaration - defined in runtime.c */
extern void inc_ref(Obj* x);
extern void dec_ref(Obj* x);
extern Obj* call_closure(Obj* clos, Obj** args, int arg_count);

/* ========== Region Stack for Prompt-Region Integration ========== */
/*
 * CTRR Integration: Each prompt creates a child region.
 * Allocations during the delimited computation should go to this region.
 * When the prompt exits, the region is exited.
 * When a continuation is captured, values are transmigrated out.
 */

#define REGION_STACK_MAX 64
static _Thread_local Region* tl_region_stack[REGION_STACK_MAX];
static _Thread_local int tl_region_stack_top = 0;

/* Get current region (top of stack, or global if empty) */
Region* cont_current_region(void) {
    if (tl_region_stack_top > 0) {
        return tl_region_stack[tl_region_stack_top - 1];
    }
    return region_get_or_create();  /* Fall back to global region */
}

/* Push region onto stack */
static void region_stack_push(Region* r) {
    if (tl_region_stack_top < REGION_STACK_MAX) {
        tl_region_stack[tl_region_stack_top++] = r;
    } else {
        fprintf(stderr, "Warning: Region stack overflow\n");
    }
}

/* Pop region from stack */
static Region* region_stack_pop(void) {
    if (tl_region_stack_top > 0) {
        return tl_region_stack[--tl_region_stack_top];
    }
    return NULL;
}

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
            /* Note: region is NOT freed here - it's managed by prompt lifecycle */
            /* When frame is freed due to prompt exit, region is already exited */
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
            /* CTRR: Cloned prompts do NOT own the region - region belongs to original */
            clone->prompt.region = NULL;
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

/*
 * Helper: Transmigrate an Obj* from src_region to dest_region.
 * Returns the new Obj* in dest_region (or original if regions match or NULL).
 */
static Obj* transmigrate_obj(Obj* obj, Region* src_region, Region* dest_region) {
    if (!obj || !src_region || !dest_region) return obj;
    if (src_region == dest_region) return obj;
    return (Obj*)transmigrate(obj, src_region, dest_region);
}

/*
 * Helper: Transmigrate all Obj* in a frame from src to dest region.
 * This ensures captured continuations don't hold references into dead regions.
 */
static void frame_transmigrate_contents(Frame* f, Region* src_region, Region* dest_region) {
    if (!f || !src_region || !dest_region || src_region == dest_region) return;

    /* Transmigrate environment */
    if (f->env) {
        f->env = transmigrate_obj(f->env, src_region, dest_region);
    }

    /* Transmigrate frame-specific contents */
    switch (f->tag) {
        case FRAME_APP_FN:
        case FRAME_APP_ARGS:
        case FRAME_APP_DONE:
            if (f->app.fn)
                f->app.fn = transmigrate_obj(f->app.fn, src_region, dest_region);
            if (f->app.args_done)
                f->app.args_done = transmigrate_obj(f->app.args_done, src_region, dest_region);
            if (f->app.args_todo)
                f->app.args_todo = transmigrate_obj(f->app.args_todo, src_region, dest_region);
            break;

        case FRAME_IF_TEST:
            if (f->if_test.then_branch)
                f->if_test.then_branch = transmigrate_obj(f->if_test.then_branch, src_region, dest_region);
            if (f->if_test.else_branch)
                f->if_test.else_branch = transmigrate_obj(f->if_test.else_branch, src_region, dest_region);
            break;

        case FRAME_LET_BIND:
        case FRAME_LET_BODY:
            if (f->let.var)
                f->let.var = transmigrate_obj(f->let.var, src_region, dest_region);
            if (f->let.body)
                f->let.body = transmigrate_obj(f->let.body, src_region, dest_region);
            if (f->let.bindings_todo)
                f->let.bindings_todo = transmigrate_obj(f->let.bindings_todo, src_region, dest_region);
            break;

        case FRAME_SEQ:
            if (f->seq.remaining)
                f->seq.remaining = transmigrate_obj(f->seq.remaining, src_region, dest_region);
            break;

        case FRAME_PROMPT:
            if (f->prompt.result)
                f->prompt.result = transmigrate_obj(f->prompt.result, src_region, dest_region);
            if (f->prompt.handler)
                f->prompt.handler = transmigrate_obj(f->prompt.handler, src_region, dest_region);
            break;

        default:
            break;
    }
}

Continuation* cont_capture(uint32_t tag) {
    Continuation* k = continuation_alloc();
    if (!k) return NULL;

    k->prompt_tag = tag;
    k->one_shot = false;
    k->invoked = false;
    k->captured_from = NULL;

    /* Walk up frame chain, cloning frames until we hit matching prompt */
    Frame* captured_head = NULL;
    Frame* captured_tail = NULL;
    Frame* current = tl_current_frame;
    Region* prompt_region = NULL;
    Region* parent_region = NULL;

    while (current) {
        if (current->tag == FRAME_PROMPT &&
            current->prompt.tag == tag) {
            /* Found the delimiter */
            k->prompt_env = current->env;
            if (k->prompt_env) inc_ref(k->prompt_env);

            /* CTRR: Get the prompt's region for transmigration */
            prompt_region = current->prompt.region;
            parent_region = prompt_region ? prompt_region->parent : NULL;
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

    /*
     * CTRR Integration: Transmigrate captured frame contents.
     * Objects allocated in the prompt's region need to move to the parent region
     * so they survive after the prompt exits.
     */
    if (prompt_region && parent_region) {
        k->captured_from = prompt_region;
        Frame* f = captured_head;
        while (f) {
            frame_transmigrate_contents(f, prompt_region, parent_region);
            f = f->prev;
        }
        /* Also transmigrate the prompt environment */
        if (k->prompt_env) {
            k->prompt_env = transmigrate_obj(k->prompt_env, prompt_region, parent_region);
        }
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

    /*
     * CTRR Integration: Create child region for this prompt.
     * All allocations during the delimited computation will go to this region.
     * When the prompt exits, the region is exited (deallocating prompt-local data).
     * When a continuation is captured, values are transmigrated out.
     */
    Region* parent_region = cont_current_region();
    Region* prompt_region = region_create();
    if (prompt_region) {
        /* Set up parent-child relationship for outlives tracking */
        prompt_region->parent = parent_region;
        prompt_region->lifetime_rank = parent_region ? parent_region->lifetime_rank + 1 : 1;
        f->prompt.region = prompt_region;
        region_stack_push(prompt_region);
    } else {
        f->prompt.region = NULL;  /* Fallback: no region isolation */
    }

    tl_current_frame = f;

    /* Set up escape point */
    if (setjmp(f->prompt.escape) != 0) {
        /* Returned via control (longjmp from cont_capture or abort) */
        Obj* result = f->prompt.result;
        f->prompt.result = NULL;

        /* CTRR: Pop and exit the prompt's region */
        if (f->prompt.region) {
            Region* popped = region_stack_pop();
            if (popped == f->prompt.region) {
                region_exit(f->prompt.region);
            }
            f->prompt.region = NULL;
        }

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

/*
 * cont_prompt_exit - Exit a prompt normally with a result.
 *
 * CTRR Integration: This function:
 * 1. Finds the matching prompt frame
 * 2. Transmigrates the result to the parent region
 * 3. Pops and exits the prompt's region
 * 4. Removes the prompt frame from the stack
 *
 * Call this when the body of a prompt completes normally (no capture).
 */
Obj* cont_prompt_exit(uint32_t tag, Obj* result) {
    /* Find matching prompt frame */
    Frame* prompt_frame = tl_current_frame;
    Frame* prev_of_prompt = NULL;

    while (prompt_frame) {
        if (prompt_frame->tag == FRAME_PROMPT &&
            prompt_frame->prompt.tag == tag) {
            break;
        }
        prev_of_prompt = prompt_frame;
        prompt_frame = prompt_frame->prev;
    }

    if (!prompt_frame) {
        /* No matching prompt - return result as-is */
        return result;
    }

    /* CTRR: Transmigrate result to parent region before exiting prompt's region */
    Region* prompt_region = prompt_frame->prompt.region;
    Region* parent_region = prompt_region ? prompt_region->parent : NULL;

    if (prompt_region && parent_region && result) {
        result = transmigrate_obj(result, prompt_region, parent_region);
    }

    /* CTRR: Pop and exit the prompt's region */
    if (prompt_region) {
        Region* popped = region_stack_pop();
        if (popped == prompt_region) {
            region_exit(prompt_region);
        }
        prompt_frame->prompt.region = NULL;
    }

    /* Remove prompt frame from stack */
    if (prev_of_prompt) {
        /* Prompt is not at top - unlink it */
        prev_of_prompt->prev = prompt_frame->prev;
        if (prompt_frame->prev) frame_inc_ref(prompt_frame->prev);
    } else {
        /* Prompt is at top */
        tl_current_frame = prompt_frame->prev;
        if (tl_current_frame) frame_inc_ref(tl_current_frame);
    }
    frame_dec_ref(prompt_frame);

    return result;
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

    /* Resume task with value or execute thunk */
    Obj* result = task->value;

    /* If fiber has a thunk (closure) to execute, call it */
    if (task->expr && IS_BOXED(task->expr) && task->expr->tag == TAG_CLOSURE) {
        result = call_closure(task->expr, NULL, 0);
        task->expr = NULL;
    }

    /* Mark task as done */
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
    fiber_unpark_error(t, value, false);
}

void fiber_unpark_error(Fiber* t, Obj* value, bool is_error) {
    if (!t || t->state != FIBER_PARKED) return;

    t->value = value;
    t->is_error = is_error;
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
    /* Select waiter lists are NULL-initialized by memset */

    if (capacity > 0) {
        ch->buffer = calloc(capacity, sizeof(Obj*));
        if (!ch->buffer) {
            free(ch);
            return NULL;
        }
    }

    return ch;
}

/* ========== Select Waiter Helpers ========== */

/*
 * Remove a SelectWaiter from its channel's waiter list.
 * Called during select cleanup to remove waiters from channels
 * that didn't fire.
 */
static void select_waiter_remove(SelectWaiter* sw, FiberChannel* ch, bool is_send) {
    if (!sw || !ch) return;

    SelectWaiter** head = is_send ? &ch->select_send_waiters : &ch->select_recv_waiters;

    if (sw->prev) {
        sw->prev->next = sw->next;
    } else {
        *head = sw->next;
    }
    if (sw->next) {
        sw->next->prev = sw->prev;
    }
    sw->next = sw->prev = NULL;
}

/*
 * Wake a select waiter: remove from channel, set fiber's select_ready,
 * and unpark the fiber.
 */
static void select_waiter_wake(SelectWaiter* sw, FiberChannel* ch, bool is_send, Obj* value) {
    if (!sw || !sw->fiber) return;

    Fiber* f = sw->fiber;

    /* Remove from channel's waiter list */
    select_waiter_remove(sw, ch, is_send);

    /* Mark which case completed */
    f->select_ready = sw->case_index;

    /* Unpark the fiber */
    fiber_unpark(f, value);
}

/* ========== Fiber Channel Operations ========== */

void fiber_channel_send(FiberChannel* ch, Obj* value) {
    if (!ch || ch->closed) return;

    /* Check for waiting receiver (regular first, then select) */
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

    /* Check for select receiver */
    if (ch->select_recv_waiters) {
        SelectWaiter* sw = ch->select_recv_waiters;
        select_waiter_wake(sw, ch, false, value);
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

        /* Wake a waiting sender if any (regular first, then select) */
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
        } else if (ch->select_send_waiters) {
            SelectWaiter* sw = ch->select_send_waiters;
            Obj* send_val = sw->value;

            ch->buffer[ch->tail] = send_val;
            if (send_val) inc_ref(send_val);
            ch->tail = (ch->tail + 1) % ch->capacity;
            ch->count++;

            select_waiter_wake(sw, ch, true, NULL);
        }

        return value;
    }

    /* Check for waiting sender - regular first */
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

    /* Check for select sender */
    if (ch->select_send_waiters) {
        SelectWaiter* sw = ch->select_send_waiters;
        Obj* value = sw->value;
        select_waiter_wake(sw, ch, true, NULL);
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

    /* Regular receiver waiting? (priority over select receivers) */
    if (ch->recv_waiters) {
        Fiber* receiver = ch->recv_waiters;
        ch->recv_waiters = receiver->next;
        if (ch->recv_waiters) ch->recv_waiters->prev = NULL;
        receiver->next = NULL;
        fiber_unpark(receiver, value);
        return true;
    }

    /* Select receiver waiting? */
    if (ch->select_recv_waiters) {
        SelectWaiter* sw = ch->select_recv_waiters;
        select_waiter_wake(sw, ch, false, value);
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

        /* Wake sender if waiting (regular first, then select) */
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
        } else if (ch->select_send_waiters) {
            SelectWaiter* sw = ch->select_send_waiters;
            Obj* send_val = sw->value;  /* Value stored in SelectWaiter */

            ch->buffer[ch->tail] = send_val;
            if (send_val) inc_ref(send_val);
            ch->tail = (ch->tail + 1) % ch->capacity;
            ch->count++;

            select_waiter_wake(sw, ch, true, NULL);
        }

        return value;
    }

    /* Regular sender waiting? */
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

    /* Select sender waiting? */
    if (ch->select_send_waiters) {
        SelectWaiter* sw = ch->select_send_waiters;
        Obj* value = sw->value;
        select_waiter_wake(sw, ch, true, NULL);
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

    /* Wake all select receivers with NULL (channel closed) */
    SelectWaiter* sw = ch->select_recv_waiters;
    while (sw) {
        SelectWaiter* next = sw->next;
        select_waiter_wake(sw, ch, false, NULL);
        sw = next;
    }
    ch->select_recv_waiters = NULL;

    /* Wake all select senders */
    sw = ch->select_send_waiters;
    while (sw) {
        SelectWaiter* next = sw->next;
        if (sw->value) {
            dec_ref(sw->value);
            sw->value = NULL;
        }
        select_waiter_wake(sw, ch, true, NULL);
        sw = next;
    }
    ch->select_send_waiters = NULL;
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

/* Create an Obj wrapper for a Generator (Issue 14 P4) */
Obj* mk_generator_obj(Generator* g) {
    if (!g) return NULL;

    extern Region* _global_region;
    extern void _ensure_global_region(void);

    _ensure_global_region();
    Obj* obj = (Obj*)region_alloc(_global_region, sizeof(Obj));
    if (!obj) return NULL;

    obj->tag = TAG_GENERATOR;
    obj->ptr = g;
    obj->mark = 1;
    g->refcount++;

    return obj;
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

    /* Call on_fulfill callbacks */
    Obj* lst = p->on_fulfill;
    while (lst && lst->is_pair) {
        Obj* callback = lst->a;
        Obj* args[1] = { value };
        call_closure(callback, args, 1);
        lst = lst->b;
    }

    /* Clear callback list (they've been called) */
    if (p->on_fulfill) {
        dec_ref(p->on_fulfill);
        p->on_fulfill = NULL;
    }
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
        fiber_unpark_error(t, error, true);
        t = next;
    }
    p->waiters = NULL;

    /* Call on_reject callbacks */
    Obj* lst = p->on_reject;
    while (lst && lst->is_pair) {
        Obj* callback = lst->a;
        Obj* args[1] = { error };
        call_closure(callback, args, 1);
        lst = lst->b;
    }

    /* Clear callback list (they've been called) */
    if (p->on_reject) {
        dec_ref(p->on_reject);
        p->on_reject = NULL;
    }
}

Obj* promise_await(Promise* p) {
    if (!p) return NULL;

    /* Already settled? */
    if (p->state == PROMISE_FULFILLED) {
        return p->value;
    }
    if (p->state == PROMISE_REJECTED) {
        /* Note: Returns error value directly - caller should check promise_state(p)
         * to distinguish between successful error value and rejection.
         * In fiber context, the error is set via fiber_unpark and stored in
         * Fiber->value when the fiber resumes.
         */
        return p->error;
    }

    /* Must wait */
    Fiber* current = fiber_current();
    if (!current) {
        /* No fiber context - use busy-wait with sleep (Phase 5.1) */
        while (!promise_is_settled(p)) {
            struct timespec ts = {0, 1000000};  /* 1ms */
            nanosleep(&ts, NULL);
        }
        if (p->state == PROMISE_FULFILLED) {
            return p->value;
        }
        return p->error;
    }

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
    Region* r = cont_current_region();
    if (on_fulfill) {
        inc_ref(on_fulfill);
        p->on_fulfill = mk_cell_region(r, on_fulfill, p->on_fulfill);
    }
    if (on_reject) {
        inc_ref(on_reject);
        p->on_reject = mk_cell_region(r, on_reject, p->on_reject);
    }
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

    /* Cancel waiting tasks by unparking them with error */
    Fiber* t = p->waiters;
    while (t) {
        Fiber* next = t->next;
        /* Unpark fiber with error indicating promise was destroyed/cancelled */
        /* fiber_unpark_error will enqueue fiber and set state to FIBER_READY */
        fiber_unpark_error(t, NULL, true);
        t = next;
    }
    /* Clear waiters list to prevent accessing freed fibers */
    p->waiters = NULL;

    free(p);
}

/* ========== Select (Multi-Wait) ========== */

/*
 * Helper: Add a SelectWaiter to a channel's waiter list.
 */
static void select_waiter_add(SelectWaiter* sw, FiberChannel* ch, bool is_send) {
    SelectWaiter** head = is_send ? &ch->select_send_waiters : &ch->select_recv_waiters;

    sw->next = *head;
    sw->prev = NULL;
    if (*head) {
        (*head)->prev = sw;
    }
    *head = sw;
}

/*
 * Helper: Clean up all select waiters from a fiber after one case completes.
 * Removes waiters from all channels and frees memory.
 */
static void select_cleanup(Fiber* f, SelectCase* cases) {
    if (!f || !f->select_waiters) return;

    for (int i = 0; i < f->select_count; i++) {
        SelectWaiter* sw = f->select_waiters[i];
        if (!sw) continue;

        /* Skip the waiter that fired (already removed from channel) */
        if (i == f->select_ready) {
            free(sw);
            continue;
        }

        /* Remove from channel's waiter list */
        SelectCase* c = &cases[i];
        if (c->op == SELECT_DEFAULT) {
            free(sw);
            continue;
        }

        bool is_send = (c->op == SELECT_SEND);
        select_waiter_remove(sw, c->channel, is_send);

        /* Free value reference for send cases */
        if (is_send && sw->value) {
            dec_ref(sw->value);
        }

        free(sw);
    }

    free(f->select_waiters);
    f->select_waiters = NULL;
    f->select_count = 0;
    f->in_select = false;
}

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

    /* Count channel cases (excluding DEFAULT) */
    int channel_count = 0;
    for (int i = 0; i < count; i++) {
        if (cases[i].op != SELECT_DEFAULT) {
            channel_count++;
        }
    }

    if (channel_count == 0) {
        /* No channel cases and no default - would block forever */
        return -1;
    }

    /* Allocate waiter array */
    SelectWaiter** waiters = calloc(count, sizeof(SelectWaiter*));
    if (!waiters) return -1;

    /* Create and register waiters for each channel case */
    for (int i = 0; i < count; i++) {
        SelectCase* c = &cases[i];

        if (c->op == SELECT_DEFAULT) {
            waiters[i] = NULL;
            continue;
        }

        SelectWaiter* sw = malloc(sizeof(SelectWaiter));
        if (!sw) {
            /* Cleanup on allocation failure */
            for (int j = 0; j < i; j++) {
                if (waiters[j]) {
                    SelectCase* cj = &cases[j];
                    bool is_send = (cj->op == SELECT_SEND);
                    select_waiter_remove(waiters[j], cj->channel, is_send);
                    if (is_send && waiters[j]->value) {
                        dec_ref(waiters[j]->value);
                    }
                    free(waiters[j]);
                }
            }
            free(waiters);
            return -1;
        }

        sw->fiber = current;
        sw->case_index = i;
        sw->value = (c->op == SELECT_SEND) ? c->value : NULL;
        if (sw->value) inc_ref(sw->value);
        sw->next = sw->prev = NULL;

        waiters[i] = sw;

        /* Add to channel's waiter list */
        bool is_send = (c->op == SELECT_SEND);
        select_waiter_add(sw, c->channel, is_send);
    }

    /* Store select state in fiber */
    current->select_waiters = waiters;
    current->select_count = count;
    current->select_ready = -1;
    current->in_select = true;

    /* Park the fiber - will be woken when any channel is ready */
    fiber_park(NULL, NULL);

    /* When we resume, select_ready has been set by the waking channel */
    int ready = current->select_ready;

    /* Store the received value if it was a recv case */
    if (ready >= 0 && cases[ready].op == SELECT_RECV && cases[ready].result) {
        *cases[ready].result = current->value;
    }

    /* Clean up all waiters from other channels */
    select_cleanup(current, cases);

    return ready;
}

/* ========== Timer Infrastructure (Phase 5.1) ========== */

#include <time.h>

/* Timer entry for the timer registry */
typedef struct TimerEntry {
    uint64_t deadline_ns;      /* Absolute deadline in nanoseconds */
    Promise* promise;          /* Promise to resolve when timer fires */
    struct TimerEntry* next;   /* Next timer in sorted list */
} TimerEntry;

/* Timer registry - sorted linked list of pending timers */
static TimerEntry* g_timer_list = NULL;
static pthread_mutex_t g_timer_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_timer_cond = PTHREAD_COND_INITIALIZER;
static pthread_t g_timer_thread;
static volatile bool g_timer_running = false;

/* Get current time in nanoseconds (monotonic clock) */
static uint64_t get_time_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

/* Timer thread function - processes expired timers */
static void* timer_thread_func(void* arg) {
    (void)arg;

    pthread_mutex_lock(&g_timer_mutex);
    while (g_timer_running) {
        uint64_t now = get_time_ns();

        /* Process all expired timers */
        while (g_timer_list && g_timer_list->deadline_ns <= now) {
            TimerEntry* entry = g_timer_list;
            g_timer_list = entry->next;

            /* Resolve the promise (unlock during resolve to avoid deadlock) */
            Promise* p = entry->promise;
            pthread_mutex_unlock(&g_timer_mutex);

            promise_resolve(p, NULL);  /* Timer fires with nil value */

            pthread_mutex_lock(&g_timer_mutex);
            free(entry);
            now = get_time_ns();  /* Refresh time after unlocking */
        }

        /* Calculate wait time */
        if (g_timer_list) {
            /* Wait until next timer deadline */
            uint64_t wait_ns = g_timer_list->deadline_ns - now;
            struct timespec ts;
            ts.tv_sec = wait_ns / 1000000000ULL;
            ts.tv_nsec = wait_ns % 1000000000ULL;

            /* Use absolute time for pthread_cond_timedwait */
            struct timespec abs_time;
            clock_gettime(CLOCK_REALTIME, &abs_time);
            abs_time.tv_sec += ts.tv_sec;
            abs_time.tv_nsec += ts.tv_nsec;
            if (abs_time.tv_nsec >= 1000000000) {
                abs_time.tv_sec++;
                abs_time.tv_nsec -= 1000000000;
            }

            pthread_cond_timedwait(&g_timer_cond, &g_timer_mutex, &abs_time);
        } else {
            /* No timers, wait indefinitely for new timer */
            pthread_cond_wait(&g_timer_cond, &g_timer_mutex);
        }
    }
    pthread_mutex_unlock(&g_timer_mutex);
    return NULL;
}

/* Initialize timer system (call once at startup) */
void timer_system_init(void) {
    if (g_timer_running) return;  /* Already initialized */

    g_timer_running = true;
    pthread_create(&g_timer_thread, NULL, timer_thread_func, NULL);
}

/* Shutdown timer system (call at cleanup) */
void timer_system_shutdown(void) {
    if (!g_timer_running) return;

    pthread_mutex_lock(&g_timer_mutex);
    g_timer_running = false;

    /* Cancel all pending timers */
    while (g_timer_list) {
        TimerEntry* entry = g_timer_list;
        g_timer_list = entry->next;
        /* Don't resolve - just clean up */
        free(entry);
    }

    pthread_cond_signal(&g_timer_cond);
    pthread_mutex_unlock(&g_timer_mutex);

    pthread_join(g_timer_thread, NULL);
}

/* Register a timer with the timer system */
static void timer_register(Promise* p, uint64_t ms) {
    TimerEntry* entry = malloc(sizeof(TimerEntry));
    if (!entry) {
        /* OOM - resolve immediately as fallback */
        promise_resolve(p, NULL);
        return;
    }

    entry->deadline_ns = get_time_ns() + (ms * 1000000ULL);
    entry->promise = p;
    entry->next = NULL;

    pthread_mutex_lock(&g_timer_mutex);

    /* Insert in sorted order (by deadline) */
    if (!g_timer_list || entry->deadline_ns < g_timer_list->deadline_ns) {
        entry->next = g_timer_list;
        g_timer_list = entry;
    } else {
        TimerEntry* curr = g_timer_list;
        while (curr->next && curr->next->deadline_ns < entry->deadline_ns) {
            curr = curr->next;
        }
        entry->next = curr->next;
        curr->next = entry;
    }

    /* Wake timer thread if we added at front */
    if (g_timer_list == entry) {
        pthread_cond_signal(&g_timer_cond);
    }

    pthread_mutex_unlock(&g_timer_mutex);
}

Promise* timer_after(uint64_t ms) {
    Promise* p = promise_create();
    if (!p) return NULL;

    if (ms == 0) {
        /* Zero delay - resolve immediately */
        promise_resolve(p, NULL);
    } else if (g_timer_running) {
        /* Register with timer system */
        timer_register(p, ms);
    } else {
        /* Timer system not initialized - fallback to immediate resolve */
        promise_resolve(p, NULL);
    }

    return p;
}

Obj* await_timeout(Promise* p, uint64_t ms) {
    if (!p) return NULL;

    /* If already settled, return immediately */
    if (promise_is_settled(p)) {
        return p->value;
    }

    /* Create a timeout timer */
    Promise* timeout = timer_after(ms);
    if (!timeout) {
        return promise_await(p);  /* Fallback to no timeout */
    }

    /* Race: wait for either promise or timeout
     * Simple implementation: poll both until one settles */
    while (!promise_is_settled(p) && !promise_is_settled(timeout)) {
        /* Try to run scheduler step to let fibers progress */
        Scheduler* sched = scheduler_current();
        if (sched && !scheduler_is_idle()) {
            scheduler_step();
        } else {
            /* No scheduler or idle - brief sleep to avoid busy-wait */
            struct timespec ts = {0, 1000000};  /* 1ms */
            nanosleep(&ts, NULL);
        }
    }

    if (promise_is_settled(p)) {
        return p->value;
    }

    /* Timeout expired */
    return NULL;  /* Return nil on timeout */
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

/* ========== CEK Machine Implementation ========== */
/*
 * This section implements the CEK (Control, Environment, Continuation) machine
 * that unifies trampoline-style evaluation with continuation-based control.
 *
 * The key insight is that "bounce" objects are just FRAME_APP_DONE frames
 * waiting to apply a function. By using CEK frames consistently, we get:
 * - Stack-safe recursion (trampolined)
 * - First-class continuations (capturable)
 * - Effects (prompt-based)
 * - All unified in one mechanism
 */

/* Thread-local CEK state pool for allocation efficiency */
#define CEK_STATE_POOL_SIZE 32
static _Thread_local CEKState tl_cek_pool[CEK_STATE_POOL_SIZE];
static _Thread_local int tl_cek_pool_used = 0;

static CEKState* cek_state_alloc(void) {
    if (tl_cek_pool_used < CEK_STATE_POOL_SIZE) {
        return &tl_cek_pool[tl_cek_pool_used++];
    }
    return malloc(sizeof(CEKState));
}

void cek_state_init(CEKState* state, Obj* expr, Obj* env, Frame* cont, Obj* value) {
    if (!state) return;
    state->expr = expr;
    state->env = env;
    state->cont = cont;
    state->value = value;
    state->status = CEK_CONTINUE;
    state->error_msg = NULL;
}

Frame* cek_push_frame(CEKState* state, FrameTag tag) {
    Frame* f = frame_alloc(tag);
    if (!f) {
        state->status = CEK_ERROR;
        state->error_msg = "Failed to allocate frame";
        return NULL;
    }

    f->env = state->env;
    if (state->env) inc_ref(state->env);
    f->prev = state->cont;
    if (state->cont) frame_inc_ref(state->cont);

    state->cont = f;
    return f;
}

void cek_pop_frame(CEKState* state, Obj* value) {
    if (!state->cont) {
        /* No more frames - evaluation complete */
        state->value = value;
        state->status = CEK_DONE;
        return;
    }

    Frame* f = state->cont;
    state->cont = f->prev;
    if (state->cont) frame_inc_ref(state->cont);
    state->env = f->env;
    state->value = value;

    frame_dec_ref(f);
}

/*
 * cek_step_apply_frame - Apply a value to a continuation frame
 *
 * This is the "return" direction of CEK: we have a value and need to
 * pass it to the current frame to continue evaluation.
 */
static CEKResult cek_step_apply_frame(CEKState* state) {
    if (!state->cont) {
        /* No continuation - we're done */
        state->status = CEK_DONE;
        return CEK_DONE;
    }

    Frame* f = state->cont;
    Obj* val = state->value;

    switch (f->tag) {
        case FRAME_APP_FN:
            /* We just evaluated the function position */
            /* Store function, move to evaluating arguments */
            f->tag = FRAME_APP_ARGS;
            f->app.fn = val;
            if (val) inc_ref(val);

            /* Start evaluating first argument (if any) */
            if (f->app.args_todo) {
                /* args_todo is a list of expressions */
                Obj* first_arg = f->app.args_todo;
                if (first_arg && first_arg->a) {
                    state->expr = first_arg->a;
                    f->app.args_todo = first_arg->b;
                    return CEK_CONTINUE;
                }
            }
            /* No arguments - fall through to apply */
            f->tag = FRAME_APP_DONE;
            /* Fall through */

        case FRAME_APP_ARGS:
            /* We just evaluated an argument */
            /* Add to args_done (reversed), check for more */
            {
                /* Prepend val to args_done */
                Obj* new_done = mk_pair(val, f->app.args_done);
                if (f->app.args_done) dec_ref(f->app.args_done);
                f->app.args_done = new_done;

                if (f->app.args_todo && f->app.args_todo->a) {
                    /* More arguments to evaluate */
                    state->expr = f->app.args_todo->a;
                    f->app.args_todo = f->app.args_todo->b;
                    return CEK_CONTINUE;
                }
                /* All arguments evaluated - apply */
                f->tag = FRAME_APP_DONE;
            }
            /* Fall through */

        case FRAME_APP_DONE:
            /* Ready to apply function to arguments */
            {
                Obj* fn = f->app.fn;
                Obj* args = f->app.args_done;

                /* Count and reverse arguments */
                int argc = 0;
                Obj* p = args;
                while (p) {
                    argc++;
                    p = p->b;
                }

                Obj** argv = NULL;
                if (argc > 0) {
                    argv = alloca(sizeof(Obj*) * argc);
                    p = args;
                    for (int i = argc - 1; i >= 0 && p; i--) {
                        argv[i] = p->a;
                        p = p->b;
                    }
                }

                /* Pop the frame before calling */
                Frame* prev = f->prev;
                if (prev) frame_inc_ref(prev);
                Obj* saved_env = f->env;
                state->cont = prev;
                state->env = saved_env;
                frame_dec_ref(f);

                /* Call the function */
                Obj* result = call_closure(fn, argv, argc);
                state->value = result;

                /* Check if result is a thunk (for trampolining) */
                /* A thunk would be represented as a special object */
                /* For now, just return the result to the continuation */
                return state->cont ? CEK_CONTINUE : CEK_DONE;
            }

        case FRAME_IF_TEST:
            /* We just evaluated the test expression */
            {
                /* Pick branch based on truthiness */
                Obj* branch;
                if (val && (val != OMNI_FALSE) && (val != NULL)) {
                    branch = f->if_test.then_branch;
                } else {
                    branch = f->if_test.else_branch;
                }

                /* Pop frame and evaluate the branch */
                Frame* prev = f->prev;
                if (prev) frame_inc_ref(prev);
                state->cont = prev;
                state->env = f->env;
                state->expr = branch;
                state->value = NULL;
                frame_dec_ref(f);
                return CEK_CONTINUE;
            }

        case FRAME_LET_BIND:
            /* We just evaluated a binding value */
            {
                /* Extend environment with new binding */
                /* var contains the variable symbol */
                Obj* var = f->let.var;
                Obj* new_env = mk_pair(mk_pair(var, val), state->env);
                state->env = new_env;

                /* Check for more bindings */
                if (f->let.bindings_todo && f->let.bindings_todo->a) {
                    /* More bindings - update var and evaluate next */
                    Obj* next_binding = f->let.bindings_todo->a;
                    f->let.var = next_binding->a;  /* variable */
                    state->expr = next_binding->b ? next_binding->b->a : NULL;  /* expression */
                    f->let.bindings_todo = f->let.bindings_todo->b;
                    f->env = new_env;
                    return CEK_CONTINUE;
                }

                /* All bindings done - evaluate body */
                f->tag = FRAME_LET_BODY;
                state->expr = f->let.body;
                state->value = NULL;
                f->env = new_env;
                return CEK_CONTINUE;
            }

        case FRAME_LET_BODY:
            /* We just evaluated the let body - return the value */
            cek_pop_frame(state, val);
            return state->status;

        case FRAME_SEQ:
            /* We just evaluated a sequence expression */
            {
                if (f->seq.remaining && f->seq.remaining->a) {
                    /* More expressions to evaluate */
                    state->expr = f->seq.remaining->a;
                    f->seq.remaining = f->seq.remaining->b;
                    return CEK_CONTINUE;
                }
                /* Sequence complete - return last value */
                cek_pop_frame(state, val);
                return state->status;
            }

        case FRAME_PROMPT:
            /* Normal completion of prompt - exit with value */
            {
                /* CTRR: Handle region exit */
                if (f->prompt.region) {
                    Region* parent = f->prompt.region->parent;
                    if (parent && val) {
                        val = transmigrate_obj(val, f->prompt.region, parent);
                    }
                    Region* popped = region_stack_pop();
                    if (popped == f->prompt.region) {
                        region_exit(f->prompt.region);
                    }
                    f->prompt.region = NULL;
                }

                cek_pop_frame(state, val);
                return state->status;
            }

        case FRAME_YIELD:
            /* Generator yield - needs special handling */
            state->status = CEK_YIELD;
            state->value = val;
            return CEK_YIELD;

        case FRAME_AWAIT:
            /* Promise await - needs scheduler integration */
            state->status = CEK_YIELD;
            state->value = val;
            return CEK_YIELD;

        case FRAME_HANDLER:
            /* Effect handler return */
            cek_pop_frame(state, val);
            return state->status;
    }

    /* Unknown frame type */
    state->status = CEK_ERROR;
    state->error_msg = "Unknown frame type in CEK step";
    return CEK_ERROR;
}

/*
 * cek_step - Take one step of CEK evaluation
 *
 * If we have a value (and no expr), apply value to continuation.
 * If we have an expr, evaluate it one step.
 */
CEKResult cek_step(CEKState* state) {
    if (!state) return CEK_ERROR;

    /* If we have a value, apply it to the current frame */
    if (state->value && !state->expr) {
        return cek_step_apply_frame(state);
    }

    /* If we have an expression, we need to evaluate it */
    /* This would integrate with the expression evaluator */
    /* For now, we treat expressions as values (already evaluated) */
    if (state->expr) {
        /* Check if expr is a self-evaluating value */
        Obj* e = state->expr;
        state->expr = NULL;
        state->value = e;
        return cek_step_apply_frame(state);
    }

    /* No expr and no value - shouldn't happen */
    state->status = CEK_ERROR;
    state->error_msg = "CEK step with no expr and no value";
    return CEK_ERROR;
}

/*
 * cek_run - Run CEK machine to completion
 */
Obj* cek_run(Obj* expr, Obj* env) {
    CEKState state;
    cek_state_init(&state, expr, env, NULL, NULL);

    const int MAX_STEPS = 10000000;  /* Prevent infinite loops */
    int steps = 0;

    while (state.status == CEK_CONTINUE && steps < MAX_STEPS) {
        cek_step(&state);
        steps++;
    }

    if (steps >= MAX_STEPS) {
        fprintf(stderr, "cek_run: Exceeded maximum steps (possible infinite loop)\n");
        return NULL;
    }

    if (state.status == CEK_ERROR) {
        fprintf(stderr, "cek_run: Error: %s\n",
                state.error_msg ? state.error_msg : "unknown error");
        return NULL;
    }

    return state.value;
}

/*
 * cek_apply_closure - Set up CEK state for closure application
 */
void cek_apply_closure(CEKState* state, Obj* closure, Obj* args) {
    Frame* f = cek_push_frame(state, FRAME_APP_DONE);
    if (!f) return;

    f->app.fn = closure;
    if (closure) inc_ref(closure);
    f->app.args_done = NULL;
    f->app.args_todo = args;

    /* If there are arguments, start evaluating them */
    if (args && args->a) {
        f->tag = FRAME_APP_ARGS;
        state->expr = args->a;
        f->app.args_todo = args->b;
    } else {
        /* No arguments - ready to apply */
        state->value = closure;
        state->expr = NULL;
    }
}

/*
 * cek_make_thunk - Create a thunk as a CEK state
 *
 * This replaces the old bounce mechanism.
 */
CEKState* cek_make_thunk(Obj* closure, Obj* arg) {
    CEKState* state = cek_state_alloc();
    if (!state) return NULL;

    cek_state_init(state, NULL, NULL, NULL, NULL);

    /* Create an APP_DONE frame with the closure and arg */
    Frame* f = frame_alloc(FRAME_APP_DONE);
    if (!f) {
        state->status = CEK_ERROR;
        return state;
    }

    f->app.fn = closure;
    if (closure) inc_ref(closure);
    f->app.args_done = arg ? mk_pair(arg, NULL) : NULL;
    f->app.args_todo = NULL;
    f->env = NULL;
    f->prev = NULL;
    f->refcount = 1;

    state->cont = f;
    state->value = closure;  /* Trigger application */
    state->status = CEK_CONTINUE;

    return state;
}

/*
 * cek_trampoline - Legacy trampoline API using CEK
 *
 * If thunk is a closure, call it with no arguments and trampoline the result.
 * If result is another closure (continuation), call it again.
 */
Obj* cek_trampoline(Obj* thunk) {
    if (!thunk) return NULL;

    Obj* current = thunk;
    const int MAX_BOUNCES = 10000000;
    int bounces = 0;

    while (bounces < MAX_BOUNCES) {
        /* Check if current is a closure/thunk */
        if (!current || !IS_BOXED(current) || current->tag != TAG_CLOSURE) {
            /* Not a closure - this is the final result */
            return current;
        }

        /* Call the closure with no arguments */
        Obj* result = call_closure(current, NULL, 0);
        current = result;
        bounces++;
    }

    fprintf(stderr, "cek_trampoline: Exceeded maximum bounces\n");
    return NULL;
}
