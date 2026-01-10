/*
 * Test for BUG-0006: SCC Node Hashmap Put Failure Leak
 *
 * Expected invariant: Every SCCNode in node_map should also be in node_lookup
 *                   (hashmap) so that get_node() can find it.
 *
 * Observed behavior: When hashmap_put() fails (malloc failure for HashEntry),
 *                   get_or_create_node() still returns the node, but it's NOT
 *                   in the hashmap. This creates an orphaned node.
 *
 * Why it violates architecture:
 *
 * 1. In scc.c:106-125, get_or_create_node() does:
 *    - Line 111: malloc SCCNode
 *    - Lines 117-119: Add node to node_map linked list
 *    - Line 122: hashmap_put() - CAN FAIL if malloc fails for HashEntry
 *    - Line 124: Return n anyway
 *
 * 2. If hashmap_put() fails:
 *    - Node is in node_map linked list (will be freed by reset_tarjan_state)
 *    - Node is NOT in node_lookup hashmap (get_node() won't find it)
 *    - Node is returned to caller as if it were properly registered
 *
 * 3. This creates two problems:
 *    a) The node is "orphaned" - exists in linked list but not findable via hashmap
 *    b) If get_or_create_node() is called again for same obj, it creates a
 *       SECOND node for the same object (duplicate node bug)
 *
 * The fix should:
 * a) Check if hashmap_put() succeeded (check had_alloc_failure flag)
 * b) If it failed, remove node from node_map and free it, then return NULL
 * c) Or change hashmap_put() to return a status code
 */

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <setjmp.h>
#include <stdarg.h>

/* Mock hashmap that simulates allocation failure */
typedef struct HashEntry {
    void* key;
    void* value;
    struct HashEntry* next;
} HashEntry;

typedef struct HashMap {
    HashEntry** buckets;
    size_t bucket_count;
    size_t entry_count;
    float load_factor;
    int had_alloc_failure;
    int fail_next_put;  /* Testing: force next put to fail */
} HashMap;

/* Minimal SCC structures */
typedef struct SCCNode {
    int id;
    int lowlink;
    int on_stack;
    void* obj;
    struct SCCNode* next;
    struct SCCNode* stack_next;
} SCCNode;

typedef struct SCCRegistry {
    SCCNode* node_map;
    HashMap* node_lookup;
    SCCNode* stack;
    int index;
} SCCRegistry;

/* Test helper to create a mock object */
static void* mock_obj = (void*)0xDEADBEEF;

/* Mock hashmap functions */
HashMap* hashmap_new(void) {
    HashMap* map = calloc(1, sizeof(HashMap));
    if (map) {
        map->bucket_count = 16;
        map->buckets = calloc(16, sizeof(HashEntry*));
        if (!map->buckets) {
            free(map);
            return NULL;
        }
        map->entry_count = 0;
        map->load_factor = 0.75f;
        map->had_alloc_failure = 0;
        map->fail_next_put = 0;
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

    /* Simulate allocation failure for testing */
    if (map->fail_next_put) {
        map->had_alloc_failure = 1;
        map->fail_next_put = 0;  /* Reset for next call */
        return;  /* Don't insert, simulating malloc failure */
    }

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

    /* Create new entry - can fail if fail_next_put is set */
    entry = malloc(sizeof(HashEntry));
    if (!entry) {
        map->had_alloc_failure = 1;
        return;
    }

    entry->key = key;
    entry->value = value;
    entry->next = map->buckets[idx];
    map->buckets[idx] = entry;
    map->entry_count++;
}

/* The buggy function from scc.c */
SCCNode* get_or_create_node(SCCRegistry* reg, void* obj) {
    if (!reg || !obj) return NULL;

    /* Check if node already exists */
    SCCNode* existing = hashmap_get(reg->node_lookup, obj);
    if (existing) return existing;

    /* Create new node */
    SCCNode* n = malloc(sizeof(SCCNode));
    if (!n) return NULL;
    n->id = -1;
    n->lowlink = -1;
    n->on_stack = 0;
    n->obj = obj;
    n->next = reg->node_map;
    n->stack_next = NULL;
    reg->node_map = n;  /* Add to linked list FIRST */

    /* Add to hash map - CAN FAIL */
    hashmap_put(reg->node_lookup, obj, n);

    /* BUG: Returns node even if hashmap_put() failed! */
    return n;
}

/* Cleanup function to free node_map */
void cleanup_node_map(SCCRegistry* reg) {
    SCCNode* node = reg->node_map;
    while (node) {
        SCCNode* next = node->next;
        free(node);
        node = next;
    }
    reg->node_map = NULL;
}

int test_scc_hashmap_put_failure_leak(void) {
    printf("Testing SCC node hashmap put failure leak...\n\n");

    /* Test 1: Node is returned but not in hashmap after put failure */
    printf("=== Test 1: Node orphaned after hashmap_put() failure ===\n");

    SCCRegistry reg = {0};
    reg.node_lookup = hashmap_new();
    if (!reg.node_lookup) {
        printf("FAIL: Could not create hashmap\n");
        return -1;
    }

    /* Force next hashmap_put to fail */
    reg.node_lookup->fail_next_put = 1;

    printf("Creating node with forced hashmap_put() failure...\n");
    SCCNode* node1 = get_or_create_node(&reg, mock_obj);

    if (!node1) {
        printf("Result: get_or_create_node() returned NULL (allocation failed at node level)\n");
        printf("This is OK - if node malloc fails, we expect NULL\n");
        hashmap_free(reg.node_lookup);
        cleanup_node_map(&reg);
        return 0;
    }

    printf("Node created at address: %p\n", (void*)node1);
    printf("Node in node_map linked list: %s\n",
           (node1 == reg.node_map) ? "YES" : "NO");

    /* Check if node is in hashmap */
    void* found = hashmap_get(reg.node_lookup, mock_obj);
    printf("Node found in hashmap: %s\n", (found == node1) ? "YES" : "NO");

    if (found != node1) {
        printf("\n*** BUG CONFIRMED ***\n");
        printf("Node exists in node_map but NOT in node_lookup hashmap!\n");
        printf("This is an ORPHANED node - it exists but can't be found by get_node()\n");

        /* Demonstrate the duplicate node problem */
        printf("\nDemonstrating duplicate node creation:\n");
        printf("Calling get_or_create_node() again for same object...\n");
        SCCNode* node2 = get_or_create_node(&reg, mock_obj);
        printf("Second node created at address: %p\n", (void*)node2);

        if (node2 && node2 != node1) {
            printf("*** DUPLICATE NODE BUG CONFIRMED ***\n");
            printf("Now there are TWO nodes for the same object!\n");
            printf("node1 = %p (orphaned, in linked list only)\n", (void*)node1);
            printf("node2 = %p (properly registered)\n", (void*)node2);

            /* Count nodes in linked list */
            int count = 0;
            SCCNode* n = reg.node_map;
            while (n) {
                count++;
                n = n->next;
            }
            printf("Total nodes in linked list: %d (should be 1)\n", count);

            /* Check how many are in hashmap */
            size_t hashmap_count = 0;
            for (size_t i = 0; i < reg.node_lookup->bucket_count; i++) {
                HashEntry* entry = reg.node_lookup->buckets[i];
                while (entry) {
                    hashmap_count++;
                    entry = entry->next;
                }
            }
            printf("Total nodes in hashmap: %zu (should be 1)\n", hashmap_count);

            if (count == 2 && hashmap_count == 1) {
                printf("\n*** INVARIANT VIOLATION ***\n");
                printf("node_map has 2 nodes but node_lookup has only 1 entry!\n");
                printf("This breaks the invariant that all nodes should be findable.\n");
            }
        }

        cleanup_node_map(&reg);
        hashmap_free(reg.node_lookup);
        return 1;  /* Bug confirmed */
    } else {
        printf("\nResult: Node IS in hashmap - no bug detected in this configuration\n");
        printf("(This might happen if mock setup is incorrect)\n");
    }

    cleanup_node_map(&reg);
    hashmap_free(reg.node_lookup);
    return 0;
}

int test_scc_hashmap_alloc_failure_flag(void) {
    printf("\n=== Test 2: Checking had_alloc_failure flag ===\n");

    SCCRegistry reg = {0};
    reg.node_lookup = hashmap_new();
    if (!reg.node_lookup) {
        printf("FAIL: Could not create hashmap\n");
        return -1;
    }

    /* Force next put to fail */
    reg.node_lookup->fail_next_put = 1;

    printf("Creating node with forced put failure...\n");
    SCCNode* node = get_or_create_node(&reg, mock_obj);

    printf("had_alloc_failure flag: %d\n", reg.node_lookup->had_alloc_failure);

    if (reg.node_lookup->had_alloc_failure) {
        printf("\n*** BUG CONFIRMED ***\n");
        printf("hashmap_put() failed (had_alloc_failure=1) but node was still returned!\n");
        printf("The caller has no way to know the node is not properly registered.\n");

        if (node) {
            printf("Node address: %p\n", (void*)node);
            printf("The node is usable but will cause issues later:\n");
            printf("  - get_node() won't find it\n");
            printf("  - get_or_create_node() will create a duplicate\n");
        }
    }

    cleanup_node_map(&reg);
    hashmap_free(reg.node_lookup);
    return reg.node_lookup->had_alloc_failure ? 1 : 0;
}

int main(void) {
    printf("========================================\n");
    printf("SCC Hashmap Put Failure Leak Test\n");
    printf("========================================\n\n");

    int result1 = test_scc_hashmap_put_failure_leak();
    int result2 = test_scc_hashmap_alloc_failure_flag();

    printf("\n========================================\n");
    printf("Test Summary:\n");
    printf("========================================\n");

    if (result1 > 0 || result2 > 0) {
        printf("BUG CONFIRMED: SCC node hashmap put failure causes memory leak\n");
        printf("and duplicate node creation.\n");
        printf("\nRecommended fix:\n");
        printf("  In get_or_create_node(), after calling hashmap_put():\n");
        printf("  1. Check hashmap_had_alloc_failure(reg->node_lookup)\n");
        printf("  2. If true, remove node from node_map and free it\n");
        printf("  3. Return NULL to indicate failure\n");
        return 1;
    } else if (result1 < 0 || result2 < 0) {
        printf("TEST ERROR: Test setup failed\n");
        return -1;
    } else {
        printf("No bug detected (test may need refinement)\n");
        return 0;
    }
}
