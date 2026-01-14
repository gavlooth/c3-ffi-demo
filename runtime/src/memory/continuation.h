/*
 * Fiber-Based Concurrency for OmniLisp Runtime
 *
 * This module provides:
 * - Delimited continuations (prompt/control)
 * - Fiber scheduler (lightweight cooperative tasks)
 * - Fiber-based channels
 * - Generators/iterators
 * - Async/await (promises)
 *
 * Design: CEK machine with explicit continuation frames.
 * All "stack" is heap-allocated, enabling capture without copying.
 */

#ifndef OMNI_CONTINUATION_H
#define OMNI_CONTINUATION_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <setjmp.h>
#include <pthread.h>

/* Forward declarations */
#ifndef OMNI_OBJ_DECLARED
#define OMNI_OBJ_DECLARED
typedef struct Obj Obj;
#endif
#ifndef OMNI_REGION_TYPEDEF
#define OMNI_REGION_TYPEDEF 1
typedef struct Region Region;
#endif
typedef struct Frame Frame;
typedef struct Continuation Continuation;
typedef struct Fiber Fiber;
typedef struct Scheduler Scheduler;
typedef struct FiberChannel FiberChannel;
typedef struct Generator Generator;
typedef struct Promise Promise;
typedef struct WorkerThread WorkerThread;
typedef struct ThreadPool ThreadPool;

/* ========== Continuation Frame Types ========== */

typedef enum {
    FRAME_APP_FN,       /* Evaluating function position */
    FRAME_APP_ARGS,     /* Evaluating arguments */
    FRAME_APP_DONE,     /* Ready to apply */
    FRAME_IF_TEST,      /* Evaluated test, pick branch */
    FRAME_LET_BIND,     /* Evaluated binding value */
    FRAME_LET_BODY,     /* Evaluating let body */
    FRAME_SEQ,          /* Sequence, more exprs to eval */
    FRAME_PROMPT,       /* Delimiter frame */
    FRAME_HANDLER,      /* Effect handler */
    FRAME_YIELD,        /* Generator yield point */
    FRAME_AWAIT,        /* Promise await point */
} FrameTag;

/* Continuation frame - one "stack frame" of pending work */
struct Frame {
    FrameTag tag;
    Frame* prev;            /* Previous frame (toward base) */
    Obj* env;               /* Environment at this point */
    uint32_t refcount;      /* For sharing frames */

    union {
        /* FRAME_APP_* */
        struct {
            Obj* fn;            /* Function (when evaluated) */
            Obj* args_done;     /* Already evaluated args (reversed) */
            Obj* args_todo;     /* Args still to evaluate */
        } app;

        /* FRAME_IF_TEST */
        struct {
            Obj* then_branch;
            Obj* else_branch;
        } if_test;

        /* FRAME_LET_* */
        struct {
            Obj* var;           /* Variable to bind */
            Obj* body;          /* Body to evaluate */
            Obj* bindings_todo; /* Remaining bindings (let*) */
        } let;

        /* FRAME_SEQ */
        struct {
            Obj* remaining;     /* Remaining expressions */
        } seq;

        /* FRAME_PROMPT */
        struct {
            uint32_t tag;       /* Prompt identifier */
            jmp_buf escape;     /* For non-local return */
            Obj* result;        /* Result passed back */
            Obj* handler;       /* Optional effect handler */
            bool cloned;        /* TRUE if this frame was cloned (jmp_buf unsafe) */
            Region* region;     /* Region owned by this prompt (CTRR integration) */
        } prompt;

        /* FRAME_YIELD */
        struct {
            Generator* gen;     /* Owning generator */
        } yield;

        /* FRAME_AWAIT */
        struct {
            Promise* promise;   /* Promise being awaited */
        } await;
    };
};

/* ========== Continuation ========== */

/*
 * A captured continuation is a chain of frames from the capture
 * point back to a delimiter (prompt). When invoked, these frames
 * are "replayed" to continue the computation.
 */
struct Continuation {
    Frame* frames;          /* Captured frame chain */
    uint32_t prompt_tag;    /* Which prompt this belongs to */
    Obj* prompt_env;        /* Environment at prompt point */
    uint32_t refcount;      /* Reference count */
    bool one_shot;          /* Can only be invoked once? */
    bool invoked;           /* Has been invoked? (for one-shot) */
    Region* captured_from;  /* Region values were transmigrated from (CTRR) */
};

/* ========== Fiber (Lightweight Cooperative Task) ========== */

typedef enum {
    FIBER_READY,            /* In run queue, ready to execute */
    FIBER_RUNNING,          /* Currently executing */
    FIBER_PARKED,           /* Waiting on channel/promise */
    FIBER_DONE,             /* Completed */
} FiberState;

struct Fiber {
    uint64_t id;            /* Unique fiber ID */
    FiberState state;

    /* Saved execution state */
    Obj* expr;              /* Current expression (C in CEK) */
    Obj* env;               /* Current environment (E in CEK) */
    Frame* cont;            /* Current continuation (K in CEK) */
    Obj* value;             /* Value to resume with */

    /* Scheduling */
    Fiber* next;            /* Next in queue */
    Fiber* prev;            /* Previous in queue (for removal) */

    /* Parking info */
    void* park_reason;      /* Channel/promise we're waiting on */
    Obj* park_value;        /* Value to send (for channel send) */

    /* Result */
    Obj* result;            /* Final result when FIBER_DONE */
    Promise* completion;    /* Promise fulfilled when done */
};

/* ========== Scheduler ========== */

struct Scheduler {
    /* Run queue (doubly-linked for O(1) removal) */
    Fiber* ready_head;
    Fiber* ready_tail;

    /* Current fiber */
    Fiber* current;

    /* Fiber management */
    uint64_t next_fiber_id;
    uint64_t fiber_count;

    /* Frame pool (avoid malloc in hot path) */
    Frame* frame_freelist;
    size_t frame_pool_size;

    /* Prompt stack for current fiber */
    Frame* prompt_stack;

    /* Statistics */
    uint64_t fibers_created;
    uint64_t fibers_completed;
    uint64_t context_switches;
};

/* ========== Work-Stealing Deque ========== */

/*
 * Lock-free work-stealing deque (Chase-Lev style).
 * Owner pushes/pops from bottom, thieves steal from top.
 */
#define DEQUE_INITIAL_SIZE 64

typedef struct WorkStealDeque {
    Fiber** buffer;             /* Circular buffer of fibers */
    volatile int64_t top;       /* Top index (thieves steal from here) */
    volatile int64_t bottom;    /* Bottom index (owner pushes/pops here) */
    size_t capacity;            /* Buffer size (power of 2) */
    pthread_mutex_t grow_lock;  /* Lock for resizing only */
} WorkStealDeque;

/* ========== Worker Thread ========== */

typedef enum {
    WORKER_IDLE,                /* Waiting for work */
    WORKER_RUNNING,             /* Executing a fiber */
    WORKER_STEALING,            /* Attempting to steal work */
    WORKER_SHUTDOWN,            /* Shutting down */
} WorkerState;

struct WorkerThread {
    pthread_t thread;           /* POSIX thread handle */
    uint32_t id;                /* Worker ID (0 = main thread) */
    volatile WorkerState state;

    /* Local work queue (work-stealing deque) */
    WorkStealDeque local_queue;

    /* Local scheduler state */
    Scheduler* scheduler;

    /* Statistics */
    uint64_t tasks_executed;
    uint64_t tasks_stolen;
    uint64_t steal_attempts;

    /* Pool reference */
    ThreadPool* pool;
};

/* ========== Thread Pool ========== */

struct ThreadPool {
    WorkerThread* workers;      /* Array of workers */
    uint32_t num_workers;       /* Total workers including main */
    volatile bool running;      /* Pool is active */
    volatile bool shutdown_requested;

    /* Global work queue for overflow/initial distribution */
    Fiber* global_head;
    Fiber* global_tail;
    pthread_mutex_t global_lock;

    /* Condition for workers waiting for work */
    pthread_cond_t work_available;
    pthread_mutex_t work_mutex;

    /* Statistics */
    volatile uint64_t total_tasks_spawned;
    volatile uint64_t total_tasks_completed;
    volatile uint64_t total_steals;
};

/* ========== Fiber Channel ========== */

/*
 * Fiber-based channel. No pthread primitives.
 * Blocking operations park the fiber and yield to scheduler.
 */
struct FiberChannel {
    /* Buffer (for buffered channels) */
    Obj** buffer;
    int capacity;           /* 0 = unbuffered (rendezvous) */
    int count;
    int head;
    int tail;

    /* Wait queues (parked fibers) */
    Fiber* send_waiters;    /* Fibers waiting to send */
    Fiber* recv_waiters;    /* Fibers waiting to receive */

    /* State */
    bool closed;
    uint32_t refcount;
};

/* ========== Generator (Iterator) ========== */

typedef enum {
    GEN_CREATED,            /* Not yet started */
    GEN_SUSPENDED,          /* Yielded, waiting for next() */
    GEN_RUNNING,            /* Currently executing */
    GEN_DONE,               /* Exhausted */
} GeneratorState;

struct Generator {
    GeneratorState state;

    /* Saved continuation when suspended */
    Continuation* cont;

    /* Current/last yielded value */
    Obj* current;

    /* The producer function */
    Obj* producer;

    /* Reference count */
    uint32_t refcount;
};

/* ========== Promise (Async) ========== */

typedef enum {
    PROMISE_PENDING,
    PROMISE_FULFILLED,
    PROMISE_REJECTED,
} PromiseState;

struct Promise {
    PromiseState state;

    /* Result (when fulfilled/rejected) */
    Obj* value;
    Obj* error;

    /* Waiters (fibers awaiting this promise) */
    Fiber* waiters;

    /* Callbacks (for non-fiber contexts) */
    Obj* on_fulfill;        /* List of (callback . env) */
    Obj* on_reject;         /* List of (callback . env) */

    /* Reference count */
    uint32_t refcount;
};

/* ========== API: Frame Management ========== */

Frame* frame_alloc(FrameTag tag);
void frame_free(Frame* f);
Frame* frame_clone(Frame* f);
void frame_inc_ref(Frame* f);
void frame_dec_ref(Frame* f);

/* ========== API: Continuation Operations ========== */

/* Create a new prompt (delimiter) - creates child region (CTRR) */
Obj* cont_prompt(uint32_t tag, Obj* body, Obj* env, Obj* handler);

/* Exit a prompt normally with result - exits region (CTRR) */
Obj* cont_prompt_exit(uint32_t tag, Obj* result);

/* Capture continuation up to matching prompt - transmigrates values (CTRR) */
Continuation* cont_capture(uint32_t tag);

/* Invoke a continuation with a value */
Obj* cont_invoke(Continuation* k, Obj* value);

/* Create continuation value */
Obj* mk_continuation_obj(Continuation* k);

/* Check if continuation is still valid */
bool cont_is_valid(Continuation* k);

/* Release continuation */
void cont_release(Continuation* k);

/* ========== API: Region Stack (CTRR Integration) ========== */

/* Get current region (prompt's region if inside prompt, else global) */
Region* cont_current_region(void);

/* ========== API: Scheduler ========== */

/* Initialize the global scheduler */
void scheduler_init(void);

/* Shutdown scheduler */
void scheduler_shutdown(void);

/* Get current scheduler (thread-local) */
Scheduler* scheduler_current(void);

/* Run scheduler until all tasks complete */
void scheduler_run(void);

/* Step one task (for integration with event loops) */
bool scheduler_step(void);

/* Is scheduler idle? */
bool scheduler_is_idle(void);

/* ========== API: Thread Pool (Work-Stealing) ========== */

/* Initialize thread pool with n worker threads (0 = auto-detect CPUs) */
void fiber_pool_init(int num_workers);

/* Shutdown thread pool and wait for workers to finish */
void fiber_pool_shutdown(void);

/* Get current thread pool (NULL if not initialized) */
ThreadPool* fiber_pool_current(void);

/* Get number of workers in pool */
int fiber_pool_size(void);

/* Spawn a fiber in the thread pool */
Fiber* fiber_pool_spawn(Obj* thunk);

/* Get current worker thread (NULL if not in pool) */
WorkerThread* fiber_pool_current_worker(void);

/* ========== API: Work-Stealing Deque ========== */

/* Initialize a work-stealing deque */
void deque_init(WorkStealDeque* d);

/* Destroy a work-stealing deque */
void deque_destroy(WorkStealDeque* d);

/* Push a fiber onto the bottom (owner only) */
void deque_push(WorkStealDeque* d, Fiber* f);

/* Pop a fiber from the bottom (owner only) */
Fiber* deque_pop(WorkStealDeque* d);

/* Steal a fiber from the top (thieves) */
Fiber* deque_steal(WorkStealDeque* d);

/* Check if deque is empty */
bool deque_is_empty(WorkStealDeque* d);

/* ========== API: Fibers ========== */

/* Spawn a new fiber */
Fiber* fiber_spawn(Obj* thunk);

/* Spawn and get completion promise */
Promise* fiber_spawn_async(Obj* thunk);

/* Yield to scheduler (cooperative) */
void fiber_yield(void);

/* Get current fiber */
Fiber* fiber_current(void);

/* Park current fiber (internal) */
void fiber_park(void* reason, Obj* value);

/* Unpark a fiber (internal) */
void fiber_unpark(Fiber* f, Obj* value);

/* ========== API: Fiber Channels ========== */

/* Create channel */
FiberChannel* fiber_channel_create(int capacity);

/* Send value (may park) */
void fiber_channel_send(FiberChannel* ch, Obj* value);

/* Receive value (may park) */
Obj* fiber_channel_recv(FiberChannel* ch);

/* Try send (non-blocking) */
bool fiber_channel_try_send(FiberChannel* ch, Obj* value);

/* Try receive (non-blocking) */
Obj* fiber_channel_try_recv(FiberChannel* ch, bool* ok);

/* Close channel */
void fiber_channel_close(FiberChannel* ch);

/* Is channel closed? */
bool fiber_channel_is_closed(FiberChannel* ch);

/* Release channel */
void fiber_channel_release(FiberChannel* ch);

/* ========== API: Generators (Iterators) ========== */

/* Create a generator from a producer function */
Generator* generator_create(Obj* producer);

/* Get next value (resumes generator) */
Obj* generator_next(Generator* g);

/* Is generator exhausted? */
bool generator_is_done(Generator* g);

/* Yield a value from within generator (parks generator) */
void generator_yield(Obj* value);

/* Close generator early */
void generator_close(Generator* g);

/* Release generator */
void generator_release(Generator* g);

/* Create generator Obj wrapper */
Obj* mk_generator_obj(Generator* g);

/* ========== API: Promises (Async/Await) ========== */

/* Create a pending promise */
Promise* promise_create(void);

/* Resolve promise with value */
void promise_resolve(Promise* p, Obj* value);

/* Reject promise with error */
void promise_reject(Promise* p, Obj* error);

/* Await promise (parks current task) */
Obj* promise_await(Promise* p);

/* Add fulfillment callback */
void promise_then(Promise* p, Obj* on_fulfill, Obj* on_reject);

/* Is promise settled? */
bool promise_is_settled(Promise* p);

/* Get promise state */
PromiseState promise_state(Promise* p);

/* Release promise */
void promise_release(Promise* p);

/* Create promise Obj wrapper */
Obj* mk_promise_obj(Promise* p);

/* ========== API: Select (Multi-Wait) ========== */

typedef struct SelectCase {
    enum { SELECT_RECV, SELECT_SEND, SELECT_DEFAULT } op;
    FiberChannel* channel;
    Obj* value;             /* For send */
    Obj** result;           /* For recv */
} SelectCase;

/* Select from multiple channel operations */
int fiber_select(SelectCase* cases, int count);

/* ========== API: Timeout ========== */

/* Create a timer that resolves after ms milliseconds */
Promise* timer_after(uint64_t ms);

/* Await with timeout (returns NULL on timeout) */
Obj* await_timeout(Promise* p, uint64_t ms);

/* ========== Prompt Tags ========== */

/* Reserved prompt tags */
#define PROMPT_TAG_SCHEDULER  0x00000001
#define PROMPT_TAG_GENERATOR  0x00000002
#define PROMPT_TAG_ASYNC      0x00000003
#define PROMPT_TAG_EXCEPTION  0x00000004
#define PROMPT_TAG_USER_BASE  0x00001000

/* Generate unique prompt tag */
uint32_t prompt_tag_generate(void);

/* ========== Thread-Local State ========== */

/* Get/set current environment */
Obj* cont_get_env(void);
void cont_set_env(Obj* env);

/* Get/set current continuation */
Frame* cont_get_current(void);
void cont_set_current(Frame* f);

/* ========== API: CEK Machine (Trampoline Unification) ========== */

/*
 * CEK Step Result
 *
 * The CEK stepper returns a result indicating what happened:
 * - CEK_DONE: Evaluation complete, result is the final value
 * - CEK_CONTINUE: More work needed, call cek_step again
 * - CEK_YIELD: Control yielded (to scheduler, prompt, etc)
 * - CEK_ERROR: Evaluation failed with an error
 */
typedef enum {
    CEK_DONE,       /* Evaluation complete */
    CEK_CONTINUE,   /* More steps needed */
    CEK_YIELD,      /* Yielded to scheduler/prompt */
    CEK_ERROR,      /* Error occurred */
} CEKResult;

/*
 * CEK State
 *
 * Holds the current state of a CEK machine evaluation.
 * This unifies:
 * - Fiber state (expr, env, cont, value)
 * - Trampoline state (bounce function + argument)
 * - Continuation capture/invoke
 */
typedef struct CEKState {
    Obj* expr;      /* C: Current expression/control */
    Obj* env;       /* E: Current environment */
    Frame* cont;    /* K: Current continuation */
    Obj* value;     /* Last evaluated value */
    CEKResult status;
    const char* error_msg;
} CEKState;

/*
 * cek_state_init - Initialize a CEK state for evaluation
 *
 * @expr: The expression to evaluate (or NULL if resuming with value)
 * @env: The environment
 * @cont: The continuation frame (NULL for top-level)
 * @value: Initial value (NULL for expr evaluation, non-NULL for applying to cont)
 */
void cek_state_init(CEKState* state, Obj* expr, Obj* env, Frame* cont, Obj* value);

/*
 * cek_step - Take one step of CEK evaluation
 *
 * This is the core of the CEK machine. It processes:
 * 1. If we have a value and a continuation frame, apply the value to the frame
 * 2. If we have an expression, evaluate it one step
 *
 * Returns the result status (DONE, CONTINUE, YIELD, ERROR)
 *
 * @state: The CEK state (modified in place)
 * Returns: CEKResult indicating what happened
 */
CEKResult cek_step(CEKState* state);

/*
 * cek_run - Run CEK machine until completion
 *
 * Repeatedly calls cek_step until DONE or ERROR.
 * This is the "trampoline" that replaces the old bounce-based system.
 *
 * @expr: Expression to evaluate
 * @env: Environment
 * Returns: Final result (or NULL on error)
 */
Obj* cek_run(Obj* expr, Obj* env);

/*
 * cek_apply_closure - Apply a closure using CEK frames
 *
 * Instead of direct function call, this sets up frames for:
 * 1. Argument evaluation (FRAME_APP_ARGS)
 * 2. Function application (FRAME_APP_DONE)
 *
 * @state: CEK state to modify
 * @closure: The closure to apply
 * @args: List of argument expressions
 */
void cek_apply_closure(CEKState* state, Obj* closure, Obj* args);

/*
 * cek_push_frame - Push a new frame onto the continuation
 *
 * @state: CEK state
 * @tag: Frame type tag
 * Returns: The new frame (caller should fill in frame-specific data)
 */
Frame* cek_push_frame(CEKState* state, FrameTag tag);

/*
 * cek_pop_frame - Pop the current frame and return value
 *
 * @state: CEK state
 * @value: Value to return to the previous frame
 */
void cek_pop_frame(CEKState* state, Obj* value);

/*
 * cek_make_thunk - Create a thunk (delayed computation) as a CEK frame
 *
 * This replaces the old "bounce" objects. Instead of storing a function
 * pointer in a mark field hack, we create proper FRAME_APP_DONE frames.
 *
 * @closure: The closure to call
 * @arg: The argument
 * Returns: CEK state ready to run
 */
CEKState* cek_make_thunk(Obj* closure, Obj* arg);

/*
 * cek_trampoline - Run a thunk to completion (legacy compatibility)
 *
 * This provides backwards compatibility with the old trampoline API.
 * Internally uses cek_run.
 *
 * @thunk: Either a closure (run it) or a CEKState (continue it)
 * Returns: Final result
 */
Obj* cek_trampoline(Obj* thunk);

#endif /* OMNI_CONTINUATION_H */
