#ifndef OMNI_HASHMAP_H
#define OMNI_HASHMAP_H

#include <stddef.h>
#include <stdint.h>

// Simple pointer-keyed hash map for O(1) lookups
// Used for fast object->node mappings in SCC and deferred RC

typedef struct HashEntry {
    void* key;
    void* value;
    struct HashEntry* next;  // For collision chaining
} HashEntry;

struct Region;

typedef struct HashMap {
    HashEntry** buckets;
    size_t bucket_count;
    size_t entry_count;
    float load_factor;
    int had_alloc_failure;
} HashMap;

// Create/destroy
HashMap* hashmap_new(void);
HashMap* hashmap_with_capacity(size_t capacity);
void hashmap_free(HashMap* map);
void hashmap_free_entries(HashMap* map);  // Free entries but not values

// Operations (pointer keys)
/*
 * hashmap_get - Look up a value by key
 *
 * OPTIMIZATION (T-opt-inline-hash-fastpath): Inline hot path for common case.
 * The hash computation and bucket lookup are now inlineable.
 */
static inline void* hashmap_get(HashMap* map, void* key) {
    if (!map || !key) return NULL;

    // Hash function: pointer cast to size_t (FNV-1a style)
    size_t hash = (size_t)key;
    hash ^= hash >> 7;
    hash *= 0x100000001b3;
    hash ^= hash >> 11;

    size_t bucket = hash % map->bucket_count;

    // Linear search in bucket
    HashEntry* entry = map->buckets[bucket];
    while (entry) {
        if (entry->key == key) {
            return entry->value;
        }
        entry = entry->next;
    }

    return NULL;
}

void hashmap_put(HashMap* map, void* key, void* value);
void hashmap_put_region(HashMap* map, void* key, void* value, void* r);
void* hashmap_remove(HashMap* map, void* key);

/*
 * hashmap_contains - Check if key exists in map
 *
 * OPTIMIZATION (T-opt-inline-hash-fastpath): Inline for hot path.
 */
static inline int hashmap_contains(HashMap* map, void* key) {
    return hashmap_get(map, key) != NULL;
}

// Iteration
typedef void (*HashMapIterFn)(void* key, void* value, void* ctx);
void hashmap_foreach(HashMap* map, HashMapIterFn fn, void* ctx);

// Utility
size_t hashmap_size(HashMap* map);
void hashmap_clear(HashMap* map);
int hashmap_had_alloc_failure(HashMap* map);

#endif // OMNI_HASHMAP_H
