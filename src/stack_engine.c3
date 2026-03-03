/**
 * =============================================================================
 * STACK ENGINE — Separate-stack execution contexts for delimited continuations
 * =============================================================================
 *
 * Provides the low-level primitives for running code on independently allocated
 * stacks and switching between them. This is the implementation mechanism for:
 *
 *   - Delimited continuations (reset/shift)
 *   - Algebraic effects (handle/signal/resolve)
 *   - User-facing coroutines (coroutine/yield/resume)
 *
 * Semantic layering (what users see):
 *   continuations (reset/shift) ← the primitive
 *     ├── effects (handle/signal/resolve) ← tagged continuations
 *     └── coroutines (coroutine/yield/resume) ← a usage pattern
 *
 * Implementation layering (this file):
 *   StackContext + stack_context_switch ← x86_64 assembly
 *   StackRegion (mmap + guard page)    ← stack allocation
 *   StackPool (free-list)              ← recycling
 *   StackCtx (execution context handle)     ← lifecycle management
 *
 * Platform: x86_64 Linux only.
 */
module main;

import std::io;

// =============================================================================
// SECTION 1: PLATFORM CONSTANTS (Linux x86_64)
// =============================================================================

extern fn void* mmap(void* addr, usz length, int prot, int flags,
                     int fd, long offset) @extern("mmap");
extern fn int munmap(void* addr, usz length) @extern("munmap");
extern fn int mprotect(void* addr, usz length, int prot) @extern("mprotect");
extern fn ZString getenv(ZString name) @extern("getenv");

const int PROT_NONE  = 0x0;
const int PROT_READ  = 0x1;
const int PROT_WRITE = 0x2;
const int MAP_PRIVATE   = 0x02;
const int MAP_ANONYMOUS = 0x20;
const usz PAGE_SIZE = 4096;

// mmap returns (void*)-1 on failure
const usz MAP_FAILED_VAL = ~(usz)0;

// =============================================================================
// C HELPERS (csrc/stack_helpers.c)
// =============================================================================
// D1: FPU state save/restore (stmxcsr/ldmxcsr/fnstcw/fldcw)
extern fn void fpu_save(uint* mxcsr, uint* x87cw) @extern("fpu_save");
extern fn void fpu_restore(uint mxcsr, uint x87cw) @extern("fpu_restore");
// D2: Stack overflow detection via SIGSEGV + sigaltstack
extern fn int stack_guard_init() @extern("stack_guard_init");
extern fn void stack_guard_shutdown() @extern("stack_guard_shutdown");
extern fn void stack_guard_register(void* base, usz size) @extern("stack_guard_register");
extern fn void stack_guard_unregister(void* base) @extern("stack_guard_unregister");
extern fn int stack_guard_protected_switch(void* old_ctx, void* new_ctx) @extern("stack_guard_protected_switch");
extern fn void stack_asan_start_switch(void** fake_stack_save, void* stack_bottom, usz stack_size) @extern("stack_asan_start_switch");
extern fn void stack_asan_finish_switch(void* fake_stack_save) @extern("stack_asan_finish_switch");
extern fn void stack_raw_copy(void* dst, void* src, usz size) @extern("stack_raw_copy");
extern fn int stack_asan_enabled() @extern("stack_asan_enabled");

// =============================================================================
// SECTION 2: STACK CONTEXT (72 bytes — one cache line on most CPUs)
// =============================================================================
//
// Minimal register save area for cooperative context switching.
// Only callee-saved registers per the x86_64 System V ABI, plus RSP/RIP.
//

struct StackContext {
    ulong rbx;       // offset  0
    ulong rbp;       // offset  8
    ulong r12;       // offset 16
    ulong r13;       // offset 24
    ulong r14;       // offset 32
    ulong r15;       // offset 40
    ulong rsp;       // offset 48
    ulong rip;       // offset 56
    uint  mxcsr;     // offset 64: SSE control/status (callee-saved bits)
    uint  x87cw;     // offset 68: x87 FPU control word (callee-saved bits)
}
// Total: 72 bytes, 8-byte aligned

// =============================================================================
// SECTION 3: CONTEXT SWITCH — x86_64 assembly (~20 instructions)
// =============================================================================
//
// Symmetric context switch: saves current state into old_ctx, loads from new_ctx.
// This is the single most critical function in the stack engine.
//

/**
 * Switch execution context.
 *
 * Saves the caller's registers into old_ctx, then restores registers from
 * new_ctx and jumps to new_ctx.rip. From the caller's perspective, this
 * function "returns" when someone else switches back to old_ctx.
 *
 * @param old_ctx  Where to save current state (RDI)
 * @param new_ctx  State to restore and jump to (RSI)
 */
fn void stack_context_switch(StackContext* old_ctx, StackContext* new_ctx) @naked @extern("omni_context_switch") {
    asm {
        // ---- Save current context into old_ctx (RDI) ----

        // Callee-saved GPRs
        movq [$rdi],      $rbx;         // offset 0:  rbx
        movq [$rdi + 8],  $rbp;         // offset 8:  rbp
        movq [$rdi + 16], $r12;         // offset 16: r12
        movq [$rdi + 24], $r13;         // offset 24: r13
        movq [$rdi + 32], $r14;         // offset 32: r14
        movq [$rdi + 40], $r15;         // offset 40: r15

        // RSP: caller's stack pointer = current RSP + 8 (past return address)
        movq $rax, $rsp;
        addq $rax, 8;
        movq [$rdi + 48], $rax;         // offset 48: rsp

        // RIP: return address is at [RSP]
        movq $rax, [$rsp];
        movq [$rdi + 56], $rax;         // offset 56: rip

        // MXCSR/x87CW saved via fpu_save() in wrapper functions
        // (stack_ctx_switch_to, stack_ctx_resume, stack_ctx_suspend) before calling switch.

        // ---- Restore from new_ctx (RSI) ----

        // Callee-saved GPRs
        movq $rbx, [$rsi];              // rbx
        movq $rbp, [$rsi + 8];          // rbp
        movq $r12, [$rsi + 16];         // r12
        movq $r13, [$rsi + 24];         // r13
        movq $r14, [$rsi + 32];         // r14
        movq $r15, [$rsi + 40];         // r15

        // RSP
        movq $rsp, [$rsi + 48];

        // Jump to saved RIP via push+ret (compatible with existing context.c3 pattern)
        movq $rcx, [$rsi + 56];
        pushq $rcx;
        ret;
    }
}

// =============================================================================
// SECTION 4: COROUTINE STATUS
// =============================================================================

enum StackCtxStatus : char {
    CTX_READY,       // Created, not yet started
    CTX_RUNNING,     // Currently executing
    CTX_SUSPENDED,   // Yielded/shifted, can be resumed
    CTX_COMPLETED,   // Returned normally
    CTX_DEAD         // Error or invalidated
}

/**
 * Entry function signature for coroutines.
 */
alias StackCtxEntryFn = fn void(void*);

/**
 * Bootstrap state — tells the trampoline what function to call.
 */
struct StackCtxBootstrap {
    StackCtxEntryFn entry;
    void*       arg;
    StackCtx*       self;
}

// =============================================================================
// SECTION 5: STACK REGION (mmap + guard page)
// =============================================================================

struct StackRegion {
    void* base;        // mmap'd region start (includes guard page at bottom)
    usz   total_size;  // Total mmap'd size (guard + usable)
    usz   usable_size; // Usable stack size (total - guard page)
    void* stack_top;   // Top of usable stack (highest address, initial RSP)
}

const usz DEFAULT_STACK_SIZE = 65536;        // 64KB usable stack (normal builds)
const usz ASAN_STACK_SIZE = 262144;          // 256KB usable stack (ASAN-instrumented builds)
const usz GUARD_SIZE = PAGE_SIZE;      // 4KB guard page

fn bool stack_runtime_asan_enabled() @inline {
    $if env::ADDRESS_SANITIZER:
        return true;
    $else
        return stack_asan_enabled() != 0;
    $endif
}

/**
 * Allocate a stack region: guard page (PROT_NONE) + usable stack (RW).
 *
 * Memory layout (low → high addresses):
 *   [guard page: PROT_NONE, 4KB] [usable stack: RW, stack_size bytes]
 *                                                                    ^ stack_top
 *
 * Stack grows downward, so stack_top is the initial RSP.
 */
fn StackRegion stack_region_alloc(usz stack_size) {
    StackRegion r;
    usz total = stack_size + GUARD_SIZE;

    void* mem = mmap(null, total, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

    if ((usz)mem == MAP_FAILED_VAL) {
        r.base = null;
        r.total_size = 0;
        r.usable_size = 0;
        r.stack_top = null;
        return r;
    }

    // Guard page at lowest address — crashes on stack overflow
    mprotect(mem, GUARD_SIZE, PROT_NONE);

    r.base = mem;
    r.total_size = total;
    r.usable_size = stack_size;
    r.stack_top = (void*)((usz)mem + total);
    return r;
}

/**
 * Free a stack region.
 */
fn void stack_region_free(StackRegion* r) {
    if (r.base != null) {
        munmap(r.base, r.total_size);
        r.base = null;
    }
}

/**
 * Check if a pointer falls within this stack region's usable area.
 */
fn bool stack_region_contains(StackRegion* r, void* ptr) {
    usz p = (usz)ptr;
    usz lo = (usz)r.base + GUARD_SIZE;
    usz hi = (usz)r.stack_top;
    return p >= lo && p < hi;
}

// =============================================================================
// SECTION 6: COROUTINE HANDLE
// =============================================================================

/**
 * StackCtx — An execution context with its own stack.
 *
 * This is the runtime unit that backs delimited continuations, effect handlers,
 * and user-facing coroutines. The name "StackCtx" is internal — users see
 * "continuation" or "coroutine" depending on the abstraction layer.
 */
struct StackCtx {
    StackContext    ctx;         // Saved register state
    StackRegion    stack;       // Owned stack memory
    StackCtxStatus     status;
    void*          result;      // Return value when COMPLETED
    StackContext*   parent_ctx;  // Where to switch back to on yield/complete
    StackCtx*       parent_stack_ctx; // Parent stack context (null = main stack)
    ScopeRegion*    pinned_scope; // Scope retained across suspend points (prevents UAF)
    void*           asan_fake_stack;  // ASAN fiber-switch token for this stack
    uint           id;          // Unique identifier
    StackCtx*          pool_next;   // Free-list link for StackPool
    StackCtxBootstrap  boot;        // Bootstrap info for first switch
    void*          user_data;   // Application-level data (e.g. CoroutineThunkState*)
}

// =============================================================================
// SECTION 7: STACK POOL (free-list of recycled stacks)
// =============================================================================

struct StackPool {
    StackCtx*  free_list;   // Singly-linked list of available coros
    usz    pool_size;   // Current count in free list
    usz    max_pool;    // Max cached before freeing
    uint   next_id;     // Monotonic ID counter
    usz    stack_size;  // Default stack size for this pool
}

const usz STACK_POOL_MAX = 64;

fn void stack_pool_init(StackPool* pool) {
    pool.free_list = null;
    pool.pool_size = 0;
    pool.max_pool = STACK_POOL_MAX;
    pool.next_id = 1;
    pool.stack_size = stack_runtime_asan_enabled() ? ASAN_STACK_SIZE : DEFAULT_STACK_SIZE;
    stack_guard_init();
}

fn void stack_pool_shutdown(StackPool* pool) {
    // Free all cached coros and unregister their guard pages
    StackCtx* c = pool.free_list;
    while (c != null) {
        StackCtx* next = c.pool_next;
        stack_guard_unregister(c.stack.base);
        stack_region_free(&c.stack);
        mem::free(c);
        c = next;
    }
    pool.free_list = null;
    pool.pool_size = 0;
    stack_guard_shutdown();
}

// =============================================================================
// SECTION 8: COROUTINE LIFECYCLE
// =============================================================================

/**
 * Create a coroutine. Pops from pool if available, else allocates fresh.
 */
fn StackCtx* stack_ctx_create(StackPool* pool) {
    StackCtx* c;

    if (pool.free_list != null) {
        // Recycle from pool
        c = pool.free_list;
        pool.free_list = c.pool_next;
        pool.pool_size--;
    } else {
        // Fresh allocation
        c = (StackCtx*)mem::malloc(StackCtx.sizeof);
        c.stack = stack_region_alloc(pool.stack_size);
        if (c.stack.base == null) {
            mem::free(c);
            return null;  // mmap failed
        }
        // Register guard page for stack overflow detection
        stack_guard_register(c.stack.base, GUARD_SIZE);
    }

    // Initialize state
    StackContext empty_ctx;
    for (usz i = 0; i < StackContext.sizeof; i++) {
        ((char*)&empty_ctx)[i] = 0;
    }
    c.ctx = empty_ctx;
    c.status = CTX_READY;
    c.result = null;
    c.parent_ctx = null;
    c.parent_stack_ctx = null;
    c.pinned_scope = null;
    c.asan_fake_stack = null;
    c.pool_next = null;
    c.user_data = null;
    c.id = pool.next_id;
    pool.next_id++;

    return c;
}

fn void stack_ctx_pin_scope(StackCtx* c, ScopeRegion* scope) {
    if (c == null || scope == null) return;
    if (c.pinned_scope == scope) return;
    if (c.pinned_scope != null) scope_release(c.pinned_scope);
    c.pinned_scope = scope;
    scope_retain(scope);
}

fn void stack_ctx_unpin_scope(StackCtx* c) {
    if (c == null || c.pinned_scope == null) return;
    scope_release(c.pinned_scope);
    c.pinned_scope = null;
}

/**
 * Return a coroutine to the pool, or free it if pool is full.
 */
fn void stack_ctx_destroy(StackCtx* c, StackPool* pool) {
    stack_ctx_unpin_scope(c);
    if (pool.pool_size < pool.max_pool) {
        // Return to pool — keep the stack allocation
        c.status = CTX_DEAD;
        c.result = null;
        c.parent_ctx = null;
        c.parent_stack_ctx = null;
        c.pinned_scope = null;
        c.asan_fake_stack = null;
        c.user_data = null;
        c.pool_next = pool.free_list;
        pool.free_list = c;
        pool.pool_size++;
    } else {
        // Pool full — unregister guard page and free everything
        stack_guard_unregister(c.stack.base);
        stack_region_free(&c.stack);
        mem::free(c);
    }
}

// =============================================================================
// SECTION 9: COROUTINE INITIALIZATION (set up initial stack frame)
// =============================================================================

// Global bootstrap pointer — set before first context switch into a stack context.
// Safe because context switches are cooperative (single-threaded).
tlocal StackCtxBootstrap* g_stack_ctx_bootstrap;
// ASAN fake-stack token for the main thread stack.
tlocal void* g_main_asan_fake_stack;

fn void* stack_ctx_bottom(StackCtx* c) @inline {
    return (void*)((usz)c.stack.base + GUARD_SIZE);
}

fn void asan_switch_out(StackCtx* from_ctx, StackCtx* to_ctx) @inline {
    void** save_slot;
    if (from_ctx != null) {
        save_slot = &from_ctx.asan_fake_stack;
    } else {
        save_slot = &g_main_asan_fake_stack;
    }

    void* to_bottom = null;
    usz to_size = 0;
    if (to_ctx != null) {
        to_bottom = stack_ctx_bottom(to_ctx);
        to_size = to_ctx.stack.usable_size;
    }
    stack_asan_start_switch(save_slot, to_bottom, to_size);
}

fn void asan_switch_in(StackCtx* current_ctx) @inline {
    void* token = current_ctx != null ? current_ctx.asan_fake_stack : g_main_asan_fake_stack;
    stack_asan_finish_switch(token);
}

/**
 * Trampoline that runs when a coroutine starts for the first time.
 *
 * This is a normal C3 function (not naked), so it can call arbitrary code.
 * It reads the bootstrap info from the thread-local, calls the entry function,
 * then marks the coroutine as completed and switches back to the parent.
 */
fn void stack_ctx_trampoline() {
    StackCtxBootstrap* boot = g_stack_ctx_bootstrap;
    StackCtxEntryFn entry = boot.entry;
    void* arg = boot.arg;

    // Set current context from bootstrap (only valid for first switch)
    g_current_stack_ctx = boot.self;
    asan_switch_in(g_current_stack_ctx);

    // Call the actual entry function
    entry(arg);

    // Entry returned — mark as completed and switch back to parent.
    // IMPORTANT: Re-read g_current_stack_ctx here (do NOT use the stale boot.self).
    // After stack_ctx_clone + stack_ctx_resume, g_current_stack_ctx points to the clone,
    // but local variables on the cloned stack still reference the original.
    StackCtx* self = g_current_stack_ctx;
    self.status = CTX_COMPLETED;
    g_current_stack_ctx = null;
    asan_switch_out(self, self.parent_stack_ctx);
    stack_context_switch(&self.ctx, self.parent_ctx);

    // Should never reach here
}

/**
 * Initialize a coroutine's context to start executing entry(arg).
 *
 * Sets up the stack so that the first context_switch into this context
 * will jump to stack_ctx_trampoline, which reads the bootstrap info and
 * calls entry(arg).
 *
 * @param c     Stack context to initialize
 * @param entry Entry function: fn void(void*)
 * @param arg   Argument passed to entry
 */
fn void stack_ctx_init(StackCtx* c, StackCtxEntryFn entry, void* arg) {
    // Start RSP at top of usable stack, 16-byte aligned
    usz sp = (usz)c.stack.stack_top;
    sp &= ~(usz)15;  // Align to 16

    // SysV ABI: RSP must be 16-byte aligned BEFORE a CALL instruction.
    // The trampoline is a normal function — when context_switch does
    // push+ret to enter it, that simulates a CALL (pushes 8-byte return addr).
    // So we need RSP % 16 == 0 at the push point, which means RSP % 16 == 8
    // after the push, which is what the function prologue expects.
    sp -= 8;

    c.ctx.rsp = sp;
    c.ctx.rip = (ulong)&stack_ctx_trampoline;
    c.ctx.rbp = 0;  // Clean frame pointer (no parent frame)
    c.ctx.rbx = 0;
    c.ctx.r12 = 0;
    c.ctx.r13 = 0;
    c.ctx.r14 = 0;
    c.ctx.r15 = 0;

    // Default FPU control values
    c.ctx.mxcsr = 0x1F80;  // All exceptions masked
    c.ctx.x87cw = 0x037F;  // Double precision, all masked

    // Store bootstrap info on the StackCtx itself (read by trampoline)
    c.boot.entry = entry;
    c.boot.arg = arg;
    c.boot.self = c;

    c.status = CTX_READY;
}

/**
 * Switch into a stack context. Sets up bootstrap and parent, then context-switches.
 *
 * @param c          Stack context to run
 * @param parent_ctx Where to save the caller's context
 */
fn void stack_ctx_switch_to(StackCtx* c, StackContext* parent_ctx) {
    c.parent_ctx = parent_ctx;

    // Set bootstrap for first switch (trampoline reads this)
    if (c.status == CTX_READY) {
        g_stack_ctx_bootstrap = &c.boot;
        c.status = CTX_RUNNING;
    }

    StackCtx* saved = g_current_stack_ctx;
    c.parent_stack_ctx = saved;
    g_current_stack_ctx = c;

    // D1: Save parent FPU state before switch
    fpu_save(&parent_ctx.mxcsr, &parent_ctx.x87cw);

    // D2: Protected switch with stack overflow recovery
    asan_switch_out(saved, c);
    int overflow = stack_guard_protected_switch(parent_ctx, &c.ctx);
    asan_switch_in(saved);

    // D1: Restore parent FPU state (also after overflow — siglongjmp doesn't restore FPU)
    fpu_restore(parent_ctx.mxcsr, parent_ctx.x87cw);

    if (overflow != 0) {
        c.status = CTX_DEAD;
    }

    g_current_stack_ctx = saved;
}

// Thread-local: currently executing coroutine (null if on main stack)
tlocal StackCtx* g_current_stack_ctx;

/**
 * Suspend the current coroutine and switch back to its parent.
 * This is the primitive behind yield/shift/signal.
 */
fn void stack_ctx_suspend() {
    StackCtx* c = g_current_stack_ctx;
    assert(c != null, "stack_ctx_suspend: not inside a coroutine");
    c.status = CTX_SUSPENDED;
    // D1: Save FPU state before switch to parent
    fpu_save(&c.ctx.mxcsr, &c.ctx.x87cw);
    asan_switch_out(c, c.parent_stack_ctx);
    stack_context_switch(&c.ctx, c.parent_ctx);
    asan_switch_in(c);
    // Execution resumes here when parent calls stack_ctx_resume
    // D1: Restore FPU state on resume
    fpu_restore(c.ctx.mxcsr, c.ctx.x87cw);
}

/**
 * Resume a suspended coroutine. Updates tracking and switches.
 */
fn void stack_ctx_resume(StackCtx* c, StackContext* parent_ctx) {
    assert(c.status == CTX_SUSPENDED, "stack_ctx_resume: context not suspended");
    c.parent_ctx = parent_ctx;
    c.status = CTX_RUNNING;
    StackCtx* saved = g_current_stack_ctx;
    c.parent_stack_ctx = saved;
    g_current_stack_ctx = c;

    // D1: Save parent FPU state before switch
    fpu_save(&parent_ctx.mxcsr, &parent_ctx.x87cw);

    // D2: Protected switch with stack overflow recovery
    asan_switch_out(saved, c);
    int overflow = stack_guard_protected_switch(parent_ctx, &c.ctx);
    asan_switch_in(saved);

    // D1: Restore parent FPU state
    fpu_restore(parent_ctx.mxcsr, parent_ctx.x87cw);

    if (overflow != 0) {
        c.status = CTX_DEAD;
    }

    g_current_stack_ctx = saved;
}

// Also update stack_ctx_switch_to to track current context
// (done inline below in the existing stack_ctx_switch_to — we add tracking)

// =============================================================================
// SECTION 10: STACK CLONING (for multi-shot continuations)
// =============================================================================

/**
 * Clone a suspended coroutine for multi-shot continuation invocation.
 *
 * Creates a new coroutine with an identical copy of the source's stack
 * contents and register state. Adjusts all frame pointers (RBP chain)
 * in the cloned stack to point into the new stack.
 *
 * @param source  Suspended coroutine to clone
 * @param pool    Stack pool for allocation
 * @return New coroutine (caller owns), or null on failure
 */
fn StackCtx* stack_ctx_clone(StackCtx* source, StackPool* pool) {
    if (source.status != CTX_SUSPENDED) return null;
    if (source.stack.base == null || source.stack.stack_top == null || source.stack.usable_size == 0) return null;

    usz src_lo = (usz)source.stack.base + GUARD_SIZE;
    usz src_hi = (usz)source.stack.stack_top;
    usz src_rsp = (usz)source.ctx.rsp;
    if (src_rsp < src_lo || src_rsp > src_hi) return null;
    if ((usz)source.ctx.rbp != 0 && ((usz)source.ctx.rbp < src_lo || (usz)source.ctx.rbp > src_hi)) return null;

    StackCtx* clone = stack_ctx_create(pool);
    if (clone == null) return null;

    // Copy only the active stack segment [RSP, stack_top). This preserves live frames
    // and avoids touching unrelated bytes below RSP.
    usz active_bytes = src_hi - src_rsp;
    if (active_bytes > clone.stack.usable_size) {
        stack_ctx_destroy(clone, pool);
        return null;
    }

    usz dst_hi = (usz)clone.stack.stack_top;
    usz dst_rsp = dst_hi - active_bytes;
    stack_raw_copy((void*)dst_rsp, (void*)src_rsp, active_bytes);

    // Copy register context and relocate stack pointers into the cloned stack.
    clone.ctx = source.ctx;
    clone.status = CTX_SUSPENDED;
    clone.result = source.result;
    clone.parent_ctx = null;
    clone.parent_stack_ctx = null;
    clone.asan_fake_stack = null;

    long delta = (long)(dst_rsp - src_rsp);
    clone.ctx.rsp = (ulong)((long)source.ctx.rsp + delta);
    clone.ctx.rbp = (ulong)((long)source.ctx.rbp + delta);

    // Walk and fixup the RBP chain on the cloned stack.
    usz lo = (usz)clone.stack.base + GUARD_SIZE;
    usz hi = (usz)clone.stack.stack_top;

    ulong* bp = (ulong*)(ulong)clone.ctx.rbp;
    for (int safety = 0; safety < 10000; safety++) {
        usz bp_addr = (usz)bp;
        if (bp_addr < lo || bp_addr >= hi || bp_addr == 0) break;

        ulong old_val = *bp;
        if (old_val == 0) break;  // End of chain

        if (old_val >= src_lo && old_val < src_hi) {
            *bp = (ulong)((long)old_val + delta);
        } else {
            break;
        }

        bp = (ulong*)*bp;
    }

    return clone;
}

// =============================================================================
// SECTION 11: TESTS
// =============================================================================

// --- Test helpers ---

struct TestState {
    int value;
    int step;
}

fn void test_entry_simple(void* arg) {
    TestState* s = (TestState*)arg;
    s.value = 42;
}

fn void test_entry_100(void* arg) {
    TestState* s = (TestState*)arg;
    s.value = 100;
}

// Generator-style entry: yields 3 values then completes
fn void test_entry_generator(void* arg) {
    TestState* s = (TestState*)arg;

    s.value = 10;
    s.step = 1;
    stack_ctx_suspend();  // yield 10

    s.value = 20;
    s.step = 2;
    stack_ctx_suspend();  // yield 20

    s.value = 30;
    s.step = 3;
    // returns — trampoline marks COMPLETED
}

// Deeply recursive entry — tests stack depth on separate stack
fn void test_entry_recursive(void* arg) {
    TestState* s = (TestState*)arg;
    s.value = test_recursive_sum(100);
}

fn int test_recursive_sum(int n) {
    if (n <= 0) return 0;
    return n + test_recursive_sum(n - 1);
}

// --- Tests ---

fn bool test_stack_region() {
    StackRegion r = stack_region_alloc(DEFAULT_STACK_SIZE);
    if (r.base == null) return false;

    bool ok = r.total_size == DEFAULT_STACK_SIZE + GUARD_SIZE;
    ok = ok && r.usable_size == DEFAULT_STACK_SIZE;
    ok = ok && r.stack_top == (void*)((usz)r.base + r.total_size);

    // Verify usable area is writable
    char* mid = (char*)((usz)r.base + GUARD_SIZE + DEFAULT_STACK_SIZE / 2);
    *mid = 0xAB;
    ok = ok && *mid == (char)0xAB;

    stack_region_free(&r);
    return ok;
}

fn bool test_pool_lifecycle() {
    StackPool pool;
    stack_pool_init(&pool);

    StackCtx* c1 = stack_ctx_create(&pool);
    if (c1 == null) return false;
    uint id1 = c1.id;

    stack_ctx_destroy(c1, &pool);
    if (pool.pool_size != 1) return false;

    StackCtx* c2 = stack_ctx_create(&pool);
    if (c2 == null) return false;
    if (pool.pool_size != 0) return false;

    bool same_ptr = (c2 == c1);
    bool new_id = (c2.id != id1);

    stack_ctx_destroy(c2, &pool);
    stack_pool_shutdown(&pool);
    return same_ptr && new_id;
}

fn bool test_basic_coro() {
    StackPool pool;
    stack_pool_init(&pool);

    StackCtx* c = stack_ctx_create(&pool);
    if (c == null) { stack_pool_shutdown(&pool); return false; }

    TestState state;
    state.value = 0;

    stack_ctx_init(c, &test_entry_simple, &state);

    // Switch into the context — it runs test_entry_simple, sets value=42,
    // returns, trampoline marks COMPLETED and switches back
    StackContext main_ctx;
    stack_ctx_switch_to(c, &main_ctx);

    bool ok = (c.status == CTX_COMPLETED) && (state.value == 42);

    stack_ctx_destroy(c, &pool);
    stack_pool_shutdown(&pool);
    return ok;
}

fn bool test_stack_ctx_second() {
    StackPool pool;
    stack_pool_init(&pool);

    StackCtx* c = stack_ctx_create(&pool);
    if (c == null) { stack_pool_shutdown(&pool); return false; }

    TestState state;
    state.value = 0;

    stack_ctx_init(c, &test_entry_100, &state);

    StackContext main_ctx;
    stack_ctx_switch_to(c, &main_ctx);

    bool ok = (c.status == CTX_COMPLETED) && (state.value == 100);

    stack_ctx_destroy(c, &pool);
    stack_pool_shutdown(&pool);
    return ok;
}

// THE critical test: suspend/resume (generator pattern)
fn bool test_suspend_resume() {
    StackPool pool;
    stack_pool_init(&pool);

    StackCtx* c = stack_ctx_create(&pool);
    if (c == null) { stack_pool_shutdown(&pool); return false; }

    TestState state;
    state.value = 0;
    state.step = 0;

    stack_ctx_init(c, &test_entry_generator, &state);

    // First switch: runs until first stack_ctx_suspend()
    StackContext main_ctx;
    stack_ctx_switch_to(c, &main_ctx);

    bool ok = (c.status == CTX_SUSPENDED) && (state.value == 10) && (state.step == 1);

    // Resume: runs until second stack_ctx_suspend()
    stack_ctx_resume(c, &main_ctx);

    ok = ok && (c.status == CTX_SUSPENDED) && (state.value == 20) && (state.step == 2);

    // Resume again: runs to completion
    stack_ctx_resume(c, &main_ctx);

    ok = ok && (c.status == CTX_COMPLETED) && (state.value == 30) && (state.step == 3);

    stack_ctx_destroy(c, &pool);
    stack_pool_shutdown(&pool);
    return ok;
}

// Test that recursive code works on separate stack
fn bool test_stack_ctx_recursion() {
    StackPool pool;
    stack_pool_init(&pool);

    StackCtx* c = stack_ctx_create(&pool);
    if (c == null) { stack_pool_shutdown(&pool); return false; }

    TestState state;
    state.value = 0;

    stack_ctx_init(c, &test_entry_recursive, &state);

    StackContext main_ctx;
    stack_ctx_switch_to(c, &main_ctx);

    // sum(1..100) = 5050
    bool ok = (c.status == CTX_COMPLETED) && (state.value == 5050);

    stack_ctx_destroy(c, &pool);
    stack_pool_shutdown(&pool);
    return ok;
}

// Test two coroutines interleaving
fn bool test_two_coros_interleaved() {
    StackPool pool;
    stack_pool_init(&pool);

    StackCtx* c1 = stack_ctx_create(&pool);
    StackCtx* c2 = stack_ctx_create(&pool);
    if (c1 == null || c2 == null) { stack_pool_shutdown(&pool); return false; }

    TestState s1;
    TestState s2;
    s1.value = 0; s1.step = 0;
    s2.value = 0; s2.step = 0;

    stack_ctx_init(c1, &test_entry_generator, &s1);
    stack_ctx_init(c2, &test_entry_generator, &s2);

    StackContext main_ctx;

    // Run c1 to first yield
    stack_ctx_switch_to(c1, &main_ctx);
    bool ok = (s1.value == 10) && (c1.status == CTX_SUSPENDED);

    // Run c2 to first yield
    stack_ctx_switch_to(c2, &main_ctx);
    ok = ok && (s2.value == 10) && (c2.status == CTX_SUSPENDED);

    // Resume c1 to second yield
    stack_ctx_resume(c1, &main_ctx);
    ok = ok && (s1.value == 20);

    // Resume c2 to completion (skip second yield, go to end)
    stack_ctx_resume(c2, &main_ctx);
    ok = ok && (s2.value == 20) && (c2.status == CTX_SUSPENDED);
    stack_ctx_resume(c2, &main_ctx);
    ok = ok && (s2.value == 30) && (c2.status == CTX_COMPLETED);

    // Resume c1 to completion
    stack_ctx_resume(c1, &main_ctx);
    ok = ok && (s1.value == 30) && (c1.status == CTX_COMPLETED);

    stack_ctx_destroy(c1, &pool);
    stack_ctx_destroy(c2, &pool);
    stack_pool_shutdown(&pool);
    return ok;
}

// Stack overflow entry — recurses until guard page hit
fn void test_entry_overflow(void* arg) {
    TestState* s = (TestState*)arg;
    test_overflow_recurse(s, 0);
}

fn int test_overflow_recurse(TestState* s, int depth) {
    s.value = depth;
    // 256-byte frame ensures we overflow the 64KB stack quickly (~250 frames)
    char[256] padding;
    padding[0] = (char)depth;
    return test_overflow_recurse(s, depth + 1) + (int)padding[0];
}

fn bool test_stack_overflow_recovery() {
    StackPool pool;
    stack_pool_init(&pool);

    StackCtx* c = stack_ctx_create(&pool);
    if (c == null) { stack_pool_shutdown(&pool); return false; }

    TestState state;
    state.value = 0;

    stack_ctx_init(c, &test_entry_overflow, &state);

    StackContext main_ctx;
    stack_ctx_switch_to(c, &main_ctx);

    // Should recover gracefully — context marked DEAD, not crashed
    bool ok = (c.status == CTX_DEAD);
    // Should have recursed some before overflow
    ok = ok && (state.value > 10);

    stack_ctx_destroy(c, &pool);
    stack_pool_shutdown(&pool);
    return ok;
}

// Test that FPU state is preserved across context switches
fn void test_entry_fpu(void* arg) {
    TestState* s = (TestState*)arg;
    // The FPU state should be default values on entry
    // Just set a marker to confirm we ran
    s.value = 99;
    stack_ctx_suspend();
    s.value = 100;
}

fn bool test_fpu_preservation() {
    StackPool pool;
    stack_pool_init(&pool);

    StackCtx* c = stack_ctx_create(&pool);
    if (c == null) { stack_pool_shutdown(&pool); return false; }

    TestState state;
    state.value = 0;
    stack_ctx_init(c, &test_entry_fpu, &state);

    // Save current FPU state
    uint mxcsr_before;
    uint x87cw_before;
    fpu_save(&mxcsr_before, &x87cw_before);

    StackContext main_ctx;
    stack_ctx_switch_to(c, &main_ctx);  // runs until suspend
    bool ok = (state.value == 99) && (c.status == CTX_SUSPENDED);

    // Verify parent FPU state preserved after switch
    uint mxcsr_after;
    uint x87cw_after;
    fpu_save(&mxcsr_after, &x87cw_after);
    ok = ok && (mxcsr_before == mxcsr_after);
    ok = ok && (x87cw_before == x87cw_after);

    // Resume and complete
    stack_ctx_resume(c, &main_ctx);
    ok = ok && (state.value == 100) && (c.status == CTX_COMPLETED);

    // FPU still preserved
    fpu_save(&mxcsr_after, &x87cw_after);
    ok = ok && (mxcsr_before == mxcsr_after);
    ok = ok && (x87cw_before == x87cw_after);

    stack_ctx_destroy(c, &pool);
    stack_pool_shutdown(&pool);
    return ok;
}

// Clone a suspended context and verify multi-shot behavior:
// cloned execution completes independently while the source remains resumable.
fn bool test_stack_ctx_clone_multishot() {
    StackPool pool;
    stack_pool_init(&pool);

    StackCtx* source = stack_ctx_create(&pool);
    if (source == null) { stack_pool_shutdown(&pool); return false; }

    TestState state;
    state.value = 0;
    state.step = 0;
    stack_ctx_init(source, &test_entry_generator, &state);

    StackContext main_ctx;
    stack_ctx_switch_to(source, &main_ctx);  // yield #1
    if (source.status != CTX_SUSPENDED || state.step != 1) {
        stack_ctx_destroy(source, &pool);
        stack_pool_shutdown(&pool);
        return false;
    }

    StackCtx* clone = stack_ctx_clone(source, &pool);
    if (clone == null || clone.status != CTX_SUSPENDED) {
        stack_ctx_destroy(source, &pool);
        if (clone != null) stack_ctx_destroy(clone, &pool);
        stack_pool_shutdown(&pool);
        return false;
    }

    // Drive clone to completion first.
    stack_ctx_resume(clone, &main_ctx);  // yield #2
    bool ok = (clone.status == CTX_SUSPENDED);
    stack_ctx_resume(clone, &main_ctx);  // complete
    ok = ok && (clone.status == CTX_COMPLETED);

    // Source must remain suspended and independently resumable.
    ok = ok && (source.status == CTX_SUSPENDED);
    stack_ctx_resume(source, &main_ctx);
    ok = ok && (source.status == CTX_SUSPENDED);
    stack_ctx_resume(source, &main_ctx);
    ok = ok && (source.status == CTX_COMPLETED);

    stack_ctx_destroy(clone, &pool);
    stack_ctx_destroy(source, &pool);
    stack_pool_shutdown(&pool);
    return ok;
}

/**
 * Run all stack engine tests.
 */
fn void run_stack_engine_tests() {
    io::printfn("\n========================================");
    io::printfn("STACK ENGINE TESTS");
    io::printfn("========================================\n");

    int pass = 0;
    int fail = 0;

    if (test_stack_region()) { io::printfn("  PASS: stack region alloc/free"); pass++; }
    else { io::printfn("  FAIL: stack region alloc/free"); fail++; }

    if (test_pool_lifecycle()) { io::printfn("  PASS: pool create/recycle"); pass++; }
    else { io::printfn("  FAIL: pool create/recycle"); fail++; }

    if (test_basic_coro()) { io::printfn("  PASS: basic context run + complete"); pass++; }
    else { io::printfn("  FAIL: basic context run + complete"); fail++; }

    if (test_stack_ctx_second()) { io::printfn("  PASS: second context entry"); pass++; }
    else { io::printfn("  FAIL: second context entry"); fail++; }

    if (test_suspend_resume()) { io::printfn("  PASS: suspend/resume (generator)"); pass++; }
    else { io::printfn("  FAIL: suspend/resume (generator)"); fail++; }

    if (test_stack_ctx_recursion()) { io::printfn("  PASS: recursion on separate stack"); pass++; }
    else { io::printfn("  FAIL: recursion on separate stack"); fail++; }

    if (test_two_coros_interleaved()) { io::printfn("  PASS: two contexts interleaved"); pass++; }
    else { io::printfn("  FAIL: two contexts interleaved"); fail++; }

    if (test_fpu_preservation()) { io::printfn("  PASS: FPU state preserved across switch"); pass++; }
    else { io::printfn("  FAIL: FPU state preserved across switch"); fail++; }

    if (stack_runtime_asan_enabled()) {
        io::printfn("  SKIP: stack overflow recovery (ASAN runtime)");
    } else if (test_stack_overflow_recovery()) { io::printfn("  PASS: stack overflow recovery"); pass++; }
    else { io::printfn("  FAIL: stack overflow recovery"); fail++; }

    if (test_stack_ctx_clone_multishot()) { io::printfn("  PASS: clone/resume multi-shot"); pass++; }
    else { io::printfn("  FAIL: clone/resume multi-shot"); fail++; }

    io::printfn("\nStack engine: %d passed, %d failed", pass, fail);
    io::printfn("========================================\n");
}
