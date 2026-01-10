/*
 * Test: Deferred Decrement OOM Memory Leak
 *
 * Expected invariant:
 *   When defer_decrement() is called, it should ensure the decrement
 *   is eventually applied to the target object, even if malloc fails.
 *
 * Observed behavior:
 *   In defer_decrement() at deferred.c:49-62, when malloc fails for
 *   the DeferredDec structure, the function:
 *   1. Increments dropped_decrements counter
 *   2. Returns without applying the decrement
 *
 *   This causes the decrement to be completely lost, leading to:
 *   - Memory leak (object refcount never decremented, never freed)
 *   - Silent failure (caller has no way to know the decrement was dropped)
 *
 * Why this violates architecture/invariants:
 *   1. Reference counting invariant violated: every dec_ref() must eventually
 *      result in the refcount being decremented.
 *   2. Memory safety violated: dropped decrements accumulate, causing leaks.
 *   3. API contract broken: defer_decrement() provides no way to signal failure.
 *
 * Root cause:
 *   The code at deferred.c:49-53 does:
 *     d = malloc(sizeof(DeferredDec));
 *     if (!d) {
 *         ctx->dropped_decrements++;
 *         return;  // BUG: Silent failure, decrement lost
 *     }
 *
 *   The correct behavior should be:
 *   a) Return an error code so caller can handle it
 *   b) Apply the decrement immediately as fallback
 *   c) Use a pre-allocated freelist to avoid malloc in the common case
 */

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <stdint.h>

/* Mock hashmap for testing */
typedef struct HashMap HashMap;
typedef struct HashEntry {
    void* key;
    void* value;
    struct HashEntry* next;
} HashEntry;

struct HashMap {
    HashEntry** buckets;
    size_t bucket_count;
    size_t entry_count;
};

HashMap* hashmap_new(void) {
    HashMap* map = calloc(1, sizeof(HashMap));
    if (map) {
        map->bucket_count = 16;
        map->buckets = calloc(16, sizeof(HashEntry*));
        if (!map->buckets) {
            free(map);
            return NULL;
        }
    }
    return map;
}

void hashmap_free(HashMap* map) {
    if (!map) return;
    for (size_t i = 0; i < map->bucket_count; i++) {
        HashEntry* entry = map->buckets[i];
        while (entry) {
            HashEntry* next = entry->next;
            free(entry);
            entry = next;
        }
    }
    free(map->buckets);
    free(map);
}

void* hashmap_get(HashMap* map, void* key) {
    if (!map) return NULL;
    size_t idx = (size_t)key % map->bucket_count;
    HashEntry* entry = map->buckets[idx];
    while (entry) {
        if (entry->key == key) return entry->value;
        entry = entry->next;
    }
    return NULL;
}

void hashmap_put(HashMap* map, void* key, void* value) {
    if (!map) return;
    size_t idx = (size_t)key % map->bucket_count;
    HashEntry* entry = map->buckets[idx];

    /* Check if key exists */
    while (entry) {
        if (entry->key == key) {
            entry->value = value;
            return;
        }
        entry = entry->next;
    }

    /* Create new entry */
    entry = malloc(sizeof(HashEntry));
    if (!entry) return;  /* Can fail */
    entry->key = key;
    entry->value = value;
    entry->next = map->buckets[idx];
    map->buckets[idx] = entry;
    map->entry_count++;
}

void hashmap_remove(HashMap* map, void* key) {
    if (!map) return;
    size_t idx = (size_t)key % map->bucket_count;
    HashEntry** entry = &map->buckets[idx];
    while (*entry) {
        if ((*entry)->key == key) {
            HashEntry* to_remove = *entry;
            *entry = to_remove->next;
            free(to_remove);
            map->entry_count--;
            return;
        }
        entry = &(*entry)->next;
    }
}

/* Deferred structures from deferred.h */
typedef struct DeferredDec {
    void* obj;
    int count;
    struct DeferredDec* next;
} DeferredDec;

typedef struct DeferredContext {
    DeferredDec* pending;
    HashMap* obj_lookup;
    int pending_count;
    int batch_size;
    int total_deferred;
    int dropped_decrements;
} DeferredContext;

/* Custom malloc wrapper for testing */
static void* test_malloc(size_t size, int* fail_flag) {
    if (fail_flag && *fail_flag) {
        return NULL;
    }
    return malloc(size);
}

/* Buggy version from deferred.c */
void defer_decrement_buggy(DeferredContext* ctx, void* obj, int* fail_malloc) {
    if (!ctx || !obj) return;

    /* O(1) lookup using hash map */
    DeferredDec* d = (DeferredDec*)hashmap_get(ctx->obj_lookup, obj);
    if (d) {
        d->count++;
        return;
    }

    /* Add new entry */
    d = test_malloc(sizeof(DeferredDec), fail_malloc);
    if (!d) {
        ctx->dropped_decrements++;
        return;  /* BUG: Silent failure, decrement completely lost */
    }
    d->obj = obj;
    d->count = 1;
    d->next = ctx->pending;
    ctx->pending = d;
    ctx->pending_count++;
    ctx->total_deferred++;

    /* Add to hash map for O(1) lookup */
    hashmap_put(ctx->obj_lookup, obj, d);
}

/* Test object with refcount */
typedef struct TestObj {
    void* id;
    int refcount;
    int freed;
} TestObj;

void process_deferred(DeferredContext* ctx, int max_count) {
    if (!ctx || !ctx->pending) return;

    int processed = 0;
    DeferredDec** prev = &ctx->pending;

    while (*prev && processed < max_count) {
        DeferredDec* d = *prev;

        /* Process one decrement for this object */
        d->count--;
        processed++;

        if (d->count <= 0) {
            /* Remove from hash map */
            hashmap_remove(ctx->obj_lookup, d->obj);
            /* Remove from list */
            *prev = d->next;
            ctx->pending_count--;
            free(d);
        } else {
            prev = &d->next;
        }
    }
}

int main(void) {
    printf("=== Test: Deferred Decrement OOM Memory Leak ===\n\n");

    int test_failed = 0;

    /* Test 1: malloc failure causes silent decrement loss */
    printf("Test 1: malloc failure causes silent decrement loss\n");
    printf("-----------------------------------------------------\n");

    {
        DeferredContext* ctx = calloc(1, sizeof(DeferredContext));
        ctx->obj_lookup = hashmap_new();
        ctx->batch_size = 32;
        ctx->dropped_decrements = 0;

        TestObj obj = {.id = (void*)0xDEADBEEF, .refcount = 5, .freed = 0};

        /* Force malloc to fail */
        int fail_malloc = 1;

        printf("Calling defer_decrement() with forced malloc failure...\n");
        defer_decrement_buggy(ctx, &obj, &fail_malloc);

        printf("  dropped_decrements: %d\n", ctx->dropped_decrements);
        printf("  pending_count: %d\n", ctx->pending_count);
        printf("  obj refcount: %d (unchanged, should be 4)\n", obj.refcount);

        if (ctx->dropped_decrements > 0) {
            printf("\n*** BUG CONFIRMED ***\n");
            printf("The decrement was silently dropped!\n");
            printf("Expected behavior:\n");
            printf("  - Either return error so caller can handle\n");
            printf("  - Or apply decrement immediately as fallback\n");
            printf("  - Or use pre-allocated freelist to avoid malloc\n");
            printf("\nActual behavior:\n");
            printf("  - Incremented dropped_decrements counter\n");
            printf("  - Returned without doing anything else\n");
            printf("  - Object refcount never decremented -> MEMORY LEAK\n");
            test_failed = 1;
        }

        /* Cleanup */
        hashmap_free(ctx->obj_lookup);
        while (ctx->pending) {
            DeferredDec* next = ctx->pending->next;
            free(ctx->pending);
            ctx->pending = next;
        }
        free(ctx);
    }

    printf("\n");

    /* Test 2: Multiple dropped decrements compound the problem */
    printf("Test 2: Multiple dropped decrements compound the problem\n");
    printf("--------------------------------------------------------\n");

    {
        DeferredContext* ctx = calloc(1, sizeof(DeferredContext));
        ctx->obj_lookup = hashmap_new();
        ctx->batch_size = 32;
        ctx->dropped_decrements = 0;

        TestObj obj = {.id = (void*)0xBADCAFE, .refcount = 10, .freed = 0};

        int fail_malloc = 1;

        printf("Calling defer_decrement() 5 times with forced malloc failure...\n");
        for (int i = 0; i < 5; i++) {
            defer_decrement_buggy(ctx, &obj, &fail_malloc);
        }

        printf("  dropped_decrements: %d (5 decrements lost)\n", ctx->dropped_decrements);
        printf("  obj refcount: %d (should be 5, is still %d)\n", 10 - 5, obj.refcount);

        if (obj.refcount == 10 && ctx->dropped_decrements == 5) {
            printf("\n*** BUG CONFIRMED ***\n");
            printf("All 5 decrements were lost!\n");
            printf("The object will never be freed because:\n");
            printf("  - Refcount stays at 10 instead of 5\n");
            printf("  - Each lost decrement represents a permanent leak\n");
            printf("  - No way to recover or apply lost decrements later\n");
            test_failed = 1;
        }

        /* Cleanup */
        hashmap_free(ctx->obj_lookup);
        while (ctx->pending) {
            DeferredDec* next = ctx->pending->next;
            free(ctx->pending);
            ctx->pending = next;
        }
        free(ctx);
    }

    printf("\n");

    /* Test 3: Demonstrate correct fallback behavior */
    printf("Test 3: Correct fallback behavior (for comparison)\n");
    printf("--------------------------------------------------\n");

    printf("A correct implementation would:\n");
    printf("  1. Apply the decrement immediately on malloc failure\n");
    printf("  2. OR return an error code (change return type to int)\n");
    printf("  3. OR use a pre-allocated freelist (malloc upfront)\n");
    printf("\n");
    printf("Current implementation at deferred.c:49-53:\n");
    printf("  d = malloc(sizeof(DeferredDec));\n");
    printf("  if (!d) {\n");
    printf("      ctx->dropped_decrements++;\n");
    printf("      return;  // BUG: Lost decrement!\n");
    printf("  }\n");
    printf("\n");
    printf("Recommended fix:\n");
    printf("  d = malloc(sizeof(DeferredDec));\n");
    printf("  if (!d) {\n");
    printf("      ctx->dropped_decrements++;\n");
    printf("      /* Fallback: apply decrement immediately */\n");
    printf("      /* This requires passing obj's refcount pointer */\n");
    printf("      /* Or having a callback mechanism */\n");
    printf("      return -1;  /* Signal error to caller */\n");
    printf("  }\n");

    printf("\n=== Summary ===\n");
    if (test_failed) {
        printf("FAILED: Memory leak bug confirmed in defer_decrement()\n");
        printf("Location: runtime/src/memory/deferred.c:49-53\n");
        printf("Issue: malloc failure silently drops decrements, causing leaks\n");
        printf("\nFix options:\n");
        printf("  1. Return error code so caller can handle immediately\n");
        printf("  2. Apply decrement immediately as fallback\n");
        printf("  3. Use freelist to avoid malloc in common path\n");
        return 1;
    } else {
        printf("PASSED: No bug detected (test may need refinement)\n");
        return 0;
    }
}
