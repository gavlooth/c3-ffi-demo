#include "hashmap.h"
#include "wyhash.h"
#include "../memory/region_core.h"
#include <stdlib.h>
#include <string.h>

#define INITIAL_BUCKETS 64
#define MAX_LOAD_FACTOR 0.75f

/*
 * Hash function for pointer keys using wyhash64.
 *
 * CRITICAL: This MUST match the inline fast-path hash in
 * `runtime/src/util/hashmap.h` (`hashmap_get`), otherwise lookups
 * will fail (put uses one hash, get uses another).
 *
 * Uses wyhash64 - a high-quality 64-bit mixer that passes BigCrush/PractRand.
 * Much better avalanche than the previous FNV-like sequence.
 * See: https://github.com/wangyi-fudan/wyhash (public domain)
 */
static size_t hash_ptr(void* ptr)
{
    return (size_t)wyhash64((uint64_t)(uintptr_t)ptr, 0x9E3779B97F4A7C15ULL);
}

// Create with default capacity
HashMap* hashmap_new(void) {
    return hashmap_with_capacity(INITIAL_BUCKETS);
}

// Create with specified capacity
HashMap* hashmap_with_capacity(size_t capacity) {
    HashMap* map = malloc(sizeof(HashMap));
    if (!map) return NULL;

    if (capacity < 16) capacity = 16;

    map->buckets = calloc(capacity, sizeof(HashEntry*));
    if (!map->buckets) {
        free(map);
        return NULL;
    }

    map->bucket_count = capacity;
    map->entry_count = 0;
    map->load_factor = MAX_LOAD_FACTOR;
    map->had_alloc_failure = 0;
    return map;
}

// Free hash map
void hashmap_free(HashMap* map) {
    if (!map) return;
    hashmap_free_entries(map);
    free(map->buckets);
    free(map);
}

// Free all entries (logical clear only)
// Note: Does NOT free individual entries - in region-based memory management,
// entries may be allocated from regions (via hashmap_put_region) and
// calling free() on them would corrupt the heap. Memory is reclaimed
// when the owning region is destroyed.
void hashmap_free_entries(HashMap* map) {
    if (!map) return;

    for (size_t i = 0; i < map->bucket_count; i++) {
        /* Just clear the bucket pointer - don't free entries */
        map->buckets[i] = NULL;
    }
    map->entry_count = 0;
}

// Resize when load factor exceeded
static void hashmap_resize(HashMap* map) {
    // Check for integer overflow before doubling
    if (map->bucket_count > SIZE_MAX / 2) {
        map->had_alloc_failure = 1;
        return;  // Cannot double, at max capacity
    }
    size_t new_count = map->bucket_count * 2;
    HashEntry** new_buckets = calloc(new_count, sizeof(HashEntry*));
    if (!new_buckets) {
        map->had_alloc_failure = 1;
        return;  // Keep old buckets on failure
    }

    // Rehash all entries
    for (size_t i = 0; i < map->bucket_count; i++) {
        HashEntry* entry = map->buckets[i];
        while (entry) {
            HashEntry* next = entry->next;
            size_t idx = hash_ptr(entry->key) % new_count;
            entry->next = new_buckets[idx];
            new_buckets[idx] = entry;
            entry = next;
        }
    }

    free(map->buckets);
    map->buckets = new_buckets;
    map->bucket_count = new_count;
}

// Resize when load factor exceeded (region-aware)
static void hashmap_resize_region(HashMap* map, struct Region* r) {
    if (map->bucket_count > SIZE_MAX / 2) {
        map->had_alloc_failure = 1;
        return;
    }
    size_t new_count = map->bucket_count * 2;
    // We don't have region_calloc, so we use region_alloc + memset
    HashEntry** new_buckets = (HashEntry**)region_alloc(r, new_count * sizeof(HashEntry*));
    if (!new_buckets) {
        map->had_alloc_failure = 1;
        return;
    }
    memset(new_buckets, 0, new_count * sizeof(HashEntry*));

    // Rehash all entries
    for (size_t i = 0; i < map->bucket_count; i++) {
        HashEntry* entry = map->buckets[i];
        while (entry) {
            HashEntry* next = entry->next;
            size_t idx = hash_ptr(entry->key) % new_count;
            entry->next = new_buckets[idx];
            new_buckets[idx] = entry;
            entry = next;
        }
    }

    // In a region, we don't "free" the old buckets, they stay until region dies
    map->buckets = new_buckets;
    map->bucket_count = new_count;
}

// Put key-value pair (region-aware)
void hashmap_put_region(HashMap* map, void* key, void* value, void* r_void) {
    struct Region* r = (struct Region*)r_void;
    if (!map) return;

    // Check if need to resize
    if ((float)map->entry_count / map->bucket_count > map->load_factor) {
        hashmap_resize_region(map, r);
    }

    size_t idx = hash_ptr(key) % map->bucket_count;
    HashEntry* entry = map->buckets[idx];

    // Check if key exists
    while (entry) {
        if (entry->key == key) {
            entry->value = value;
            return;
        }
        entry = entry->next;
    }

    // Create new entry in region
    entry = (HashEntry*)region_alloc(r, sizeof(HashEntry));
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

// hashmap_get is now static inline in hashmap.h (T-opt-inline-hash-fastpath)

// Put key-value pair
void hashmap_put(HashMap* map, void* key, void* value) {
    if (!map) return;

    // Check if need to resize
    if ((float)map->entry_count / map->bucket_count > map->load_factor) {
        hashmap_resize(map);
    }

    size_t idx = hash_ptr(key) % map->bucket_count;
    HashEntry* entry = map->buckets[idx];

    // Check if key exists
    while (entry) {
        if (entry->key == key) {
            entry->value = value;
            return;
        }
        entry = entry->next;
    }

    // Create new entry
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

// Remove key
// Note: Does NOT free the entry - in region-based memory management,
// entries may be allocated from regions (via hashmap_put_region) and
// calling free() on them would corrupt the heap. Memory is reclaimed
// when the owning region is destroyed.
void* hashmap_remove(HashMap* map, void* key) {
    if (!map) return NULL;

    size_t idx = hash_ptr(key) % map->bucket_count;
    HashEntry** prev = &map->buckets[idx];
    HashEntry* entry = *prev;

    while (entry) {
        if (entry->key == key) {
            void* value = entry->value;
            *prev = entry->next;
            /* Don't free(entry) - may be region-allocated */
            map->entry_count--;
            return value;
        }
        prev = &entry->next;
        entry = entry->next;
    }
    return NULL;
}

// hashmap_contains is now static inline in hashmap.h (T-opt-inline-hash-fastpath)

// Iterate over all entries
void hashmap_foreach(HashMap* map, HashMapIterFn fn, void* ctx) {
    if (!map || !fn) return;

    for (size_t i = 0; i < map->bucket_count; i++) {
        HashEntry* entry = map->buckets[i];
        while (entry) {
            fn(entry->key, entry->value, ctx);
            entry = entry->next;
        }
    }
}

// Get entry count
size_t hashmap_size(HashMap* map) {
    return map ? map->entry_count : 0;
}

// Clear all entries
void hashmap_clear(HashMap* map) {
    hashmap_free_entries(map);
}

int hashmap_had_alloc_failure(HashMap* map) {
    return map ? map->had_alloc_failure : 0;
}
