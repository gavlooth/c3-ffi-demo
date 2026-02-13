/**
 * =============================================================================
 * CONTEXT CAPTURE/RESTORE PRIMITIVES
 * =============================================================================
 *
 * Low-level building blocks for continuations and coroutines.
 * Handles saving and restoring CPU state and stack contents.
 *
 * Platform Support:
 * - x86_64 Linux (primary target)
 *
 * These primitives are similar to setjmp/longjmp but give more control
 * over exactly what state is captured and restored.
 *
 * NOTE: This implementation targets x86_64 Linux. For other platforms,
 * the architecture-specific functions would need to be added.
 */
module main;

import std::io;

// =============================================================================
// SECTION 1: TYPE ALIASES FOR FUNCTION POINTERS
// =============================================================================

/**
 * Allocator function type.
 * Takes opaque data pointer and size, returns allocated memory.
 */
alias ContextAllocatorFn = fn void*(void*, usz);

/**
 * Deallocator function type.
 * Takes opaque data pointer, memory pointer, and size.
 */
alias ContextDeallocatorFn = fn void(void*, void*, usz);

// =============================================================================
// SECTION 2: REGISTER CONTEXT
// =============================================================================

/**
 * RegisterContext - Saved CPU register state.
 *
 * For x86_64 System V ABI:
 *   Callee-saved: RBX, RBP, R12, R13, R14, R15
 *   Plus: RSP (stack pointer), RIP (return address)
 *
 * Layout:
 *   gpr[0-5]:  RBX, RBP, R12, R13, R14, R15
 *   gpr[6-15]: Reserved for future use
 *   sp:        Stack pointer (adjusted past return address)
 *   ip:        Instruction pointer (return address)
 *   fp:        Frame pointer (RBP)
 *   flags:     RFLAGS register
 */
struct RegisterContext {
    // General purpose registers (callee-saved)
    // [0]=RBX, [1]=RBP, [2]=R12, [3]=R13, [4]=R14, [5]=R15
    ulong[16] gpr;

    // Stack pointer
    void* sp;

    // Instruction pointer / return address
    void* ip;

    // Frame pointer (RBP)
    void* fp;

    // CPU flags (RFLAGS)
    uint flags;

    // Padding for alignment
    uint _pad;
}

// =============================================================================
// SECTION 3: CONTEXT CAPTURE - x86_64 IMPLEMENTATION
// =============================================================================

/**
 * Capture the current execution context.
 *
 * Saves all callee-saved registers, stack pointer, and return address.
 * Returns 0 on initial call, non-zero when restored via context_restore.
 *
 * This is similar to setjmp() but gives us explicit control over the layout.
 *
 * @param ctx Pointer to RegisterContext to fill
 * @return 0 on capture, restore_value on restore
 *
 * Implementation notes (x86_64 System V ABI):
 * - Uses @naked to prevent compiler prologue/epilogue
 * - RDI = first argument (ctx pointer)
 * - Return value in RAX
 * - Callee-saved registers: RBX, RBP, R12, R13, R14, R15
 *
 * RegisterContext layout offsets:
 *   0-127:   gpr[16] (8 bytes each)
 *   128:     sp (8 bytes)
 *   136:     ip (8 bytes)
 *   144:     fp (8 bytes)
 *   152:     flags (4 bytes)
 */
fn int context_capture(RegisterContext* ctx) @naked {
    asm {
        // Save callee-saved registers to ctx->gpr[]
        movq [$rdi], $rbx;           // gpr[0] = RBX
        movq [$rdi + 8], $rbp;       // gpr[1] = RBP
        movq [$rdi + 16], $r12;      // gpr[2] = R12
        movq [$rdi + 24], $r13;      // gpr[3] = R13
        movq [$rdi + 32], $r14;      // gpr[4] = R14
        movq [$rdi + 40], $r15;      // gpr[5] = R15

        // Save stack pointer (adjusted past return address on stack)
        // When this function was called, RSP pointed to return address
        // Caller's RSP = current RSP + 8
        movq $rax, $rsp;
        addq $rax, 8;
        movq [$rdi + 128], $rax;     // ctx->sp

        // Save return address (currently at top of stack) as instruction pointer
        movq $rax, [$rsp];
        movq [$rdi + 136], $rax;     // ctx->ip

        // Save frame pointer
        movq [$rdi + 144], $rbp;     // ctx->fp

        // Save flags register using lahf/seto pattern
        // (pushfq may not be supported in all C3 asm contexts)
        // For simplicity, we'll skip flags for now - they're rarely needed
        // and can be reconstructed if needed
        movl [$rdi + 152], 0;        // ctx->flags = 0

        // Return 0 to indicate initial capture
        xorl $eax, $eax;
        ret;
    }
}

/**
 * Restore a previously captured context.
 *
 * Restores all saved registers and jumps to the saved instruction pointer.
 * The context_capture() call that created this context will return `value`.
 *
 * This function NEVER returns to its caller - it jumps to the saved IP.
 *
 * @param ctx   Pointer to RegisterContext to restore
 * @param value Value to return from the restored context_capture() call
 *              IMPORTANT: Caller must ensure value is non-zero; zero would be
 *              indistinguishable from initial capture return
 *
 * Implementation notes (x86_64 System V ABI):
 * - Uses @naked and @noreturn
 * - RDI = ctx, ESI = value
 */
fn void context_restore(RegisterContext* ctx, int value) @naked @noreturn {
    asm {
        // Restore callee-saved registers from ctx->gpr[]
        movq $rbx, [$rdi];           // RBX = gpr[0]
        movq $rbp, [$rdi + 8];       // RBP = gpr[1]
        movq $r12, [$rdi + 16];      // R12 = gpr[2]
        movq $r13, [$rdi + 24];      // R13 = gpr[3]
        movq $r14, [$rdi + 32];      // R14 = gpr[4]
        movq $r15, [$rdi + 40];      // R15 = gpr[5]

        // Set return value (will be returned by context_capture)
        // Value is passed in ESI (second argument), move to EAX for return
        // Caller must ensure value is non-zero (documented requirement)
        movl $eax, $esi;

        // Restore stack pointer
        movq $rsp, [$rdi + 128];

        // Jump to saved instruction pointer by pushing it and using ret
        // This returns to the point after context_capture() was called
        movq $rcx, [$rdi + 136];
        pushq $rcx;
        ret;
    }
}

/**
 * Get the current stack pointer value.
 *
 * @return Current stack pointer as void*
 */
fn void* get_stack_pointer() @naked {
    asm {
        movq $rax, $rsp;
        addq $rax, 8;
        ret;
    }
}

/**
 * Get the current frame pointer value.
 *
 * @return Current frame pointer as void*
 */
fn void* get_frame_pointer() @naked {
    asm {
        movq $rax, $rbp;
        ret;
    }
}

/**
 * Get the return address of the current function.
 *
 * Note: Must be called from a non-leaf function for reliable results.
 *
 * @return Return address as void*
 */
fn void* get_return_address() @naked {
    asm {
        movq $rax, [$rsp];
        ret;
    }
}

// =============================================================================
// SECTION 4: STACK SEGMENT OPERATIONS
// =============================================================================
//
// NOTE: StackSegment struct is defined in continuation.c3.
// This file provides helper functions for working with stack segments.
//

// Maximum allowed stack copy size (1 MB) — prevents buffer overflow from
// incorrect pointer arithmetic or corrupted size values.
const usz MAX_STACK_COPY_SIZE = 1048576;

/**
 * Copy memory from source to destination.
 *
 * Simple byte-by-byte copy. Used for stack segment capture/restore.
 * Validates size is within bounds before copying.
 *
 * @param dest Destination buffer
 * @param src  Source buffer
 * @param size Number of bytes to copy
 */
fn void stack_copy(void* dest, void* src, usz size) @inline {
    // Guard: skip zero-size copies and reject sizes that indicate
    // pointer arithmetic errors or wraparound (usz is unsigned, so
    // very large values signal a bug).
    if (size == 0 || size > MAX_STACK_COPY_SIZE) {
        return;
    }
    char* d = (char*)dest;
    char* s = (char*)src;
    for (usz i = 0; i < size; i++) {
        d[i] = s[i];
    }
}

/**
 * Calculate the size of a stack segment between two pointers.
 *
 * Stack grows downward on x86_64:
 *   - base_sp is at a higher address (older frames, "bottom" of segment)
 *   - top_sp is at a lower address (newer frames, "top" of segment)
 *   - segment_size = base_sp - top_sp
 *
 * @param base_sp Higher address (older frames)
 * @param top_sp  Lower address (newer frames)
 * @return Size in bytes, or 0 if pointers are inverted
 */
fn usz stack_segment_size(void* base_sp, void* top_sp) @inline {
    usz base = (usz)base_sp;
    usz top = (usz)top_sp;

    // Stack grows down, so base should be >= top
    if (base <= top) {
        return 0;
    }
    return base - top;
}

/**
 * Capture a stack segment.
 *
 * Allocates memory and copies stack contents from top_sp to base_sp.
 *
 * @param segment   Output: StackSegment to fill
 * @param base_sp   Higher address (prompt's SP)
 * @param top_sp    Lower address (current SP)
 * @param allocator Function to allocate memory
 * @param alloc_data Opaque data passed to allocator
 */
fn void stack_segment_capture(
    StackSegment* segment,
    void* base_sp,
    void* top_sp,
    ContextAllocatorFn allocator,
    void* alloc_data
) {
    usz size = stack_segment_size(base_sp, top_sp);

    segment.base_sp = base_sp;
    segment.top_sp = top_sp;
    segment.size = size;

    if (size > 0) {
        segment.data = allocator(alloc_data, size);
        stack_copy(segment.data, top_sp, size);
    } else {
        segment.data = null;
    }
}

/**
 * Restore a stack segment.
 *
 * Copies saved stack data back to the original stack location.
 *
 * WARNING: This is a dangerous operation. The caller must ensure:
 * - The stack is in a valid state
 * - No local variables in the restored region are being used
 * - The restore happens just before context_restore()
 *
 * @param segment The saved stack segment to restore
 */
fn void stack_segment_restore(StackSegment* segment) {
    if (segment.data == null || segment.size == 0) {
        return;
    }
    stack_copy(segment.top_sp, segment.data, segment.size);
}

/**
 * Free a stack segment's allocated memory.
 *
 * @param segment    The segment to free
 * @param deallocator Function to free memory
 * @param dealloc_data Opaque data passed to deallocator
 */
fn void stack_segment_free(
    StackSegment* segment,
    ContextDeallocatorFn deallocator,
    void* dealloc_data
) {
    if (segment.data != null) {
        deallocator(dealloc_data, segment.data, segment.size);
        segment.data = null;
    }
    segment.size = 0;
}

// =============================================================================
// SECTION 5: CONTINUATION CONTEXT
// =============================================================================

/**
 * ContinuationContext - Full context needed to resume a continuation.
 *
 * Combines:
 * - RegisterContext: CPU register state
 * - StackSegment: Captured stack contents
 *
 * This is everything needed to restore execution to a previous point.
 */
struct ContinuationContext {
    RegisterContext registers;   // CPU register state
    StackSegment    stack;       // Captured stack data
    bool            valid;       // Has this context been captured?
}

/**
 * Default allocator using malloc.
 * Conforms to ContextAllocatorFn signature.
 */
fn void* default_context_allocator(void* data, usz size) {
    return mem::malloc(size);
}

/**
 * Default deallocator using free.
 * Conforms to ContextDeallocatorFn signature.
 */
fn void default_context_deallocator(void* data, void* ptr, usz size) {
    mem::free(ptr);
}

/**
 * Capture a full continuation context.
 *
 * Captures both CPU registers and the stack segment from current SP
 * up to the given boundary (typically the prompt's SP).
 *
 * Usage pattern:
 *   1. At prompt installation, record boundary = get_stack_pointer()
 *   2. When capturing continuation, call this function
 *   3. Returns true on initial capture, false when restored
 *
 * @param ctx        Context struct to fill
 * @param boundary   Stack pointer boundary (prompt's SP, higher address)
 * @param allocator  Memory allocator function
 * @param alloc_data Opaque data for allocator
 * @return true on initial capture, false after restore
 */
fn bool capture_continuation_context(
    ContinuationContext* ctx,
    void* boundary,
    ContextAllocatorFn allocator,
    void* alloc_data
) {
    // Capture registers first
    // Returns 0 on initial capture, non-zero on restore
    int result = context_capture(&ctx.registers);

    if (result == 0) {
        // Initial capture path

        // Get current stack pointer (after context_capture returned)
        void* current_sp = get_stack_pointer();

        // Capture stack segment from current SP up to boundary
        usz size = stack_segment_size(boundary, current_sp);

        if (size > 0 && allocator != null) {
            ctx.stack.data = allocator(alloc_data, size);
            ctx.stack.size = size;
            ctx.stack.base_sp = boundary;
            ctx.stack.top_sp = current_sp;

            // Copy the stack contents
            stack_copy(ctx.stack.data, current_sp, size);
        } else {
            ctx.stack.data = null;
            ctx.stack.size = 0;
            ctx.stack.base_sp = boundary;
            ctx.stack.top_sp = current_sp;
        }

        ctx.valid = true;
        return true;  // Initial capture
    } else {
        // Restore path - context was already set up before context_restore
        return false;
    }
}

/**
 * Capture continuation context using default malloc allocator.
 */
fn bool capture_continuation_context_default(
    ContinuationContext* ctx,
    void* boundary
) {
    return capture_continuation_context(
        ctx,
        boundary,
        &default_context_allocator,
        null
    );
}

/**
 * Restore a full continuation context.
 *
 * Restores the stack segment first, then restores CPU registers
 * and jumps to the saved instruction pointer.
 *
 * This function NEVER returns - it jumps to the captured context.
 *
 * @param ctx   Context to restore (must be valid)
 * @param value Value to return from capture_continuation_context (non-zero)
 */
fn void restore_continuation_context(ContinuationContext* ctx, int value) @noreturn {
    assert(ctx.valid, "Cannot restore invalid context");

    // Restore the stack segment first
    // This copies the saved stack back to its original location
    stack_segment_restore(&ctx.stack);

    // Now restore registers and jump
    // This never returns - it jumps to ctx.registers.ip
    context_restore(&ctx.registers, value);
}

/**
 * Free resources held by a continuation context.
 *
 * Call this when a continuation will no longer be used.
 *
 * @param ctx          Context to free
 * @param deallocator  Memory deallocator function
 * @param dealloc_data Opaque data for deallocator
 */
fn void free_continuation_context(
    ContinuationContext* ctx,
    ContextDeallocatorFn deallocator,
    void* dealloc_data
) {
    stack_segment_free(&ctx.stack, deallocator, dealloc_data);
    ctx.valid = false;
}

/**
 * Free continuation context using default free deallocator.
 */
fn void free_continuation_context_default(ContinuationContext* ctx) {
    free_continuation_context(ctx, &default_context_deallocator, null);
}

// =============================================================================
// SECTION 6: CONTEXT VALIDATION AND DEBUGGING
// =============================================================================

/**
 * Check if a RegisterContext appears valid.
 *
 * Basic sanity checks on captured register values.
 *
 * @param ctx Context to validate
 * @return true if context appears valid
 */
fn bool is_valid_register_context(RegisterContext* ctx) {
    // Stack pointer should be non-null and aligned
    if (ctx.sp == null) return false;
    if (((usz)ctx.sp & 0x7) != 0) return false;  // 8-byte aligned

    // Instruction pointer should be non-null
    if (ctx.ip == null) return false;

    // Frame pointer should be non-null and aligned
    if (ctx.fp == null) return false;
    if ((usz)ctx.fp & 0x7 != 0) return false;  // 8-byte aligned

    // Frame pointer should be at or above stack pointer (stack grows down)
    if ((usz)ctx.fp < (usz)ctx.sp) return false;

    return true;
}

/**
 * Check if a ContinuationContext appears valid.
 *
 * @param ctx Context to validate
 * @return true if context appears valid
 */
fn bool is_valid_continuation_context(ContinuationContext* ctx) {
    if (!ctx.valid) return false;
    if (!is_valid_register_context(&ctx.registers)) return false;

    // If we have a stack segment, validate it
    if (ctx.stack.size > 0) {
        if (ctx.stack.data == null) return false;
        if (ctx.stack.base_sp == null) return false;
        if (ctx.stack.top_sp == null) return false;

        // Verify size matches pointers
        usz computed_size = stack_segment_size(ctx.stack.base_sp, ctx.stack.top_sp);
        if (computed_size != ctx.stack.size) return false;
    }

    return true;
}

/**
 * Print debug information about a RegisterContext.
 *
 * @param ctx Context to print
 */
fn void debug_print_register_context(RegisterContext* ctx) {
    io::printfn("RegisterContext:");
    io::printfn("  SP:    %p", ctx.sp);
    io::printfn("  IP:    %p", ctx.ip);
    io::printfn("  FP:    %p", ctx.fp);
    io::printfn("  Flags: 0x%08x", ctx.flags);
    io::printfn("  RBX:   0x%016x", ctx.gpr[0]);
    io::printfn("  RBP:   0x%016x", ctx.gpr[1]);
    io::printfn("  R12:   0x%016x", ctx.gpr[2]);
    io::printfn("  R13:   0x%016x", ctx.gpr[3]);
    io::printfn("  R14:   0x%016x", ctx.gpr[4]);
    io::printfn("  R15:   0x%016x", ctx.gpr[5]);
}

/**
 * Print debug information about a ContinuationContext.
 *
 * @param ctx Context to print
 */
fn void debug_print_continuation_context(ContinuationContext* ctx) {
    io::printfn("ContinuationContext (valid=%s):", ctx.valid ? "true" : "false");
    debug_print_register_context(&ctx.registers);
    io::printfn("  Stack segment:");
    io::printfn("    Data:    %p", ctx.stack.data);
    io::printfn("    Size:    %d bytes", (int)ctx.stack.size);
    io::printfn("    Base SP: %p", ctx.stack.base_sp);
    io::printfn("    Top SP:  %p", ctx.stack.top_sp);
}

// =============================================================================
// SECTION 7: TEST FUNCTIONS
// =============================================================================

/**
 * Test basic context capture and restore.
 *
 * Captures a context, then restores it with a different value.
 * Verifies that the restore value is returned correctly.
 */
fn void test_context_capture_restore() {
    io::printfn("Testing context capture/restore...");

    RegisterContext ctx;
    int capture_count = 0;

    int result = context_capture(&ctx);
    capture_count++;

    if (result == 0) {
        io::printfn("  Initial capture (result=0, count=%d)", capture_count);

        // Verify context looks valid
        if (is_valid_register_context(&ctx)) {
            io::printfn("  Context is valid, SP=%p, IP=%p", ctx.sp, ctx.ip);
        }

        // Restore with value 42
        io::printfn("  Calling context_restore with value=42...");
        context_restore(&ctx, 42);

        // Should not reach here
        io::printfn("  ERROR: context_restore returned!");
    } else {
        io::printfn("  Restored! (result=%d, count=%d)", result, capture_count);

        if (result == 42) {
            io::printfn("  SUCCESS: Got expected restore value");
        } else {
            io::printfn("  FAILURE: Expected 42, got %d", result);
        }
    }
}

/**
 * Test stack pointer intrinsics.
 */
fn void test_stack_intrinsics() {
    io::printfn("Testing stack intrinsics...");

    void* sp = get_stack_pointer();
    void* fp = get_frame_pointer();
    void* ra = get_return_address();

    io::printfn("  Stack pointer:   %p", sp);
    io::printfn("  Frame pointer:   %p", fp);
    io::printfn("  Return address:  %p", ra);

    // Basic sanity checks
    if (sp != null && ((usz)sp & 0x7) == 0) {
        io::printfn("  SP alignment: OK (8-byte aligned)");
    } else {
        io::printfn("  SP alignment: FAIL");
    }
}

/**
 * Test stack segment size calculation.
 */
fn void test_stack_segment_size_calc() {
    io::printfn("Testing stack segment size calculation...");

    // Simulate stack addresses (stack grows down)
    void* base = (void*)0x7fff0000;  // Higher address (older)
    void* top = (void*)0x7ffef000;   // Lower address (newer)

    usz size = stack_segment_size(base, top);
    usz expected = 0x1000;  // 4096 bytes

    if (size == expected) {
        io::printfn("  Size calculation: OK (%d bytes)", (int)size);
    } else {
        io::printfn("  Size calculation: FAIL (expected %d, got %d)", (int)expected, (int)size);
    }

    // Test inverted pointers (should return 0)
    usz inverted_size = stack_segment_size(top, base);
    if (inverted_size == 0) {
        io::printfn("  Inverted check: OK (returns 0)");
    } else {
        io::printfn("  Inverted check: FAIL (expected 0, got %d)", (int)inverted_size);
    }
}

/**
 * Run all context tests.
 */
fn void run_context_tests() {
    io::printfn("\n========================================");
    io::printfn("CONTEXT PRIMITIVE TESTS");
    io::printfn("========================================\n");

    io::printfn("Platform: x86_64 Linux");

    io::printfn("");

    test_stack_intrinsics();
    io::printfn("");

    test_stack_segment_size_calc();
    io::printfn("");

    test_context_capture_restore();
    io::printfn("");

    io::printfn("Context tests complete.");
    io::printfn("========================================\n");
}

