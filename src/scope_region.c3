/**
 * =============================================================================
 * SCOPE REGION — Lightweight bump-allocating scopes with reference counting
 * =============================================================================
 *
 * Replaces the heavyweight Region system for Value/Env allocation with
 * fine-grained scopes at let/loop/lambda boundaries.
 *
 * Key properties:
 * - Bump allocation: ~3 instructions per alloc (load, add, compare)
 * - Reference counting: scopes freed deterministically at known program points
 * - No cycles: closures form a lexical tree (child→parent, never reverse)
 * - Freelist recycling: ScopeRegion structs reused across scope lifetimes
 * - Destructor list: heap-backed values (strings, arrays, etc.) cleaned up on scope death
 *
 * Memory layout:
 *   ScopeRegion → ScopeChunk (linked list of bump chunks)
 *                 ScopeDtor  (linked list of destructors, bump-allocated in chunks)
 */
module main;

import std::io;
import std::thread;

extern fn ZString scope_c_getenv(ZString name) @extern("getenv");

// =============================================================================
// Types
// =============================================================================

alias ScopeDestructorFn = fn void(void* ptr);

// =============================================================================
// Structs
// =============================================================================

/**
 * ScopeChunk — A contiguous block of memory for bump allocation.
 * Chunks form a singly-linked list (newest first). Data follows inline
 * after the struct fields.
 */
struct ScopeChunk {
    ScopeChunk* next;       // Previous chunk (linked list, newest→oldest)
    usz         capacity;   // Usable bytes in this chunk (after header)
}

/**
 * ScopeDtor — Destructor entry for heap-backed values.
 * Allocated within the scope's own chunks (freed automatically).
 */
struct ScopeDtor {
    ScopeDtor*       next;  // Linked list (LIFO — last registered runs first)
    void*            ptr;   // Object to destroy
    ScopeDestructorFn func; // Destructor function
}

/**
 * ScopeRegion — A lightweight, bump-allocating memory scope.
 *
 * Typical size: ~80 bytes (vs hundreds for the old Region).
 * Fast path allocation: load bump, add size, compare limit.
 */
struct ScopeRegion {
    // Identity & lifetime
    uint         generation;    // Incremented on recycle (for handle validation)
    uint         refcount;      // +1 per external reference (owner + captures)

    // Hierarchy
    ScopeRegion* parent;        // Lexical parent scope (retained via RC)

    // Bump allocator
    char*        bump;          // Current allocation pointer
    char*        limit;         // End of current chunk's data area
    ScopeChunk*  chunks;        // Linked list of chunks (head = current)

    // Escape lane bump allocator (return values / boundary survivors)
    char*        escape_bump;   // Current escape allocation pointer
    char*        escape_limit;  // End of current escape chunk's data area
    ScopeChunk*  escape_chunks; // Linked list of escape chunks (head = current)

    // Cleanup
    ScopeDtor*   dtors;         // Destructor list (LIFO)
    ScopeDtor*   escape_dtors;  // Escape-lane destructors (LIFO)
    usz          alloc_bytes;   // Total bytes allocated (for split decisions)
    usz          alloc_count;   // Object count (for stats)
    uint         escape_generation; // Unique stamp for ESCAPE-lane membership checks
    void*        owner_thread_token; // Thread-affinity token for runtime safety checks
    bool         fiber_temp_eligible; // True when scope may use fiber-temp TEMP backing

    // Recycling
    ScopeRegion* pool_next;     // Freelist link (only used when in freelist)
}

// =============================================================================
// Constants
// =============================================================================

const usz SCOPE_CHUNK_INITIAL   = 512;      // First chunk size (bytes)
const usz SCOPE_CHUNK_MAX       = 65536;    // Maximum chunk size (64KB)

// =============================================================================
// Global freelist
// =============================================================================

ScopeRegion* g_scope_freelist = null;
usz g_scope_freelist_count = 0;
const usz SCOPE_FREELIST_MAX = 64;  // Don't hoard too many recycled scopes

// Global monotonic generation counter — ensures every scope gets a unique generation.
// Used by Value.scope_gen stamps for O(1) scope membership checks.
uint g_scope_generation_counter = 0;

thread::Mutex g_scope_global_mu;
thread::OnceFlag g_scope_global_once;
tlocal char g_scope_thread_marker;

fn void scope_global_mu_init_once() {
    g_scope_global_mu.init()!!;
}

fn void scope_global_lock() @inline {
    g_scope_global_once.call(&scope_global_mu_init_once);
    g_scope_global_mu.lock();
}

fn void scope_global_unlock() @inline {
    g_scope_global_mu.unlock();
}

fn bool scope_freelist_recycle_enabled() @inline {
    // Under ASAN prefer direct free over freelist reuse to reduce stale-pointer
    // reuse noise and surface the first invalid writer.
    $if env::ADDRESS_SANITIZER:
        return false;
    $else
        return true;
    $endif
}

fn void* scope_current_thread_token() @inline {
    return (void*)&g_scope_thread_marker;
}

fn void scope_thread_violation(char[] op) @inline {
    io::eprintfn("scope-region thread-affinity violation in %s", op);
    unreachable("scope-region accessed from non-owner thread");
}

fn void scope_require_owner(ScopeRegion* scope, char[] op) @inline {
    if (scope == null) return;
    if (scope.owner_thread_token != scope_current_thread_token()) {
        scope_thread_violation(op);
    }
}

macro scope_guard_owner(scope, op) {
    scope_require_owner(scope, op);
}

macro scope_invariant_refcount_live(scope) {
    assert(scope.refcount > 0, "scope invariant: refcount must be > 0");
}

macro scope_invariant_distinct_scopes(parent, child) {
    assert(parent != child, "scope invariant: parent and child must differ");
}

struct ScopeTransferStats {
    usz splice_calls;
    usz splice_escape_chunk_bytes;
    usz splice_temp_chunk_bytes_freed;
    usz splice_temp_dtors_run;
    usz splice_escape_dtors_transferred;
}

ScopeTransferStats g_scope_transfer_stats;

struct FiberTempPoolStats {
    usz take_hits;
    usz take_misses;
    usz return_count;
    usz drop_frees;
    usz peak_pool_count;
    usz eligible_slow_allocs;
    usz bypass_large_allocs;
    usz bypass_escape_activity_allocs;
}

ScopeChunk* g_fiber_temp_chunk_pool = null;
usz g_fiber_temp_chunk_pool_count = 0;
const usz FIBER_TEMP_CHUNK_POOL_MAX = 2048;
const usz FIBER_TEMP_ELIGIBLE_MAX_ALLOC = 4096;
FiberTempPoolStats g_fiber_temp_pool_stats;

fn bool scope_fiber_temp_flag_enabled() @inline {
    return scope_c_getenv("OMNI_FIBER_TEMP") != null;
}

fn bool scope_fiber_temp_context_eligible() @inline {
    if (!scope_fiber_temp_flag_enabled()) return false;
    return g_current_stack_ctx != null;
}

fn bool fiber_temp_alloc_size_eligible(usz aligned_size) @inline {
    return aligned_size <= FIBER_TEMP_ELIGIBLE_MAX_ALLOC;
}

fn bool fiber_temp_scope_shape_eligible(ScopeRegion* scope) @inline {
    return scope.escape_chunks == null && scope.escape_dtors == null;
}

fn void fiber_temp_pool_record_peak() @inline {
    if (g_fiber_temp_chunk_pool_count > g_fiber_temp_pool_stats.peak_pool_count) {
        g_fiber_temp_pool_stats.peak_pool_count = g_fiber_temp_chunk_pool_count;
    }
}

fn ScopeChunk* fiber_temp_chunk_try_take(usz min_bytes) {
    if (!scope_fiber_temp_flag_enabled()) return null;

    scope_global_lock();
    ScopeChunk* prev = null;
    ScopeChunk* cur = g_fiber_temp_chunk_pool;
    while (cur != null) {
        if (cur.capacity >= min_bytes) {
            ScopeChunk* next = cur.next;
            if (prev != null) {
                prev.next = next;
            } else {
                g_fiber_temp_chunk_pool = next;
            }
            g_fiber_temp_chunk_pool_count--;
            g_fiber_temp_pool_stats.take_hits++;
            scope_global_unlock();
            cur.next = null;
            return cur;
        }
        prev = cur;
        cur = cur.next;
    }
    g_fiber_temp_pool_stats.take_misses++;
    scope_global_unlock();
    return null;
}

fn void scope_chunk_reclaim_temp(ScopeChunk* chunk) {
    if (chunk == null) return;
    if (!scope_fiber_temp_flag_enabled()) {
        mem::free(chunk);
        return;
    }

    scope_global_lock();
    if (g_fiber_temp_chunk_pool_count < FIBER_TEMP_CHUNK_POOL_MAX) {
        chunk.next = g_fiber_temp_chunk_pool;
        g_fiber_temp_chunk_pool = chunk;
        g_fiber_temp_chunk_pool_count++;
        g_fiber_temp_pool_stats.return_count++;
        fiber_temp_pool_record_peak();
        scope_global_unlock();
        return;
    }
    g_fiber_temp_pool_stats.drop_frees++;
    scope_global_unlock();
    mem::free(chunk);
}

fn void scope_temp_chunk_release(ScopeRegion* scope, ScopeChunk* chunk) @inline {
    if (scope != null && scope.fiber_temp_eligible) {
        scope_chunk_reclaim_temp(chunk);
        return;
    }
    mem::free(chunk);
}

fn usz scope_chunk_bytes(ScopeChunk* chunk) @inline {
    usz total = 0;
    while (chunk != null) {
        total += chunk.capacity;
        chunk = chunk.next;
    }
    return total;
}

fn usz scope_dtor_count(ScopeDtor* dtor) @inline {
    usz total = 0;
    while (dtor != null) {
        total++;
        dtor = dtor.next;
    }
    return total;
}

fn void scope_print_memory_stats() {
    io::printn("=== Scope Transfer Stats ===");
    io::printfn("scope_splice_escapes calls: %d", (long)g_scope_transfer_stats.splice_calls);
    io::printfn("scope_splice escape bytes moved: %d", (long)g_scope_transfer_stats.splice_escape_chunk_bytes);
    io::printfn("scope_splice temp bytes freed: %d", (long)g_scope_transfer_stats.splice_temp_chunk_bytes_freed);
    io::printfn("scope_splice temp dtors run: %d", (long)g_scope_transfer_stats.splice_temp_dtors_run);
    io::printfn("scope_splice escape dtors moved: %d", (long)g_scope_transfer_stats.splice_escape_dtors_transferred);
}

// =============================================================================
// Chunk allocation
// =============================================================================

/**
 * Allocate a new chunk with at least `min_bytes` of usable data space.
 * The chunk struct header is placed at the start; data follows inline.
 */
fn ScopeChunk* scope_chunk_alloc_raw(usz min_bytes) {
    // Double from initial, capped at max
    usz capacity = SCOPE_CHUNK_INITIAL;
    while (capacity < min_bytes) {
        capacity *= 2;
        if (capacity > SCOPE_CHUNK_MAX) {
            capacity = min_bytes;  // Oversized allocation — exact fit
            break;
        }
    }
    usz total = ScopeChunk.sizeof + capacity;
    ScopeChunk* chunk = (ScopeChunk*)mem::malloc(total);
    chunk.next = null;
    chunk.capacity = capacity;
    return chunk;
}

fn ScopeChunk* scope_chunk_alloc_temp(usz min_bytes) {
    ScopeChunk* pooled = fiber_temp_chunk_try_take(min_bytes);
    if (pooled != null) return pooled;
    return scope_chunk_alloc_raw(min_bytes);
}

/**
 * Get the data start pointer for a chunk (immediately after the header).
 */
fn char* scope_chunk_data(ScopeChunk* chunk) @inline {
    return (char*)chunk + ScopeChunk.sizeof;
}

// =============================================================================
// ScopeRegion lifecycle
// =============================================================================

<*
 * Create a new ScopeRegion, optionally as a child of `parent`.
 * If parent is non-null, it is retained (RC incremented).
 * Returns a scope with refcount=1 (the caller owns it).
 @ensure return != null
*>
fn ScopeRegion* scope_create(ScopeRegion* parent = null) {
    ScopeRegion* scope = null;
    uint generation = 0;
    uint escape_generation = 0;
    bool fiber_temp_eligible = scope_fiber_temp_context_eligible() && parent != null;

    // Global freelist and generation stamps are shared across threads.
    if (scope_freelist_recycle_enabled()) {
        scope_global_lock();
        if (g_scope_freelist != null) {
            scope = g_scope_freelist;
            g_scope_freelist = scope.pool_next;
            g_scope_freelist_count--;
        }
        generation = ++g_scope_generation_counter;
        escape_generation = ++g_scope_generation_counter;
        scope_global_unlock();
    } else {
        scope_global_lock();
        generation = ++g_scope_generation_counter;
        escape_generation = ++g_scope_generation_counter;
        scope_global_unlock();
    }

    if (scope == null) {
        scope = (ScopeRegion*)mem::malloc(ScopeRegion.sizeof);
        scope.chunks = null;
        scope.escape_chunks = null;
    }
    scope.generation = generation;
    scope.escape_generation = escape_generation;

    // Reuse existing chunk if present, else allocate
    ScopeChunk* chunk = scope.chunks;
    if (chunk == null) {
        chunk = fiber_temp_eligible
            ? scope_chunk_alloc_temp(SCOPE_CHUNK_INITIAL)
            : scope_chunk_alloc_raw(SCOPE_CHUNK_INITIAL);
        chunk.next = null;
        scope.chunks = chunk;
    }

    // Initialize scope
    scope.refcount = 1;
    scope.parent = parent;
    scope.bump = scope_chunk_data(chunk);
    scope.limit = scope.bump + chunk.capacity;
    scope.escape_bump = null;
    scope.escape_limit = null;
    scope.dtors = null;
    scope.escape_dtors = null;
    scope.alloc_bytes = 0;
    scope.alloc_count = 0;
    scope.pool_next = null;
    scope.owner_thread_token = scope_current_thread_token();
    scope.fiber_temp_eligible = fiber_temp_eligible;

    // Retain parent
    if (parent != null) {
        scope_retain(parent);
    }

    return scope;
}

/**
 * Increment the reference count of a scope.
 */
fn void scope_retain(ScopeRegion* scope) @inline {
    if (scope == null) return;
    scope_guard_owner(scope, "scope_retain");
    scope_invariant_refcount_live(scope);
    scope.refcount++;
}

/**
 * Decrement the reference count. Destroys the scope when RC hits 0.
 */
fn void scope_release(ScopeRegion* scope) @inline {
    if (scope == null) return;
    scope_guard_owner(scope, "scope_release");
    scope_invariant_refcount_live(scope);
    if (--scope.refcount == 0) {
        scope_destroy(scope);
    }
}

fn void scope_run_dtor_chain(ScopeDtor* d) @inline {
    while (d != null) {
        d.func(d.ptr);
        d = d.next;
    }
}

<* @require scope != null *>
fn void scope_run_dtors(ScopeRegion* scope) {
    scope_run_dtor_chain(scope.dtors);
}

<* @require scope != null *>
fn void scope_run_escape_dtors(ScopeRegion* scope) {
    scope_run_dtor_chain(scope.escape_dtors);
}

<*
 @require scope != null
 @require scope.refcount == 0 : "scope_destroy: refcount not zero"
*>
fn void scope_destroy(ScopeRegion* scope) {
    scope_guard_owner(scope, "scope_destroy");
    // 1. Run destructors (reverse order — LIFO) for both lanes
    scope_run_dtors(scope);
    scope_run_escape_dtors(scope);

    // 2. Release parent scope (may cascade)
    if (scope.parent != null) {
        scope_release(scope.parent);
        scope.parent = null;
    }

    // 3. Free all TEMP chunks
    ScopeChunk* c = scope.chunks;
    while (c != null) {
        ScopeChunk* next = c.next;
        scope_temp_chunk_release(scope, c);
        c = next;
    }

    // 4. Free all ESCAPE chunks
    c = scope.escape_chunks;
    while (c != null) {
        ScopeChunk* next = c.next;
        mem::free(c);
        c = next;
    }

    scope.chunks = null;
    scope.bump = null;
    scope.limit = null;
    scope.escape_chunks = null;
    scope.escape_bump = null;
    scope.escape_limit = null;
    scope.dtors = null;
    scope.escape_dtors = null;
    scope.fiber_temp_eligible = false;

    // 5. Recycle ScopeRegion struct (increment generation, add to freelist)
    scope.generation++;
    scope.escape_generation++;
    if (!scope_freelist_recycle_enabled()) {
        mem::free(scope);
        return;
    }
    scope_global_lock();
    if (g_scope_freelist_count < SCOPE_FREELIST_MAX) {
        scope.pool_next = g_scope_freelist;
        g_scope_freelist = scope;
        g_scope_freelist_count++;
        scope_global_unlock();
    } else {
        scope_global_unlock();
        mem::free(scope);
    }
}

// =============================================================================
// Bump allocator
// =============================================================================

/**
 * Slow path: allocate a new chunk when the current one is full.
 */
fn void* ScopeRegion.alloc_slow(ScopeRegion* self, usz aligned_size) {
    scope_guard_owner(self, "scope_alloc_slow");
    // Determine new chunk capacity: double the previous, or at least fit the request
    usz prev_cap = SCOPE_CHUNK_INITIAL;
    if (self.chunks != null) {
        prev_cap = self.chunks.capacity;
    }
    usz new_cap = prev_cap * 2;
    if (new_cap > SCOPE_CHUNK_MAX) new_cap = SCOPE_CHUNK_MAX;
    if (new_cap < aligned_size) new_cap = aligned_size;

    bool size_eligible = fiber_temp_alloc_size_eligible(aligned_size);
    bool shape_eligible = fiber_temp_scope_shape_eligible(self);
    bool use_fiber_temp = self.fiber_temp_eligible && size_eligible && shape_eligible;
    if (use_fiber_temp) {
        g_fiber_temp_pool_stats.eligible_slow_allocs++;
    } else if (self.fiber_temp_eligible) {
        if (!size_eligible) {
            g_fiber_temp_pool_stats.bypass_large_allocs++;
        } else if (!shape_eligible) {
            g_fiber_temp_pool_stats.bypass_escape_activity_allocs++;
        }
    }

    ScopeChunk* chunk = use_fiber_temp
        ? scope_chunk_alloc_temp(new_cap)
        : scope_chunk_alloc_raw(new_cap);
    chunk.next = self.chunks;
    self.chunks = chunk;

    char* data = scope_chunk_data(chunk);
    self.bump = data + aligned_size;
    self.limit = data + chunk.capacity;
    self.alloc_bytes += aligned_size;
    self.alloc_count++;
    return (void*)data;
}

/**
 * Escape slow path: allocate a new ESCAPE chunk when current escape chunk is full.
 */
fn void* ScopeRegion.alloc_escape_slow(ScopeRegion* self, usz aligned_size) {
    scope_guard_owner(self, "scope_alloc_escape_slow");
    usz prev_cap = SCOPE_CHUNK_INITIAL;
    if (self.escape_chunks != null) {
        prev_cap = self.escape_chunks.capacity;
    }
    usz new_cap = prev_cap * 2;
    if (new_cap > SCOPE_CHUNK_MAX) new_cap = SCOPE_CHUNK_MAX;
    if (new_cap < aligned_size) new_cap = aligned_size;

    ScopeChunk* chunk = scope_chunk_alloc_raw(new_cap);
    chunk.next = self.escape_chunks;
    self.escape_chunks = chunk;

    char* data = scope_chunk_data(chunk);
    self.escape_bump = data + aligned_size;
    self.escape_limit = data + chunk.capacity;
    self.alloc_bytes += aligned_size;
    self.alloc_count++;
    return (void*)data;
}

/**
 * Bump-allocate `size` bytes from this scope. 8-byte aligned.
 * Fast path: ~3 instructions (load bump, add, compare).
 */
fn void* ScopeRegion.alloc(ScopeRegion* self, usz size) @inline {
    scope_guard_owner(self, "scope_alloc");
    usz mask = 7;
    usz aligned = (size + mask) & ~mask;
    char* result = self.bump;
    char* new_bump = result + aligned;
    if (new_bump <= self.limit) {
        self.bump = new_bump;
        self.alloc_bytes += aligned;
        self.alloc_count++;
        return (void*)result;
    }
    return self.alloc_slow(aligned);
}

/**
 * Bump-allocate `size` bytes from ESCAPE lane. 8-byte aligned.
 */
fn void* ScopeRegion.alloc_escape(ScopeRegion* self, usz size) @inline {
    scope_guard_owner(self, "scope_alloc_escape");
    usz mask = 7;
    usz aligned = (size + mask) & ~mask;
    if (self.escape_bump != null) {
        char* result = self.escape_bump;
        char* new_bump = result + aligned;
        if (new_bump <= self.escape_limit) {
            self.escape_bump = new_bump;
            self.alloc_bytes += aligned;
            self.alloc_count++;
            return (void*)result;
        }
    }
    return self.alloc_escape_slow(aligned);
}

// =============================================================================
// Destructor registration
// =============================================================================

/**
 * Register a destructor to run when this scope is destroyed.
 * The ScopeDtor struct itself is bump-allocated in the scope's chunks.
 */
fn void scope_register_dtor(ScopeRegion* scope, void* ptr, ScopeDestructorFn func) {
    scope_guard_owner(scope, "scope_register_dtor");
    ScopeDtor* d = (ScopeDtor*)scope.alloc(ScopeDtor.sizeof);
    d.ptr = ptr;
    d.func = func;
    d.next = scope.dtors;
    scope.dtors = d;
}

/**
 * Register a destructor to run when this scope's ESCAPE lane is destroyed.
 * The ScopeDtor struct itself is ESCAPE-lane allocated.
 */
fn void scope_register_dtor_escape(ScopeRegion* scope, void* ptr, ScopeDestructorFn func) {
    scope_guard_owner(scope, "scope_register_dtor_escape");
    ScopeDtor* d = (ScopeDtor*)scope.alloc_escape(ScopeDtor.sizeof);
    d.ptr = ptr;
    d.func = func;
    d.next = scope.escape_dtors;
    scope.escape_dtors = d;
}

/**
 * Check if a pointer points to memory allocated within this scope's chunks.
 * O(N) where N is the number of chunks in this scope (usually 1 or 2).
 */
fn bool scope_contains(ScopeRegion* scope, void* ptr) {
    if (scope == null || ptr == null) return false;
    usz p = (usz)ptr;
    ScopeChunk* chunk = scope.chunks;
    while (chunk != null) {
        usz start = (usz)chunk + ScopeChunk.sizeof;
        usz end = start + chunk.capacity;
        if (p >= start && p < end) return true;
        chunk = chunk.next;
    }
    chunk = scope.escape_chunks;
    while (chunk != null) {
        usz start = (usz)chunk + ScopeChunk.sizeof;
        usz end = start + chunk.capacity;
        if (p >= start && p < end) return true;
        chunk = chunk.next;
    }
    return false;
}

// =============================================================================
// Scope reset (for loop reuse — Perceus reuse analysis)
// =============================================================================

/**
 * Reset a scope for reuse: run destructors, reset bump pointer to start of first chunk.
 * Only safe when RC == 1 (no external references — nothing escaped).
 *
 * Frees overflow chunks (keeps only the first), resets bump to start.
 * O(1) amortized per loop iteration when no escapes occur.
 */
fn void scope_reset(ScopeRegion* scope) {
    scope_guard_owner(scope, "scope_reset");
    assert(scope.refcount == 1, "scope_reset: refcount must be 1 (nothing escaped)");
    // Run destructors first for both lanes
    scope_run_dtors(scope);
    scope_run_escape_dtors(scope);

    // Free overflow chunks (keep only the oldest = last in linked list)
    ScopeChunk* keep = null;
    ScopeChunk* c = scope.chunks;
    while (c != null) {
        ScopeChunk* next = c.next;
        if (next == null) {
            // This is the oldest (first allocated) chunk — keep it
            keep = c;
        } else {
            scope_temp_chunk_release(scope, c);
        }
        c = next;
    }

    // Reset to the kept chunk
    if (keep != null) {
        keep.next = null;
        scope.chunks = keep;
        scope.bump = scope_chunk_data(keep);
        scope.limit = scope.bump + keep.capacity;
    }

    // Free all ESCAPE chunks on full reset.
    ScopeChunk* ec = scope.escape_chunks;
    while (ec != null) {
        ScopeChunk* next = ec.next;
        mem::free(ec);
        ec = next;
    }
    scope.escape_chunks = null;
    scope.escape_bump = null;
    scope.escape_limit = null;

    scope.dtors = null;
    scope.escape_dtors = null;
    scope.alloc_bytes = 0;
    scope.alloc_count = 0;
}

/**
 * Reset only the TEMP lane for reuse while preserving ESCAPE lane allocations.
 * Safe only when RC == 1 (no external scope ownership changes during reset).
 */
fn void scope_reset_temp_lane(ScopeRegion* scope) {
    scope_guard_owner(scope, "scope_reset_temp_lane");
    assert(scope.refcount == 1, "scope_reset_temp_lane: refcount must be 1");

    // TEMP dtors only; ESCAPE dtors remain registered.
    scope_run_dtors(scope);

    // Free overflow TEMP chunks, keep oldest TEMP chunk.
    ScopeChunk* keep = null;
    ScopeChunk* c = scope.chunks;
    while (c != null) {
        ScopeChunk* next = c.next;
        if (next == null) {
            keep = c;
        } else {
            scope_temp_chunk_release(scope, c);
        }
        c = next;
    }

    if (keep != null) {
        keep.next = null;
        scope.chunks = keep;
        scope.bump = scope_chunk_data(keep);
        scope.limit = scope.bump + keep.capacity;
    } else {
        // Defensive fallback: recreate baseline TEMP chunk if scope had none.
        ScopeChunk* chunk = scope.fiber_temp_eligible
            ? scope_chunk_alloc_temp(SCOPE_CHUNK_INITIAL)
            : scope_chunk_alloc_raw(SCOPE_CHUNK_INITIAL);
        chunk.next = null;
        scope.chunks = chunk;
        scope.bump = scope_chunk_data(chunk);
        scope.limit = scope.bump + chunk.capacity;
    }

    scope.dtors = null;
}

// =============================================================================
// Scope membership check (for escape-scope optimization)
// =============================================================================

/**
 * Check whether a pointer was allocated within a given scope.
 * Walks the scope's chunk list checking address ranges.
 * Used by copy_to_parent to skip values not in the scope being released.
 */
fn bool is_in_scope(void* ptr, ScopeRegion* scope) {
    if (ptr == null || scope == null) return false;
    ScopeChunk* chunk = scope.chunks;
    while (chunk != null) {
        char* data = scope_chunk_data(chunk);
        if ((char*)ptr >= data && (char*)ptr < data + chunk.capacity) return true;
        chunk = chunk.next;
    }
    chunk = scope.escape_chunks;
    while (chunk != null) {
        char* data = scope_chunk_data(chunk);
        if ((char*)ptr >= data && (char*)ptr < data + chunk.capacity) return true;
        chunk = chunk.next;
    }
    return false;
}

// =============================================================================
// Scope adoption (merge child memory into parent)
// =============================================================================

fn void scope_splice_escapes(ScopeRegion* parent, ScopeRegion* child) {
    if (child == null || parent == null) return;
    scope_guard_owner(parent, "scope_splice_escapes(parent)");
    scope_guard_owner(child, "scope_splice_escapes(child)");
    scope_invariant_distinct_scopes(parent, child);

    g_scope_transfer_stats.splice_calls++;
    g_scope_transfer_stats.splice_escape_chunk_bytes += scope_chunk_bytes(child.escape_chunks);
    g_scope_transfer_stats.splice_temp_chunk_bytes_freed += scope_chunk_bytes(child.chunks);
    g_scope_transfer_stats.splice_temp_dtors_run += scope_dtor_count(child.dtors);
    g_scope_transfer_stats.splice_escape_dtors_transferred += scope_dtor_count(child.escape_dtors);

    // Move child's ESCAPE dtors to parent's ESCAPE dtor list.
    if (child.escape_dtors != null) {
        ScopeDtor* last = child.escape_dtors;
        while (last.next != null) last = last.next;
        last.next = parent.escape_dtors;
        parent.escape_dtors = child.escape_dtors;
        child.escape_dtors = null;
    }

    // Move child's ESCAPE chunks to parent's ESCAPE chunk list.
    if (child.escape_chunks != null) {
        ScopeChunk* last = child.escape_chunks;
        while (last.next != null) last = last.next;
        last.next = parent.escape_chunks;
        parent.escape_chunks = child.escape_chunks;
        child.escape_chunks = null;
    }

    // Destroy TEMP lane immediately.
    scope_run_dtors(child);
    child.dtors = null;

    ScopeChunk* c = child.chunks;
    while (c != null) {
        ScopeChunk* next = c.next;
        scope_temp_chunk_release(child, c);
        c = next;
    }
    child.chunks = null;
    child.bump = null;
    child.limit = null;
    child.escape_bump = null;
    child.escape_limit = null;
    child.fiber_temp_eligible = false;

    // Detach and release child scope ownership.
    if (child.parent != null) {
        scope_release(child.parent);
        child.parent = null;
    }

    // Recycle child struct.
    child.refcount = 0;
    child.generation++;
    child.escape_generation++;
    if (!scope_freelist_recycle_enabled()) {
        mem::free(child);
        return;
    }
    scope_global_lock();
    if (g_scope_freelist_count < SCOPE_FREELIST_MAX) {
        child.pool_next = g_scope_freelist;
        g_scope_freelist = child;
        g_scope_freelist_count++;
        scope_global_unlock();
    } else {
        scope_global_unlock();
        mem::free(child);
    }
}

// =============================================================================
// Freelist cleanup (for shutdown)
// =============================================================================

/**
 * Free all ScopeRegion structs in the global freelist.
 * Call at program shutdown.
 */
fn void scope_freelist_cleanup() {
    scope_global_lock();
    ScopeRegion* s = g_scope_freelist;
    g_scope_freelist = null;
    g_scope_freelist_count = 0;
    scope_global_unlock();

    while (s != null) {
        ScopeRegion* next = s.pool_next;
        mem::free(s);
        s = next;
    }

    scope_global_lock();
    ScopeChunk* chunk = g_fiber_temp_chunk_pool;
    g_fiber_temp_chunk_pool = null;
    g_fiber_temp_chunk_pool_count = 0;
    scope_global_unlock();

    while (chunk != null) {
        ScopeChunk* next = chunk.next;
        mem::free(chunk);
        chunk = next;
    }
}

// =============================================================================
// Unit tests
// =============================================================================

fn void run_scope_region_tests() {
    io::print("  Scope region tests... ");
    usz passed = 0;
    usz failed = 0;

    // Test 1: Create and destroy a scope
    {
        ScopeRegion* s = scope_create(null);
        if (s != null && s.refcount == 1 && s.parent == null && s.chunks != null) {
            passed++;
        } else {
            io::printn("FAIL: scope_create basic");
            failed++;
        }
        scope_release(s);
        passed++; // didn't crash
    }

    // Test 2: Bump allocation
    {
        ScopeRegion* s = scope_create(null);
        void* p1 = s.alloc(24);  // Value-sized
        void* p2 = s.alloc(24);
        void* p3 = s.alloc(8);
        // Allocations should be sequential (8-byte aligned)
        if (p1 != null && p2 != null && p3 != null) {
            usz diff = (usz)p2 - (usz)p1;
            if (diff == 24) { passed++; } else { io::printfn("FAIL: bump sequential (diff=%d)", (int)diff); failed++; }
        } else {
            io::printn("FAIL: bump alloc returned null");
            failed++;
        }
        if (s.alloc_count == 3) { passed++; } else { io::printn("FAIL: alloc_count"); failed++; }
        if (s.alloc_bytes == 24 + 24 + 8) { passed++; } else { io::printfn("FAIL: alloc_bytes=%d", (int)s.alloc_bytes); failed++; }
        scope_release(s);
    }

    // Test 2b: Escape-lane allocation + generation separation
    {
        ScopeRegion* s = scope_create(null);
        void* e1 = s.alloc_escape(24);
        void* e2 = s.alloc_escape(24);
        if (e1 != null && e2 != null && ((usz)e2 - (usz)e1) == 24) {
            passed++;
        } else {
            io::printn("FAIL: escape alloc sequential");
            failed++;
        }
        if (s.escape_generation != s.generation) { passed++; } else { io::printn("FAIL: escape_generation separation"); failed++; }
        if (s.escape_chunks != null) { passed++; } else { io::printn("FAIL: escape chunks missing"); failed++; }
        scope_release(s);
    }

    // Test 2c: Escape-lane destructor execution
    {
        ScopeRegion* s = scope_create(null);
        int* counter = (int*)mem::malloc(int.sizeof);
        *counter = 0;
        scope_register_dtor_escape(s, (void*)counter, fn void(void* ptr) {
            int* c = (int*)ptr;
            *c = *c + 1;
        });
        scope_register_dtor_escape(s, (void*)counter, fn void(void* ptr) {
            int* c = (int*)ptr;
            *c = *c + 10;
        });
        scope_release(s);
        if (*counter == 11) { passed++; } else { io::printfn("FAIL: escape dtor counter=%d", *counter); failed++; }
        mem::free(counter);
    }

    // Test 3: Chunk overflow (allocate more than initial chunk)
    {
        ScopeRegion* s = scope_create(null);
        // Fill initial chunk (512 bytes) and overflow
        for (usz i = 0; i < 100; i++) {
            void* p = s.alloc(64);  // 100 * 64 = 6400 bytes
            if (p == null) { io::printn("FAIL: overflow alloc null"); failed++; break; }
        }
        // Should have multiple chunks
        usz chunk_count = 0;
        ScopeChunk* c = s.chunks;
        while (c != null) { chunk_count++; c = c.next; }
        if (chunk_count > 1) { passed++; } else { io::printn("FAIL: no overflow chunks"); failed++; }
        scope_release(s);
        passed++;
    }

    // Test 4: Reference counting (parent-child)
    {
        ScopeRegion* parent = scope_create(null);
        ScopeRegion* child = scope_create(parent);
        // Parent RC should be 2 (original + child's reference)
        if (parent.refcount == 2) { passed++; } else { io::printfn("FAIL: parent RC=%d", (int)parent.refcount); failed++; }
        if (child.refcount == 1) { passed++; } else { io::printfn("FAIL: child RC=%d", (int)child.refcount); failed++; }
        if (child.parent == parent) { passed++; } else { io::printn("FAIL: child.parent"); failed++; }
        // Release child — parent RC drops to 1
        scope_release(child);
        if (parent.refcount == 1) { passed++; } else { io::printfn("FAIL: parent RC after child release=%d", (int)parent.refcount); failed++; }
        scope_release(parent);
        passed++;
    }

    // Test 5: Destructor execution
    {
        ScopeRegion* s = scope_create(null);
        // Allocate a small heap buffer, register its destructor
        int* counter = (int*)mem::malloc(int.sizeof);
        *counter = 0;

        // We'll use a trick: allocate an int in the scope, point dtor at the malloc'd counter
        // The destructor increments the counter
        scope_register_dtor(s, (void*)counter, fn void(void* ptr) {
            int* c = (int*)ptr;
            *c = *c + 1;
        });
        scope_register_dtor(s, (void*)counter, fn void(void* ptr) {
            int* c = (int*)ptr;
            *c = *c + 10;
        });

        // Destroy scope — dtors should run in LIFO order (10 first, then 1)
        scope_release(s);
        if (*counter == 11) { passed++; } else { io::printfn("FAIL: dtor counter=%d", *counter); failed++; }
        mem::free(counter);
    }

    // Test 6: Freelist recycling (generation increment)
    {
        ScopeRegion* s = scope_create(null);
        uint gen_before = s.generation;
        scope_release(s);
        // After release, scope should be in freelist with incremented generation
        // Create a new scope — should get the recycled one
        ScopeRegion* s2 = scope_create(null);
        if (s2.generation > gen_before) {
            passed++;
        } else {
            io::printfn("FAIL: gen=%d expected > %d", (int)s2.generation, (int)gen_before);
            failed++;
        }
        scope_release(s2);
    }

    // Test 7: Scope reset (loop reuse)
    {
        ScopeRegion* s = scope_create(null);
        // Allocate some data
        void* p1 = s.alloc(64);
        void* p2 = s.alloc(64);

        int* dtor_counter = (int*)mem::malloc(int.sizeof);
        *dtor_counter = 0;
        scope_register_dtor(s, (void*)dtor_counter, fn void(void* ptr) {
            int* c = (int*)ptr;
            *c = *c + 1;
        });

        // Reset the scope
        scope_reset(s);
        if (*dtor_counter == 1) { passed++; } else { io::printfn("FAIL: reset dtor counter=%d", *dtor_counter); failed++; }
        if (s.alloc_bytes == 0) { passed++; } else { io::printn("FAIL: reset alloc_bytes"); failed++; }
        if (s.alloc_count == 0) { passed++; } else { io::printn("FAIL: reset alloc_count"); failed++; }
        if (s.dtors == null) { passed++; } else { io::printn("FAIL: reset dtors"); failed++; }

        // Can allocate again after reset
        void* p3 = s.alloc(32);
        if (p3 != null) { passed++; } else { io::printn("FAIL: alloc after reset"); failed++; }

        scope_release(s);
        mem::free(dtor_counter);
    }

    // Test 7b: scope_reset clears ESCAPE lane
    {
        ScopeRegion* s = scope_create(null);
        void* e = s.alloc_escape(32);
        if (e != null) { passed++; } else { io::printn("FAIL: reset escape alloc null"); failed++; }
        scope_reset(s);
        if (s.escape_chunks == null) { passed++; } else { io::printn("FAIL: reset escape chunks"); failed++; }
        if (s.escape_dtors == null) { passed++; } else { io::printn("FAIL: reset escape dtors"); failed++; }
        if (s.escape_bump == null && s.escape_limit == null) { passed++; } else { io::printn("FAIL: reset escape bump/limit"); failed++; }
        scope_release(s);
    }

    // Test 7c: scope_reset_temp_lane preserves ESCAPE lane
    {
        ScopeRegion* s = scope_create(null);
        int* temp_counter = (int*)mem::malloc(int.sizeof);
        int* escape_counter = (int*)mem::malloc(int.sizeof);
        *temp_counter = 0;
        *escape_counter = 0;

        void* temp_ptr = s.alloc(16);
        void* escape_ptr = s.alloc_escape(16);

        scope_register_dtor(s, (void*)temp_counter, fn void(void* ptr) {
            int* c = (int*)ptr;
            *c = *c + 1;
        });
        scope_register_dtor_escape(s, (void*)escape_counter, fn void(void* ptr) {
            int* c = (int*)ptr;
            *c = *c + 1;
        });

        scope_reset_temp_lane(s);

        bool ok = *temp_counter == 1 &&
                  *escape_counter == 0 &&
                  s.dtors == null &&
                  s.escape_dtors != null &&
                  escape_ptr != null &&
                  is_in_scope(escape_ptr, s) &&
                  temp_ptr != null;

        if (ok) { passed++; } else { io::printn("FAIL: reset_temp_lane invariants"); failed++; }

        scope_release(s);
        if (*escape_counter == 1) { passed++; } else { io::printn("FAIL: reset_temp_lane escape dtor on destroy"); failed++; }

        mem::free(temp_counter);
        mem::free(escape_counter);
    }

    // Test 10: Large allocation (bigger than initial chunk)
    {
        ScopeRegion* s = scope_create(null);
        void* big = s.alloc(2048);  // Bigger than initial 512-byte chunk
        if (big != null) { passed++; } else { io::printn("FAIL: large alloc"); failed++; }
        scope_release(s);
    }

    // Test 11: Deep parent chain
    {
        ScopeRegion* root = scope_create(null);
        ScopeRegion* prev = root;
        for (usz i = 0; i < 10; i++) {
            ScopeRegion* child = scope_create(prev);
            if (i > 0) scope_release(prev);  // Release our reference (child holds one)
            prev = child;
        }
        // Release the leaf — should cascade all the way up
        if (root.refcount == 2) { passed++; } else { io::printfn("FAIL: deep chain root RC=%d", (int)root.refcount); failed++; }
        scope_release(prev);
        // Root should now have RC=1 (our original reference)
        if (root.refcount == 1) { passed++; } else { io::printfn("FAIL: deep chain root RC after cascade=%d", (int)root.refcount); failed++; }
        scope_release(root);
        passed++;
    }

    // Test 12: Retain/release symmetry
    {
        ScopeRegion* s = scope_create(null);
        scope_retain(s);
        scope_retain(s);
        if (s.refcount == 3) { passed++; } else { io::printfn("FAIL: triple retain RC=%d", (int)s.refcount); failed++; }
        scope_release(s);
        scope_release(s);
        if (s.refcount == 1) { passed++; } else { io::printfn("FAIL: after double release RC=%d", (int)s.refcount); failed++; }
        scope_release(s);
        passed++;
    }

    // Test 13: Alignment check
    {
        ScopeRegion* s = scope_create(null);
        void* p1 = s.alloc(1);   // 1 byte → 8-aligned
        void* p2 = s.alloc(3);   // 3 bytes → 8-aligned
        void* p3 = s.alloc(16);  // 16 bytes → 16-aligned (already 8-aligned)
        if ((usz)p1 % 8 == 0) { passed++; } else { io::printn("FAIL: align p1"); failed++; }
        if ((usz)p2 % 8 == 0) { passed++; } else { io::printn("FAIL: align p2"); failed++; }
        if ((usz)p3 % 8 == 0) { passed++; } else { io::printn("FAIL: align p3"); failed++; }
        // p2 should be 8 bytes after p1 (1 byte rounded up to 8)
        usz gap = (usz)p2 - (usz)p1;
        if (gap == 8) { passed++; } else { io::printfn("FAIL: align gap=%d", (int)gap); failed++; }
        scope_release(s);
    }

    // Test 14: is_in_scope
    {
        ScopeRegion* s = scope_create(null);
        void* p1 = s.alloc(24);
        void* p2 = s.alloc(24);
        ScopeRegion* other = scope_create(null);
        void* p3 = other.alloc(24);

        if (is_in_scope(p1, s)) { passed++; } else { io::printn("FAIL: is_in_scope p1 in s"); failed++; }
        if (is_in_scope(p2, s)) { passed++; } else { io::printn("FAIL: is_in_scope p2 in s"); failed++; }
        if (!is_in_scope(p3, s)) { passed++; } else { io::printn("FAIL: is_in_scope p3 not in s"); failed++; }
        if (is_in_scope(p3, other)) { passed++; } else { io::printn("FAIL: is_in_scope p3 in other"); failed++; }
        if (!is_in_scope(null, s)) { passed++; } else { io::printn("FAIL: is_in_scope null"); failed++; }

        scope_release(other);
        scope_release(s);
    }

    // Test 15: scope_splice_escapes
    {
        ScopeRegion* parent = scope_create(null);
        ScopeRegion* child = scope_create(parent);

        void* escape_ptr = child.alloc_escape(24);
        if (escape_ptr != null) { passed++; } else { io::printn("FAIL: splice escape alloc"); failed++; }

        int* temp_counter = (int*)mem::malloc(int.sizeof);
        int* escape_counter = (int*)mem::malloc(int.sizeof);
        *temp_counter = 0;
        *escape_counter = 0;

        scope_register_dtor(child, (void*)temp_counter, fn void(void* ptr) {
            int* c = (int*)ptr;
            *c = *c + 1;
        });
        scope_register_dtor_escape(child, (void*)escape_counter, fn void(void* ptr) {
            int* c = (int*)ptr;
            *c = *c + 1;
        });

        scope_splice_escapes(parent, child);
        if (*temp_counter == 1) { passed++; } else { io::printfn("FAIL: splice temp dtor=%d", *temp_counter); failed++; }
        if (*escape_counter == 0) { passed++; } else { io::printfn("FAIL: splice escape dtor early=%d", *escape_counter); failed++; }
        if (is_in_scope(escape_ptr, parent)) { passed++; } else { io::printn("FAIL: splice escape ptr in parent"); failed++; }

        scope_release(parent);
        if (*escape_counter == 1) { passed++; } else { io::printfn("FAIL: splice escape dtor late=%d", *escape_counter); failed++; }

        mem::free(temp_counter);
        mem::free(escape_counter);
    }

    io::printfn("%d passed, %d failed", (int)passed, (int)failed);
    if (scope_c_getenv("OMNI_TEST_SUMMARY") != null) {
        io::printfn("OMNI_TEST_SUMMARY suite=scope_region pass=%d fail=%d", (int)passed, (int)failed);
        io::printfn(
            "OMNI_TEST_SUMMARY suite=fiber_temp_pool enabled=%d hits=%d misses=%d returns=%d drop_frees=%d pooled=%d peak=%d eligible_slow=%d bypass_large=%d bypass_escape=%d",
            (int)(scope_fiber_temp_flag_enabled() ? 1 : 0),
            (int)g_fiber_temp_pool_stats.take_hits,
            (int)g_fiber_temp_pool_stats.take_misses,
            (int)g_fiber_temp_pool_stats.return_count,
            (int)g_fiber_temp_pool_stats.drop_frees,
            (int)g_fiber_temp_chunk_pool_count,
            (int)g_fiber_temp_pool_stats.peak_pool_count,
            (int)g_fiber_temp_pool_stats.eligible_slow_allocs,
            (int)g_fiber_temp_pool_stats.bypass_large_allocs,
            (int)g_fiber_temp_pool_stats.bypass_escape_activity_allocs
        );
    }
}
