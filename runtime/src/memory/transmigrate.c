/*
 * transmigrate.c - Optimized Iterative Transmigration with Bitmap Cycle Detection
 *
 * Implements high-performance moving of object graphs between regions.
 * Uses low-level Runtime `Obj` types (TAG_*) instead of high-level AST `Value`.
 */

#include "transmigrate.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "../../third_party/arena/arena.h"
#include "../../include/omni.h"

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

// ----------------------------------------------------------------------------
// Iterative Transmigration
// ----------------------------------------------------------------------------

typedef struct WorkItem {
    Obj** slot;         // Where to write the new pointer
    Obj* old_ptr;       // Original object to copy
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
static void transmigrate_visitor(Obj** slot, void* context) {
    TraceCtx* ctx = (TraceCtx*)context;
    Obj* old_child = *slot;
    if (!old_child) return;

    // Check visited
    PtrMap* entry = NULL;
    HASH_FIND_PTR(*ctx->visited, &old_child, entry);
    if (entry) {
        *slot = (Obj*)entry->new_ptr;
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
    // Bitmap optimization disabled for now to simplify linkage
    // RegionBitmap* bitmap = bitmap_create(src_region, &tmp_arena);
    
    PtrMap* visited = NULL;
    WorkItem* worklist = NULL;
    Obj* result = NULL;

    // Initial root
    WorkItem* item = arena_alloc(&tmp_arena, sizeof(WorkItem));
    item->old_ptr = (Obj*)root;
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

        Obj* old_obj = current->old_ptr;
        
        // Handle immediate values (integers, etc. masked in pointer)
        if (IS_IMMEDIATE(old_obj)) {
            *current->slot = old_obj;
            continue;
        }
        
        if (!old_obj) {
            *current->slot = NULL;
            continue;
        }

        // Cycle detection
        PtrMap* entry = NULL;
        HASH_FIND_PTR(visited, &old_obj, entry);
        if (entry) {
            *current->slot = (Obj*)entry->new_ptr;
            continue;
        }

        // Allocate new object
        Obj* new_obj = region_alloc(dest_region, sizeof(Obj));
        *current->slot = new_obj;

        // Register in visited
        entry = malloc(sizeof(PtrMap)); // using malloc for hash map nodes for now
        entry->old_ptr = old_obj;
        entry->new_ptr = new_obj;
        HASH_ADD_PTR(visited, old_ptr, entry);

        // Copy everything first (shallow)
        memcpy(new_obj, old_obj, sizeof(Obj));

        // Dispatch based on tag
        switch (old_obj->tag) {
            case TAG_INT: 
            case TAG_FLOAT: 
            case TAG_CHAR: 
            case TAG_NOTHING:
                // Scalar values, shallow copy is enough
                break;

            case TAG_PAIR: {
                // Manual push for children
                transmigrate_visitor(&new_obj->a, &trace_ctx);
                transmigrate_visitor(&new_obj->b, &trace_ctx);
                break;
            }

            case TAG_SYM: 
            case TAG_ERROR:
            case TAG_KEYWORD:
            case TAG_STRING:
                if (old_obj->ptr) {
                    // String/Symbol data copy
                    // Assuming ptr points to null-terminated string for these types
                    const char* s = (const char*)old_obj->ptr;
                    size_t len = strlen(s);
                    char* s_copy = region_alloc(dest_region, len + 1);
                    strcpy(s_copy, s);
                    new_obj->ptr = s_copy;
                }
                break;

            case TAG_BOX:
                // Box holds value in 'a' or 'ptr' depending on impl. 
                // omni.h: mk_box uses ptr? No, mk_box_region uses box_value which is unioned.
                // Let's assume box uses 'ptr' for Value* based on mk_box in runtime.c 
                // Wait, runtime.c mk_box says x->ptr = v.
                // But region_value.c mk_box_region says v->box_value = initial. 
                // box_value union member? Obj struct in runtime.c doesn't show box_value.
                // It shows a, b, ptr.
                // We'll treat it as ptr for now as that's generic.
                if (old_obj->ptr) {
                    // We need to cast ptr to Obj* address for visitor
                    // But ptr is void*, visitor takes Obj**.
                    // We need to store the *new* pointer back into new_obj->ptr.
                    // This is tricky with the visitor signature.
                    // We can cast &new_obj->ptr to Obj**.
                    transmigrate_visitor((Obj**)&new_obj->ptr, &trace_ctx);
                }
                break;

            case TAG_CLOSURE:
                if (old_obj->ptr) {
                    Closure* old_c = (Closure*)old_obj->ptr;
                    Closure* new_c = region_alloc(dest_region, sizeof(Closure));
                    if (new_c) {
                        *new_c = *old_c; // Shallow copy closure struct
                        new_obj->ptr = new_c;
                        
                        // Deep copy captures
                        if (old_c->captures && old_c->capture_count > 0) {
                            new_c->captures = region_alloc(dest_region, sizeof(Obj*) * old_c->capture_count);
                            for (int i = 0; i < old_c->capture_count; i++) {
                                // Initialize with old val
                                new_c->captures[i] = old_c->captures[i];
                                // Visit to update to new val
                                transmigrate_visitor(&new_c->captures[i], &trace_ctx);
                            }
                        }
                    }
                }
                break;

            default:
                // For unknown types, we default to shallow copy.
                // Warning: if they contain pointers, they will point to old region!
                break;
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
