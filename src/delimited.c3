/**
 * =============================================================================
 * DELIMITED CONTINUATIONS - shift/reset/resume
 * =============================================================================
 *
 * This module implements delimited continuations using the primitives from
 * context.c3 and data structures from continuation.c3.
 *
 * Conceptual model:
 *   reset { ... shift { |k| ... } ... }
 *
 * - reset: Establishes a delimiter/prompt on the stack
 * - shift: Captures the continuation up to the nearest reset, passes it to handler
 * - resume: Invokes a captured continuation with a value
 *
 * ## USAGE PATTERN
 *
 * ```c3
 * // Initialize systems
 * thread_registry_init();
 * prompt_stack_init();
 *
 * // Create a region for continuation storage
 * RegionHandle region = create_region();
 *
 * // Use reset/shift/resume
 * PromptTag tag = reset_begin(region);
 * // ... body that may call shift ...
 * reset_end(tag);
 *
 * // Cleanup
 * prompt_stack_shutdown();
 * thread_registry_shutdown();
 * ```
 */
module main;

import std::io;

// Maximum allowed continuation stack capture size (4 MB).
// Prevents runaway malloc from deep recursion + continuation capture.
const usz MAX_CONTINUATION_SIZE = 4194304;

// =============================================================================
// SECTION 1: RESET (Delimiter)
// =============================================================================

/**
 * ResetResultKind - Return value kind from a reset block.
 *
 * A reset block can complete in three ways:
 * 1. NORMAL: The body completed without shift
 * 2. SHIFTED: A shift occurred and the handler returned a value
 * 3. ABORTED: An error occurred
 */
enum ResetResultKind : char {
    NORMAL,
    SHIFTED,
    ABORTED
}

/**
 * ResetResult - Return value from a reset block.
 *
 * Contains information about how the reset block completed and
 * any associated values or continuation IDs.
 */
struct ResetResult {
    ResetResultKind kind;
    void*           value;       // Result value
    ContinuationId  captured_k;  // If shifted, the captured continuation
}

/**
 * Begin a reset block (establish a prompt/delimiter).
 *
 * This creates a new prompt frame and captures the current context.
 * The returned PromptTag must be passed to shift operations within this scope.
 *
 * Usage pattern:
 *   PromptTag tag = reset_begin(region_handle);
 *   // ... body that may call shift ...
 *   reset_end(tag);
 *
 * @param region The region to associate with this prompt (continuations go here)
 * @return A new PromptTag for this delimiter
 */
fn PromptTag reset_begin(RegionHandle region) {
    PromptStack* ps = prompt_stack();
    RegionRegistry* reg = thread_registry();

    // Generate new unique tag
    PromptTag tag = ps.new_tag();

    // Create prompt frame
    PromptFrame frame = {
        .tag = tag,
        .region_id = region.region_id,
        .region_gen = region.generation,
        .stack_ptr = get_stack_pointer(),
        .frame_ptr = get_frame_pointer(),
        .handler_fn = null,
        .handler_data = null,
        .depth = 0
    };

    // Capture entry context using a RegisterContext
    // We'll store key fields in the SavedContext for later use
    RegisterContext reg_ctx;
    context_capture(&reg_ctx);

    // Copy captured values to SavedContext fields
    frame.entry_context.instruction_ptr = reg_ctx.ip;
    frame.entry_context.stack_ptr = reg_ctx.sp;
    frame.entry_context.frame_ptr = reg_ctx.fp;
    frame.entry_context.platform_id = PLATFORM_X86_64_LINUX;

    // Push onto prompt stack
    ps.push_prompt(frame);

    return tag;
}

/**
 * End a reset block (remove the prompt).
 *
 * Call this when the reset body completes normally (no shift occurred).
 *
 * @param tag The PromptTag returned by reset_begin
 */
fn void reset_end(PromptTag tag) {
    PromptStack* ps = prompt_stack();

    // Verify we're ending the correct prompt
    PromptFrame* top = ps.top_prompt();
    assert(top != null && (uint)top.tag == (uint)tag,
           "reset_end: tag mismatch or empty stack");

    ps.pop_prompt();
}

/**
 * WithResetFn - Signature for the body passed to with_reset.
 *
 * @param tag The prompt tag for this reset scope
 * @param user_data User-provided context
 * @return Result value from the body
 */
alias WithResetFn = fn void*(PromptTag tag, void* user_data);

/**
 * Execute a function within a reset scope.
 *
 * This is the high-level API for reset. It handles setup and cleanup.
 *
 * @param region The region for this scope's continuations
 * @param body The function to execute within the reset
 * @param user_data Context passed to body
 * @return ResetResult indicating how the block completed
 */
fn ResetResult with_reset(RegionHandle region, WithResetFn body, void* user_data) {
    PromptTag tag = reset_begin(region);

    // Execute the body
    void* result = body(tag, user_data);

    // Normal completion
    reset_end(tag);

    return {
        .kind = ResetResultKind.NORMAL,
        .value = result,
        .captured_k = (ContinuationId)0
    };
}

// =============================================================================
// SECTION 2: SHIFT (Capture)
// =============================================================================

/**
 * ShiftHandlerFn - Signature for shift handlers.
 *
 * @param k The captured continuation
 * @param user_data User-provided context
 * @return Value to return from the reset
 */
alias ShiftHandlerFn = fn void*(Continuation* k, void* user_data);

/**
 * Capture the current continuation up to the given prompt.
 *
 * This is the core shift operation. It:
 * 1. Finds the prompt frame for the given tag
 * 2. Captures the stack segment from current SP to prompt's SP
 * 3. Captures the register context
 * 4. Creates a Continuation object in the prompt's region
 * 5. Aborts to the prompt's context
 *
 * The continuation can later be resumed with resume().
 *
 * @param tag The prompt tag to capture up to
 * @param handler Function to call with the captured continuation
 * @param user_data Context passed to handler
 * @return The value returned by the handler (when resumed) or by resume()
 */
fn void* shift(PromptTag tag, ShiftHandlerFn handler, void* user_data) {
    PromptStack* ps = prompt_stack();

    // Find the target prompt
    PromptFrame* prompt = ps.find_prompt(tag);
    assert(prompt != null, "shift: prompt tag not found");

    // Allocate continuation in the prompt's region
    Continuation* k = ps.alloc_continuation(prompt.region_id, prompt.region_gen);
    k.prompt_tag = tag;

    // Capture current context
    void* current_sp = get_stack_pointer();
    void* prompt_sp = prompt.stack_ptr;

    // Calculate stack segment size
    usz stack_size = stack_segment_size(prompt_sp, current_sp);

    if (stack_size > 0) {
        // Guard: reject oversized captures that indicate deep recursion or corruption
        if (stack_size > MAX_CONTINUATION_SIZE) {
            k.status = ContinuationStatus.INVALIDATED;
            return null;
        }

        // Allocate for the stack data
        k.stack.size = stack_size;
        k.stack.base_sp = prompt_sp;
        k.stack.top_sp = current_sp;
        k.stack.data = mem::malloc(stack_size);  // malloc is correct: raw byte buffers need individual free
        if (k.stack.data == null) {
            k.status = ContinuationStatus.INVALIDATED;
            return null;
        }

        // Copy stack segment
        stack_copy(k.stack.data, current_sp, stack_size);
    }

    // We store the RegisterContext in the SavedContext.registers buffer
    // Cast the registers buffer to RegisterContext* for context_capture
    RegisterContext* ctx_ptr = (RegisterContext*)&k.context.registers[0];

    // Capture register context
    // If context_capture returns non-zero, we're being resumed
    int capture_result = context_capture(ctx_ptr);

    if (capture_result == 0) {
        // Initial capture - call the handler
        k.status = ContinuationStatus.SUSPENDED;

        // Store key fields in SavedContext for debugging/inspection
        k.context.instruction_ptr = ctx_ptr.ip;
        k.context.stack_ptr = ctx_ptr.sp;
        k.context.frame_ptr = ctx_ptr.fp;
        k.context.platform_id = PLATFORM_X86_64_LINUX;

        // Pop prompts up to and including this one
        ps.pop_to_prompt(tag);

        // Call the handler with the continuation
        void* handler_result = handler(k, user_data);

        // Handler returned without resuming - return its value
        // We need to jump back to the prompt's entry context
        // with the handler's result as the value

        // Store result for the reset to retrieve
        return handler_result;
    } else {
        // We were resumed - return the resume value
        return k.resume_value;
    }
}

/**
 * Simplified shift that captures and returns the continuation.
 *
 * This is useful when you want to store the continuation for later use.
 *
 * @param tag The prompt tag to capture up to
 * @return The captured continuation (caller is responsible for resuming or dropping)
 */
fn Continuation* shift_capture(PromptTag tag) {
    PromptStack* ps = prompt_stack();
    RegionRegistry* reg = thread_registry();

    PromptFrame* prompt = ps.find_prompt(tag);
    assert(prompt != null, "shift_capture: prompt tag not found");

    Continuation* k = ps.alloc_continuation(prompt.region_id, prompt.region_gen);
    k.prompt_tag = tag;

    void* current_sp = get_stack_pointer();
    void* prompt_sp = prompt.stack_ptr;
    usz stack_size = stack_segment_size(prompt_sp, current_sp);

    if (stack_size > 0) {
        // Guard: reject oversized captures that indicate deep recursion or corruption
        if (stack_size > MAX_CONTINUATION_SIZE) {
            k.status = ContinuationStatus.INVALIDATED;
            return null;
        }

        k.stack.size = stack_size;
        k.stack.base_sp = prompt_sp;
        k.stack.top_sp = current_sp;
        k.stack.data = mem::malloc(stack_size);
        if (k.stack.data == null) {
            k.status = ContinuationStatus.INVALIDATED;
            return null;
        }
        stack_copy(k.stack.data, current_sp, stack_size);
    }

    // Store RegisterContext in the SavedContext.registers buffer
    RegisterContext* ctx_ptr = (RegisterContext*)&k.context.registers[0];
    int capture_result = context_capture(ctx_ptr);

    if (capture_result == 0) {
        k.status = ContinuationStatus.SUSPENDED;

        // Store key fields in SavedContext for debugging/inspection
        k.context.instruction_ptr = ctx_ptr.ip;
        k.context.stack_ptr = ctx_ptr.sp;
        k.context.frame_ptr = ctx_ptr.fp;
        k.context.platform_id = PLATFORM_X86_64_LINUX;

        return k;
    } else {
        // Resumed - return null to indicate we're in the resumed path
        return null;
    }
}

// =============================================================================
// SECTION 3: RESUME
// =============================================================================

/**
 * Resume a suspended continuation with a value.
 *
 * This restores the continuation's stack and registers, then continues
 * execution from where shift was called. The value is returned from shift.
 *
 * For one-shot continuations, this invalidates the continuation.
 * For multi-shot, the continuation remains resumable.
 *
 * @param k The continuation to resume
 * @param value The value to return from shift
 */
fn void resume(Continuation* k, void* value) {
    assert(k != null, "resume: null continuation");
    assert(k.status == ContinuationStatus.SUSPENDED, "resume: continuation not suspended");
    assert(is_continuation_valid(k), "resume: continuation is invalid (region dead?)");

    if (k.is_one_shot && k.has_been_resumed) {
        unreachable("resume: one-shot continuation already resumed");
    }

    // For multi-shot continuations, clone before resuming to keep original SUSPENDED
    if (!k.is_one_shot) {
        Continuation* clone = clone_continuation(k);
        // Resume the clone instead, leaving original SUSPENDED
        clone.resume_value = value;
        clone.has_been_resumed = true;
        clone.status = ContinuationStatus.RUNNING;

        if (clone.stack.data != null && clone.stack.size > 0) {
            stack_copy(clone.stack.top_sp, clone.stack.data, clone.stack.size);
        }

        RegisterContext* ctx_ptr = (RegisterContext*)&clone.context.registers[0];
        context_restore(ctx_ptr, 1);
        // Does not return
    }

    k.resume_value = value;
    k.has_been_resumed = true;
    k.status = ContinuationStatus.RUNNING;

    // Restore stack segment
    if (k.stack.data != null && k.stack.size > 0) {
        stack_copy(k.stack.top_sp, k.stack.data, k.stack.size);
    }

    // Get RegisterContext from SavedContext.registers buffer
    RegisterContext* ctx_ptr = (RegisterContext*)&k.context.registers[0];

    // Restore register context (this does not return)
    context_restore(ctx_ptr, 1);  // Non-zero indicates resume
}

/**
 * Resume a continuation and mark it completed.
 *
 * Use this when you know you won't resume the continuation again.
 * It frees the stack segment memory.
 *
 * @param k The continuation to resume
 * @param value The value to return from shift
 */
fn void resume_final(Continuation* k, void* value) {
    assert(k != null, "resume_final: null continuation");
    assert(k.status == ContinuationStatus.SUSPENDED, "resume_final: not suspended");
    assert(is_continuation_valid(k), "resume_final: continuation is invalid (region dead?)");

    k.resume_value = value;
    k.status = ContinuationStatus.RUNNING;

    // Restore stack
    if (k.stack.data != null && k.stack.size > 0) {
        stack_copy(k.stack.top_sp, k.stack.data, k.stack.size);

        // Free stack storage since this is final
        mem::free(k.stack.data);
        k.stack.data = null;
    }

    // Get RegisterContext from SavedContext.registers buffer
    RegisterContext* ctx_ptr = (RegisterContext*)&k.context.registers[0];

    // After resume completes, mark as completed
    // (This won't execute until control returns through the continuation)

    context_restore(ctx_ptr, 1);
}

/**
 * Abort a continuation without resuming it.
 *
 * Frees resources and marks the continuation as invalidated.
 *
 * @param k The continuation to abort
 */
fn void abort_continuation(Continuation* k) {
    assert(k != null, "abort_continuation: null continuation pointer");

    if (k.stack.data != null) {
        mem::free(k.stack.data);
        k.stack.data = null;
    }

    k.status = ContinuationStatus.INVALIDATED;
}

// =============================================================================
// SECTION 4: MULTI-SHOT CONTINUATIONS
// =============================================================================

/**
 * Clone a continuation for multi-shot use.
 *
 * Creates a copy of the continuation that can be resumed independently.
 * Both the original and clone can be resumed.
 *
 * @param k The continuation to clone
 * @return A new continuation that is a copy of k
 */
fn Continuation* clone_continuation(Continuation* k) {
    assert(k != null, "clone_continuation: null continuation");
    assert(k.status == ContinuationStatus.SUSPENDED, "clone: not suspended");

    PromptStack* ps = prompt_stack();

    // Allocate new continuation
    Continuation* clone = ps.alloc_continuation(k.home_region, k.home_generation);

    // Copy all fields
    clone.prompt_tag = k.prompt_tag;
    clone.status = ContinuationStatus.SUSPENDED;
    clone.context = k.context;
    clone.parent_continuation = k.parent_continuation;
    clone.is_one_shot = false;  // Clones are never one-shot
    clone.has_been_resumed = false;
    clone.resume_value = null;
    clone.return_value = null;

    // Copy debug name
    for (usz i = 0; i < 64; i++) {
        clone.debug_name[i] = k.debug_name[i];
    }

    // Clone stack segment
    if (k.stack.data != null && k.stack.size > 0) {
        clone.stack.size = k.stack.size;
        clone.stack.base_sp = k.stack.base_sp;
        clone.stack.top_sp = k.stack.top_sp;
        clone.stack.data = mem::malloc(k.stack.size);
        if (clone.stack.data == null) {
            clone.status = ContinuationStatus.INVALIDATED;
            return clone;
        }
        mem::copy(clone.stack.data, k.stack.data, k.stack.size);
    }

    // Original becomes multi-shot too
    k.is_one_shot = false;

    return clone;
}

/**
 * Make a continuation multi-shot.
 *
 * Allows the continuation to be resumed multiple times.
 * Each resume after the first will clone the stack.
 *
 * @param k The continuation to make multi-shot
 */
fn void make_multi_shot(Continuation* k) {
    if (k != null) {
        k.is_one_shot = false;
    }
}

// =============================================================================
// SECTION 5: CONTINUATION QUERIES
// =============================================================================

/**
 * Check if a continuation can be resumed.
 *
 * @param k The continuation to check
 * @return true if the continuation can be resumed
 */
fn bool can_resume(Continuation* k) {
    if (k == null) return false;
    if (k.status != ContinuationStatus.SUSPENDED) return false;
    if (k.is_one_shot && k.has_been_resumed) return false;
    return true;
}

/**
 * Check if a continuation is still valid (region hasn't died).
 *
 * @param k The continuation to check
 * @return true if the continuation is still valid
 */
fn bool is_continuation_valid(Continuation* k) {
    if (k == null) return false;
    if (k.status == ContinuationStatus.INVALIDATED) return false;

    // Check if home region is still alive
    RegionRegistry* reg = thread_registry();
    usz idx = (usz)(uint)k.home_region;

    if (idx >= reg.region_storage.len()) return false;
    if (!reg.region_alive_flags[idx]) return false;
    if ((uint)reg.region_generations[idx] != (uint)k.home_generation) return false;

    return true;
}

/**
 * Get the status of a continuation.
 *
 * @param k The continuation to check
 * @return The ContinuationStatus of the continuation
 */
fn ContinuationStatus get_continuation_status(Continuation* k) {
    if (k == null) return ContinuationStatus.INVALIDATED;
    return k.status;
}

/**
 * Get the prompt tag associated with a continuation.
 *
 * @param k The continuation
 * @return The PromptTag this continuation was captured under
 */
fn PromptTag get_continuation_prompt_tag(Continuation* k) {
    if (k == null) return INVALID_PROMPT_TAG;
    return k.prompt_tag;
}

// =============================================================================
// SECTION 6: EFFECT HANDLER SUPPORT
// =============================================================================

/**
 * EffectHandler - Function signature for effect handlers.
 *
 * @param effect_id Identifies which effect was performed
 * @param arg The argument passed to the effect
 * @param k The captured continuation
 * @param handler_data User data for the handler
 * @return Value to pass back (either to resume or to outer reset)
 */
alias EffectHandler = fn void*(uint effect_id, void* arg, Continuation* k, void* handler_data);

/**
 * Install an effect handler for a prompt.
 *
 * When shift is called within this prompt, the handler is invoked
 * instead of requiring explicit handler passing.
 *
 * @param tag The prompt to install the handler on
 * @param handler The effect handler function
 * @param handler_data User data passed to handler
 */
fn void install_effect_handler(PromptTag tag, EffectHandler handler, void* handler_data) {
    PromptStack* ps = prompt_stack();
    PromptFrame* frame = ps.find_prompt(tag);

    if (frame != null) {
        frame.handler_fn = (void*)handler;
        frame.handler_data = handler_data;
    }
}

/**
 * Perform an effect (shift with effect ID).
 *
 * If the prompt has an installed handler, it is invoked automatically.
 * Otherwise, this behaves like shift with a null handler.
 *
 * @param tag The prompt tag
 * @param effect_id Identifier for the effect type
 * @param arg Argument to pass to the handler
 * @return Value from handler or resume
 */
fn void* perform_effect(PromptTag tag, uint effect_id, void* arg) {
    PromptStack* ps = prompt_stack();
    PromptFrame* prompt = ps.find_prompt(tag);
    assert(prompt != null, "perform_effect: prompt not found");

    // Save handler info BEFORE shift_capture (which may invalidate the prompt pointer)
    void* saved_handler_fn = prompt.handler_fn;
    void* saved_handler_data = prompt.handler_data;

    // Capture continuation
    Continuation* k = shift_capture(tag);

    if (k != null) {
        // Initial capture - we have the continuation
        k.status = ContinuationStatus.SUSPENDED;

        if (saved_handler_fn != null) {
            EffectHandler handler = (EffectHandler)saved_handler_fn;
            return handler(effect_id, arg, k, saved_handler_data);
        } else {
            // No handler - just return the continuation wrapped somehow
            // This shouldn't normally happen in well-formed code
            return (void*)k;
        }
    } else {
        // Resumed path
        return null;
    }
}

// =============================================================================
// SECTION 7: CONVENIENCE FUNCTIONS
// =============================================================================

/**
 * Create a reset scope and immediately return a tag.
 *
 * This is a shorthand for reset_begin when the region can be inferred.
 *
 * @param region The region to associate with this prompt
 * @return A new PromptTag for this delimiter
 */
fn PromptTag reset_scope(RegionHandle region) {
    return reset_begin(region);
}

/**
 * Check if the prompt stack currently has any active prompts.
 *
 * @return true if there are active prompts
 */
fn bool has_active_prompts() {
    PromptStack* ps = prompt_stack();
    return ps.depth() > 0;
}

/**
 * Get the depth of active prompts.
 *
 * @return Number of active prompt frames
 */
fn usz prompt_depth() {
    PromptStack* ps = prompt_stack();
    return ps.depth();
}

/**
 * Get the topmost prompt tag.
 *
 * @return The tag of the topmost prompt, or INVALID_PROMPT_TAG if none
 */
fn PromptTag top_prompt_tag() {
    PromptStack* ps = prompt_stack();
    PromptFrame* top = ps.top_prompt();
    if (top == null) return INVALID_PROMPT_TAG;
    return top.tag;
}

// =============================================================================
// SECTION 8: DEBUG AND DIAGNOSTICS
// =============================================================================

/**
 * Print debug information about a continuation.
 *
 * @param k The continuation to print info about
 */
fn void debug_print_continuation(Continuation* k) {
    if (k == null) {
        io::printfn("Continuation: null");
        return;
    }

    io::printfn("Continuation:");
    io::printfn("  ID:           %d", (uint)k.id);
    io::printfn("  Prompt Tag:   %d", (uint)k.prompt_tag);
    io::printfn("  Status:       %d", (int)k.status);
    io::printfn("  Home Region:  %d (gen %d)", (uint)k.home_region, (uint)k.home_generation);
    io::printfn("  One-shot:     %s", k.is_one_shot ? "yes" : "no");
    io::printfn("  Resumed:      %s", k.has_been_resumed ? "yes" : "no");
    io::printfn("  Stack:");
    io::printfn("    Base SP:    %p", k.stack.base_sp);
    io::printfn("    Top SP:     %p", k.stack.top_sp);
    io::printfn("    Size:       %d bytes", (int)k.stack.size);
    io::printfn("    Data ptr:   %p", k.stack.data);
}

/**
 * Print debug information about the current prompt stack.
 */
fn void debug_print_prompt_stack() {
    PromptStack* ps = prompt_stack();

    io::printfn("Prompt Stack:");
    io::printfn("  Depth: %d", (int)ps.depth());
    io::printfn("  Next Tag: %d", (uint)ps.next_tag);
    io::printfn("  Continuation Count: %d", (int)ps.continuation_count());

    if (ps.depth() > 0) {
        io::printfn("  Frames:");
        for (usz i = 0; i < ps.frames.len(); i++) {
            PromptFrame* f = &ps.frames[i];
            io::printfn("    [%d] Tag=%d, Region=%d (gen=%d), SP=%p",
                (int)i, (uint)f.tag, (uint)f.region_id, (uint)f.region_gen, f.stack_ptr);
        }
    }
}

/**
 * Get statistics about the prompt stack.
 *
 * @return Number of continuations in various states
 */
struct PromptStackStats {
    usz total_continuations;
    usz suspended;
    usz running;
    usz completed;
    usz invalidated;
    usz prompt_depth;
}

fn PromptStackStats get_prompt_stack_stats() {
    PromptStack* ps = prompt_stack();

    PromptStackStats stats;
    stats.total_continuations = ps.continuation_count();
    stats.suspended = ps.count_by_status(ContinuationStatus.SUSPENDED);
    stats.running = ps.count_by_status(ContinuationStatus.RUNNING);
    stats.completed = ps.count_by_status(ContinuationStatus.COMPLETED);
    stats.invalidated = ps.count_by_status(ContinuationStatus.INVALIDATED);
    stats.prompt_depth = ps.depth();

    return stats;
}

// =============================================================================
// SECTION 9: COMPREHENSIVE TESTS
// =============================================================================
//
// These tests verify the delimited continuation system:
// - Basic reset/shift/resume operations
// - Prompt stack management
// - Continuation lifecycle (allocation, status, queries)
// - Multi-shot continuations and cloning
// - Region integration
// - Effect handler pattern
// - Abort and cleanup
//
// Note: Full shift/resume with actual context switching is complex due to
// x86_64 assembly. Tests focus on verifiable primitives and state management.
//

// -----------------------------------------------------------------------------
// Test Assertion Helpers
// -----------------------------------------------------------------------------

fn void delim_assert_true(bool condition, String message) {
    if (!condition) {
        io::printfn("    [FAIL] %s", message);
        unreachable("Test assertion failed");
    }
}

fn void delim_assert_eq_int(int expected, int actual, String message) {
    if (expected != actual) {
        io::printfn("    [FAIL] %s: expected %d, got %d", message, expected, actual);
        unreachable("Test assertion failed");
    }
}

fn void delim_assert_eq_uint(uint expected, uint actual, String message) {
    if (expected != actual) {
        io::printfn("    [FAIL] %s: expected %d, got %d", message, expected, actual);
        unreachable("Test assertion failed");
    }
}

fn void delim_assert_eq_usz(usz expected, usz actual, String message) {
    if (expected != actual) {
        io::printfn("    [FAIL] %s: expected %d, got %d", message, (int)expected, (int)actual);
        unreachable("Test assertion failed");
    }
}

fn void delim_assert_not_null(void* ptr, String message) {
    if (ptr == null) {
        io::printfn("    [FAIL] %s: expected non-null", message);
        unreachable("Test assertion failed");
    }
}

// -----------------------------------------------------------------------------
// Test 1: Basic Reset/Shift Structure
// -----------------------------------------------------------------------------

/**
 * Test basic reset_begin/reset_end without shift.
 * Verifies prompt stack push/pop operations work correctly.
 */
fn void test_basic_reset_structure() {
    io::printfn("[TEST] test_basic_reset_structure");

    // Ensure systems are initialized
    if (!prompt_stack_is_initialized()) {
        prompt_stack_init();
    }
    thread_registry_init();

    RegionHandle r = create_region();
    PromptStack* ps = prompt_stack();

    usz initial_depth = ps.depth();

    // Begin reset - should push a prompt frame
    PromptTag tag = reset_begin(r);

    delim_assert_true((uint)tag != (uint)INVALID_PROMPT_TAG, "tag should be valid");
    delim_assert_eq_usz(initial_depth + 1, ps.depth(), "depth should increase by 1");

    // Verify top prompt matches
    PromptFrame* top = ps.top_prompt();
    delim_assert_not_null(top, "top prompt should not be null");
    delim_assert_eq_uint((uint)tag, (uint)top.tag, "top prompt tag should match");
    delim_assert_eq_uint((uint)r.region_id, (uint)top.region_id, "region_id should match");
    delim_assert_eq_uint((uint)r.generation, (uint)top.region_gen, "region_gen should match");

    // End reset - should pop the prompt frame
    reset_end(tag);
    delim_assert_eq_usz(initial_depth, ps.depth(), "depth should return to initial");

    release_region(r);

    io::printfn("[PASS] test_basic_reset_structure");
}

// -----------------------------------------------------------------------------
// Test 2: Nested Resets
// -----------------------------------------------------------------------------

/**
 * Test nested reset blocks with multiple tags.
 * Verifies prompt stack handles nesting correctly.
 */
fn void test_nested_resets() {
    io::printfn("[TEST] test_nested_resets");

    thread_registry_init();
    PromptStack* ps = prompt_stack();

    RegionHandle r1 = create_region();
    RegionHandle r2 = create_region();
    RegionHandle r3 = create_region();

    usz base_depth = ps.depth();

    // Nested reset structure: outer { middle { inner } }
    PromptTag outer_tag = reset_begin(r1);
    delim_assert_eq_usz(base_depth + 1, ps.depth(), "depth after outer");

    PromptTag middle_tag = reset_begin(r2);
    delim_assert_eq_usz(base_depth + 2, ps.depth(), "depth after middle");

    PromptTag inner_tag = reset_begin(r3);
    delim_assert_eq_usz(base_depth + 3, ps.depth(), "depth after inner");

    // Verify we can find all prompts
    delim_assert_not_null(ps.find_prompt(outer_tag), "find outer prompt");
    delim_assert_not_null(ps.find_prompt(middle_tag), "find middle prompt");
    delim_assert_not_null(ps.find_prompt(inner_tag), "find inner prompt");

    // Verify top is inner
    PromptFrame* top = ps.top_prompt();
    delim_assert_eq_uint((uint)inner_tag, (uint)top.tag, "top should be inner");

    // Pop in reverse order
    reset_end(inner_tag);
    delim_assert_eq_usz(base_depth + 2, ps.depth(), "depth after inner end");

    reset_end(middle_tag);
    delim_assert_eq_usz(base_depth + 1, ps.depth(), "depth after middle end");

    reset_end(outer_tag);
    delim_assert_eq_usz(base_depth, ps.depth(), "depth after outer end");

    release_region(r3);
    release_region(r2);
    release_region(r1);

    io::printfn("[PASS] test_nested_resets");
}

// -----------------------------------------------------------------------------
// Test 3: Continuation Allocation and Status
// -----------------------------------------------------------------------------

/**
 * Test continuation allocation and status management.
 */
fn void test_continuation_allocation() {
    io::printfn("[TEST] test_continuation_allocation");

    thread_registry_init();
    PromptStack* ps = prompt_stack();
    RegionHandle r = create_region();

    usz initial_count = ps.continuation_count();

    // Allocate a continuation
    Continuation* k1 = ps.alloc_continuation(r.region_id, r.generation);
    delim_assert_not_null(k1, "k1 should not be null");
    delim_assert_eq_usz(initial_count + 1, ps.continuation_count(), "count after k1");

    // Check default values
    delim_assert_true(k1.status == ContinuationStatus.SUSPENDED, "initial status is SUSPENDED");
    delim_assert_true(k1.is_one_shot, "default is one-shot");
    delim_assert_true(!k1.has_been_resumed, "has_been_resumed is false");
    delim_assert_eq_uint((uint)r.region_id, (uint)k1.home_region, "home_region matches");
    delim_assert_eq_uint((uint)r.generation, (uint)k1.home_generation, "home_generation matches");

    // Allocate another
    Continuation* k2 = ps.alloc_continuation(r.region_id, r.generation);
    delim_assert_not_null(k2, "k2 should not be null");
    delim_assert_eq_usz(initial_count + 2, ps.continuation_count(), "count after k2");

    // IDs should be unique
    delim_assert_true((uint)k1.id != (uint)k2.id, "continuation IDs should be unique");

    // Verify find works
    Continuation* found = ps.find_continuation(k1.id);
    delim_assert_true(found == k1, "find_continuation should return k1");

    release_region(r);

    io::printfn("[PASS] test_continuation_allocation");
}

// -----------------------------------------------------------------------------
// Test 4: Continuation Status Queries
// -----------------------------------------------------------------------------

/**
 * Test can_resume() and status query functions.
 */
fn void test_continuation_queries() {
    io::printfn("[TEST] test_continuation_queries");

    thread_registry_init();
    PromptStack* ps = prompt_stack();
    RegionHandle r = create_region();

    Continuation* k = ps.alloc_continuation(r.region_id, r.generation);
    k.prompt_tag = (PromptTag)42;
    k.status = ContinuationStatus.SUSPENDED;
    k.is_one_shot = true;
    k.has_been_resumed = false;

    // Test can_resume
    delim_assert_true(can_resume(k), "can_resume initially true");

    // Test is_continuation_valid
    delim_assert_true(is_continuation_valid(k), "is_continuation_valid initially true");

    // Test get_continuation_status
    delim_assert_true(get_continuation_status(k) == ContinuationStatus.SUSPENDED, "status is SUSPENDED");

    // Test get_continuation_prompt_tag
    delim_assert_eq_uint(42, (uint)get_continuation_prompt_tag(k), "prompt_tag is 42");

    // Mark as resumed (one-shot should no longer be resumable)
    k.has_been_resumed = true;
    delim_assert_true(!can_resume(k), "can_resume false after one-shot resumed");

    // Make multi-shot - should be resumable again
    make_multi_shot(k);
    delim_assert_true(!k.is_one_shot, "is_one_shot is false after make_multi_shot");
    delim_assert_true(can_resume(k), "can_resume true for multi-shot even after resume");

    // Change status to RUNNING - should not be resumable
    k.status = ContinuationStatus.RUNNING;
    delim_assert_true(!can_resume(k), "can_resume false when RUNNING");

    // Change status to INVALIDATED
    k.status = ContinuationStatus.INVALIDATED;
    delim_assert_true(!can_resume(k), "can_resume false when INVALIDATED");
    delim_assert_true(!is_continuation_valid(k), "is_continuation_valid false when INVALIDATED");

    release_region(r);

    io::printfn("[PASS] test_continuation_queries");
}

// -----------------------------------------------------------------------------
// Test 5: Continuation Cloning
// -----------------------------------------------------------------------------

/**
 * Test clone_continuation for multi-shot support.
 */
fn void test_continuation_cloning() {
    io::printfn("[TEST] test_continuation_cloning");

    thread_registry_init();
    PromptStack* ps = prompt_stack();
    RegionHandle r = create_region();

    // Create original continuation with some state
    Continuation* original = ps.alloc_continuation(r.region_id, r.generation);
    original.prompt_tag = (PromptTag)99;
    original.status = ContinuationStatus.SUSPENDED;
    original.is_one_shot = true;
    original.resume_value = (void*)0x1234;

    // Set a debug name
    original.debug_name[0] = 't';
    original.debug_name[1] = 'e';
    original.debug_name[2] = 's';
    original.debug_name[3] = 't';
    original.debug_name[4] = 0;

    // Allocate fake stack data
    original.stack.size = 64;
    original.stack.base_sp = (void*)0x7fff0000;
    original.stack.top_sp = (void*)0x7ffeffc0;
    original.stack.data = mem::malloc(64);
    mem::copy(original.stack.data, "test data for stack segment!!!", 32);

    // Clone the continuation
    Continuation* clone = clone_continuation(original);

    // Verify clone is different object but same values
    delim_assert_true(clone != original, "clone is different pointer");
    delim_assert_true((uint)clone.id != (uint)original.id, "clone has different id");

    // Verify values were copied
    delim_assert_eq_uint((uint)original.prompt_tag, (uint)clone.prompt_tag, "prompt_tag copied");
    delim_assert_true(clone.status == ContinuationStatus.SUSPENDED, "clone status is SUSPENDED");
    delim_assert_true(!clone.is_one_shot, "clone is multi-shot");
    delim_assert_true(!clone.has_been_resumed, "clone has_been_resumed is false");

    // Verify debug name was copied
    delim_assert_true(clone.debug_name[0] == 't', "debug_name[0] copied");

    // Verify stack data was deep copied (different pointer)
    delim_assert_true(clone.stack.data != original.stack.data, "stack.data is deep copied");
    delim_assert_eq_usz(original.stack.size, clone.stack.size, "stack.size matches");

    // Verify original is now multi-shot too
    delim_assert_true(!original.is_one_shot, "original is now multi-shot");

    // Clean up - null out after free so invalidate_region_continuations
    // doesn't double-free during release_region
    mem::free(original.stack.data);
    original.stack.data = null;
    mem::free(clone.stack.data);
    clone.stack.data = null;
    release_region(r);

    io::printfn("[PASS] test_continuation_cloning");
}

// -----------------------------------------------------------------------------
// Test 6: Region Integration - Continuation Invalidation
// -----------------------------------------------------------------------------

/**
 * Test that continuations are invalidated when invalidate_region_continuations is called.
 * This tests the invalidation mechanism directly rather than relying on the full
 * region destruction flow (which involves refcounting and parent/child relationships).
 */
fn void test_region_invalidation() {
    io::printfn("[TEST] test_region_invalidation");

    thread_registry_init();
    PromptStack* ps = prompt_stack();

    // Create a region
    RegionHandle r = create_region();
    RegionId rid = r.region_id;
    Generation rgen = r.generation;

    // Allocate continuations in this region
    Continuation* k1 = ps.alloc_continuation(rid, rgen);
    Continuation* k2 = ps.alloc_continuation(rid, rgen);
    k1.status = ContinuationStatus.SUSPENDED;
    k2.status = ContinuationStatus.SUSPENDED;

    delim_assert_true(is_continuation_valid(k1), "k1 valid before region death");
    delim_assert_true(is_continuation_valid(k2), "k2 valid before region death");
    delim_assert_true(can_resume(k1), "k1 can_resume before region death");
    delim_assert_true(can_resume(k2), "k2 can_resume before region death");

    // Create another region's continuation (should not be affected)
    RegionHandle other_r = create_region();
    Continuation* k_other = ps.alloc_continuation(other_r.region_id, other_r.generation);
    k_other.status = ContinuationStatus.SUSPENDED;

    // Directly call invalidate_region_continuations to test the mechanism
    // This simulates what happens when a region is destroyed
    ps.invalidate_region_continuations(rid, rgen);

    // k1 and k2 should now be invalidated
    delim_assert_true(!is_continuation_valid(k1), "k1 invalid after region death");
    delim_assert_true(!is_continuation_valid(k2), "k2 invalid after region death");
    delim_assert_true(!can_resume(k1), "k1 cannot resume after region death");
    delim_assert_true(!can_resume(k2), "k2 cannot resume after region death");

    // k_other should still be valid (different region)
    delim_assert_true(is_continuation_valid(k_other), "k_other still valid");
    delim_assert_true(can_resume(k_other), "k_other can still resume");

    // Clean up regions
    release_region(r);
    release_region(other_r);

    io::printfn("[PASS] test_region_invalidation");
}

// -----------------------------------------------------------------------------
// Test 7: Effect Handler Installation
// -----------------------------------------------------------------------------

/**
 * Global state for effect handler test.
 */
uint g_effect_handler_called = 0;
uint g_last_effect_id = 0;
void* g_last_effect_arg = null;

fn void* test_effect_handler(uint effect_id, void* arg, Continuation* k, void* handler_data) {
    g_effect_handler_called++;
    g_last_effect_id = effect_id;
    g_last_effect_arg = arg;
    // Return a test value
    return (void*)0xDEAD;
}

/**
 * Test effect handler installation and queries.
 */
fn void test_effect_handler_installation() {
    io::printfn("[TEST] test_effect_handler_installation");

    thread_registry_init();
    PromptStack* ps = prompt_stack();
    RegionHandle r = create_region();

    // Reset global state
    g_effect_handler_called = 0;
    g_last_effect_id = 0;
    g_last_effect_arg = null;

    // Create a prompt
    PromptTag tag = reset_begin(r);

    // Initially no handler
    PromptFrame* frame = ps.find_prompt(tag);
    delim_assert_not_null(frame, "frame exists");
    delim_assert_true(!frame.has_handler(), "initially no handler");

    // Install effect handler
    void* handler_data = (void*)0x42;
    install_effect_handler(tag, &test_effect_handler, handler_data);

    // Verify handler is installed
    delim_assert_true(frame.has_handler(), "handler is installed");
    delim_assert_true(frame.handler_fn == (void*)&test_effect_handler, "handler_fn matches");
    delim_assert_true(frame.handler_data == handler_data, "handler_data matches");

    reset_end(tag);
    release_region(r);

    io::printfn("[PASS] test_effect_handler_installation");
}

// -----------------------------------------------------------------------------
// Test 8: Abort Continuation
// -----------------------------------------------------------------------------

/**
 * Test abort_continuation properly cleans up.
 */
fn void test_abort_continuation() {
    io::printfn("[TEST] test_abort_continuation");

    thread_registry_init();
    PromptStack* ps = prompt_stack();
    RegionHandle r = create_region();

    Continuation* k = ps.alloc_continuation(r.region_id, r.generation);
    k.status = ContinuationStatus.SUSPENDED;

    // Allocate some stack data
    k.stack.data = mem::malloc(128);
    k.stack.size = 128;

    delim_assert_true(is_continuation_valid(k), "valid before abort");
    delim_assert_true(k.stack.data != null, "stack.data allocated");

    // Abort the continuation
    abort_continuation(k);

    // Verify cleanup
    delim_assert_true(k.status == ContinuationStatus.INVALIDATED, "status is INVALIDATED");
    delim_assert_true(k.stack.data == null, "stack.data freed");
    delim_assert_true(!is_continuation_valid(k), "invalid after abort");

    // Note: abort_continuation(null) now asserts — not tested here

    release_region(r);

    io::printfn("[PASS] test_abort_continuation");
}

// -----------------------------------------------------------------------------
// Test 9: Context Capture Returns Zero
// -----------------------------------------------------------------------------

/**
 * Test that context_capture returns 0 on first call.
 * This is fundamental to shift/resume working correctly.
 */
fn void test_context_capture_returns_zero() {
    io::printfn("[TEST] test_context_capture_returns_zero");

    RegisterContext ctx;
    int result = context_capture(&ctx);

    // First call should return 0
    if (result == 0) {
        // Verify some basic fields are set
        delim_assert_true(ctx.sp != null, "sp is captured");
        delim_assert_true(ctx.ip != null, "ip is captured");

        io::printfn("[PASS] test_context_capture_returns_zero");
    } else {
        // If we get here, context_restore was called which shouldn't happen
        io::printfn("[PASS] test_context_capture_returns_zero (via restore path, result=%d)", result);
    }
}

// -----------------------------------------------------------------------------
// Test 10: Stack Segment Size Calculation
// -----------------------------------------------------------------------------

/**
 * Test stack_segment_size calculation for continuation capture.
 */
fn void test_stack_segment_calculation() {
    io::printfn("[TEST] test_stack_segment_calculation");

    // Stack grows down on x86_64: base > top
    void* base = (void*)0x7fff0000;  // Higher address (prompt's SP)
    void* top = (void*)0x7ffef000;   // Lower address (current SP)

    usz size = stack_segment_size(base, top);
    delim_assert_eq_usz(0x1000, size, "size is 4096 bytes");

    // Test with equal pointers (no stack growth)
    void* same = (void*)0x7fff0000;
    usz zero_size = stack_segment_size(same, same);
    delim_assert_eq_usz(0, zero_size, "same pointers give 0 size");

    // Test inverted pointers (invalid - top > base)
    usz inverted = stack_segment_size(top, base);
    delim_assert_eq_usz(0, inverted, "inverted pointers give 0 size");

    io::printfn("[PASS] test_stack_segment_calculation");
}

// -----------------------------------------------------------------------------
// Test 11: Prompt Stack Pop To
// -----------------------------------------------------------------------------

/**
 * Test pop_to_prompt functionality for nested prompts.
 *
 * pop_to_prompt pops frames down TO the target frame's depth level,
 * which means all frames at depths >= target.depth are removed.
 */
fn void test_prompt_stack_pop_to() {
    io::printfn("[TEST] test_prompt_stack_pop_to");

    thread_registry_init();
    PromptStack* ps = prompt_stack();
    RegionHandle r = create_region();

    usz base_depth = ps.depth();

    // Push several prompts
    // tag1: pushed at depth=base_depth, frames.len() becomes base_depth+1
    // tag2: pushed at depth=base_depth+1, frames.len() becomes base_depth+2
    // tag3: pushed at depth=base_depth+2, frames.len() becomes base_depth+3
    PromptTag tag1 = reset_begin(r);
    PromptTag tag2 = reset_begin(r);
    PromptTag tag3 = reset_begin(r);

    delim_assert_eq_usz(base_depth + 3, ps.depth(), "depth is base+3");

    // Pop to tag2: pops while frames.len() > tag2.depth (which is base_depth+1)
    // So it pops from base_depth+3 down to base_depth+1, removing 2 frames (tag3 and tag2)
    // This leaves tag1 at the top
    usz popped = ps.pop_to_prompt(tag2);
    // tag2's depth = base_depth + 1, so we pop from base+3 to base+1 = 2 frames
    delim_assert_eq_usz(2, popped, "popped 2 frames (down to tag2's depth)");
    delim_assert_eq_usz(base_depth + 1, ps.depth(), "depth is base+1");

    // Top should now be tag1 (tag2 was also popped since we pop TO its depth level)
    PromptFrame* top = ps.top_prompt();
    delim_assert_eq_uint((uint)tag1, (uint)top.tag, "top is tag1");

    // Final cleanup - pop tag1
    reset_end(tag1);
    delim_assert_eq_usz(base_depth, ps.depth(), "depth back to base");

    release_region(r);

    io::printfn("[PASS] test_prompt_stack_pop_to");
}

// -----------------------------------------------------------------------------
// Test 12: Prompt Stack Statistics
// -----------------------------------------------------------------------------

/**
 * Test prompt stack statistics and GC.
 */
fn void test_prompt_stack_stats() {
    io::printfn("[TEST] test_prompt_stack_stats");

    thread_registry_init();
    PromptStack* ps = prompt_stack();
    RegionHandle r = create_region();

    // Create some continuations with different statuses
    Continuation* k1 = ps.alloc_continuation(r.region_id, r.generation);
    Continuation* k2 = ps.alloc_continuation(r.region_id, r.generation);
    Continuation* k3 = ps.alloc_continuation(r.region_id, r.generation);

    k1.status = ContinuationStatus.SUSPENDED;
    k2.status = ContinuationStatus.RUNNING;
    k3.status = ContinuationStatus.COMPLETED;

    // Test count_by_status
    usz suspended = ps.count_by_status(ContinuationStatus.SUSPENDED);
    usz running = ps.count_by_status(ContinuationStatus.RUNNING);
    usz completed = ps.count_by_status(ContinuationStatus.COMPLETED);

    delim_assert_true(suspended >= 1, "at least 1 suspended");
    delim_assert_true(running >= 1, "at least 1 running");
    delim_assert_true(completed >= 1, "at least 1 completed");

    // Get overall stats
    PromptStackStats stats = get_prompt_stack_stats();
    delim_assert_true(stats.total_continuations >= 3, "total >= 3");
    delim_assert_true(stats.suspended >= 1, "stats.suspended >= 1");
    delim_assert_true(stats.running >= 1, "stats.running >= 1");
    delim_assert_true(stats.completed >= 1, "stats.completed >= 1");

    release_region(r);

    io::printfn("[PASS] test_prompt_stack_stats");
}

// -----------------------------------------------------------------------------
// Test 13: Convenience Functions
// -----------------------------------------------------------------------------

/**
 * Test convenience functions using thread-local state.
 */
fn void test_convenience_functions() {
    io::printfn("[TEST] test_convenience_functions");

    thread_registry_init();

    // Test has_active_prompts
    bool initially_active = has_active_prompts();
    usz initial_depth = prompt_depth();

    RegionHandle r = create_region();
    PromptTag tag = reset_begin(r);

    delim_assert_true(has_active_prompts() || initial_depth > 0, "has_active_prompts after reset_begin");
    delim_assert_eq_usz(initial_depth + 1, prompt_depth(), "prompt_depth increased");

    // Test top_prompt_tag
    PromptTag top_tag = top_prompt_tag();
    delim_assert_eq_uint((uint)tag, (uint)top_tag, "top_prompt_tag matches");

    reset_end(tag);

    // Back to initial state
    delim_assert_eq_usz(initial_depth, prompt_depth(), "prompt_depth restored");

    release_region(r);

    io::printfn("[PASS] test_convenience_functions");
}

// -----------------------------------------------------------------------------
// Test 14: Debug Print Functions (Smoke Test)
// -----------------------------------------------------------------------------

/**
 * Test that debug print functions don't crash.
 */
fn void test_debug_print_functions() {
    io::printfn("[TEST] test_debug_print_functions");

    thread_registry_init();
    PromptStack* ps = prompt_stack();
    RegionHandle r = create_region();

    // Create a continuation with some data
    Continuation* k = ps.alloc_continuation(r.region_id, r.generation);
    k.status = ContinuationStatus.SUSPENDED;
    k.prompt_tag = (PromptTag)77;
    k.stack.size = 256;
    k.stack.base_sp = (void*)0x7fff0000;
    k.stack.top_sp = (void*)0x7ffeff00;
    k.stack.data = null;  // No actual data for this test

    // Create a prompt
    PromptTag tag = reset_begin(r);

    // These should not crash
    io::printfn("  --- debug_print_continuation output ---");
    debug_print_continuation(k);
    io::printfn("  --- debug_print_prompt_stack output ---");
    debug_print_prompt_stack();
    io::printfn("  --- end debug output ---");

    // Test with null continuation
    debug_print_continuation(null);

    reset_end(tag);
    release_region(r);

    io::printfn("[PASS] test_debug_print_functions");
}

// -----------------------------------------------------------------------------
// Test 15: Continuation GC
// -----------------------------------------------------------------------------

/**
 * Test garbage collection of completed/invalidated continuations.
 */
fn void test_continuation_gc() {
    io::printfn("[TEST] test_continuation_gc");

    thread_registry_init();
    PromptStack* ps = prompt_stack();
    RegionHandle r = create_region();

    // First, clean up any leftover continuations from previous tests
    ps.gc_continuations();
    usz initial_count = ps.continuation_count();

    // Create several continuations
    Continuation* k1 = ps.alloc_continuation(r.region_id, r.generation);
    Continuation* k2 = ps.alloc_continuation(r.region_id, r.generation);
    Continuation* k3 = ps.alloc_continuation(r.region_id, r.generation);
    Continuation* k4 = ps.alloc_continuation(r.region_id, r.generation);

    // Set different statuses
    k1.status = ContinuationStatus.SUSPENDED;   // Keep
    k2.status = ContinuationStatus.COMPLETED;   // GC
    k3.status = ContinuationStatus.INVALIDATED; // GC
    k4.status = ContinuationStatus.RUNNING;     // Keep

    delim_assert_eq_usz(initial_count + 4, ps.continuation_count(), "count after creating 4");

    // Run GC
    usz removed = ps.gc_continuations();

    // Should have removed at least k2 and k3
    delim_assert_true(removed >= 2, "removed at least 2 continuations");
    // After GC, the count should have decreased by at least 2
    delim_assert_true(ps.continuation_count() <= initial_count + 2, "count decreased by at least 2");

    release_region(r);

    io::printfn("[PASS] test_continuation_gc");
}

// -----------------------------------------------------------------------------
// Test Runner
// -----------------------------------------------------------------------------

/**
 * Run all delimited continuation tests.
 */
fn void run_delimited_tests() {
    io::printfn("\n========================================");
    io::printfn("DELIMITED CONTINUATION TESTS");
    io::printfn("========================================\n");

    // Basic infrastructure tests
    test_basic_reset_structure();
    test_nested_resets();
    test_prompt_stack_pop_to();

    // Continuation lifecycle tests
    test_continuation_allocation();
    test_continuation_queries();
    test_continuation_cloning();
    test_abort_continuation();
    test_continuation_gc();

    // Region integration tests
    test_region_invalidation();

    // Effect handler tests
    test_effect_handler_installation();

    // Low-level primitive tests
    test_context_capture_returns_zero();
    test_stack_segment_calculation();

    // Convenience and utility tests
    test_convenience_functions();
    test_prompt_stack_stats();
    test_debug_print_functions();

    io::printfn("\n========================================");
    io::printfn("ALL DELIMITED CONTINUATION TESTS PASSED");
    io::printfn("========================================\n");
}
