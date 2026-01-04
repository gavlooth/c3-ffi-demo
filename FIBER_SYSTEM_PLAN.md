# Fiber System Redesign Plan

## Overview

Redesign the concurrency system with clear separation between:
1. **Fibers** - lightweight cooperative tasks (continuation-based)
2. **OS Threads** - heavy parallelism primitives (pthread-based)

---

## Naming Convention

### Fiber Primitives (Continuation-Based)

| Primitive | Description |
|-----------|-------------|
| `fiber` | Create a paused program (returns a continuation) |
| `resume` | Step into a fiber (run until yield/done) |
| `yield` | Step out of current fiber (return control to caller) |
| `spawn` | Run fiber under scheduler (non-blocking) |
| `join` | Wait for spawned fiber to finish |
| `with-fibers` | Scoped fiber context (cleanup on exit) |
| `cancel` | Request cooperative cancellation |
| `canceled?` | Check if current fiber is canceled |
| `sleep` | Timer-based yield |
| `chan` | Create communication channel |
| `send` | Send value to channel (may park) |
| `recv` | Receive value from channel (may park) |

### OS Thread Primitives (pthread-based)

| Primitive | Description |
|-----------|-------------|
| `thread` | Create OS thread (heavy, for parallelism) |
| `thread-join` | Wait for OS thread to finish |
| `thread-id` | Get current thread ID |
| `atomic` | Create atomic reference |
| `atomic-get` | Read atomic value |
| `atomic-set!` | Write atomic value |
| `atomic-cas!` | Compare-and-swap |
| `mutex` | Create mutex |
| `mutex-lock!` | Acquire mutex |
| `mutex-unlock!` | Release mutex |

---

## Implementation Plan

### Phase 1: Remove pthread Channels

**Objective:** Remove `make_channel`, `channel_send`, `channel_recv` (pthread-based)

**Where:** `runtime/src/runtime.c`

**What to change:**
- [ ] Remove `Channel` struct (lines ~2615-2630)
- [ ] Remove `make_channel()` function
- [ ] Remove `channel_send()` function
- [ ] Remove `channel_recv()` function
- [ ] Remove `channel_close()` function
- [ ] Remove `free_channel()` function
- [ ] Keep `TAG_CHANNEL` but repurpose for green channels

**Acceptance:**
- No pthread-based channel code remains
- `TAG_CHANNEL` refers to continuation-based channels only

---

### Phase 2: Rename Green Thread Infrastructure

**Objective:** Rename internal APIs to match fiber terminology

**Where:** `runtime/src/memory/continuation.c`, `continuation.h`

**What to change:**

| Old Name | New Name |
|----------|----------|
| `Task` | `Fiber` |
| `spawn_green()` | `fiber_spawn()` |
| `yield_green()` | `fiber_yield()` |
| `task_park()` | `fiber_park()` |
| `task_unpark()` | `fiber_unpark()` |
| `task_current()` | `fiber_current()` |
| `GreenChannel` | `Channel` |
| `green_channel_*` | `channel_*` |
| `spawn_async()` | `fiber_spawn_async()` |

**Acceptance:**
- All internal APIs use fiber terminology
- No "green thread" or "task" naming in public API

---

### Phase 3: Add Fiber Primitives to Evaluator

**Objective:** Wire up fiber primitives in OmniLisp evaluator

**Where:** `omnilisp/src/runtime/eval/omni_eval.c`

**What to change:**

```c
// Add to omni_env_init():
env_define(env, "fiber",     mk_prim(prim_fiber));
env_define(env, "resume",    mk_prim(prim_resume));
env_define(env, "yield",     mk_prim(prim_yield));
env_define(env, "spawn",     mk_prim(prim_spawn));
env_define(env, "join",      mk_prim(prim_join));
env_define(env, "cancel",    mk_prim(prim_cancel));
env_define(env, "canceled?", mk_prim(prim_canceled_q));
env_define(env, "sleep",     mk_prim(prim_sleep));
env_define(env, "chan",      mk_prim(prim_chan));
env_define(env, "send",      mk_prim(prim_send));
env_define(env, "recv",      mk_prim(prim_recv));
```

**Primitive implementations:**

```c
// (fiber body) -> creates paused continuation
Value* prim_fiber(Value* args, Value* env);

// (resume f) -> step into fiber, run until yield/done
Value* prim_resume(Value* args, Value* env);

// (yield) or (yield value) -> step out, return value to resumer
Value* prim_yield(Value* args, Value* env);

// (spawn f) -> run fiber under scheduler
Value* prim_spawn(Value* args, Value* env);

// (join f) -> wait for spawned fiber, return result
Value* prim_join(Value* args, Value* env);

// (cancel f) -> request cooperative cancellation
Value* prim_cancel(Value* args, Value* env);

// (canceled?) -> check if current fiber is canceled
Value* prim_canceled_q(Value* args, Value* env);

// (sleep ms) -> timer-based yield
Value* prim_sleep(Value* args, Value* env);

// (chan) or (chan capacity) -> create channel
Value* prim_chan(Value* args, Value* env);

// (send ch value) -> send to channel
Value* prim_send(Value* args, Value* env);

// (recv ch) -> receive from channel
Value* prim_recv(Value* args, Value* env);
```

**Acceptance:**
- All fiber primitives callable from OmniLisp
- Basic test: `(join (spawn (fiber (+ 1 2))))` → `3`

---

### Phase 4: Add `with-fibers` Scoped Context

**Objective:** Implement scoped fiber cleanup

**Where:** `omnilisp/src/runtime/eval/omni_eval.c`

**Syntax:**
```scheme
(with-fibers
  (spawn (fiber task1))
  (spawn (fiber task2))
  result)
;; All spawned fibers are joined/canceled on exit
```

**What to change:**
- [ ] Add `with-fibers` special form
- [ ] Track spawned fibers in scope
- [ ] On scope exit: join all, cancel stragglers
- [ ] Propagate exceptions properly

**Acceptance:**
- Fibers don't leak outside `with-fibers`
- Exceptions cancel pending fibers

---

### Phase 5: Rename OS Thread Primitives

**Objective:** Clear distinction between fibers and OS threads

**Where:** `runtime/src/runtime.c`, `omnilisp/src/runtime/eval/omni_eval.c`

**What to change:**

| Old Name | New Name |
|----------|----------|
| `spawn_thread()` | `os_thread_create()` |
| `thread_join()` | `os_thread_join()` |
| `spawn_goroutine()` | Remove (use fibers instead) |
| `TAG_THREAD` | `TAG_OS_THREAD` |

**OmniLisp primitives:**
```scheme
(thread thunk)       ;; Create OS thread (for parallelism)
(thread-join t)      ;; Wait for OS thread
(thread-id)          ;; Current OS thread ID
```

**Acceptance:**
- Clear naming: `fiber` = lightweight, `thread` = OS-level
- No confusion between the two systems

---

### Phase 6: Add Thread Pool for Fiber Scheduler

**Objective:** Allow fibers to run across multiple OS threads

**Where:** `runtime/src/memory/continuation.c`

**What to change:**
- [ ] Add `FiberPool` struct with N worker threads
- [ ] Global work-stealing queue
- [ ] `(fiber-pool-size)` to query
- [ ] `(set-fiber-pool-size! n)` to configure
- [ ] Workers steal from each other when idle

```c
typedef struct FiberPool {
    pthread_t* workers;
    int worker_count;
    WorkStealingQueue* queues;  // One per worker
    GlobalQueue overflow;       // Shared overflow
    pthread_mutex_t lock;
    bool running;
} FiberPool;

void fiber_pool_init(int n);
void fiber_pool_shutdown(void);
```

**Acceptance:**
- Fibers can run on any worker thread
- Work-stealing for load balancing
- Configurable parallelism

---

## Task Dependency Graph

```
Phase 1 ─────► Phase 2 ─────► Phase 3 ─────► Phase 4
(Remove         (Rename        (Wire up       (Scoped
 pthread         internal       primitives)    cleanup)
 channels)       APIs)              │
                                    │
                                    ▼
                              Phase 5 ─────► Phase 6
                              (Rename OS      (Thread
                               threads)        pool)
```

---

## Example Usage After Implementation

```scheme
;; Basic fiber
(define f (fiber (+ 1 2)))
(resume f)  ;; => 3

;; Generator pattern
(define counter
  (fiber
    (let loop ((n 0))
      (yield n)
      (loop (+ n 1)))))

(resume counter)  ;; => 0
(resume counter)  ;; => 1
(resume counter)  ;; => 2

;; Spawned fiber with join
(define result
  (join (spawn (fiber (expensive-computation)))))

;; Channel communication
(define ch (chan))
(spawn (fiber (send ch 42)))
(recv ch)  ;; => 42

;; Scoped cleanup
(with-fibers
  (spawn (fiber (worker 1)))
  (spawn (fiber (worker 2)))
  (spawn (fiber (worker 3)))
  (collect-results))
;; All workers joined/canceled on exit

;; Cancellation
(define f (spawn (fiber
  (let loop ()
    (when (not (canceled?))
      (do-work)
      (loop))))))
(sleep 1000)
(cancel f)

;; OS thread for true parallelism
(define t (thread (lambda () (cpu-bound-work))))
(thread-join t)
```

---

## Files to Modify

| File | Changes |
|------|---------|
| `runtime/src/runtime.c` | Remove pthread channels, rename thread APIs |
| `runtime/src/memory/continuation.c` | Rename Task→Fiber, add pool |
| `runtime/src/memory/continuation.h` | Update structs and prototypes |
| `omnilisp/src/runtime/eval/omni_eval.c` | Add fiber primitives |
| `omnilisp/src/runtime/types.h` | Update tags if needed |

---

## Testing Strategy

1. **Unit tests:** Each primitive in isolation
2. **Integration:** Producer-consumer with channels
3. **Stress:** Many fibers, work-stealing correctness
4. **Cancellation:** Proper cleanup on cancel
5. **Scoped:** `with-fibers` cleanup verification
