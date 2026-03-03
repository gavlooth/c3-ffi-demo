/**
 * Stack helpers for Omni Lisp stack engine.
 *
 * Provides:
 *   D1: FPU state save/restore (stmxcsr/ldmxcsr/fnstcw/fldcw)
 *   D2: Stack overflow detection via SIGSEGV + sigaltstack
 *
 * Called from C3 via extern declarations in stack_engine.c3.
 */

#define _GNU_SOURCE
#include <signal.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* ============================================================
 * AddressSanitizer integration (fiber stack switching)
 * ============================================================ */

#if defined(__has_feature)
#  if __has_feature(address_sanitizer)
#    define OMNI_WITH_ASAN 1
#  endif
#endif
#if defined(__SANITIZE_ADDRESS__)
#  define OMNI_WITH_ASAN 1
#endif
#if !defined(OMNI_WITH_ASAN)
#  define OMNI_WITH_ASAN 0
#endif

#if defined(__clang__)
#  define OMNI_NO_ASAN __attribute__((no_sanitize("address")))
#elif defined(__GNUC__)
#  define OMNI_NO_ASAN __attribute__((no_sanitize_address))
#else
#  define OMNI_NO_ASAN
#endif

extern void __asan_init(void) __attribute__((weak));
extern void __sanitizer_start_switch_fiber(void **fake_stack_save,
                                           const void *bottom, size_t size)
    __attribute__((weak));
extern void __sanitizer_finish_switch_fiber(void *fake_stack_save,
                                            const void **bottom_old, size_t *size_old)
    __attribute__((weak));

int stack_asan_enabled(void) {
    return __asan_init != NULL ? 1 : 0;
}

/* ============================================================
 * D1: FPU State Save/Restore
 * ============================================================
 * C3 inline asm doesn't support stmxcsr/ldmxcsr/fnstcw/fldcw.
 */

void fpu_save(uint32_t* mxcsr, uint32_t* x87cw) {
    uint32_t m;
    uint16_t c;
    __asm__ volatile("stmxcsr %0" : "=m" (m));
    __asm__ volatile("fnstcw %0" : "=m" (c));
    *mxcsr = m;
    *x87cw = (uint32_t)c;
}

void fpu_restore(uint32_t mxcsr, uint32_t x87cw) {
    uint16_t c = (uint16_t)x87cw;
    __asm__ volatile("ldmxcsr %0" : : "m" (mxcsr));
    __asm__ volatile("fldcw %0" : : "m" (c));
}

/* ASAN-aware stack switch hooks (no-op without ASAN). */
void stack_asan_start_switch(void** fake_stack_save, void* stack_bottom, size_t stack_size) {
    if (__sanitizer_start_switch_fiber != NULL) {
        __sanitizer_start_switch_fiber(fake_stack_save, stack_bottom, stack_size);
        return;
    }
    (void)fake_stack_save;
    (void)stack_bottom;
    (void)stack_size;
}

void stack_asan_finish_switch(void* fake_stack_save) {
    if (__sanitizer_finish_switch_fiber != NULL) {
        const void* old_bottom = NULL;
        size_t old_size = 0;
        __sanitizer_finish_switch_fiber(fake_stack_save, &old_bottom, &old_size);
        return;
    }
    (void)fake_stack_save;
}

/* Raw stack copy for continuation clone.
 * ASAN redzones exist inside suspended stacks; cloning must copy raw bytes.
 */
OMNI_NO_ASAN
void stack_raw_copy(void* dst, const void* src, size_t size) {
    unsigned char* d = (unsigned char*)dst;
    const unsigned char* s = (const unsigned char*)src;
    for (size_t i = 0; i < size; i++) {
        d[i] = s[i];
    }
}

/* ============================================================
 * D2: Stack Overflow Detection via SIGSEGV + sigaltstack
 * ============================================================
 *
 * When a coroutine overflows its stack and hits the guard page,
 * SIGSEGV fires on an alternate signal stack. The handler checks
 * if the fault address is in a known guard page. If so, it
 * siglongjmps to the recovery point set before the context switch.
 *
 * Recovery points form a stack to support nested coro switches.
 */

#define MAX_GUARD_PAGES 256
#define MAX_RECOVERY_DEPTH 64

struct guard_entry {
    void*  base;
    size_t size;
};

static struct guard_entry g_guards[MAX_GUARD_PAGES];
static int g_guard_count = 0;

static sigjmp_buf g_recovery_stack[MAX_RECOVERY_DEPTH];
static int g_recovery_depth = 0;
static volatile sig_atomic_t g_guard_hit = 0;

static void* g_sigstack = NULL;
static int g_initialized = 0;

/* The context switch function defined in stack_engine.c3 (@naked, SysV ABI) */
extern void omni_context_switch(void* old_ctx, void* new_ctx);

static void sigsegv_handler(int sig, siginfo_t* info, void* ucontext) {
    (void)sig; (void)ucontext;

    if (g_recovery_depth <= 0)
        goto reraise;

    {
        uintptr_t addr = (uintptr_t)info->si_addr;
        for (int i = 0; i < g_guard_count; i++) {
            uintptr_t lo = (uintptr_t)g_guards[i].base;
            uintptr_t hi = lo + g_guards[i].size;
            if (addr >= lo && addr < hi) {
                g_guard_hit = 1;
                int depth = g_recovery_depth - 1;
                g_recovery_depth = depth;
                siglongjmp(g_recovery_stack[depth], 1);
                /* NOTREACHED */
            }
        }
    }

reraise:
    /* Not a known guard page — restore default handler and re-raise */
    {
        struct sigaction sa;
        memset(&sa, 0, sizeof(sa));
        sa.sa_handler = SIG_DFL;
        sigaction(SIGSEGV, &sa, NULL);
        raise(SIGSEGV);
    }
}

int stack_guard_init(void) {
    if (g_initialized) return 0;

    /* Allocate alternate signal stack */
    g_sigstack = malloc(SIGSTKSZ);
    if (!g_sigstack) return -1;

    stack_t ss;
    memset(&ss, 0, sizeof(ss));
    ss.ss_sp = g_sigstack;
    ss.ss_size = SIGSTKSZ;
    ss.ss_flags = 0;
    if (sigaltstack(&ss, NULL) < 0) {
        free(g_sigstack); g_sigstack = NULL;
        return -1;
    }

    /* Install SIGSEGV handler on alternate stack */
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = sigsegv_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
    if (sigaction(SIGSEGV, &sa, NULL) < 0) {
        free(g_sigstack); g_sigstack = NULL;
        return -1;
    }

    g_initialized = 1;
    return 0;
}

void stack_guard_shutdown(void) {
    if (!g_initialized) return;

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = SIG_DFL;
    sigaction(SIGSEGV, &sa, NULL);

    stack_t ss;
    memset(&ss, 0, sizeof(ss));
    ss.ss_flags = SS_DISABLE;
    sigaltstack(&ss, NULL);

    free(g_sigstack); g_sigstack = NULL;
    g_guard_count = 0;
    g_recovery_depth = 0;
    g_initialized = 0;
}

void stack_guard_register(void* base, size_t size) {
    if (g_guard_count < MAX_GUARD_PAGES) {
        g_guards[g_guard_count].base = base;
        g_guards[g_guard_count].size = size;
        g_guard_count++;
    }
}

void stack_guard_unregister(void* base) {
    for (int i = 0; i < g_guard_count; i++) {
        if (g_guards[i].base == base) {
            g_guards[i] = g_guards[g_guard_count - 1];
            g_guard_count--;
            return;
        }
    }
}

/**
 * Protected context switch with stack overflow recovery.
 *
 * Wraps sigsetjmp + context switch so the sigsetjmp frame is live
 * when siglongjmp fires from the SIGSEGV handler.
 *
 * Returns 0 on normal switch-back, 1 on stack overflow recovery.
 */
int stack_guard_protected_switch(void* old_ctx, void* new_ctx) {
    if (g_recovery_depth >= MAX_RECOVERY_DEPTH) {
        /* Too deep — switch without protection */
        omni_context_switch(old_ctx, new_ctx);
        return 0;
    }

    g_guard_hit = 0;
    if (sigsetjmp(g_recovery_stack[g_recovery_depth], 1) != 0) {
        /* Stack overflow — siglongjmp'd back here */
        return 1;
    }
    g_recovery_depth++;

    omni_context_switch(old_ctx, new_ctx);

    g_recovery_depth--;
    return 0;
}
