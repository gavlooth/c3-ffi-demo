/*
 * transmigrate.c - Optimized Iterative Transmigration with Bitmap Cycle Detection
 *
 * Implements high-performance moving of object graphs between regions.
 */

#include "transmigrate.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "../../../third_party/arena/arena.h"
#include "../../../src/runtime/types.h"

// ----------------------------------------------------------------------------
// Region Bitmap for Cycle Detection
// ----------------------------------------------------------------------------

typedef struct {
    uintptr_t start;
    size_t size_words;
    uint64_t* bits;
} RegionBitmap;

static RegionBitmap* bitmap_create(Region* r, Arena* tmp_arena) {
    if (!r || !r->arena.begin) return NULL;

    // Find the min/max address range of the source region
    uintptr_t min_addr = UINTPTR_MAX;
    uintptr_t max_addr = 0;

    for (ArenaChunk* c = r->arena.begin; c; c = c->next) {
        uintptr_t start = (uintptr_t)c->data;
        uintptr_t end = start + (c->capacity * sizeof(uintptr_t));
        if (start < min_addr) min_addr = start;
        if (end > max_addr) max_addr = end;
    }

    size_t range = max_addr - min_addr;
    size_t words = (range + sizeof(uintptr_t) - 1) / sizeof(uintptr_t);
    size_t bitmap_len = (words + 63) / 64;

    RegionBitmap* b = arena_alloc(tmp_arena, sizeof(RegionBitmap));
    b->start = min_addr;
    b->size_words = words;
    b->bits = arena_alloc(tmp_arena, bitmap_len * sizeof(uint64_t));
    memset(b->bits, 0, bitmap_len * sizeof(uint64_t));

    return b;
}

static inline bool bitmap_test_and_set(RegionBitmap* b, void* ptr) {
    if (!b) return false;
    uintptr_t addr = (uintptr_t)ptr;
    if (addr < b->start) return false;
    
    size_t offset = (addr - b->start) / sizeof(uintptr_t);
    if (offset >= b->size_words) return false;

    size_t idx = offset / 64;
    uint64_t mask = 1ULL << (offset % 64);
    
    if (b->bits[idx] & mask) return true;
    b->bits[idx] |= mask;
    return false;
}

// ----------------------------------------------------------------------------
// Iterative Transmigration
// ----------------------------------------------------------------------------

typedef struct WorkItem {
    Value** slot;       // Where to write the new pointer
    Value* old_ptr;     // Original object to copy
    struct WorkItem* next;
} WorkItem;

#include "../../../third_party/uthash/uthash.h"

typedef struct {
    void* old_ptr;
    void* new_ptr;
    UT_hash_handle hh;
} PtrMap;

typedef struct {
    WorkItem** worklist;
    Arena* tmp_arena;
    PtrMap** visited;
    Region* dest;
} TraceCtx;

// Visitor for TraceFn
static void transmigrate_visitor(Value** slot, void* context) {
    TraceCtx* ctx = (TraceCtx*)context;
    Value* old_child = *slot;
    if (!old_child) return;

    // Check visited
    PtrMap* entry = NULL;
    HASH_FIND_PTR(*ctx->visited, &old_child, entry);
    if (entry) {
        *slot = (Value*)entry->new_ptr;
        return;
    }

    // Push to worklist
    WorkItem* item = arena_alloc(ctx->tmp_arena, sizeof(WorkItem));
    item->old_ptr = old_child;
    item->slot = slot;
    item->next = *ctx->worklist;
    *ctx->worklist = item;
}

void* transmigrate(void* root, Region* src_region, Region* dest_region) {
    if (!root || !dest_region) return root;

    Arena tmp_arena = {0};
    RegionBitmap* bitmap = bitmap_create(src_region, &tmp_arena);
    PtrMap* visited = NULL;
    WorkItem* worklist = NULL;
    Value* result = NULL;

    // Initial root
    WorkItem* item = arena_alloc(&tmp_arena, sizeof(WorkItem));
    item->old_ptr = (Value*)root;
    item->slot = &result;
    item->next = worklist;
    worklist = item;

    TraceCtx trace_ctx = {
        .worklist = &worklist,
        .tmp_arena = &tmp_arena,
        .visited = &visited,
        .dest = dest_region
    };

    while (worklist) {
        WorkItem* current = worklist;
        worklist = worklist->next;

        Value* old_obj = current->old_ptr;
        if (!old_obj) {
            *current->slot = NULL;
            continue;
        }

        // Cycle detection
        PtrMap* entry = NULL;
        HASH_FIND_PTR(visited, &old_obj, entry);
        if (entry) {
            *current->slot = (Value*)entry->new_ptr;
            continue;
        }

        // Allocate new object
        Value* new_obj = region_alloc(dest_region, sizeof(Value));
        *current->slot = new_obj;

        // Register in visited
        entry = malloc(sizeof(PtrMap));
        entry->old_ptr = old_obj;
        entry->new_ptr = new_obj;
        HASH_ADD_PTR(visited, old_ptr, entry);

        // Copy everything first (shallow)
        memcpy(new_obj, old_obj, sizeof(Value));

        // Specialized Trace or Switch
        if (old_obj->type && old_obj->type->trace) {
            old_obj->type->trace(new_obj, &trace_ctx, transmigrate_visitor);
        } else {
            // Fallback to manual switch for built-in types
            switch (old_obj->tag) {
                case T_INT: case T_FLOAT: case T_CHAR: case T_NIL: case T_NOTHING:
                    break;

                case T_CELL: {
                    // Manual push for children
                    transmigrate_visitor(&new_obj->cell.car, &trace_ctx);
                    transmigrate_visitor(&new_obj->cell.cdr, &trace_ctx);
                    break;
                }

                case T_SYM: case T_CODE: case T_ERROR:
                    if (old_obj->s) {
                        new_obj->s = arena_strdup(&dest_region->arena, old_obj->s);
                    }
                    break;

                case T_STRING:
                    new_obj->str.data = arena_alloc(&dest_region->arena, old_obj->str.len);
                    memcpy(new_obj->str.data, old_obj->str.data, old_obj->str.len);
                    break;

                case T_LAMBDA:
                    transmigrate_visitor(&new_obj->lam.params, &trace_ctx);
                    transmigrate_visitor(&new_obj->lam.body, &trace_ctx);
                    transmigrate_visitor(&new_obj->lam.env, &trace_ctx);
                    transmigrate_visitor(&new_obj->lam.defaults, &trace_ctx);
                    break;

                case T_BOX:
                    transmigrate_visitor(&new_obj->box_value, &trace_ctx);
                    break;

                default:
                    // For unknown types without trace, we do nothing more than shallow copy
                    break;
            }
        }
    }

    // Cleanup
    PtrMap *curr_v, *tmp_v;
    HASH_ITER(hh, visited, curr_v, tmp_v) {
        HASH_DEL(visited, curr_v);
        free(curr_v);
    }
    arena_free(&tmp_arena);

    return result;
}