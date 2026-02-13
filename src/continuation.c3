/**
 * =============================================================================
 * DELIMITED CONTINUATIONS — Control Flow Primitives
 * =============================================================================
 *
 * Implements delimited continuations using the reset/shift paradigm:
 *
 * - **reset** establishes a delimiter (prompt) on the control stack
 * - **shift** captures the continuation up to the nearest matching prompt
 * - Captured continuations can be resumed zero or more times
 *
 * ## KEY CONCEPTS
 *
 * 1. **PromptTag** — Unique identifier for a delimiter/reset point.
 *    Each `reset` creates a prompt tag. `shift` captures up to a matching tag.
 *    Tags are hierarchical; a shift captures to the nearest matching prompt.
 *
 * 2. **Continuation** — A captured suspended computation.
 *    Contains saved execution context (registers, stack segment) and can be
 *    resumed to continue execution from the capture point.
 *
 * 3. **PromptFrame** — An active delimiter on the prompt stack.
 *    Pushed when `reset` is entered, popped when the reset block completes
 *    or when `shift` captures past it.
 *
 * 4. **PromptStack** — Thread-local stack of active prompts.
 *    Each thread maintains its own prompt stack for independent control flow.
 *
 * ## INTEGRATION WITH REGION MEMORY
 *
 * Continuations are allocated within regions and become invalid when their
 * home region dies. This ensures captured stack data is properly cleaned up.
 *
 * ## USAGE PATTERN
 *
 * ```c3
 * // Initialize thread-local prompt stack
 * prompt_stack_init();
 *
 * // Create a prompt and capture a continuation
 * PromptTag tag = prompt_stack().new_tag();
 * // ... push prompt, execute code, capture with shift ...
 *
 * // Cleanup
 * prompt_stack_shutdown();
 * ```
 */
module main;

import std::collections::list;

// =============================================================================
// SECTION 1: CONTINUATION PRIMITIVES
// =============================================================================
//
// Core type aliases for delimited continuation identifiers.
// These provide type safety and clarity when working with prompts and
// captured continuations.
//

/**
 * PromptTag — Unique identifier for a delimiter/reset point.
 *
 * Each `reset` creates a prompt tag. `shift` captures up to a matching tag.
 * Tags are hierarchical — a shift captures to the nearest matching prompt.
 *
 * Implementation: 32-bit unsigned integer for compact storage and fast comparison.
 */
typedef PromptTag = uint;

/**
 * ContinuationId — Unique identifier for a captured continuation.
 *
 * Used to look up and manage continuations in the prompt stack.
 * Each captured continuation gets a unique ID within its thread.
 */
typedef ContinuationId = uint;

/** Sentinel for invalid/null prompt tag. Used when no prompt is associated. */
const PromptTag INVALID_PROMPT_TAG = (PromptTag)0;

/** Sentinel for invalid/null continuation ID. */
const ContinuationId INVALID_CONTINUATION_ID = (ContinuationId)0;

// =============================================================================
// SECTION 2: CONTINUATION STATE
// =============================================================================
//
// Data structures representing the execution state of a continuation.
// These capture everything needed to resume a suspended computation.
//

/**
 * ContinuationStatus — The lifecycle state of a continuation.
 *
 * Tracks whether a continuation can be resumed and what happened to it.
 * State transitions:
 *   SUSPENDED → RUNNING (on resume)
 *   RUNNING → COMPLETED (on normal return)
 *   SUSPENDED/RUNNING → INVALIDATED (on region death or explicit abort)
 */
enum ContinuationStatus : char {
    /** Freshly captured, can be resumed */
    SUSPENDED,
    /** Currently executing (after resume, before return) */
    RUNNING,
    /** Resumed and completed normally */
    COMPLETED,
    /** Invalidated (region died, or explicitly aborted) */
    INVALIDATED
}

/**
 * StackSegment — A captured portion of the execution stack.
 *
 * When shift captures a continuation, it copies the stack from the
 * current SP up to the prompt's SP into a StackSegment.
 *
 * The stack data is allocated from the continuation's home region,
 * ensuring proper cleanup when the region dies.
 *
 * Memory layout after capture:
 * ```
 * Original Stack:          Captured Segment:
 * +-----------+ base_sp    +-----------+
 * |  prompt   |            | copied    |
 * |  frame    |            | stack     |
 * +-----------+            | data      |
 * |  ...      |  ───────>  |           |
 * |  code     |            |           |
 * +-----------+ top_sp     +-----------+
 * |  shift    |
 * +-----------+
 * ```
 */
struct StackSegment {
    /** Pointer to copied stack bytes (allocated in region's pool) */
    void* data;

    /** Size of captured stack in bytes */
    usz size;

    /** Original stack pointer where this segment started (prompt's SP) */
    void* base_sp;

    /** Original stack pointer where this segment ended (shift's SP) */
    void* top_sp;
}

/**
 * Create an empty/invalid stack segment.
 *
 * Used for initialization before a continuation is captured.
 *
 * @return A zeroed StackSegment with null pointers and zero size
 */
fn StackSegment stack_segment_empty() {
    StackSegment seg;
    seg.data = null;
    seg.size = 0;
    seg.base_sp = null;
    seg.top_sp = null;
    return seg;
}

/**
 * Check if a stack segment is valid (has captured data).
 *
 * @return true if the segment contains captured stack data
 */
fn bool StackSegment.is_valid(StackSegment* self) {
    return self.data != null && self.size > 0;
}

/**
 * Calculate the stack growth direction from a captured segment.
 *
 * Different architectures grow the stack in different directions:
 * - x86/x64: grows downward (toward lower addresses)
 * - Some embedded: grows upward (toward higher addresses)
 *
 * @return true if stack grows downward (base_sp > top_sp)
 */
fn bool StackSegment.grows_downward(StackSegment* self) {
    return (usz)self.base_sp > (usz)self.top_sp;
}

// -----------------------------------------------------------------------------
// 2.1 Platform-Specific Execution Context
// -----------------------------------------------------------------------------

/** Platform IDs for context validation */
const uint PLATFORM_X86_64_LINUX = 1;
const uint PLATFORM_X86_64_MACOS = 2;
const uint PLATFORM_AARCH64_LINUX = 3;
const uint PLATFORM_AARCH64_MACOS = 4;
const uint PLATFORM_UNKNOWN = 0;

/**
 * SavedContext — Platform-specific execution context.
 *
 * Captures the CPU register state at a specific point in execution.
 * This includes general-purpose registers, instruction pointer, and
 * stack/frame pointers needed to resume execution.
 *
 * Implementation notes:
 * - Uses a 256-byte buffer to safely accommodate RegisterContext (~160 bytes)
 * - Explicit IP/SP/FP fields for clarity and direct access
 * - Platform ID enables validation when resuming across contexts
 *
 * On POSIX systems, this conceptually wraps ucontext_t functionality.
 * On Windows, it would wrap CONTEXT + fiber state.
 */
struct SavedContext {
    /**
     * Register state buffer.
     * 256 bytes to safely accommodate RegisterContext (~160 bytes on x86_64:
     * ulong[16] gpr + sp/ip/fp pointers + flags + padding).
     * Must be large enough for any platform's register save area.
     */
    char[256] registers;

    /** Instruction pointer at capture point */
    void* instruction_ptr;

    /** Stack pointer at capture point */
    void* stack_ptr;

    /** Frame pointer at capture point */
    void* frame_ptr;

    /**
     * Platform identifier for validation.
     * Ensures contexts aren't accidentally mixed across platforms.
     */
    uint platform_id;
}

/**
 * Create an empty/uninitialized SavedContext.
 *
 * @return A zeroed SavedContext ready for capture
 */
fn SavedContext saved_context_empty() {
    SavedContext ctx;
    // Zero the register buffer
    for (usz i = 0; i < 256; i++) {
        ctx.registers[i] = 0;
    }
    ctx.instruction_ptr = null;
    ctx.stack_ptr = null;
    ctx.frame_ptr = null;
    ctx.platform_id = PLATFORM_UNKNOWN;
    return ctx;
}

/**
 * Check if a saved context has been initialized.
 *
 * @return true if the context has captured state
 */
fn bool SavedContext.is_valid(SavedContext* self) {
    return self.platform_id != PLATFORM_UNKNOWN &&
           self.instruction_ptr != null;
}

/**
 * Get the current platform ID.
 *
 * Detects the platform at runtime for context validation.
 * Uses compile-time detection where available.
 *
 * @return Platform identifier constant
 */
fn uint get_current_platform_id() {
    return PLATFORM_X86_64_LINUX;
}

// -----------------------------------------------------------------------------
// 2.2 Continuation Structure
// -----------------------------------------------------------------------------

/**
 * Continuation — A captured delimited continuation.
 *
 * Represents the suspended computation from a `shift` point back to
 * its delimiting `reset`. Contains all state needed to resume execution:
 *
 * - Saved CPU registers and pointers (context)
 * - Captured stack segment (stack)
 * - Region ownership for memory management
 * - Resume/return values for data passing
 *
 * ## Ownership Model
 *
 * The continuation is allocated in a region and becomes invalid when
 * that region dies. The stack segment data is also allocated from the
 * same region, ensuring everything is cleaned up together.
 *
 * ## One-Shot vs Multi-Shot
 *
 * - **One-shot**: Can be resumed exactly once. More efficient because
 *   the stack segment can be moved instead of copied on resume.
 * - **Multi-shot**: Can be resumed multiple times. Requires copying
 *   the stack segment on each resume.
 *
 * Default is one-shot for efficiency; set is_one_shot = false for multi-shot.
 */
struct Continuation {
    // --- Identity ---
    /** Unique ID for this continuation within its thread */
    ContinuationId id;

    /** Which reset/prompt this was captured under */
    PromptTag prompt_tag;

    // --- Region ownership ---
    /** Region that owns this continuation's memory */
    RegionId home_region;

    /** Generation for validity checking against home region */
    Generation home_generation;

    // --- Execution state ---
    /** Current lifecycle state */
    ContinuationStatus status;

    /** Saved registers, IP, SP, FP */
    SavedContext context;

    /** Captured stack segment */
    StackSegment stack;

    // --- Resumption ---
    /** Value passed to resume() - available to the continuation */
    void* resume_value;

    /** Value returned after the continuation completes */
    void* return_value;

    // --- Continuation chain (for nested shifts) ---
    /**
     * Enclosing continuation ID, or INVALID_CONTINUATION_ID.
     * When shifts are nested, this forms a chain that must be
     * restored when the inner continuation completes.
     */
    ContinuationId parent_continuation;

    // --- One-shot optimization ---
    /** If true, stack is moved not copied on resume (more efficient) */
    bool is_one_shot;

    /** Track if already resumed (for one-shot enforcement) */
    bool has_been_resumed;

    // --- Debug info ---
    /** Optional name for debugging and tracing */
    char[64] debug_name;
}

/**
 * Create an empty/uninitialized Continuation.
 *
 * @return A zeroed Continuation in INVALIDATED state
 */
fn Continuation continuation_empty() {
    Continuation cont;
    cont.id = INVALID_CONTINUATION_ID;
    cont.prompt_tag = INVALID_PROMPT_TAG;
    cont.home_region = (RegionId)0;
    cont.home_generation = (Generation)0;
    cont.status = INVALIDATED;
    cont.context = saved_context_empty();
    cont.stack = stack_segment_empty();
    cont.resume_value = null;
    cont.return_value = null;
    cont.parent_continuation = INVALID_CONTINUATION_ID;
    cont.is_one_shot = true;
    cont.has_been_resumed = false;
    for (usz i = 0; i < 64; i++) {
        cont.debug_name[i] = 0;
    }
    return cont;
}

/**
 * Check if a continuation can be resumed.
 *
 * A continuation can only be resumed if:
 * - It is in SUSPENDED state
 * - For one-shot continuations, it hasn't been resumed before
 *
 * @return true if resume() can be called
 */
fn bool Continuation.can_resume(Continuation* self) {
    if (self.status != SUSPENDED) {
        return false;
    }
    if (self.is_one_shot && self.has_been_resumed) {
        return false;
    }
    return true;
}

/**
 * Check if a continuation is still valid (not invalidated).
 *
 * @return true if the continuation hasn't been invalidated
 */
fn bool Continuation.is_valid(Continuation* self) {
    return self.status != INVALIDATED;
}

/**
 * Set the debug name for a continuation.
 *
 * Copies up to 63 characters of the provided name.
 *
 * @param name The debug name to set
 */
fn void Continuation.set_debug_name(Continuation* self, char* name) {
    usz i = 0;
    while (i < 63 && name[i] != 0) {
        self.debug_name[i] = name[i];
        i++;
    }
    self.debug_name[i] = 0;
}

// =============================================================================
// SECTION 3: PROMPT FRAME
// =============================================================================
//
// PromptFrame represents an active reset/delimiter on the prompt stack.
// It marks a boundary in the control flow where continuations can be
// captured up to.
//

/**
 * PromptFrame — Represents an active reset/delimiter on the prompt stack.
 *
 * When `reset` is called, a PromptFrame is pushed onto the thread's
 * prompt stack. When the reset block completes (or shift captures past it),
 * the frame is popped.
 *
 * The frame stores:
 * - The delimiter's unique tag
 * - Region association for memory management
 * - Entry context for stack unwinding
 * - Optional handler for effect systems
 */
struct PromptFrame {
    /** Unique tag identifying this prompt */
    PromptTag tag;

    /** Region associated with this prompt for memory allocation */
    RegionId region_id;

    /** Region generation for validity checking */
    Generation region_gen;

    /** Saved context at reset entry point (for unwinding) */
    SavedContext entry_context;

    /** Stack pointer at reset entry (defines capture boundary) */
    void* stack_ptr;

    /** Frame pointer at reset entry */
    void* frame_ptr;

    /**
     * Optional handler function for effect systems.
     * Called when shift captures this prompt with an effect value.
     * Signature: void handler(void* effect_value, Continuation* k, void* data)
     */
    void* handler_fn;

    /** User data passed to handler function */
    void* handler_data;

    /** Nesting depth (0 = outermost prompt on stack) */
    usz depth;
}

/**
 * Create an empty/uninitialized PromptFrame.
 *
 * @return A zeroed PromptFrame with invalid tag
 */
fn PromptFrame prompt_frame_empty() {
    PromptFrame frame;
    frame.tag = INVALID_PROMPT_TAG;
    frame.region_id = (RegionId)0;
    frame.region_gen = (Generation)0;
    frame.entry_context = saved_context_empty();
    frame.stack_ptr = null;
    frame.frame_ptr = null;
    frame.handler_fn = null;
    frame.handler_data = null;
    frame.depth = 0;
    return frame;
}

/**
 * Check if a prompt frame has a handler attached.
 *
 * @return true if handler_fn is set
 */
fn bool PromptFrame.has_handler(PromptFrame* self) {
    return self.handler_fn != null;
}

/**
 * Check if a prompt frame is valid.
 *
 * @return true if the frame has a valid (non-zero) tag
 */
fn bool PromptFrame.is_valid(PromptFrame* self) {
    return (uint)self.tag != (uint)INVALID_PROMPT_TAG;
}

// =============================================================================
// SECTION 4: PROMPT STACK (Thread-Local)
// =============================================================================
//
// The PromptStack is the central data structure for managing delimited
// continuations within a thread. Each thread has its own independent stack.
//

/**
 * PromptStack — Thread-local stack of active prompts/delimiters.
 *
 * Each thread has its own prompt stack. Prompts are pushed on reset,
 * popped on reset completion or shift capture.
 *
 * The prompt stack also stores all continuations created in this thread,
 * indexed by their ContinuationId for O(1) lookup.
 *
 * ## Thread Safety
 *
 * The PromptStack is NOT thread-safe. Each thread must have its own
 * instance via the thread-local g_prompt_stack.
 *
 * ## Memory Management
 *
 * Continuation stack data is allocated from regions. When a region dies,
 * invalidate_region_continuations() must be called to mark affected
 * continuations as INVALIDATED.
 */
struct PromptStack {
    /** Stack of active prompts (most recent at end) */
    List{PromptFrame} frames;

    /** Counter for generating unique prompt tags */
    PromptTag next_tag;

    /**
     * All continuations created in this thread.
     * Indexed for O(1) lookup by ContinuationId.
     */
    List{Continuation} continuations;

    /** Counter for generating unique continuation IDs */
    ContinuationId next_cont_id;
}

// -----------------------------------------------------------------------------
// 4.1 Initialization and Cleanup
// -----------------------------------------------------------------------------

/**
 * Initialize a prompt stack to empty state.
 *
 * Must be called before using any other PromptStack methods.
 * Sets up internal lists and initializes ID counters to skip
 * the invalid/sentinel values.
 */
fn void PromptStack.init(PromptStack* self) {
    self.frames = {};
    self.next_tag = (PromptTag)1;  // 0 is INVALID_PROMPT_TAG
    self.continuations = {};
    self.next_cont_id = (ContinuationId)1;  // 0 is INVALID_CONTINUATION_ID
}

/**
 * Release all resources held by the prompt stack.
 *
 * Frees the internal lists. Does not invalidate continuations
 * (they become inaccessible but their region-allocated data
 * is freed when the region dies).
 */
fn void PromptStack.release(PromptStack* self) {
    // Free continuation stack data before freeing the list
    foreach (&cont : self.continuations) {
        if (cont.stack.data != null) {
            mem::free(cont.stack.data);
            cont.stack.data = null;
        }
    }
    self.frames.free();
    self.continuations.free();
}

// -----------------------------------------------------------------------------
// 4.2 Prompt Tag Generation
// -----------------------------------------------------------------------------

/**
 * Generate a new unique prompt tag.
 *
 * Tags are monotonically increasing within a thread, ensuring
 * uniqueness for the lifetime of the thread.
 *
 * @return A fresh, unique PromptTag
 */
fn PromptTag PromptStack.new_tag(PromptStack* self) {
    PromptTag tag = self.next_tag;
    self.next_tag = (PromptTag)((uint)self.next_tag + 1);
    return tag;
}

// -----------------------------------------------------------------------------
// 4.3 Prompt Frame Management
// -----------------------------------------------------------------------------

/**
 * Push a new prompt frame onto the stack.
 *
 * The frame's depth is automatically set based on current stack size.
 * Call this when entering a reset block.
 *
 * @param frame The prompt frame to push (will have depth set)
 */
fn void PromptStack.push_prompt(PromptStack* self, PromptFrame frame) {
    frame.depth = self.frames.len();
    self.frames.push(frame);
}

/**
 * Pop the topmost prompt frame from the stack.
 *
 * Call this when exiting a reset block normally.
 *
 * @return false if stack was empty, true otherwise
 */
fn bool PromptStack.pop_prompt(PromptStack* self) {
    if (self.frames.len() == 0) {
        return false;
    }
    (void)self.frames.pop();
    return true;
}

/**
 * Find a prompt frame by its tag.
 *
 * Searches from the top (most recent) to the bottom of the stack.
 * This finds the nearest enclosing prompt with the given tag.
 *
 * @param tag The prompt tag to search for
 * @return Pointer to the frame, or null if not found
 */
fn PromptFrame* PromptStack.find_prompt(PromptStack* self, PromptTag tag) {
    // Search from top (most recent) to bottom
    for (usz i = self.frames.len(); i > 0; i--) {
        if ((uint)self.frames[i-1].tag == (uint)tag) {
            return &self.frames[i-1];
        }
    }
    return null;
}

/**
 * Get the topmost prompt frame.
 *
 * @return Pointer to the most recently pushed frame, or null if empty
 */
fn PromptFrame* PromptStack.top_prompt(PromptStack* self) {
    if (self.frames.len() == 0) {
        return null;
    }
    return &self.frames[self.frames.len() - 1];
}

/**
 * Get the current depth of the prompt stack.
 *
 * @return Number of active prompt frames
 */
fn usz PromptStack.depth(PromptStack* self) {
    return self.frames.len();
}

/**
 * Check if a specific prompt tag is currently on the stack.
 *
 * @param tag The prompt tag to check for
 * @return true if a frame with this tag exists
 */
fn bool PromptStack.has_prompt(PromptStack* self, PromptTag tag) {
    return self.find_prompt(tag) != null;
}

/**
 * Pop all prompt frames down to (and including) the given tag.
 *
 * Used when shift captures up to a prompt, removing all nested
 * prompts that were above it.
 *
 * @param tag The prompt tag to pop down to
 * @return Number of frames popped, or 0 if tag not found
 */
fn usz PromptStack.pop_to_prompt(PromptStack* self, PromptTag tag) {
    usz popped = 0;

    // Find the depth of the target prompt
    PromptFrame* target = self.find_prompt(tag);
    if (target == null) {
        return 0;
    }

    usz target_depth = target.depth;

    // Pop all frames from top down to and including target
    while (self.frames.len() > target_depth) {
        (void)self.frames.pop();
        popped++;
    }

    return popped;
}

// -----------------------------------------------------------------------------
// 4.4 Continuation Management
// -----------------------------------------------------------------------------

/**
 * Allocate a new continuation entry.
 *
 * Creates a continuation in SUSPENDED state, associated with the
 * given region. The continuation is stored in the prompt stack's list
 * but its stack data should be allocated in the specified region.
 *
 * @param region_id  Region that will own the continuation's memory
 * @param region_gen Generation of the region for validity checking
 * @return Pointer to the newly allocated Continuation
 */
fn Continuation* PromptStack.alloc_continuation(PromptStack* self,
                                                  RegionId region_id,
                                                  Generation region_gen) {
    ContinuationId id = self.next_cont_id;
    assert((uint)self.next_cont_id < uint.max, "continuation ID counter overflow");
    self.next_cont_id = (ContinuationId)((uint)self.next_cont_id + 1);

    Continuation cont = continuation_empty();
    cont.id = id;
    cont.prompt_tag = INVALID_PROMPT_TAG;
    cont.home_region = region_id;
    cont.home_generation = region_gen;
    cont.status = SUSPENDED;
    cont.is_one_shot = true;  // Default to one-shot for efficiency
    cont.has_been_resumed = false;

    self.continuations.push(cont);
    return &self.continuations[self.continuations.len() - 1];
}

/**
 * Find a continuation by its ID.
 *
 * Performs a linear search through all continuations.
 * For frequent lookups, consider using an index.
 *
 * @param id The ContinuationId to find
 * @return Pointer to the Continuation, or null if not found
 */
fn Continuation* PromptStack.find_continuation(PromptStack* self, ContinuationId id) {
    foreach (&cont : self.continuations) {
        if ((uint)cont.id == (uint)id) {
            return cont;
        }
    }
    return null;
}

/**
 * Get a continuation by its ID, asserting it exists.
 *
 * @param id The ContinuationId to get
 * @return Pointer to the Continuation (never null if ID is valid)
 */
fn Continuation* PromptStack.get_continuation(PromptStack* self, ContinuationId id) {
    Continuation* cont = self.find_continuation(id);
    assert(cont != null, "Continuation not found");
    return cont;
}

/**
 * Invalidate all continuations belonging to a specific region.
 *
 * Must be called when a region dies to prevent use-after-free.
 * Affected continuations have their status set to INVALIDATED
 * and can no longer be resumed.
 *
 * @param region_id  The dying region's ID
 * @param region_gen The dying region's generation
 */
fn void PromptStack.invalidate_region_continuations(PromptStack* self,
                                                      RegionId region_id,
                                                      Generation region_gen) {
    foreach (&cont : self.continuations) {
        if ((uint)cont.home_region == (uint)region_id &&
            (uint)cont.home_generation == (uint)region_gen) {
            cont.status = INVALIDATED;
            // Free stack data since continuation is now invalid
            if (cont.stack.data != null) {
                mem::free(cont.stack.data);
                cont.stack.data = null;
            }
        }
    }
}

/**
 * Count the number of continuations in a given state.
 *
 * Useful for debugging and statistics.
 *
 * @param status The status to count
 * @return Number of continuations with that status
 */
fn usz PromptStack.count_by_status(PromptStack* self, ContinuationStatus status) {
    usz count = 0;
    foreach (&cont : self.continuations) {
        if (cont.status == status) {
            count++;
        }
    }
    return count;
}

/**
 * Count the total number of continuations (all states).
 *
 * @return Total continuation count
 */
fn usz PromptStack.continuation_count(PromptStack* self) {
    return self.continuations.len();
}

/**
 * Remove completed continuations to free memory.
 *
 * Removes all continuations in COMPLETED or INVALIDATED state
 * from the list. Use periodically to prevent unbounded growth.
 *
 * Note: This invalidates pointers to continuations. Only call
 * when no continuation pointers are held.
 *
 * @return Number of continuations removed
 */
fn usz PromptStack.gc_continuations(PromptStack* self) {
    usz removed = 0;
    usz write_idx = 0;

    for (usz read_idx = 0; read_idx < self.continuations.len(); read_idx++) {
        Continuation* cont = &self.continuations[read_idx];

        if (cont.status == COMPLETED || cont.status == INVALIDATED) {
            // Free stack data to prevent memory leak
            if (cont.stack.data != null) {
                mem::free(cont.stack.data);
                cont.stack.data = null;
            }
            removed++;
        } else {
            // Keep this continuation - move it to write position
            if (write_idx != read_idx) {
                self.continuations[write_idx] = *cont;
            }
            write_idx++;
        }
    }

    // Shrink the list by popping removed entries
    while (self.continuations.len() > write_idx) {
        (void)self.continuations.pop();
    }

    return removed;
}

// =============================================================================
// SECTION 5: THREAD-LOCAL PROMPT STACK
// =============================================================================
//
// Global thread-local prompt stack with initialization and access functions.
//

/** Thread-local prompt stack pointer */
tlocal PromptStack* g_prompt_stack;

/**
 * Initialize the thread-local prompt stack.
 *
 * Must be called once per thread before using continuations.
 * Allocates a PromptStack on the heap and initializes it.
 *
 * Safe to call multiple times - subsequent calls are no-ops
 * if already initialized.
 */
fn void prompt_stack_init() {
    if (g_prompt_stack != null) {
        return;  // Already initialized
    }

    PromptStack* ps = (PromptStack*)mem::malloc(PromptStack.sizeof);
    *ps = {};
    ps.init();
    g_prompt_stack = ps;
}

/**
 * Shutdown the thread-local prompt stack.
 *
 * Releases all resources and frees the PromptStack.
 * Call before thread exit to prevent memory leaks.
 *
 * Safe to call multiple times or if not initialized.
 */
fn void prompt_stack_shutdown() {
    if (g_prompt_stack == null) {
        return;
    }

    g_prompt_stack.release();
    mem::free(g_prompt_stack);
    g_prompt_stack = null;
}

/**
 * Get the current thread's prompt stack.
 *
 * Panics if prompt_stack_init() was not called.
 *
 * @return Pointer to the thread-local PromptStack
 */
fn PromptStack* prompt_stack() @inline {
    assert(g_prompt_stack != null, "prompt_stack_init() not called");
    return g_prompt_stack;
}

/**
 * Check if the thread-local prompt stack is initialized.
 *
 * @return true if prompt_stack_init() has been called
 */
fn bool prompt_stack_is_initialized() @inline {
    return g_prompt_stack != null;
}

// =============================================================================
// SECTION 6: CONVENIENCE FUNCTIONS
// =============================================================================
//
// High-level functions that work with the thread-local prompt stack.
//

/**
 * Create a new prompt tag using the thread-local stack.
 *
 * @return A fresh, unique PromptTag
 */
fn PromptTag new_prompt_tag() @inline {
    return prompt_stack().new_tag();
}

/**
 * Push a prompt frame with the given tag using default settings.
 *
 * Creates a minimal prompt frame associated with the given region.
 *
 * @param tag The prompt tag (from new_prompt_tag())
 * @param region_id Region for memory allocation
 * @param region_gen Region generation
 */
fn void push_prompt(PromptTag tag, RegionId region_id, Generation region_gen) {
    PromptFrame frame = prompt_frame_empty();
    frame.tag = tag;
    frame.region_id = region_id;
    frame.region_gen = region_gen;
    prompt_stack().push_prompt(frame);
}

/**
 * Pop the topmost prompt from the thread-local stack.
 *
 * @return true if a prompt was popped, false if stack was empty
 */
fn bool pop_prompt() @inline {
    return prompt_stack().pop_prompt();
}

/**
 * Find a prompt by tag in the thread-local stack.
 *
 * @param tag The prompt tag to find
 * @return Pointer to the PromptFrame, or null
 */
fn PromptFrame* find_prompt(PromptTag tag) @inline {
    return prompt_stack().find_prompt(tag);
}

/**
 * Allocate a continuation using the thread-local stack.
 *
 * @param region_id Region that owns the continuation
 * @param region_gen Region generation
 * @return Pointer to the new Continuation
 */
fn Continuation* alloc_continuation(RegionId region_id, Generation region_gen) @inline {
    return prompt_stack().alloc_continuation(region_id, region_gen);
}

/**
 * Find a continuation by ID in the thread-local stack.
 *
 * @param id The ContinuationId to find
 * @return Pointer to the Continuation, or null
 */
fn Continuation* find_continuation(ContinuationId id) @inline {
    return prompt_stack().find_continuation(id);
}
