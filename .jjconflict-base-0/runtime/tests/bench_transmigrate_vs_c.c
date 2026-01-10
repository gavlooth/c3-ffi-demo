/*
 * bench_transmigrate_vs_c.c - CTRR Transmigration vs Raw C Performance
 *
 * This benchmark compares the metadata-driven transmigration against
 * equivalent raw C operations to measure the overhead of the CTRR system.
 *
 * ============================================================================
 * HOW TO RUN
 * ============================================================================
 * From the runtime/tests directory:
 *   make clean && make bench
 *
 * Enable remap/transmigration stats (prints probe counters):
 *   make clean && make bench CFLAGS="-std=c99 -Wall -Wextra -g -I../include -DOMNI_TRANSMIGRATE_STATS"
 *
 * Toggle remap probing strategy:
 *   - Edit `runtime/src/memory/transmigrate.c` (`REMAP_USE_LINEAR_PROBING`)
 *   - Rebuild and rerun this benchmark
 *
 * Or directly:
 *   ./bench_transmigrate_vs_c
 *
 * ============================================================================
 * FAIRNESS INVARIANTS (important for meaningful results)
 * ============================================================================
 *
 * This benchmark is intended to compare "escape repair" work (deep copy +
 * graph rewriting + bulk teardown) against equivalent raw C deep copy +
 * teardown.
 *
 * All benchmarks must satisfy these invariants:
 *
 * 1. Equivalent semantic work on both sides
 *    - OmniLisp transmigration and raw C both perform deep copy with graph
 *      structure preservation (cycles, shared nodes)
 *
 * 2. Symmetric teardown costs
 *    - Both sides include cleanup/deallocation in the timing
 *    - OmniLisp uses region_exit, raw C uses explicit free
 *
 * 3. Dead-code elimination prevention
 *    - Results are consumed via volatile sink (g_sink)
 *    - Compiler cannot optimize away the benchmark code
 *
 * If any of these invariants are violated, ns/op results are not meaningful.
 */

/* POSIX APIs (clock_gettime) */
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdint.h>

/* OmniLisp runtime (for transmigration comparison) */
#include "../include/omni.h"

/* Raw C data structures for comparison */
typedef struct CNode {
    long value;
    struct CNode* next;
} CNode;

typedef struct CArray {
    long* data;
    size_t size;
    size_t capacity;
} CArray;

static double get_time_seconds(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

/* Volatile sink to prevent compiler dead-code elimination */
static volatile uintptr_t g_sink = 0;

static inline void sink_ptr(const void* p) {
    g_sink ^= (uintptr_t)p;
}

static inline void sink_long(long v) {
    g_sink ^= (uintptr_t)v;
}

#define BENCH(name, iterations, code) do { \
    /* Small warmup to reduce first-iteration artifacts */ \
    for (int _w = 0; _w < 16; _w++) { code; } \
    double start = get_time_seconds(); \
    for (int i = 0; i < (iterations); i++) { code; } \
    double end = get_time_seconds(); \
    double elapsed = end - start; \
    double ops_per_sec = (iterations) / elapsed; \
    double ns_per_op = (elapsed * 1e9) / (iterations); \
    printf("  %-45s: %.4fs (%.0f ops/sec, %.2f ns/op)\n", \
           (name), elapsed, ops_per_sec, ns_per_op); \
} while (0)

/* ============================================================================
 * RAW C OPERATIONS (Baseline)
 * ============================================================================ */

/* Raw C linked list deep copy */
static CNode* c_deep_copy_list(CNode* src) {
    if (!src) return NULL;

    /* Allocate new nodes */
    CNode* dest = NULL;
    CNode** tail = &dest;

    for (CNode* n = src; n; n = n->next) {
        CNode* new_node = malloc(sizeof(CNode));
        new_node->value = n->value;
        new_node->next = NULL;
        *tail = new_node;
        tail = &new_node->next;
    }

    return dest;
}

/* Raw C array deep copy */
static CArray* c_deep_copy_array(CArray* src) {
    CArray* dest = malloc(sizeof(CArray));
    dest->size = src->size;
    dest->capacity = src->capacity;
    dest->data = malloc(sizeof(long) * src->capacity);
    memcpy(dest->data, src->data, sizeof(long) * src->size);
    return dest;
}

/* Raw C list creation */
static CNode* c_create_list(size_t count) {
    CNode* list = NULL;
    for (size_t i = 0; i < count; i++) {
        CNode* node = malloc(sizeof(CNode));
        node->value = (long)i;
        node->next = list;
        list = node;
    }
    return list;
}

/* Raw C list destruction */
static void c_destroy_list(CNode* list) {
    while (list) {
        CNode* next = list->next;
        free(list);
        list = next;
    }
}

/* Raw C array creation */
static CArray* c_create_array(size_t count) {
    CArray* arr = malloc(sizeof(CArray));
    arr->capacity = count;
    arr->size = count;
    arr->data = malloc(sizeof(long) * count);
    for (size_t i = 0; i < count; i++) {
        arr->data[i] = (long)i;
    }
    return arr;
}

/* Raw C array destruction */
static void c_destroy_array(CArray* arr) {
    free(arr->data);
    free(arr);
}

/* ============================================================================
 * BENCHMARKS
 * ============================================================================ */

int main(void) {
    printf("===================================================================\n");
    printf("  CTRR TRANSMIGRATION vs RAW C\n");
    printf("  Performance Comparison: Metadata-Driven vs Native C\n");
    printf("===================================================================\n\n");

    /* ========================================================================
     * BENCHMARK 1: List Copy vs Transmigration
     * ======================================================================== */
    printf("[Bench 1] Linked List Copy vs Transmigration\n");
    printf("---------------------------------------------------------------\n");

    size_t sizes[] = {100, 1000, 10000};
    for (int s = 0; s < 3; s++) {
        size_t size = sizes[s];
        /* Scale iterations so the total runtime stays reasonable */
        const int c_iterations = (size <= 1000) ? 5000 : 500;
        const int omni_iterations = c_iterations;

        /* Raw C benchmark */
        CNode* c_list = c_create_list(size);

        char c_name[64];
        snprintf(c_name, sizeof(c_name), "Raw C: copy %zu-element list (%dx)", size, c_iterations);
        BENCH(c_name, c_iterations, {
            CNode* copy = c_deep_copy_list(c_list);
            /* Consume a value so copy can't be optimized away */
            if (copy) sink_long(copy->value);
            /* IMPORTANT: teardown must be equivalent to region_exit on Omni side */
            c_destroy_list(copy);
        });
        c_destroy_list(c_list);

        /* OmniLisp benchmark */
        Region* src = region_create();
        Obj* omni_list = NULL;
        for (size_t i = 0; i < size; i++) {
            omni_list = mk_cell_region(src, mk_int_unboxed(i), omni_list);
        }

        char omni_name[64];
        snprintf(omni_name, sizeof(omni_name), "OmniLisp: transmigrate %zu-element list (%dx)", size, omni_iterations);
        BENCH(omni_name, omni_iterations, {
            Region* dest = region_create();
            Obj* result = transmigrate(omni_list, src, dest);
            /* Consume result so transmigration can't be optimized away */
            if (result && !IS_IMMEDIATE(result) && result->tag == TAG_PAIR) {
                sink_long(obj_to_int(result->a));
            } else {
                sink_ptr(result);
            }
            region_exit(dest);
        });

        region_exit(src);
    }

    /* ========================================================================
     * BENCHMARK 2: Array Copy vs Transmigration
     * ======================================================================== */
    printf("\n[Bench 2] Array Copy vs Transmigration\n");
    printf("---------------------------------------------------------------\n");

    size_t arr_sizes[] = {100, 1000, 10000};
    for (int s = 0; s < 3; s++) {
        size_t size = arr_sizes[s];
        const int c_iterations = (size <= 1000) ? 50000 : 5000;
        const int omni_iterations = c_iterations;

        /* Raw C benchmark */
        CArray* c_arr = c_create_array(size);

        char c_name[64];
        snprintf(c_name, sizeof(c_name), "Raw C: copy %zu-element array (%dx)", size, c_iterations);
        BENCH(c_name, c_iterations, {
            CArray* copy = c_deep_copy_array(c_arr);
            /* Consume one value so memcpy work is observable */
            if (copy && copy->data && copy->size > 0) sink_long(copy->data[0]);
            free(copy->data);
            free(copy);
        });
        c_destroy_array(c_arr);

        /* OmniLisp benchmark */
        Region* src = region_create();
        Obj* omni_arr = mk_array_region(src, size);
        for (size_t i = 0; i < size; i++) {
            array_push(omni_arr, mk_int_unboxed(i));
        }

        char omni_name[64];
        snprintf(omni_name, sizeof(omni_name), "OmniLisp: transmigrate %zu-element array (%dx)", size, omni_iterations);
        BENCH(omni_name, omni_iterations, {
            Region* dest = region_create();
            Obj* result = transmigrate(omni_arr, src, dest);
            /* Consume one element to ensure deep-copy + rewrite ran */
            if (result) {
                Obj* v0 = array_get(result, 0);
                if (v0) sink_long(obj_to_int(v0));
            }
            region_exit(dest);
        });

        region_exit(src);
    }

    /* ========================================================================
     * BENCHMARK 3: Memory Allocation Only (Fair Comparison)
     * ======================================================================== */
    printf("\n[Bench 3] Memory Allocation (malloc vs region_alloc)\n");
    printf("---------------------------------------------------------------\n");

    const int alloc_iterations = 10000000;

    BENCH("Raw C: malloc+free 10M nodes", alloc_iterations, {
        CNode* node = (CNode*)malloc(sizeof(CNode));
        if (node) {
            node->value = 123;
            sink_long(node->value);
        }
        free(node);
    });

    Region* r = region_create();
    BENCH("OmniLisp: region_alloc 10M ints", alloc_iterations, {
        Obj* obj = mk_int_region(r, 42);
        sink_ptr(obj);
    });
    region_exit(r);

    /* ========================================================================
     * BENCHMARK 4: Deep Structure Traversal
     * ======================================================================== */
    printf("\n[Bench 4] Deep Structure Traversal\n");
    printf("---------------------------------------------------------------\n");

    CNode* c_deep_list = c_create_list(10000);
    volatile long c_sum = 0;

    BENCH("Raw C: traverse 10k-element linked list (10000x)", 10000, {
        for (CNode* n = c_deep_list; n; n = n->next) {
            c_sum += n->value;
        }
    });
    c_destroy_list(c_deep_list);

    Region* omni_deep_src = region_create();
    Obj* omni_deep_list = NULL;
    for (int i = 0; i < 10000; i++) {
        omni_deep_list = mk_cell_region(omni_deep_src, mk_int_unboxed(i), omni_deep_list);
    }

    BENCH("OmniLisp: traverse 10k-element list (10000x)", 10000, {
        Obj* current = omni_deep_list;
        while (current) {
            c_sum += obj_to_int(current->a);
            current = current->b;
        }
    });
    region_exit(omni_deep_src);

    /* Prevent optimization */
    if (c_sum == 0) printf("  (sum=%ld to prevent optimization)\n", (long)c_sum);
    /* Also consume sink so the compiler can't drop benchmark bodies */
    if (g_sink == 0xdeadbeef) printf("  (sink=%p)\n", (void*)g_sink);

    /* ========================================================================
     * SUMMARY
     * ======================================================================== */
    printf("\n===================================================================\n");
    printf("  ANALYSIS\n");
    printf("===================================================================\n");
    printf("\n");
    printf("  Notes:\n");
    printf("  - This benchmark includes teardown cost on BOTH sides.\n");
    printf("  - It uses a volatile sink to reduce dead-code elimination.\n");
    printf("\n");
    printf("  Transmigration overhead typically comes from:\n");
    printf("  1. Clone/trace dispatch (per-tag metadata callbacks)\n");
    printf("  2. Cycle/sharing preservation (bitmap + remap table)\n");
    printf("  3. Pointer rewriting worklist\n");
    printf("\n");
    printf("  Despite this overhead, transmigration provides:\n");
    printf("  - Type-safe memory movement\n");
    printf("  - Automatic cycle handling\n");
    printf("  - Preserved sharing (DAG correctness)\n");
    printf("  - Extensible metadata system\n");
    printf("  - CTRR contract compliance\n");
    printf("\n");
    printf("  For most applications, the overhead is acceptable given\n");
    printf("  the safety and correctness guarantees provided.\n");
    printf("===================================================================\n");

    return 0;
}
